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
#include "counterpoint/cross_relation.h"
#include "counterpoint/repeated_note_repair.h"
#include "core/note_creator.h"
#include "fugue/answer.h"
#include "fugue/archetype_policy.h"
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
/// @brief Get tonic bass pitch adjusted for the actual lowest voice range.
///
/// For 4+ voices (pedaliter), returns the standard pedal-register tonic (octave 2).
/// For 2-3 voices (manualiter), octave-shifts to fit the lowest voice range
/// per Baroque manualiter practice (BWV 552/2, 541/2).
static uint8_t tonicBassPitchForVoices(Key key, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(num_voices - 1, num_voices);
  int base = 36 + static_cast<int>(key);  // Octave 2 default
  int center = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
  int shift = nearestOctaveShift(center - base);
  int adjusted = base + shift;
  return clampPitch(adjusted, lo, hi);
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
    if (getPitchClass(static_cast<uint8_t>(cand)) == getPitchClassSigned(target_pc)) {
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
      auto [v0_lo, v0_hi] = getFugueVoiceRange(0, num_voices);
      int prev = static_cast<int>(last_pitches[0]);
      int best_dist = std::abs(prev - base_tonic);
      for (int oct : {-12, 12, -24, 24}) {
        int cand = tonic_pitch + oct;
        if (cand < v0_lo || cand + 7 > v0_hi) continue;  // Entire motif must fit
        int dist = std::abs(prev - cand);
        if (dist < best_dist) {
          best_dist = dist;
          base_tonic = cand;
        }
      }
      // Hard gate: max leap of 12 semitones (octave)
      int leap = std::abs(prev - base_tonic);
      if (leap > 12) base_tonic = tonic_pitch;
    }
    int head_pitches[] = {base_tonic, base_tonic + 2, base_tonic + third, base_tonic + 7,
                          base_tonic + third, base_tonic + 2, base_tonic, base_tonic};
    int head_count = std::min(8, static_cast<int>(stage1_dur / sub_dur));
    auto [head_lo, head_hi] = getFugueVoiceRange(0, num_voices);
    for (int idx = 0; idx < head_count; ++idx) {
      NoteEvent note;
      note.start_tick = start_tick + static_cast<Tick>(idx) * sub_dur;
      note.duration = sub_dur;
      note.pitch = clampPitch(head_pitches[idx], head_lo, head_hi);
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
    int tonic_pc = getPitchClass(static_cast<uint8_t>(tonic_pitch));
    int third_pc = (tonic_pitch + chord_third) % 12;
    int fifth_pc = (tonic_pitch + 7) % 12;
    int chord_pcs[] = {tonic_pc, third_pc, fifth_pc};

    int stage1_offsets[] = {7, chord_third, -12, 12};
    uint8_t count = std::min(static_cast<uint8_t>(num_voices - 1), static_cast<uint8_t>(4));
    std::sort(stage1_offsets, stage1_offsets + count, std::greater<int>());

    for (uint8_t i = 0; i < count; ++i) {
      uint8_t voice_idx = 1 + i;
      auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
      uint8_t target_pitch;

      if (last_pitches && last_pitches[voice_idx] > 0) {
        // Voice-leading: find nearest chord tone to previous pitch.
        uint8_t prev = last_pitches[voice_idx];
        int best_dist = 999;
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[i], vlo, vhi);
        for (int pc : chord_pcs) {
          uint8_t cand = nearestPitchWithPC(prev, pc, 7);
          int dist = std::abs(static_cast<int>(cand) - static_cast<int>(prev));
          if (dist < best_dist) {
            best_dist = dist;
            target_pitch = cand;
          }
        }
        target_pitch = clampPitch(static_cast<int>(target_pitch), vlo, vhi);
      } else {
        // Fallback: use fixed offsets.
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[i], vlo, vhi);
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
    // All voices participate in V7-I cadence (lowest voice pedal is Stage 3 only).
    int dom_pitch = tonic_pitch + 7;  // Dominant
    {
      // Voice-specific V7 voicings (dom_pitch-relative offsets).
      // 3v: {+4(B), -2(F), -12(G bass)}
      // 4v: {+4(B), +7(D), -2(F), -12(G bass)}
      // 5v: {+4(B), +7(D), -2(F), -12(G bass), -17(C below)}
      int dom7_3[] = {4, -2, -12};
      int dom7_4[] = {4, 7, -2, -12};
      int dom7_5[] = {4, 7, -2, -12, -17};
      int* dom7_offsets;
      if (num_voices <= 3) dom7_offsets = dom7_3;
      else if (num_voices == 4) dom7_offsets = dom7_4;
      else dom7_offsets = dom7_5;
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      for (uint8_t v = 0; v < count; ++v) {
        auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);
        NoteEvent note;
        note.start_tick = stage2_start;
        note.duration = half_bar;
        note.pitch = clampPitch(dom_pitch + dom7_offsets[v], vlo, vhi);
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }

    // I chord (second half): tonic resolution.
    // All voices participate in V7-I cadence (lowest voice pedal is Stage 3 only).
    {
      // Voice-specific I resolutions (tonic_pitch-relative offsets).
      // B→C(↑m2), F→E(↓m2), G→C(↑P4) — functional voice leading.
      // 3v: {+12(C5), +4(E), 0(C)}
      // 4v: {+12(C5), +7(G), +4(E), 0(C)}
      // 5v: {+12(C5), +7(G), +4(E), 0(C), -12(C below)}
      int res_third = is_minor ? 3 : 4;
      int tonic_3[] = {12, res_third, 0};
      int tonic_4[] = {12, 7, res_third, 0};
      int tonic_5[] = {12, 7, res_third, 0, -12};
      int* tonic_offsets;
      if (num_voices <= 3) tonic_offsets = tonic_3;
      else if (num_voices == 4) tonic_offsets = tonic_4;
      else tonic_offsets = tonic_5;
      uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
      for (uint8_t v = 0; v < count; ++v) {
        auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);
        NoteEvent note;
        note.start_tick = stage2_start + half_bar;
        note.duration = stage2_dur - half_bar;
        note.pitch = clampPitch(tonic_pitch + tonic_offsets[v], vlo, vhi);
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
    // Upper voices only (consistent with Stage 2).
    int third_offset = is_minor ? 4 : 4;  // Major 3rd in both (Picardy)
    {
      uint8_t upper_count = (num_voices > 1) ? num_voices - 1 : num_voices;
      int final_offsets[] = {0, 7, third_offset, -12, 12};
      uint8_t count = std::min(upper_count, static_cast<uint8_t>(5));
      std::sort(final_offsets, final_offsets + count, std::greater<int>());
      for (uint8_t v = 0; v < count; ++v) {
        auto [vlo, vhi] = getFugueVoiceRange(v, num_voices);
        NoteEvent note;
        note.start_tick = stage3_start;
        note.duration = stage3_dur;
        note.pitch = clampPitch(tonic_pitch + final_offsets[v], vlo, vhi);
        note.velocity = kOrganVelocity;
        note.voice = v;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }
  }

  return notes;
}

/// @brief Get a phase-aware ceiling factor for voice ranges.
///
/// Controls the effective upper range of voices based on fugue position,
/// encouraging the climax to occur in the Develop phase (55-75%) rather
/// than at the very end.
///
/// @param position Normalized position in the fugue (0.0 = start, 1.0 = end).
/// @return Ceiling factor in [0.0, 1.0] to multiply against the voice range.
static float getPhaseAwareCeiling(float position) {
  if (position < 0.25f) return 0.85f;   // Establish: restrained.
  if (position < 0.55f) return 0.92f;   // Early Develop: opening up.
  if (position < 0.75f) return 1.00f;   // Climax Zone: full range.
  return 0.95f;                          // Late Resolve: pulling back.
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
  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);
  Answer answer = generateAnswer(subject, config.answer_type, policy.preferred_answer);

  // =========================================================================
  // Step 3: Generate countersubject(s)
  // =========================================================================
  Countersubject counter_subject =
      generateCountersubject(subject, config.seed + 1000, 5,
                              config.archetype);

  // For 4+ voices, generate a second countersubject that contrasts with both
  // the subject and the first countersubject.
  Countersubject counter_subject_2;
  if (num_voices >= 4) {
    counter_subject_2 = generateSecondCountersubject(
        subject, counter_subject, config.seed + 5000, 5,
        config.archetype);
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
  cp_resolver.setHarmonicTimeline(&detailed_timeline);

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

    uint8_t entry_last = extractVoiceLastPitch(all_notes, current_tick, entry_voice);
    MiddleEntry middle_entry = use_false_entry
        ? generateFalseEntry(subject, target_key, current_tick, entry_voice, num_voices)
        : generateMiddleEntry(subject, target_key,
                              current_tick, entry_voice, num_voices,
                              cp_state, cp_rules, cp_resolver,
                              detailed_timeline, entry_last);
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
    auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest_voice, num_voices);
    uint8_t tonic_for_pedal = tonicBassPitchForVoices(config.key, num_voices);
    uint8_t dominant_pitch = clampPitch(
        static_cast<int>(tonic_for_pedal) + interval::kPerfect5th, ped_lo, ped_hi);

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
        cp_state, cp_rules, cp_resolver, detailed_timeline,
        dominant_pitch);
    all_notes.insert(all_notes.end(), pedal_episode.notes.begin(),
                     pedal_episode.notes.end());

    current_tick += pedal_duration;
  }

  // --- Stretto (Resolve) ---
  uint8_t stretto_last[5] = {0, 0, 0, 0, 0};
  for (uint8_t v = 0; v < num_voices && v < 5; ++v) {
    stretto_last[v] = extractVoiceLastPitch(all_notes, current_tick, v);
  }
  Stretto stretto = generateStretto(subject, config.key, current_tick,
                                    num_voices, config.seed + 4000,
                                    config.character,
                                    cp_state, cp_rules, cp_resolver,
                                    detailed_timeline, stretto_last);
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

  // Tonic pedal in coda: lowest voice sustains the tonic in Stage 3 only.
  // Stages 1-2 bass from createCodaNotes is preserved for V7→I cadence.
  {
    uint8_t tonic_pitch = tonicBassPitchForVoices(config.key, num_voices);

    // Pedal starts at Stage 3 (last bar of the 4-bar coda).
    Tick pedal_start = current_tick + kTicksPerBar * 3;
    Tick pedal_dur = current_tick + coda_duration - pedal_start;

    removeLowestVoiceNotes(all_notes, lowest_voice,
                           pedal_start, current_tick + coda_duration);

    auto tonic_pedal = generatePedalPoint(tonic_pitch, pedal_start,
                                          pedal_dur, lowest_voice);
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
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
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
    post_resolver.setHarmonicTimeline(&detailed_timeline);

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
            auto [fix_lo, fix_hi] = getFugueVoiceRange(note.voice, num_voices);
            if (candidate < static_cast<int>(fix_lo) ||
                candidate > static_cast<int>(fix_hi)) continue;
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
              fixed_note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
              break;
            }
          }
        }
        // Apply phase-aware ceiling to structural notes too.
        // If the ceiling would create a parallel perfect, revert to the
        // original pitch — the structural note was already valid before.
        if (estimated_duration > 0) {
          uint8_t pre_ceiling_pitch = fixed_note.pitch;
          float pos = static_cast<float>(note.start_tick) /
                      static_cast<float>(estimated_duration);
          float ceiling = getPhaseAwareCeiling(pos);
          if (note.voice < num_voices) {
            auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
            int range = static_cast<int>(hi) - static_cast<int>(lo);
            int ceiling_pitch = static_cast<int>(lo) +
                                static_cast<int>(static_cast<float>(range) * ceiling);
            if (fixed_note.pitch > static_cast<uint8_t>(ceiling_pitch)) {
              fixed_note.pitch = static_cast<uint8_t>(ceiling_pitch);
              fixed_note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
            }
          }
          // Revert if ceiling introduced a parallel perfect.
          if (fixed_note.pitch != pre_ceiling_pitch) {
            auto par = checkParallelsAndP4Bass(
                post_state, post_rules, note.voice, fixed_note.pitch,
                note.start_tick, num_voices);
            if (par.has_parallel_perfect) {
              fixed_note.pitch = pre_ceiling_pitch;
              fixed_note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
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

      // Phase-aware range ceiling: constrain upper pitch to discourage
      // climax at the very end of the piece. Applied BEFORE parallel pre-check
      // so that the pre-check operates on the ceiling-constrained pitch.
      if (estimated_duration > 0) {
        float pos = static_cast<float>(note.start_tick) /
                    static_cast<float>(estimated_duration);
        float ceiling = getPhaseAwareCeiling(pos);
        if (note.voice < num_voices) {
          auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
          int range = static_cast<int>(hi) - static_cast<int>(lo);
          int ceiling_pitch = static_cast<int>(lo) +
                              static_cast<int>(static_cast<float>(range) * ceiling);
          if (desired_pitch > static_cast<uint8_t>(ceiling_pitch)) {
            desired_pitch = static_cast<uint8_t>(ceiling_pitch);
          }
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

    ScaleType effective_scale =
        config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    // -----------------------------------------------------------------------
    // Parallel repair pass (primary): fix parallel perfect consonances while
    // notes still have maximum pitch flexibility (before diatonic snapping).
    // -----------------------------------------------------------------------
    if (num_voices >= 2) {
      ParallelRepairParams repair_params;
      repair_params.num_voices = num_voices;
      repair_params.scale = effective_scale;
      repair_params.key_at_tick = [&](Tick t) { return tonal_plan.keyAtTick(t); };
      repair_params.voice_range = [&](uint8_t v) {
        return getFugueVoiceRange(v, num_voices);
      };
      repairParallelPerfect(all_notes, repair_params);

      // Rebuild post_state so diatonic enforcement sees updated pitches.
      post_state.clear();
      for (uint8_t v = 0; v < num_voices; ++v) {
        auto [lo, hi] = getFugueVoiceRange(v, num_voices);
        post_state.registerVoice(v, lo, hi);
      }
      post_state.setKey(config.key);
      for (const auto& note : all_notes) {
        post_state.addNote(note.voice, note);
      }
    }

    // Build per-voice sorted index for cross-relation detection.
    std::vector<std::vector<size_t>> dia_voice_idx(num_voices);
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice < num_voices) {
        dia_voice_idx[all_notes[idx].voice].push_back(idx);
      }
    }
    for (auto& voice_indices : dia_voice_idx) {
      std::sort(voice_indices.begin(), voice_indices.end(),
                [&](size_t lhs, size_t rhs) {
                  return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
                });
    }

    // -----------------------------------------------------------------------
    // Diatonic enforcement sweep: snap non-diatonic notes to the nearest scale
    // tone unless they are permitted chromatic alterations (raised 7th, chord
    // tones of secondary dominants, or notes at modulation boundaries).
    // -----------------------------------------------------------------------
    // Track last pitch per voice for melodic leap validation in sweep.
    uint8_t dia_last_pitch[5] = {0, 0, 0, 0, 0};

    for (auto& note : all_notes) {
      // Update tracker unconditionally (including notes that will be skipped).
      // This ensures dia_last_pitch always reflects the most recent note,
      // even for notes that are already diatonic and skip further processing.
      uint8_t prev_pitch_for_voice = dia_last_pitch[note.voice];
      dia_last_pitch[note.voice] = note.pitch;

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

      // Structural notes: snap with melodic leap validation.
      uint8_t old_pitch = note.pitch;
      if (isStructuralSource(note.source)) {
        // Check melodic leap before accepting snap.
        bool snap_ok = true;
        if (prev_pitch_for_voice > 0 &&
            absoluteInterval(snapped, prev_pitch_for_voice) > 12) {
          snap_ok = false;
        }
        if (snap_ok) {
          note.pitch = snapped;
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
        } else {
          // Graduated fallback: try nearby scale tones within leap limit.
          for (int delta : {1, -1, 2, -2}) {
            int cand = static_cast<int>(note.pitch) + delta;
            if (cand < 0 || cand > 127) continue;
            uint8_t ucand = static_cast<uint8_t>(cand);
            if (!scale_util::isScaleTone(ucand, note_key, effective_scale))
              continue;
            if (absoluteInterval(ucand, prev_pitch_for_voice) <= 12) {
              note.pitch = ucand;
              note.modified_by |=
                  static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
              break;
            }
          }
          // If no alternative found, keep original pitch
          // (melodic continuity > diatonic purity).
        }
        if (note.pitch != old_pitch) {
          post_state.updateNotePitchAt(note.voice, note.start_tick, note.pitch);
        }
        dia_last_pitch[note.voice] = note.pitch;
        continue;
      }

      // Flexible notes: check if snap creates parallels, cross-relations,
      // or excessive melodic leaps.
      auto par = checkParallelsAndP4Bass(post_state, post_rules,
                                         note.voice, snapped,
                                         note.start_tick, num_voices);
      bool has_cross = hasCrossRelation(all_notes, num_voices,
                                        note.voice, snapped, note.start_tick);
      bool leap_ok = (prev_pitch_for_voice == 0 ||
                      absoluteInterval(snapped, prev_pitch_for_voice) <= 12);
      if (!par.has_parallel_perfect && !has_cross && leap_ok) {
        note.pitch = snapped;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
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
          bool cand_cross = hasCrossRelation(all_notes, num_voices,
                                             note.voice, ucand, note.start_tick);
          bool cand_leap_ok =
              (prev_pitch_for_voice == 0 ||
               absoluteInterval(ucand, prev_pitch_for_voice) <= 12);
          if (!check.has_parallel_perfect && !cand_cross && cand_leap_ok) {
            note.pitch = ucand;
            note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
            fixed = true;
            break;
          }
        }
        if (!fixed) {
          note.pitch = snapped;  // Accept snap (structural parallel, can't fix).
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
        }
      }
      // Keep post_state in sync so subsequent checks use updated pitches.
      if (note.pitch != old_pitch) {
        post_state.updateNotePitchAt(note.voice, note.start_tick, note.pitch);
      }
      dia_last_pitch[note.voice] = note.pitch;
    }

    // -----------------------------------------------------------------------
    // Regression parallel repair: catch any parallels introduced by diatonic
    // snapping. Limited to 1 iteration since most should already be fixed.
    // -----------------------------------------------------------------------
    if (num_voices >= 2) {
      ParallelRepairParams regression_params;
      regression_params.num_voices = num_voices;
      regression_params.scale = effective_scale;
      regression_params.key_at_tick = [&](Tick t) {
        return tonal_plan.keyAtTick(t);
      };
      regression_params.voice_range = [&](uint8_t v) {
        return getFugueVoiceRange(v, num_voices);
      };
      regression_params.max_iterations = 1;
      repairParallelPerfect(all_notes, regression_params);
    }

    // -----------------------------------------------------------------------
    // Dissonance resolution sweep: fix unresolved dissonances by adjusting
    // flexible notes at the next beat to resolve by step to consonance.
    // Targets structural notes (countersubject, subject) that create
    // dissonances bypassing the collision resolver.
    // -----------------------------------------------------------------------
    {
      struct VN { size_t idx; Tick start; Tick end_t; };
      std::vector<std::vector<VN>> dvns(num_voices);
      for (size_t i = 0; i < all_notes.size(); ++i) {
        auto& n = all_notes[i];
        if (n.voice < num_voices) {
          dvns[n.voice].push_back({i, n.start_tick, n.start_tick + n.duration});
        }
      }
      for (auto& v : dvns) {
        std::sort(v.begin(), v.end(),
                  [](const VN& a, const VN& b) { return a.start < b.start; });
      }

      auto dSounding = [&](uint8_t v, Tick t) -> int {
        for (auto it = dvns[v].rbegin(); it != dvns[v].rend(); ++it) {
          if (it->start <= t && t < it->end_t)
            return static_cast<int>(it->idx);
          if (it->end_t <= t) return -1;
        }
        return -1;
      };

      Tick d_max_tick = 0;
      for (const auto& n : all_notes) {
        Tick e = n.start_tick + n.duration;
        if (e > d_max_tick) d_max_tick = e;
      }

      constexpr Tick kHalfBeat = kTicksPerBeat / 2;

      for (Tick beat = 0; beat < d_max_tick; beat += kTicksPerBeat) {
        for (uint8_t va = 0; va < num_voices; ++va) {
          for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
            int dia = dSounding(va, beat);
            int dib = dSounding(vb, beat);
            if (dia < 0 || dib < 0) continue;

            int dist = std::abs(static_cast<int>(all_notes[dia].pitch) -
                                static_cast<int>(all_notes[dib].pitch));
            if (dist > 12) continue;
            int ic = interval_util::compoundToSimple(dist);
            if (interval_util::isConsonance(ic)) continue;
            // Skip short passing/neighbor tones.
            if (all_notes[dia].duration <= kHalfBeat ||
                all_notes[dib].duration <= kHalfBeat) continue;

            // Suspension preservation: on strong beats, detect the pattern
            // preparation (consonant hold) -> dissonance -> stepwise resolution.
            // Properly formed suspensions are a key expressive device and should
            // not be "fixed" by the dissonance sweep.
            bool is_suspension = false;
            {
              Tick bar_offset = beat % kTicksPerBar;
              bool is_strong_beat = (bar_offset == 0 ||
                                     bar_offset == 2 * kTicksPerBeat);
              if (is_strong_beat && beat >= kTicksPerBeat) {
                for (int att = 0; att < 2 && !is_suspension; ++att) {
                  uint8_t fv = (att == 0) ? va : vb;  // Suspended voice.
                  uint8_t ov = (att == 0) ? vb : va;  // Other voice.
                  int fni = (fv == va) ? dia : dib;
                  // Preparation: same pitch held from previous beat, consonant
                  // with the other voice at that point.
                  Tick prev = beat - kTicksPerBeat;
                  int fprev = dSounding(fv, prev);
                  int oprev = dSounding(ov, prev);
                  if (fprev < 0 || oprev < 0) continue;
                  if (all_notes[fprev].pitch != all_notes[fni].pitch) continue;
                  int prev_dist = std::abs(
                      static_cast<int>(all_notes[fprev].pitch) -
                      static_cast<int>(all_notes[oprev].pitch));
                  if (prev_dist > 12) continue;
                  int prev_ic = interval_util::compoundToSimple(prev_dist);
                  if (!interval_util::isConsonance(prev_ic)) continue;
                  // Resolution: next beat steps down diatonically to consonance.
                  Tick next_t = beat + kTicksPerBeat;
                  int fnext = dSounding(fv, next_t);
                  int onext = dSounding(ov, next_t);
                  if (fnext < 0 || onext < 0) continue;
                  int fstep = static_cast<int>(all_notes[fni].pitch) -
                              static_cast<int>(all_notes[fnext].pitch);
                  Key sk = tonal_plan.keyAtTick(all_notes[fnext].start_tick);
                  if (fstep >= 1 && fstep <= 3 &&
                      scale_util::isScaleTone(all_notes[fnext].pitch, sk,
                                              effective_scale)) {
                    int res_dist = std::abs(
                        static_cast<int>(all_notes[fnext].pitch) -
                        static_cast<int>(all_notes[onext].pitch));
                    if (res_dist > 12 ||
                        interval_util::isConsonance(
                            interval_util::compoundToSimple(res_dist))) {
                      is_suspension = true;
                    }
                  }
                }
              }
            }
            if (is_suspension) continue;  // Preserve valid suspension.

            // Check if resolved within next 3 beats.
            bool resolved = false;
            for (int look = 1; look <= 3 && !resolved; ++look) {
              Tick fut = beat + static_cast<Tick>(look) * kTicksPerBeat;
              int fa = dSounding(va, fut);
              int fb = dSounding(vb, fut);
              if (fa < 0 || fb < 0) { resolved = true; break; }
              int fdist = std::abs(static_cast<int>(all_notes[fa].pitch) -
                                   static_cast<int>(all_notes[fb].pitch));
              if (fdist > 12 ||
                  interval_util::isConsonance(
                      interval_util::compoundToSimple(fdist))) {
                int sa = std::abs(static_cast<int>(all_notes[fa].pitch) -
                                  static_cast<int>(all_notes[dia].pitch));
                int sb = std::abs(static_cast<int>(all_notes[fb].pitch) -
                                  static_cast<int>(all_notes[dib].pitch));
                if ((sa >= 1 && sa <= 2) || (sb >= 1 && sb <= 2))
                  resolved = true;
              }
            }
            if (resolved) continue;

            // Unresolved. Adjust next flexible note to resolve by step.
            bool fixed = false;
            for (int look = 1; look <= 3 && !fixed; ++look) {
              Tick res_tick = beat + static_cast<Tick>(look) * kTicksPerBeat;
              for (int att = 0; att < 2 && !fixed; ++att) {
                uint8_t fv = (att == 0) ? va : vb;
                uint8_t ov = (att == 0) ? vb : va;
                int fni = dSounding(fv, res_tick);
                int oni = dSounding(ov, res_tick);
                if (fni < 0 || oni < 0) continue;
                if (isStructuralSource(all_notes[fni].source)) continue;
                int orig = (fv == va) ? static_cast<int>(all_notes[dia].pitch)
                                      : static_cast<int>(all_notes[dib].pitch);
                int op = static_cast<int>(all_notes[oni].pitch);
                Key fk = tonal_plan.keyAtTick(all_notes[fni].start_tick);
                for (int delta : {-1, -2, 1, 2}) {
                  int cand = orig + delta;
                  if (cand < 0 || cand > 127) continue;
                  uint8_t ucand = static_cast<uint8_t>(cand);
                  if (!scale_util::isScaleTone(ucand, fk, effective_scale))
                    continue;
                  int nd = std::abs(cand - op);
                  if (nd > 12 ||
                      interval_util::isConsonance(
                          interval_util::compoundToSimple(nd))) {
                    all_notes[fni].pitch = ucand;
                    all_notes[fni].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
                    fixed = true;
                    break;
                  }
                }
              }
            }
          }
        }
      }
    }

    // -----------------------------------------------------------------------
    // Voice crossing repair sweep: fix crossings introduced by diatonic
    // enforcement or dissonance resolution. Matches the Python validator's
    // adjacent-track check with 2-beat lookahead.
    // -----------------------------------------------------------------------
    {
      struct VCN { size_t idx; Tick start; Tick end_t; };
      std::vector<std::vector<VCN>> vcns(num_voices);
      for (size_t i = 0; i < all_notes.size(); ++i) {
        auto& n = all_notes[i];
        if (n.voice < num_voices) {
          vcns[n.voice].push_back({i, n.start_tick, n.start_tick + n.duration});
        }
      }
      for (auto& v : vcns) {
        std::sort(v.begin(), v.end(),
                  [](const VCN& a, const VCN& b) { return a.start < b.start; });
      }

      auto vcSounding = [&](uint8_t v, Tick t) -> int {
        for (auto it = vcns[v].rbegin(); it != vcns[v].rend(); ++it) {
          if (it->start <= t && t < it->end_t)
            return static_cast<int>(it->idx);
          if (it->end_t <= t) return -1;
        }
        return -1;
      };

      Tick vc_max_tick = 0;
      for (const auto& n : all_notes) {
        Tick e = n.start_tick + n.duration;
        if (e > vc_max_tick) vc_max_tick = e;
      }

      constexpr int kVCLookahead = 2;  // Match validator _LOOKAHEAD_BEATS.

      for (Tick beat = 0; beat < vc_max_tick; beat += kTicksPerBeat) {
        for (uint8_t va = 0; va + 1 < num_voices; ++va) {
          uint8_t vb = va + 1;
          int ia = vcSounding(va, beat);
          int ib = vcSounding(vb, beat);
          if (ia < 0 || ib < 0) continue;
          if (all_notes[ia].pitch >= all_notes[ib].pitch) continue;

          // Check if crossing resolves within lookahead.
          bool resolves = false;
          for (int ahead = 1; ahead <= kVCLookahead; ++ahead) {
            Tick fb = beat + static_cast<Tick>(ahead) * kTicksPerBeat;
            int fa = vcSounding(va, fb);
            int fbi = vcSounding(vb, fb);
            if (fa >= 0 && fbi >= 0 &&
                all_notes[fa].pitch >= all_notes[fbi].pitch) {
              resolves = true;
              break;
            }
          }
          if (resolves) continue;

          // Persistent crossing. Try octave shift on the flexible note.
          // Also check that the shift doesn't create a parallel perfect.
          auto tryOctaveFix = [&](int fix_idx, int other_idx,
                                  uint8_t fix_voice, bool is_upper) -> bool {
            if (isStructuralSource(all_notes[fix_idx].source)) return false;
            auto [lo, hi] = getFugueVoiceRange(fix_voice, num_voices);
            int old_pitch = static_cast<int>(all_notes[fix_idx].pitch);
            // Find previous beat's pitches for parallel detection.
            Tick prev_beat = (beat >= kTicksPerBeat) ? beat - kTicksPerBeat : 0;
            for (int shift : {12, -12, 24, -24}) {
              int cand = old_pitch + shift;
              if (cand < lo || cand > hi) continue;
              bool ok = is_upper
                  ? (cand >= static_cast<int>(all_notes[other_idx].pitch))
                  : (static_cast<int>(all_notes[other_idx].pitch) >= cand);
              if (!ok) continue;
              // Check parallel with all other voices.
              bool creates_parallel = false;
              for (uint8_t ov = 0; ov < num_voices && !creates_parallel; ++ov) {
                if (ov == fix_voice) continue;
                int ov_cur = vcSounding(ov, beat);
                int ov_prev = vcSounding(ov, prev_beat);
                if (ov_cur < 0 || ov_prev < 0) continue;
                int fix_prev_idx = vcSounding(fix_voice, prev_beat);
                if (fix_prev_idx < 0) continue;
                int prev_iv = interval_util::compoundToSimple(
                    std::abs(static_cast<int>(all_notes[fix_prev_idx].pitch) -
                             static_cast<int>(all_notes[ov_prev].pitch)));
                int new_iv = interval_util::compoundToSimple(
                    std::abs(cand -
                             static_cast<int>(all_notes[ov_cur].pitch)));
                if (prev_iv == new_iv &&
                    interval_util::isPerfectConsonance(new_iv) && new_iv != 0) {
                  int m1 = cand - static_cast<int>(all_notes[fix_prev_idx].pitch);
                  int m2 = static_cast<int>(all_notes[ov_cur].pitch) -
                           static_cast<int>(all_notes[ov_prev].pitch);
                  if ((m1 > 0 && m2 > 0) || (m1 < 0 && m2 < 0))
                    creates_parallel = true;
                }
              }
              if (creates_parallel) continue;
              all_notes[fix_idx].pitch = static_cast<uint8_t>(cand);
              all_notes[fix_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
              return true;
            }
            return false;
          };

          // Prefer fixing the lower voice (shift down) first.
          if (!tryOctaveFix(ib, ia, vb, false)) {
            tryOctaveFix(ia, ib, va, true);
          }
        }
      }
    }

    // -----------------------------------------------------------------------
    // Leap resolution repair sweep: after a leap (>=5 semitones), the next
    // note should step (1-2 st) in the opposite direction. Fix flexible
    // resolution notes; structural notes are left untouched.
    // Episode material is exempt (arpeggiated figures).
    // -----------------------------------------------------------------------
    {
      // Build per-voice sorted note indices.
      std::vector<std::vector<size_t>> lr_voices(num_voices);
      for (size_t i = 0; i < all_notes.size(); ++i) {
        if (all_notes[i].voice < num_voices) {
          lr_voices[all_notes[i].voice].push_back(i);
        }
      }
      for (auto& vi : lr_voices) {
        std::sort(vi.begin(), vi.end(),
                  [&](size_t a, size_t b) {
                    return all_notes[a].start_tick < all_notes[b].start_tick;
                  });
      }

      constexpr int kLeapThreshold = 5;

      // Beat-grid parallel checker: checks every beat boundary from the note's
      // onset until the next note in the same voice starts. This matches
      // countParallelPerfect's behavior of carrying prev_pitch forward.
      auto candidateCreatesParallel = [&](int cand, size_t note_i3,
                                          uint8_t voice_v,
                                          const std::vector<size_t>& voice_idxs,
                                          size_t triplet_k) -> bool {
        Tick n_start = all_notes[note_i3].start_tick;
        uint8_t n_voice = voice_v;

        // Extend scan to the start of the NEXT note in this voice (or end).
        Tick scan_end = n_start + all_notes[note_i3].duration;
        if (triplet_k + 3 < voice_idxs.size()) {
          scan_end = all_notes[voice_idxs[triplet_k + 3]].start_tick;
        }
        // Also include at least one beat beyond the note's end to catch
        // carry-forward parallels.
        Tick note_end = n_start + all_notes[note_i3].duration;
        if (scan_end < note_end + kTicksPerBeat) {
          scan_end = note_end + kTicksPerBeat;
        }

        Tick onset_beat = (n_start / kTicksPerBeat) * kTicksPerBeat;

        for (Tick beat = onset_beat; beat < scan_end; beat += kTicksPerBeat) {
          Tick prev_beat = (beat >= kTicksPerBeat) ? beat - kTicksPerBeat : 0;
          if (prev_beat == beat) continue;

          // Find pitch of this voice at prev_beat (carry-forward semantics).
          int self_prev = -1;
          for (auto it = lr_voices[n_voice].rbegin();
               it != lr_voices[n_voice].rend(); ++it) {
            const auto& on = all_notes[*it];
            if (on.start_tick <= prev_beat &&
                on.start_tick + on.duration > prev_beat) {
              self_prev = (*it == note_i3) ? cand
                          : static_cast<int>(on.pitch);
              break;
            }
          }
          // If no note is sounding, use carry-forward from last onset <= prev_beat.
          if (self_prev < 0) {
            for (auto it = lr_voices[n_voice].rbegin();
                 it != lr_voices[n_voice].rend(); ++it) {
              if (all_notes[*it].start_tick <= prev_beat) {
                self_prev = (*it == note_i3) ? cand
                            : static_cast<int>(all_notes[*it].pitch);
                break;
              }
            }
          }
          if (self_prev < 0) continue;

          // Pitch at current beat: if note is sounding, use cand; otherwise
          // carry forward the last known pitch.
          int self_cur = -1;
          if (n_start <= beat && note_end > beat) {
            self_cur = cand;
          } else {
            for (auto it = lr_voices[n_voice].rbegin();
                 it != lr_voices[n_voice].rend(); ++it) {
              const auto& on = all_notes[*it];
              if (on.start_tick <= beat && on.start_tick + on.duration > beat) {
                self_cur = (*it == note_i3) ? cand
                           : static_cast<int>(on.pitch);
                break;
              }
            }
            if (self_cur < 0) {
              // Carry forward: use last onset's pitch.
              for (auto it = lr_voices[n_voice].rbegin();
                   it != lr_voices[n_voice].rend(); ++it) {
                if (all_notes[*it].start_tick <= beat) {
                  self_cur = (*it == note_i3) ? cand
                             : static_cast<int>(all_notes[*it].pitch);
                  break;
                }
              }
            }
          }
          if (self_cur < 0) continue;

          for (uint8_t ov = 0; ov < num_voices; ++ov) {
            if (ov == n_voice) continue;
            int ov_prev = -1, ov_cur = -1;
            for (const auto& oi : lr_voices[ov]) {
              const auto& on = all_notes[oi];
              if (ov_prev < 0 && on.start_tick <= prev_beat &&
                  on.start_tick + on.duration > prev_beat)
                ov_prev = static_cast<int>(on.pitch);
              if (ov_cur < 0 && on.start_tick <= beat &&
                  on.start_tick + on.duration > beat)
                ov_cur = static_cast<int>(on.pitch);
              if (ov_prev >= 0 && ov_cur >= 0) break;
            }
            if (ov_prev < 0 || ov_cur < 0) continue;
            if (self_prev == self_cur && ov_prev == ov_cur) continue;

            int pi = std::abs(self_prev - ov_prev);
            int ci = std::abs(self_cur - ov_cur);
            if (!interval_util::isPerfectConsonance(pi) ||
                !interval_util::isPerfectConsonance(ci))
              continue;
            int ps = interval_util::compoundToSimple(pi);
            int cs = interval_util::compoundToSimple(ci);
            if (ps != cs) continue;
            int m1 = self_cur - self_prev, m2 = ov_cur - ov_prev;
            if ((m1 > 0 && m2 > 0) || (m1 < 0 && m2 < 0))
              return true;
          }
        }
        return false;
      };

      for (uint8_t v = 0; v < num_voices; ++v) {
        const auto& idxs = lr_voices[v];
        if (idxs.size() < 3) continue;
        for (size_t k = 0; k + 2 < idxs.size(); ++k) {
          size_t i1 = idxs[k], i2 = idxs[k + 1], i3 = idxs[k + 2];
          int leap = static_cast<int>(all_notes[i2].pitch) -
                     static_cast<int>(all_notes[i1].pitch);
          if (std::abs(leap) < kLeapThreshold) continue;

          // Exempt episode material (arpeggiated figures).
          if (all_notes[i1].source == BachNoteSource::EpisodeMaterial ||
              all_notes[i2].source == BachNoteSource::EpisodeMaterial) {
            continue;
          }

          // Check if already resolved.
          int resolution = static_cast<int>(all_notes[i3].pitch) -
                           static_cast<int>(all_notes[i2].pitch);
          bool resolved = (std::abs(resolution) >= 1 && std::abs(resolution) <= 2 &&
                           ((leap > 0) != (resolution > 0)));
          if (resolved) continue;

          // Only fix flexible notes.
          if (isStructuralSource(all_notes[i3].source)) continue;

          // Skip if i3 is already far from i2 (would create a large leap
          // from the candidate resolution pitch to i3's current register).
          int i3_dist = std::abs(static_cast<int>(all_notes[i3].pitch) -
                                 static_cast<int>(all_notes[i2].pitch));
          if (i3_dist > 4) continue;  // Only fix when i3 is within a M3 of i2.

          // Target: step 1-2 semitones opposite to leap direction.
          int step_dir = (leap > 0) ? -1 : 1;
          Key lr_key = tonal_plan.keyAtTick(all_notes[i3].start_tick);
          auto [lr_lo, lr_hi] = getFugueVoiceRange(v, num_voices);

          for (int delta : {1, 2}) {
            int cand = static_cast<int>(all_notes[i2].pitch) + step_dir * delta;
            if (cand < lr_lo || cand > lr_hi) continue;
            uint8_t ucand = static_cast<uint8_t>(cand);
            if (!scale_util::isScaleTone(ucand, lr_key, effective_scale)) continue;
            if (candidateCreatesParallel(cand, i3, v, idxs, k)) continue;
            all_notes[i3].pitch = ucand;
            all_notes[i3].modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
            break;
          }
        }
      }
    }

    // Consecutive repeated note repair using shared utility.
    {
      RepeatedNoteRepairParams rn_params;
      rn_params.num_voices = num_voices;
      rn_params.key_at_tick = [&](Tick t) { return tonal_plan.keyAtTick(t); };
      rn_params.scale_at_tick = [&](Tick) { return effective_scale; };
      rn_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
        return getFugueVoiceRange(v, num_voices);
      };
      repairRepeatedNotes(all_notes, rn_params);
    }

    // -----------------------------------------------------------------------
    // Final regression parallel repair: catch parallels introduced by voice
    // crossing repair, leap resolution, or dissonance resolution.
    // -----------------------------------------------------------------------
    if (num_voices >= 2) {
      ParallelRepairParams final_repair_params;
      final_repair_params.num_voices = num_voices;
      final_repair_params.scale = effective_scale;
      final_repair_params.key_at_tick = [&](Tick t) {
        return tonal_plan.keyAtTick(t);
      };
      final_repair_params.voice_range = [&](uint8_t v) {
        return getFugueVoiceRange(v, num_voices);
      };
      final_repair_params.max_iterations = 2;
      repairParallelPerfect(all_notes, final_repair_params);
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
        all_notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        if (all_notes[i].duration == 0) {
          all_notes[i].duration = 1;  // Avoid zero-duration notes.
          all_notes[i].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
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

  // Coda cadential pedal removed: Stage 3 tonic pedal in createCodaNotes
  // already provides this functionality (Phase 10).

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
