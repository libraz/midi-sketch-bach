// Implementation of the fugue generator: full pipeline from subject
// generation through stretto to MIDI track output.

#include "fugue/fugue_generator.h"

#include <algorithm>
#include <cstdint>
#include <random>

#include "core/gm_program.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/parallel_repair.h"
#include "core/note_creator.h"
#include "fugue/answer.h"
#include "fugue/cadence_plan.h"
#include "fugue/countersubject.h"
#include "fugue/episode.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"
#include "fugue/exposition.h"
#include "fugue/middle_entry.h"
#include "fugue/stretto.h"
#include "fugue/subject.h"
#include "fugue/subject_validator.h"
#include "fugue/tonal_plan.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_rhythm.h"
#include "harmony/modulation_plan.h"
#include "organ/manual.h"
#include "organ/organ_techniques.h"
#include "organ/registration.h"

namespace bach {

namespace {

/// @brief Minimum number of voices for a fugue.
constexpr uint8_t kMinVoices = 2;

/// @brief Maximum number of voices for a fugue.
constexpr uint8_t kMaxVoices = 5;

/// @brief Minimum total fugue length in bars.
constexpr Tick kMinFugueBars = 12;

/// @brief Duration of coda in bars (expanded from 2 to 4 for richer endings).
constexpr Tick kCodaBars = 4;

/// @brief Organ velocity (pipe organs have no velocity sensitivity).
constexpr uint8_t kOrganVelocity = 80;

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

/// @brief Human-readable name for an organ manual.
/// @param manual OrganManual enum value.
/// @return Descriptive string for the manual.
const char* manualTrackName(OrganManual manual) {
  switch (manual) {
    case OrganManual::Great:   return "Manual I (Great)";
    case OrganManual::Swell:   return "Manual II (Swell)";
    case OrganManual::Positiv: return "Manual III (Positiv)";
    case OrganManual::Pedal:   return "Pedal";
  }
  return "Unknown Manual";
}

/// @brief Create MIDI tracks for an organ fugue using assignManuals().
///
/// Delegates to the organ manual assignment system (organ/manual.h) which
/// handles voice-to-manual routing for all voice counts (2-5):
///   2 voices: Great + Swell
///   3 voices: Great + Swell + Positiv
///   4 voices: Great + Swell + Positiv + Pedal
///   5 voices: Great (x2) + Swell + Positiv + Pedal
///
/// @param num_voices Number of voices (2-5).
/// @return Vector of Track objects with channel/program/name configured.
std::vector<Track> createOrganTracks(uint8_t num_voices) {
  auto assignments = assignManuals(num_voices, FormType::Fugue);

  std::vector<Track> tracks;
  tracks.reserve(assignments.size());

  for (const auto& assignment : assignments) {
    Track track;
    track.channel = channelForAssignment(assignment);
    track.program = programForAssignment(assignment);
    track.name = manualTrackName(assignment.manual);
    tracks.push_back(track);
  }

  return tracks;
}

/// @brief Distribute notes into tracks by voice_id.
///
/// Each note's voice field determines which track it belongs to.
/// Notes with voice_id >= tracks.size() are silently discarded.
///
/// @param notes All collected notes from all fugue sections.
/// @param tracks Output tracks (notes appended to matching track).
void assignNotesToTracks(const std::vector<NoteEvent>& notes,
                         std::vector<Track>& tracks) {
  for (const auto& note : notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }
}

/// @brief Sort notes in each track by start_tick for MIDI output.
/// @param tracks Tracks whose notes will be sorted in place.
void sortTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

/// @brief Generate a valid subject with retry logic.
///
/// Attempts to generate a subject that passes the SubjectValidator's
/// quality threshold (composite >= 0.7). Each attempt uses a different
/// seed derived from the base seed. If all attempts fail, returns the
/// last generated subject as a best-effort fallback.
///
/// @param config Fugue configuration.
/// @param attempts_out Output: number of attempts used.
/// @return Generated Subject (may not be acceptable if all retries failed).
Subject generateValidSubject(const FugueConfig& config, int& attempts_out) {
  SubjectGenerator gen;
  SubjectValidator validator;

  Subject best_subject;
  float best_composite = -1.0f;

  for (int attempt = 0; attempt < config.max_subject_retries; ++attempt) {
    // Wrapping on uint32_t overflow is safe: any value is a valid RNG seed.
    // With max_subject_retries <= ~10, overflow does not occur in practice.
    uint32_t attempt_seed = config.seed + static_cast<uint32_t>(attempt) * 1000003u;
    Subject subject = gen.generate(config, attempt_seed);
    SubjectScore score = validator.evaluate(subject);
    float composite = score.composite();

    attempts_out = attempt + 1;

    if (score.isAcceptable()) {
      return subject;
    }

    // Track best attempt for fallback.
    if (composite > best_composite) {
      best_composite = composite;
      best_subject = subject;
    }
  }

  // Fallback: return the best attempt even if below threshold.
  return best_subject;
}

/// @brief Duration of the dominant pedal in bars (placed before stretto).
constexpr Tick kDominantPedalBars = 4;

/// @brief Generate pedal point notes for a given pitch and duration.
///
/// The pedal is split into bar-length tied notes for better MIDI compatibility
/// (many MIDI renderers handle shorter notes more reliably than very long ones).
///
/// @param pitch MIDI pitch of the pedal note.
/// @param start_tick Start position in ticks.
/// @param duration Total duration of the pedal in ticks.
/// @param voice_id Voice for the pedal (lowest voice).
/// @return Vector of pedal point NoteEvents, each one bar long (or shorter
///         for the final segment).
std::vector<NoteEvent> generatePedalPoint(uint8_t pitch, Tick start_tick,
                                          Tick duration, VoiceId voice_id) {
  std::vector<NoteEvent> notes;
  Tick remaining = duration;
  Tick tick = start_tick;
  while (remaining > 0) {
    Tick note_dur = std::min(remaining, static_cast<Tick>(kTicksPerBar));
    NoteEvent evt;
    evt.pitch = pitch;
    evt.start_tick = tick;
    evt.duration = note_dur;
    evt.velocity = kOrganVelocity;
    evt.voice = voice_id;
    evt.source = BachNoteSource::PedalPoint;
    notes.push_back(evt);
    tick += note_dur;
    remaining -= note_dur;
  }
  return notes;
}

/// @brief Remove notes from the lowest voice that overlap with a pedal region.
///
/// Any existing note in the specified voice whose time interval intersects
/// [region_start, region_end) is removed, making room for the pedal point.
/// This catches notes that start before the region but extend into it.
///
/// @param all_notes The collection of all notes (modified in place).
/// @param voice_id Voice whose notes should be removed.
/// @param region_start Start tick of the pedal region (inclusive).
/// @param region_end End tick of the pedal region (exclusive).
void removeLowestVoiceNotes(std::vector<NoteEvent>& all_notes,
                            VoiceId voice_id,
                            Tick region_start, Tick region_end) {
  all_notes.erase(
      std::remove_if(all_notes.begin(), all_notes.end(),
                     [voice_id, region_start, region_end](const NoteEvent& evt) {
                       if (evt.voice != voice_id) return false;
                       Tick note_end = evt.start_tick + evt.duration;
                       return evt.start_tick < region_end && note_end > region_start;
                     }),
      all_notes.end());
}

/// @brief Compute the tonic pitch in bass register for a given key.
///
/// Returns the tonic in octave 2 (e.g., C2 = MIDI 36 for Key::C).
/// This is the standard register for organ pedal notes.
///
/// @param key Musical key.
/// @return MIDI pitch number for the tonic in octave 2.
uint8_t tonicBassPitch(Key key) {
  // Octave 2 starts at MIDI 36 (C2). Add the key's pitch class offset.
  int pitch = 36 + static_cast<int>(key);
  return clampPitch(pitch, organ_range::kPedalLow, organ_range::kPedalHigh);
}

/// @brief Extract the last pitch played by a specific voice before a given tick.
/// @param notes All fugue notes (sorted by tick).
/// @param before_tick Only consider notes starting before this tick.
/// @param voice Target voice ID.
/// @return Last pitch (0 if no notes found for this voice).
static uint8_t extractVoiceLastPitch(
    const std::vector<NoteEvent>& notes, Tick before_tick, VoiceId voice) {
  uint8_t last_pitch = 0;
  for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
    if (it->voice == voice && it->start_tick < before_tick) {
      last_pitch = it->pitch;
      break;
    }
  }
  return last_pitch;
}

/// @brief Find the nearest chord tone to a given pitch within a max distance.
/// @param pitch Source pitch.
/// @param target_pc Target pitch class (0-11).
/// @param max_dist Maximum semitone distance.
/// @return Nearest pitch with the target pitch class, or the original pitch if none found.
static uint8_t nearestPitchWithPC(uint8_t pitch, int target_pc, int max_dist = 7) {
  int best = pitch;
  int best_dist = 999;
  for (int d = -max_dist; d <= max_dist; ++d) {
    int cand = static_cast<int>(pitch) + d;
    if (cand < 0 || cand > 127) continue;
    if (cand % 12 == ((target_pc % 12) + 12) % 12) {
      if (std::abs(d) < best_dist) {
        best_dist = std::abs(d);
        best = cand;
      }
    }
  }
  return static_cast<uint8_t>(std::clamp(best, 0, 127));
}

/// @brief Create 3-stage coda notes for a richer fugue ending.
///
/// Stage 1 (bars 1-2): Subject head fragment in voice 0 over tonic pedal.
///   Upper voices play held chord tones.
/// Stage 2 (bar 3): V7-I perfect cadence progression.
///   Leading tone resolution in upper voices.
/// Stage 3 (bar 4): Final sustained tonic chord (all voices).
///   Minor keys get Picardy third (raised 3rd).
///
/// @param start_tick When the coda begins.
/// @param duration Total coda duration in ticks.
/// @param key Musical key for the tonic chord.
/// @param num_voices Number of voices.
/// @param is_minor True for minor key (Picardy third in stage 3).
/// @return Vector of coda notes.
std::vector<NoteEvent> createCodaNotes(Tick start_tick, Tick duration,
                                       Key key, uint8_t num_voices,
                                       bool is_minor = false,
                                       const uint8_t* last_pitches = nullptr) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  // Stage 1 (bars 1-2): Subject head motif in voice 0 + held chord tones.
  Tick stage1_dur = bar_dur * 2;
  if (stage1_dur > duration) stage1_dur = duration;

  // Voice 0: subject head fragment (rising from tonic to 5th and back).
  // When last_pitches is available, shift the motif to the nearest octave
  // to minimize the entry jump.
  {
    Tick sub_dur = kTicksPerBeat;
    int third = is_minor ? 3 : 4;
    int base_tonic = tonic_pitch;
    // Octave-shift the motif base to stay close to the previous pitch.
    if (last_pitches && last_pitches[0] > 0) {
      int prev = static_cast<int>(last_pitches[0]);
      int best_dist = std::abs(prev - base_tonic);
      for (int oct : {-12, 12, -24, 24}) {
        int cand = tonic_pitch + oct;
        if (cand < organ_range::kPedalLow || cand > organ_range::kManual1High) continue;
        int dist = std::abs(prev - cand);
        if (dist < best_dist) {
          best_dist = dist;
          base_tonic = cand;
        }
      }
    }
    int head_pitches[] = {base_tonic, base_tonic + 2, base_tonic + third, base_tonic + 7,
                          base_tonic + third, base_tonic + 2, base_tonic, base_tonic};
    int head_count = std::min(8, static_cast<int>(stage1_dur / sub_dur));
    for (int idx = 0; idx < head_count; ++idx) {
      NoteEvent note;
      note.start_tick = start_tick + static_cast<Tick>(idx) * sub_dur;
      note.duration = sub_dur;
      note.pitch = clampPitch(head_pitches[idx], organ_range::kPedalLow, organ_range::kManual1High);
      note.velocity = kOrganVelocity;
      note.voice = 0;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }
  }

  // Other voices: approach nearest tonic chord tone from their last pitch.
  // If last_pitches is provided, use voice-leading; otherwise use fixed offsets.
  int chord_third = is_minor ? 3 : 4;
  {
    // Tonic chord pitch classes: root, 3rd, 5th.
    int tonic_pc = tonic_pitch % 12;
    int third_pc = (tonic_pitch + chord_third) % 12;
    int fifth_pc = (tonic_pitch + 7) % 12;
    int chord_pcs[] = {tonic_pc, third_pc, fifth_pc};

    int stage1_offsets[] = {7, chord_third, -12, 12};
    uint8_t count = std::min(static_cast<uint8_t>(num_voices - 1), static_cast<uint8_t>(4));
    std::sort(stage1_offsets, stage1_offsets + count, std::greater<int>());

    for (uint8_t i = 0; i < count; ++i) {
      uint8_t voice_idx = 1 + i;
      uint8_t target_pitch;

      if (last_pitches && last_pitches[voice_idx] > 0) {
        // Voice-leading: find nearest chord tone to previous pitch.
        uint8_t prev = last_pitches[voice_idx];
        int best_dist = 999;
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[i],
                                  organ_range::kPedalLow, organ_range::kManual1High);
        for (int pc : chord_pcs) {
          uint8_t cand = nearestPitchWithPC(prev, pc, 7);
          int dist = std::abs(static_cast<int>(cand) - static_cast<int>(prev));
          if (dist < best_dist) {
            best_dist = dist;
            target_pitch = cand;
          }
        }
        target_pitch = clampPitch(static_cast<int>(target_pitch),
                                  organ_range::kPedalLow, organ_range::kManual1High);
      } else {
        // Fallback: use fixed offsets.
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[i],
                                  organ_range::kPedalLow, organ_range::kManual1High);
      }

      NoteEvent note;
      note.start_tick = start_tick;
      note.duration = stage1_dur;
      note.pitch = target_pitch;
      note.velocity = kOrganVelocity;
      note.voice = voice_idx;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }
  }

  // Stage 2 (bar 3): V7-I cadence progression.
  if (duration > stage1_dur) {
    Tick stage2_start = start_tick + stage1_dur;
    Tick stage2_dur = std::min(bar_dur, duration - stage1_dur);
    Tick half_bar = stage2_dur / 2;

    // V7 chord (first half): dominant 7th chord.
    // Sort offsets descending so V0=highest pitch (soprano convention).
    int dom_pitch = tonic_pitch + 7;  // Dominant
    {
      int dom7_offsets[] = {0, 4, 7, 10, -5};
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      std::sort(dom7_offsets, dom7_offsets + count, std::greater<int>());
      for (uint8_t v = 0; v < count; ++v) {
        NoteEvent note;
        note.start_tick = stage2_start;
        note.duration = half_bar;
        note.pitch = clampPitch(dom_pitch + dom7_offsets[v],
                                organ_range::kPedalLow, organ_range::kManual1High);
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }

    // I chord (second half): tonic resolution.
    // Sort offsets descending so V0=highest pitch.
    {
      int res_third = is_minor ? 3 : 4;
      int tonic_offsets[] = {0, res_third, 7, -12, 12};
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      std::sort(tonic_offsets, tonic_offsets + count, std::greater<int>());
      for (uint8_t v = 0; v < count; ++v) {
        NoteEvent note;
        note.start_tick = stage2_start + half_bar;
        note.duration = stage2_dur - half_bar;
        note.pitch = clampPitch(tonic_pitch + tonic_offsets[v],
                                organ_range::kPedalLow, organ_range::kManual1High);
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }
  }

  // Stage 3 (bar 4): Final sustained tonic chord.
  Tick stage2_actual = (duration > stage1_dur)
      ? std::min(bar_dur, duration - stage1_dur)
      : static_cast<Tick>(0);
  Tick stage3_start = start_tick + stage1_dur + stage2_actual;
  if (stage3_start < start_tick + duration) {
    Tick stage3_dur = start_tick + duration - stage3_start;
    // Picardy third for minor keys: raise 3rd by 1 semitone.
    // Sort offsets descending so V0=highest pitch.
    int third_offset = is_minor ? 4 : 4;  // Major 3rd in both (Picardy)
    {
      int final_offsets[] = {0, 7, third_offset, -12, 12};
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      std::sort(final_offsets, final_offsets + count, std::greater<int>());
      for (uint8_t v = 0; v < count; ++v) {
        NoteEvent note;
        note.start_tick = stage3_start;
        note.duration = stage3_dur;
        note.pitch = clampPitch(tonic_pitch + final_offsets[v],
                                organ_range::kPedalLow, organ_range::kManual1High);
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }
  }

  return notes;
}

}  // namespace

FugueResult generateFugue(const FugueConfig& config) {
  FugueResult result;
  result.success = false;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // =========================================================================
  // Step 1: Generate valid subject
  // =========================================================================
  int subject_attempts = 0;
  Subject subject = generateValidSubject(config, subject_attempts);
  result.attempts = subject_attempts;

  if (subject.notes.empty()) {
    result.error_message = "Failed to generate a valid subject";
    return result;
  }

  // =========================================================================
  // Step 2: Generate answer (Real/Tonal auto-detection)
  // =========================================================================
  Answer answer = generateAnswer(subject, config.answer_type);

  // =========================================================================
  // Step 3: Generate countersubject(s)
  // =========================================================================
  Countersubject counter_subject =
      generateCountersubject(subject, config.seed + 1000);

  // For 4+ voices, generate a second countersubject that contrasts with both
  // the subject and the first countersubject.
  Countersubject counter_subject_2;
  if (num_voices >= 4) {
    counter_subject_2 = generateSecondCountersubject(
        subject, counter_subject, config.seed + 5000);
  }

  // =========================================================================
  // Step 4: Generate tonal plan (BEFORE exposition for timeline-first pipeline)
  // =========================================================================
  // Estimate total duration using structural formula.
  Tick expo_ticks = static_cast<Tick>(num_voices) * subject.length_ticks;
  Tick episode_bars_tick = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick develop_ticks = (episode_bars_tick * static_cast<Tick>(config.develop_pairs) * 2) +
                       subject.length_ticks * static_cast<Tick>(config.develop_pairs);
  Tick pedal_ticks = kTicksPerBar * kDominantPedalBars;
  Tick stretto_ticks = subject.length_ticks * 2;
  Tick coda_ticks = kTicksPerBar * kCodaBars;
  Tick estimated_duration = expo_ticks + develop_ticks + pedal_ticks +
                            stretto_ticks + coda_ticks;
  Tick min_duration = kTicksPerBar * kMinFugueBars;
  if (estimated_duration < min_duration) {
    estimated_duration = min_duration;
  }

  // Create modulation plan early so the tonal plan can align to actual structure.
  ModulationPlan mod_plan;
  if (config.has_modulation_plan) {
    mod_plan = config.modulation_plan;
  } else {
    mod_plan = config.is_minor ? ModulationPlan::createForMinor(config.key)
                               : ModulationPlan::createForMajor(config.key);
  }

  TonalPlan tonal_plan = generateStructureAlignedTonalPlan(
      config, mod_plan, subject.length_ticks, estimated_duration);

  // =========================================================================
  // Step 5: Create beat-resolution harmonic timeline
  // =========================================================================
  HarmonicTimeline detailed_timeline = tonal_plan.toDetailedTimeline(estimated_duration);

  // =========================================================================
  // Step 6: Initialize counterpoint validation state
  // =========================================================================
  CounterpointState cp_state;
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);  // Allow weak-beat dissonance
  CollisionResolver cp_resolver;

  for (uint8_t v = 0; v < num_voices; ++v) {
    auto [lo, hi] = getFugueVoiceRange(v, num_voices);
    cp_state.registerVoice(v, lo, hi);
  }
  cp_state.setKey(config.key);

  // =========================================================================
  // Step 7: Build exposition (FuguePhase::Establish) with counterpoint validation
  // =========================================================================
  Exposition expo = buildExposition(subject, answer, counter_subject,
                                    config, config.seed,
                                    cp_state, cp_rules, cp_resolver,
                                    detailed_timeline);

  // =========================================================================
  // Collect all notes and build structure
  // =========================================================================
  FugueStructure& structure = result.structure;
  std::vector<NoteEvent> all_notes;
  Tick current_tick = 0;

  // --- Section 1: Exposition (Establish) ---
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, expo.total_ticks, config.key);
  auto expo_notes = expo.allNotes();
  all_notes.insert(all_notes.end(), expo_notes.begin(), expo_notes.end());
  current_tick = expo.total_ticks;

  // --- Build motif pool for Fortspinnung-based episodes ---
  MotifPool motif_pool;
  motif_pool.build(subject.notes, counter_subject.notes, subject.character);

  // --- Develop phase: Episode+MiddleEntry pairs ---
  int develop_pairs = config.develop_pairs;
  Key prev_key = config.key;

  // RNG for false entry probability checks (seeded from config for determinism).
  std::mt19937 false_entry_rng(config.seed + 9999u);

  // Character-based probability for false entry with per-seed variation.
  float false_entry_prob = 0.0f;
  switch (subject.character) {
    case SubjectCharacter::Restless: false_entry_prob = 0.40f; break;
    case SubjectCharacter::Playful: false_entry_prob = 0.30f; break;
    case SubjectCharacter::Noble:   false_entry_prob = 0.15f; break;
    case SubjectCharacter::Severe:  false_entry_prob = 0.10f; break;
  }
  false_entry_prob = std::clamp(
      false_entry_prob + rng::rollFloat(false_entry_rng, -0.05f, 0.05f), 0.0f, 1.0f);

  uint32_t entries_seen_mask = 0;  // Bitmask: which voices got middle entries.
  for (int pair_idx = 0; pair_idx < develop_pairs; ++pair_idx) {
    // Odd-indexed episodes get +1 bar for structural variety (Bach uses
    // irregular episode lengths to avoid mechanical regularity).
    int ep_bars = config.episode_bars;
    if (pair_idx % 2 != 0) {
      ep_bars += 1;
    }
    Tick episode_duration = kTicksPerBar * static_cast<Tick>(ep_bars);

    Key target_key = mod_plan.getTargetKey(pair_idx, config.key);
    uint32_t pair_seed_base = config.seed + static_cast<uint32_t>(pair_idx) * 2000u + 2000u;

    // Compute energy level for this episode from the energy curve.
    float episode_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);

    // Episode: transition from prev_key to target_key (Fortspinnung-based).
    Episode episode = generateFortspinnungEpisode(
        subject, motif_pool, current_tick, episode_duration,
        prev_key, target_key, num_voices, pair_seed_base,
        pair_idx, episode_energy,
        cp_state, cp_rules, cp_resolver, detailed_timeline);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         current_tick, current_tick + episode_duration, target_key);
    all_notes.insert(all_notes.end(), episode.notes.begin(), episode.notes.end());
    current_tick += episode_duration;

    // MiddleEntry: voice rotates by pair index.
    // Uses validated overload that routes through createBachNote() with
    // Immutable protection -- notes are registered in cp_state automatically.
    //
    // False entry: sometimes insert a truncated subject opening instead of a full
    // middle entry (Bach's developmental technique). At least ONE real entry must
    // exist per develop phase, so pair_idx == 0 always gets a real entry.
    // Prefer voices that haven't had a middle entry yet (bass rotation fix).
    uint8_t entry_voice;
    {
      uint8_t bass_voice = num_voices - 1;
      bool bass_has_entry = (entries_seen_mask & (1u << bass_voice)) != 0;

      // Force bass entry in the latter portion of the develop phase.
      // This ensures the pedal voice participates in middle entries.
      int threshold = std::max(1, develop_pairs * 2 / 3);
      if (!bass_has_entry && pair_idx >= threshold && num_voices >= 4) {
        entry_voice = bass_voice;
      } else {
        uint8_t candidate = static_cast<uint8_t>(pair_idx % num_voices);
        for (uint8_t v = 0; v < num_voices; ++v) {
          uint8_t check = (candidate + v) % num_voices;
          if ((entries_seen_mask & (1u << check)) == 0) {
            candidate = check;
            break;
          }
        }
        entry_voice = candidate;
      }
      entries_seen_mask |= (1u << entry_voice);
    }
    std::uniform_real_distribution<float> false_dist(0.0f, 1.0f);
    bool use_false_entry = (pair_idx > 0) && (false_dist(false_entry_rng) < false_entry_prob);

    MiddleEntry middle_entry = use_false_entry
        ? generateFalseEntry(subject, target_key, current_tick, entry_voice, num_voices)
        : generateMiddleEntry(subject, target_key,
                              current_tick, entry_voice, num_voices,
                              cp_state, cp_rules, cp_resolver,
                              detailed_timeline);
    Tick middle_end = middle_entry.end_tick;
    if (middle_end <= current_tick) {
      middle_end = current_tick + subject.length_ticks;
      if (middle_end <= current_tick) {
        middle_end = current_tick + kTicksPerBar * 2;
      }
    }
    structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                         current_tick, middle_end, target_key);
    all_notes.insert(all_notes.end(), middle_entry.notes.begin(),
                     middle_entry.notes.end());

    // Companion counterpoint: non-entry voices get episode material during
    // the middle entry. In Bach's fugues, a subject re-entry in one voice is
    // always accompanied by active counterpoint in the remaining voices.
    {
      Tick me_duration = middle_end - current_tick;
      if (me_duration > 0 && num_voices >= 2) {
        float me_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);
        Episode companion = generateFortspinnungEpisode(
            subject, motif_pool, current_tick, me_duration,
            target_key, target_key, num_voices,
            pair_seed_base + 500u, pair_idx, me_energy,
            cp_state, cp_rules, cp_resolver, detailed_timeline);
        // Remove notes for the entry voice to avoid doubling the subject.
        companion.notes.erase(
            std::remove_if(companion.notes.begin(), companion.notes.end(),
                           [entry_voice](const NoteEvent& evt) {
                             return evt.voice == entry_voice;
                           }),
            companion.notes.end());
        all_notes.insert(all_notes.end(), companion.notes.begin(),
                         companion.notes.end());
      }
    }

    current_tick = middle_end;
    prev_key = target_key;
  }

  // --- Return episode: transition back to home key ---
  {
    Tick return_ep_duration = kTicksPerBar * static_cast<Tick>(config.episode_bars);
    float return_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);
    Episode return_episode = generateFortspinnungEpisode(
        subject, motif_pool, current_tick, return_ep_duration,
        prev_key, config.key, num_voices,
        config.seed + static_cast<uint32_t>(develop_pairs) * 2000u + 2000u,
        develop_pairs, return_energy,
        cp_state, cp_rules, cp_resolver, detailed_timeline);
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         current_tick, current_tick + return_ep_duration, config.key);
    all_notes.insert(all_notes.end(), return_episode.notes.begin(),
                     return_episode.notes.end());
    current_tick += return_ep_duration;
  }

  // --- Dominant pedal: 4 bars before stretto (Develop -> Resolve transition) ---
  // In Bach's fugues, the dominant pedal is a climactic point where the bass
  // sustains the dominant while upper voices remain highly active with
  // sequential episode material building toward the final stretto.
  VoiceId lowest_voice = num_voices - 1;
  {
    Tick pedal_duration = kTicksPerBar * kDominantPedalBars;
    uint8_t dominant_pitch =
        static_cast<uint8_t>(tonicBassPitch(config.key) + interval::kPerfect5th);
    // Clamp to pedal range.
    dominant_pitch = clampPitch(static_cast<int>(dominant_pitch),
                                organ_range::kPedalLow, organ_range::kPedalHigh);

    // Remove existing lowest-voice notes in the pedal region.
    removeLowestVoiceNotes(all_notes, lowest_voice,
                           current_tick, current_tick + pedal_duration);

    auto dominant_pedal = generatePedalPoint(dominant_pitch, current_tick,
                                             pedal_duration, lowest_voice);
    all_notes.insert(all_notes.end(), dominant_pedal.begin(), dominant_pedal.end());

    // Register pedal notes in counterpoint state.
    for (const auto& note : dominant_pedal) {
      cp_state.addNote(note.voice, note);
    }

    // Generate upper voice counterpoint over the pedal. Use episode material
    // with high energy (pre-stretto climax) for voices 0..upper_voices-1.
    // lowest_voice has the pedal and is excluded from episode generation.
    uint8_t upper_voices = num_voices > 1 ? num_voices - 1 : 1;
    float pedal_energy = FugueEnergyCurve::getLevel(current_tick, estimated_duration);
    Episode pedal_episode = generateFortspinnungEpisode(
        subject, motif_pool, current_tick, pedal_duration,
        config.key, config.key, upper_voices,
        config.seed + static_cast<uint32_t>(develop_pairs + 1) * 2000u + 7000u,
        develop_pairs + 1, pedal_energy,
        cp_state, cp_rules, cp_resolver, detailed_timeline);
    all_notes.insert(all_notes.end(), pedal_episode.notes.begin(),
                     pedal_episode.notes.end());

    current_tick += pedal_duration;
  }

  // --- Stretto (Resolve) ---
  Stretto stretto = generateStretto(subject, config.key, current_tick,
                                    num_voices, config.seed + 4000,
                                    config.character,
                                    cp_state, cp_rules, cp_resolver,
                                    detailed_timeline);
  Tick stretto_end = stretto.end_tick;
  // Ensure stretto has non-zero duration.
  if (stretto_end <= current_tick) {
    stretto_end = current_tick + kTicksPerBar * 2;
  }
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       current_tick, stretto_end, config.key);
  auto stretto_notes = stretto.allNotes();
  all_notes.insert(all_notes.end(), stretto_notes.begin(), stretto_notes.end());
  current_tick = stretto_end;

  // --- Section 6: Coda (Resolve) with tonic pedal ---
  Tick coda_duration = kTicksPerBar * kCodaBars;
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       current_tick, current_tick + coda_duration, config.key);
  // Extract last pitches for each voice for voice-leading into coda.
  uint8_t coda_last_pitches[5] = {0, 0, 0, 0, 0};
  for (uint8_t v = 0; v < num_voices && v < 5; ++v) {
    coda_last_pitches[v] = extractVoiceLastPitch(all_notes, current_tick, v);
  }
  auto coda_notes = createCodaNotes(current_tick, coda_duration,
                                    config.key, num_voices, config.is_minor,
                                    coda_last_pitches);
  all_notes.insert(all_notes.end(), coda_notes.begin(), coda_notes.end());

  // Tonic pedal in coda: lowest voice sustains the tonic.
  {
    uint8_t tonic_pitch = tonicBassPitch(config.key);

    // Remove the lowest voice's coda chord note -- replaced by pedal point.
    removeLowestVoiceNotes(all_notes, lowest_voice,
                           current_tick, current_tick + coda_duration);

    auto tonic_pedal = generatePedalPoint(tonic_pitch, current_tick,
                                          coda_duration, lowest_voice);
    all_notes.insert(all_notes.end(), tonic_pedal.begin(), tonic_pedal.end());
  }

  // =========================================================================
  // Chord-boundary truncation: cut notes that sustain into a new chord
  // where they are no longer consonant. Protects short passing tones.
  // =========================================================================
  {
    const auto& harm_events = detailed_timeline.events();
    for (auto& note : all_notes) {
      // Skip short notes (passing tones / ornaments).
      if (note.duration <= kTicksPerBeat) continue;
      // Skip structural notes (subject, answer, pedal, coda).
      if (note.source == BachNoteSource::FugueSubject ||
          note.source == BachNoteSource::FugueAnswer ||
          note.source == BachNoteSource::PedalPoint ||
          note.source == BachNoteSource::Coda ||
          note.source == BachNoteSource::Countersubject) continue;

      Tick note_end = note.start_tick + note.duration;
      for (const auto& ev : harm_events) {
        // Look for chord boundaries within the note's duration.
        if (ev.tick <= note.start_tick) continue;
        if (ev.tick >= note_end) break;
        // Check if the note is a chord tone in the new chord.
        if (!isChordTone(note.pitch, ev)) {
          // Truncate at the chord boundary.
          note.duration = ev.tick - note.start_tick;
          break;
        }
      }
    }
  }

  // =========================================================================
  // Post-validation sweep: final safety net for remaining notes
  // =========================================================================
  {
    // Sort all notes by tick for chronological processing.
    std::sort(all_notes.begin(), all_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                return a.voice < b.voice;
              });

    // Fresh counterpoint state for clean validation pass.
    CounterpointState post_state;
    BachRuleEvaluator post_rules(num_voices);
    post_rules.setFreeCounterpoint(true);
    CollisionResolver post_resolver;

    for (uint8_t v = 0; v < num_voices; ++v) {
      auto [lo, hi] = getFugueVoiceRange(v, num_voices);
      post_state.registerVoice(v, lo, hi);
    }
    post_state.setKey(config.key);

    int pre_count = static_cast<int>(all_notes.size());
    std::vector<NoteEvent> validated_notes;
    validated_notes.reserve(all_notes.size());

    int rejected = 0;
    int adjusted = 0;
    int chord_tones = 0;
    int dissonances = 0;
    int crossings = 0;

    for (size_t note_idx = 0; note_idx < all_notes.size(); ++note_idx) {
      const auto& note = all_notes[note_idx];
      if (note.voice >= num_voices) {
        ++rejected;  // Drop: assignNotesToTracks would discard anyway.
        continue;
      }

      // Determine source-based protection.
      BachNoteSource source = note.source;
      if (source == BachNoteSource::Unknown) {
        source = BachNoteSource::FreeCounterpoint;
      }

      // Check chord tone status against beat-resolution timeline for generation quality.
      // Post-validation uses detailed_timeline for accurate snapping, while the
      // analysis uses bar-resolution result.timeline for reduced SOCC counts.
      const auto& harm_ev = detailed_timeline.getAt(note.start_tick);
      bool is_chord_tone = isChordTone(note.pitch, harm_ev);
      if (is_chord_tone) ++chord_tones;

      // Structural notes (subject, answer, pedal, countersubject, false entry,
      // coda) pass through without alteration -- only register in state
      // for context. Coda notes are design values (Principle 4) and should
      // never be altered.
      bool is_structural = isStructuralSource(source);

      if (is_structural) {
        // Apply octave correction for structural voice crossings.
        NoteEvent fixed_note = note;
        bool has_crossing = false;
        for (uint8_t other = 0; other < num_voices; ++other) {
          if (other == note.voice) continue;
          const NoteEvent* other_note = post_state.getNoteAt(other, note.start_tick);
          if (!other_note) continue;
          if ((note.voice < other && note.pitch < other_note->pitch) ||
              (note.voice > other && note.pitch > other_note->pitch)) {
            has_crossing = true;
            break;
          }
        }
        if (has_crossing) {
          for (int shift : {12, -12, 24, -24}) {
            int candidate = static_cast<int>(note.pitch) + shift;
            if (candidate < 0 || candidate > 127) continue;
            bool resolved = true;
            for (uint8_t other = 0; other < num_voices; ++other) {
              if (other == note.voice) continue;
              const NoteEvent* other_note =
                  post_state.getNoteAt(other, note.start_tick);
              if (!other_note) continue;
              if ((note.voice < other &&
                   candidate < static_cast<int>(other_note->pitch)) ||
                  (note.voice > other &&
                   candidate > static_cast<int>(other_note->pitch))) {
                resolved = false;
                break;
              }
            }
            if (resolved) {
              fixed_note.pitch = static_cast<uint8_t>(candidate);
              break;
            }
          }
        }
        post_state.addNote(note.voice, fixed_note);
        validated_notes.push_back(fixed_note);

        // Count dissonances for metrics even on structural notes.
        bool is_strong = (note.start_tick % kTicksPerBeat == 0);
        if (!is_chord_tone && is_strong) ++dissonances;

        // Count remaining voice crossings after correction.
        for (uint8_t other = 0; other < num_voices; ++other) {
          if (other == note.voice) continue;
          const NoteEvent* other_note =
              post_state.getNoteAt(other, fixed_note.start_tick);
          if (!other_note) continue;
          if (note.voice < other && fixed_note.pitch < other_note->pitch)
            ++crossings;
          if (note.voice > other && fixed_note.pitch > other_note->pitch)
            ++crossings;
        }

        // Detect parallel perfects involving structural notes (metrics only).
        {
          auto par = checkParallelsAndP4Bass(
              post_state, post_rules, note.voice, fixed_note.pitch,
              note.start_tick, num_voices);
          if (par.has_parallel_perfect) {
            ++result.quality.structural_parallel_count;
          }
        }
        continue;
      }

      // Flexible notes: chord-tone snapping + createBachNote cascade.
      uint8_t desired_pitch = note.pitch;
      bool is_strong = (note.start_tick % kTicksPerBeat == 0);

      if (is_strong && !is_chord_tone) {
        // Only snap if the nearest chord tone is within 2 semitones
        // to prevent melodic destruction from large snaps.
        uint8_t snap_candidate = nearestChordTone(note.pitch, harm_ev);
        int snap_dist = std::abs(static_cast<int>(snap_candidate) -
                                 static_cast<int>(note.pitch));
        if (snap_dist <= 2) {
          desired_pitch = snap_candidate;
        }
      }

      // Pre-check: if desired_pitch would form parallels with structural notes,
      // shift by step before handing to createBachNote (C-alt1 strategy).
      {
        auto par = checkParallelsAndP4Bass(
            post_state, post_rules, note.voice, desired_pitch,
            note.start_tick, num_voices);
        if (par.has_parallel_perfect || par.has_p4_bass) {
          // Try +1, -1, +2, -2 semitone shifts.
          for (int shift : {1, -1, 2, -2}) {
            int cand = static_cast<int>(desired_pitch) + shift;
            if (cand < 0 || cand > 127) continue;
            auto check = checkParallelsAndP4Bass(
                post_state, post_rules, note.voice,
                static_cast<uint8_t>(cand), note.start_tick, num_voices);
            if (!check.has_parallel_perfect && !check.has_p4_bass) {
              desired_pitch = static_cast<uint8_t>(cand);
              break;
            }
          }
        }
      }

      // Lookahead: find next pitch for the same voice for NHT validation.
      uint8_t lookahead_pitch = 0;
      for (size_t nxt = note_idx + 1; nxt < all_notes.size(); ++nxt) {
        if (all_notes[nxt].voice == note.voice) {
          lookahead_pitch = all_notes[nxt].pitch;
          break;
        }
      }

      BachNoteOptions opts;
      opts.voice = note.voice;
      opts.desired_pitch = desired_pitch;
      opts.tick = note.start_tick;
      opts.duration = note.duration;
      opts.velocity = note.velocity;
      opts.source = source;
      opts.next_pitch = lookahead_pitch;

      BachCreateNoteResult cp_result = createBachNote(
          &post_state, &post_rules, &post_resolver, opts);

      if (cp_result.accepted) {
        validated_notes.push_back(cp_result.note);
        if (cp_result.was_adjusted) ++adjusted;

        // Check for voice crossings at this tick.
        for (uint8_t other = 0; other < num_voices; ++other) {
          if (other == note.voice) continue;
          const NoteEvent* other_note = post_state.getNoteAt(other, note.start_tick);
          if (!other_note) continue;
          if (note.voice < other && cp_result.note.pitch < other_note->pitch) ++crossings;
          if (note.voice > other && cp_result.note.pitch > other_note->pitch) ++crossings;
        }

        // Count dissonances.
        if (!isChordTone(cp_result.note.pitch, harm_ev) && is_strong) {
          ++dissonances;
        }
      } else {
        ++rejected;
      }
    }

    all_notes = std::move(validated_notes);

    // Diatonic enforcement sweep: snap non-diatonic notes to the nearest scale
    // tone unless they are permitted chromatic alterations (raised 7th, chord
    // tones of secondary dominants, or notes at modulation boundaries).
    ScaleType effective_scale =
        config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    for (auto& note : all_notes) {
      Key note_key = tonal_plan.keyAtTick(note.start_tick);
      const HarmonicEvent& harm = detailed_timeline.getAt(note.start_tick);

      if (scale_util::isScaleTone(note.pitch, note_key, effective_scale)) {
        continue;
      }

      // Allow permitted chromatic alterations.
      if (isAllowedChromatic(note.pitch, note_key, effective_scale, &harm)) {
        continue;
      }

      // Modulation boundary tolerance: within 1 beat of a key change, allow
      // pitches that are diatonic in either the old or new key.
      constexpr Tick kBoundaryTolerance = kTicksPerBeat;
      Key prev_key = tonal_plan.keyAtTick(
          (note.start_tick > kBoundaryTolerance)
              ? note.start_tick - kBoundaryTolerance : 0);
      if (prev_key != note_key) {
        if (scale_util::isScaleTone(note.pitch, prev_key, effective_scale)) {
          continue;
        }
      }

      // Snap to nearest diatonic tone.
      uint8_t snapped = scale_util::nearestScaleTone(note.pitch, note_key,
                                                     effective_scale);

      // Structural notes: snap without parallel check (identity preservation).
      uint8_t old_pitch = note.pitch;
      if (isStructuralSource(note.source)) {
        note.pitch = snapped;
        if (note.pitch != old_pitch) {
          post_state.updateNotePitchAt(note.voice, note.start_tick, note.pitch);
        }
        continue;
      }

      // Flexible notes: check if snap creates parallels with registered voices.
      auto par = checkParallelsAndP4Bass(post_state, post_rules,
                                         note.voice, snapped,
                                         note.start_tick, num_voices);
      if (!par.has_parallel_perfect) {
        note.pitch = snapped;
      } else {
        // Try alternate scale tones within +/-4 semitones.
        bool fixed = false;
        for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
          int cand = static_cast<int>(note.pitch) + delta;
          if (cand < 0 || cand > 127) continue;
          uint8_t ucand = static_cast<uint8_t>(cand);
          if (!scale_util::isScaleTone(ucand, note_key, effective_scale)) continue;
          auto check = checkParallelsAndP4Bass(post_state, post_rules,
                                               note.voice, ucand,
                                               note.start_tick, num_voices);
          if (!check.has_parallel_perfect) {
            note.pitch = ucand;
            fixed = true;
            break;
          }
        }
        if (!fixed) {
          note.pitch = snapped;  // Accept snap (structural parallel, can't fix).
        }
      }
      // Keep post_state in sync so subsequent checks use updated pitches.
      if (note.pitch != old_pitch) {
        post_state.updateNotePitchAt(note.voice, note.start_tick, note.pitch);
      }
    }

    // Parallel repair pass: shared utility for detecting and fixing
    // parallel perfect consonances via dual-scan and pitch shifting.
    if (num_voices >= 2) {
      ParallelRepairParams repair_params;
      repair_params.num_voices = num_voices;
      repair_params.scale = effective_scale;
      repair_params.key_at_tick = [&](Tick t) { return tonal_plan.keyAtTick(t); };
      repair_params.voice_range = [&](uint8_t v) {
        return getFugueVoiceRange(v, num_voices);
      };
      repairParallelPerfect(all_notes, repair_params);
    }

    // Compute quality metrics.
    // NOTE: current_tick == stretto_end here (not updated after coda generation).
    // Adding kCodaBars is correct -- this is the only place coda duration is counted.
    Tick total_duration_ticks = current_tick + kTicksPerBar * kCodaBars;
    float total_beats = static_cast<float>(total_duration_ticks) /
                        static_cast<float>(kTicksPerBeat);
    int total_notes = static_cast<int>(all_notes.size());

    result.quality.dissonance_per_beat = (total_beats > 0)
        ? static_cast<float>(dissonances) / total_beats : 0.0f;
    result.quality.chord_tone_ratio = (total_notes > 0)
        ? static_cast<float>(chord_tones) / static_cast<float>(pre_count) : 0.0f;
    result.quality.voice_crossings = crossings;
    result.quality.notes_rejected = rejected;
    result.quality.notes_adjusted = adjusted;
    result.quality.counterpoint_compliance = (pre_count > 0)
        ? static_cast<float>(total_notes) / static_cast<float>(pre_count) : 1.0f;
  }

  // =========================================================================
  // Within-voice overlap cleanup: truncate earlier notes that extend into
  // the next note's start tick in the same voice.
  // =========================================================================
  {
    // Sort by voice, then start_tick, then duration descending (so that
    // std::unique keeps the longer note when removing same-tick duplicates).
    std::sort(all_notes.begin(), all_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.voice != b.voice) return a.voice < b.voice;
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                return a.duration > b.duration;  // Longer note first.
              });

    // Standard musical duration values for snapping truncated notes.
    static constexpr Tick kStandardDurations[] = {
        1920, 1440, 960, 720, 480, 360, 240, 120};

    // Remove same-tick duplicates within the same voice: keep the longer note.
    all_notes.erase(
        std::unique(all_notes.begin(), all_notes.end(),
                    [](const NoteEvent& a, const NoteEvent& b) {
                      return a.voice == b.voice && a.start_tick == b.start_tick;
                    }),
        all_notes.end());

    for (size_t i = 0; i + 1 < all_notes.size(); ++i) {
      if (all_notes[i].voice != all_notes[i + 1].voice) continue;
      Tick end_tick = all_notes[i].start_tick + all_notes[i].duration;
      if (end_tick > all_notes[i + 1].start_tick) {
        Tick trimmed = all_notes[i + 1].start_tick - all_notes[i].start_tick;
        // Snap trimmed duration to nearest standard musical value (floor),
        // but never exceed the gap to avoid re-introducing overlaps.
        Tick snapped = trimmed;  // Default: exact gap (safe).
        for (Tick std_dur : kStandardDurations) {
          if (trimmed >= std_dur) {
            snapped = std_dur;
            break;
          }
        }
        all_notes[i].duration = std::min(snapped, trimmed);
        if (all_notes[i].duration == 0) {
          all_notes[i].duration = 1;  // Avoid zero-duration notes.
        }
      }
    }
  }

  // Store the beat-resolution timeline for dual-timeline analysis.
  // Cannot std::move because detailed_timeline is still referenced below.
  result.generation_timeline = detailed_timeline;

  // =========================================================================
  // Create tracks and assign notes
  // =========================================================================
  result.tracks = createOrganTracks(num_voices);
  assignNotesToTracks(all_notes, result.tracks);
  sortTrackNotes(result.tracks);

  // Build bar-resolution timeline with real progressions for analysis output.
  // This matches Bach's actual harmonic rhythm and prevents inflated SOCC counts.
  // NOTE: current_tick is still stretto_end. Coda adds kCodaBars once, not double-counted.
  Tick total_ticks = current_tick + kTicksPerBar * kCodaBars;
  {
    HarmonicTimeline bar_timeline;
    struct Region { Key key; Tick start; Tick end; };
    std::vector<Region> regions;
    const auto& mods = tonal_plan.modulations;
    if (mods.empty()) {
      regions.push_back({config.key, 0, total_ticks});
    } else {
      if (mods[0].tick > 0) {
        regions.push_back({config.key, 0, mods[0].tick});
      }
      for (size_t i = 0; i < mods.size(); ++i) {
        Tick rstart = mods[i].tick;
        Tick rend = (i + 1 < mods.size()) ? mods[i + 1].tick : total_ticks;
        if (rend > rstart) {
          regions.push_back({mods[i].target_key, rstart, rend});
        }
      }
    }
    for (const auto& region : regions) {
      Tick rdur = region.end - region.start;
      if (rdur == 0) continue;
      KeySignature ks;
      ks.tonic = region.key;
      ks.is_minor = config.is_minor;
      HarmonicTimeline rtl = HarmonicTimeline::createProgression(
          ks, rdur, HarmonicResolution::Bar, ProgressionType::CircleOfFifths);
      for (const auto& ev : rtl.events()) {
        HarmonicEvent offset_ev = ev;
        offset_ev.tick += region.start;
        offset_ev.end_tick += region.start;
        bar_timeline.addEvent(offset_ev);
      }
    }
    result.timeline = bar_timeline;
  }

  // Apply cadence plan to the timeline (activates existing CadenceType/applyCadence).
  KeySignature home_key_sig;
  home_key_sig.tonic = config.key;
  home_key_sig.is_minor = config.is_minor;
  CadencePlan cadence_plan = CadencePlan::createForFugue(
      structure, home_key_sig, config.is_minor);
  cadence_plan.applyTo(result.timeline);

  // --- Apply harmonic rhythm factors (lazy evaluation on the timeline events) ---
  {
    std::vector<Tick> cadence_ticks;
    cadence_ticks.reserve(cadence_plan.points.size());
    for (const auto& cadence_point : cadence_plan.points) {
      cadence_ticks.push_back(cadence_point.tick);
    }
    applyRhythmFactors(result.timeline.mutableEvents(), total_ticks, cadence_ticks);
  }

  // --- Coda cadential pedal point (tonic, last 2-4 bars) ---
  // Only add if the fugue has a pedal voice and enough duration.
  if (config.num_voices >= 3 && total_ticks > kTicksPerBar * 4) {
    uint8_t pedal_voice = config.num_voices - 1;
    Tick pedal_bars = (config.num_voices >= 4) ? 4 : 2;
    Tick pedal_start = total_ticks - kTicksPerBar * pedal_bars;
    auto pedal_notes = generateCadentialPedal(
        home_key_sig, pedal_start, total_ticks,
        PedalPointType::Tonic, pedal_voice);
    if (pedal_voice < result.tracks.size()) {
      for (auto& n : pedal_notes) {
        result.tracks[pedal_voice].notes.push_back(n);
      }
    }
  }

  // --- Picardy third (minor keys only) ---
  if (config.enable_picardy && config.is_minor) {
    for (auto& track : result.tracks) {
      applyPicardyToFinalChord(track.notes, home_key_sig,
                               total_ticks - kTicksPerBar);
    }
  }

  // Re-sort tracks after adding pedal/Picardy notes.
  for (auto& track : result.tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });
  }

  // --- Apply extended N-point registration plan ---
  // Insert CC#7/CC#11 events at every section boundary for fine-grained
  // dynamic control following the fugue's structural arc.
  {
    ExtendedRegistrationPlan ext_plan =
        createExtendedRegistrationPlan(structure.sections, total_ticks);

    // Ensure tutti registration at coda if not already present.
    bool has_coda_tutti = false;
    Tick coda_threshold = total_ticks > kTicksPerBar * 4
                              ? total_ticks - kTicksPerBar * 4
                              : 0;
    for (const auto& pt : ext_plan.points) {
      if (pt.tick >= coda_threshold && pt.registration.velocity_hint >= 100) {
        has_coda_tutti = true;
        break;
      }
    }
    if (!has_coda_tutti && total_ticks > kTicksPerBar * 2) {
      ext_plan.addPoint(total_ticks - kTicksPerBar * 2,
                        OrganRegistrationPresets::tutti(), "coda_tutti");
    }

    applyExtendedRegistrationPlan(result.tracks, ext_plan);
  }

  result.success = true;
  return result;
}

}  // namespace bach
