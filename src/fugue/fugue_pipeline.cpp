// Fugue pipeline: constraint-driven generation with 4-step architecture.
// Replaces the monolithic generateFugue() with buildMaterial -> planStructure
// -> generateSections -> finalize.

#include "fugue/fugue_pipeline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <random>

#include "constraint/constraint_state.h"
#include "constraint/feasibility_estimator.h"
#include "constraint/feasibility_harness.h"
#include "constraint/obligation_analyzer.h"
#include "core/markov_tables.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "core/form_profile.h"
#include "core/note_creator.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "forms/form_constraint_setup.h"
#include "fugue/answer.h"
#include "fugue/archetype_policy.h"
#include "fugue/cadence_insertion.h"
#include "fugue/cadence_plan.h"
#include "fugue/countersubject.h"
#include "fugue/episode.h"
#include "fugue/exposition.h"
#include "fugue/fortspinnung.h"
#include "fugue/middle_entry.h"
#include "fugue/motif_pool.h"
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

// ===========================================================================
// Internal data structures
// ===========================================================================

/// @brief All pre-computed material for fugue generation.
struct FugueMaterial {
  Subject subject;
  int subject_attempts = 0;
  SubjectConstraintProfile constraint_profile;
  Answer answer;
  Countersubject countersubject;
  Countersubject countersubject_2;
  MotifPool motif_pool;
};

/// @brief Planned section in the fugue structure.
struct PlannedSection {
  SectionType type = SectionType::Exposition;
  FuguePhase phase = FuguePhase::Establish;
  Key key = Key::C;
  Key prev_key = Key::C;            // Key before this section (for modulation tracking)
  Tick start_tick = 0;
  Tick end_tick = 0;
  int episode_index = -1;
  VoiceId entry_voice = 0;
  bool is_false_entry = false;
  bool companion_needed = false;     // True for MiddleEntry sections needing companion counterpoint
  float energy_level = 0.5f;
};

/// @brief Complete structural plan for the fugue.
struct FuguePlan {
  TonalPlan tonal_plan;
  ModulationPlan modulation_plan;
  HarmonicTimeline detailed_timeline;
  Tick estimated_duration = 0;
  std::vector<PlannedSection> sections;
  CadencePlan cadence_plan;
};

// ===========================================================================
// Constants
// ===========================================================================

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

/// @brief Duration of the dominant pedal in bars (placed before stretto).
constexpr Tick kDominantPedalBars = 4;

/// @brief Maximum feasibility retry attempts before accepting subject as-is.
///
/// After generating a subject that passes SubjectValidator, buildMaterial()
/// runs FeasibilityHarness checks (voice assignment, micro-sim, pair verification).
/// If any check fails, the subject is regenerated with a different seed.
/// Conservative limit: 3 retries to avoid excessive computation.
constexpr int kMaxFeasibilityRetries = 3;

/// @brief Number of MicroSim trials for pipeline feasibility gating.
///
/// Fewer than the default 20 trials for faster pipeline throughput.
/// 5 trials provide sufficient signal for pass/fail decisions while
/// keeping latency low.
constexpr int kPipelineMicroSimTrials = 5;

// ===========================================================================
// Helper functions (shared with fugue_generator.cpp)
// ===========================================================================

/// @brief Clamp voice count to valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

/// @brief Determine pedal mode for 3-voice fugues.
///
/// Checks if the subject and answer can fit within the organ pedal range [24, 50]
/// via octave transposition. If both fit, TruePedal is used; otherwise ManualBass.
/// For non-3-voice fugues, returns the input mode unchanged.
///
/// @param config Fugue configuration.
/// @param subject Generated subject.
/// @param answer Generated answer.
/// @return Resolved PedalMode (never Auto).
PedalMode determinePedalMode(const FugueConfig& config,
                             const Subject& subject,
                             const Answer& answer) {
  if (config.pedal_mode != PedalMode::Auto) return config.pedal_mode;
  if (config.num_voices != 3) return PedalMode::ManualBass;

  // Check if subject fits in pedal range [24, 50] at any octave.
  constexpr uint8_t kPedalLo = 24;
  constexpr uint8_t kPedalHi = 50;

  auto fitsInPedalRange = [&](const std::vector<NoteEvent>& notes) -> bool {
    if (notes.empty()) return false;
    uint8_t min_p = 127, max_p = 0;
    for (const auto& n : notes) {
      if (n.pitch < min_p) min_p = n.pitch;
      if (n.pitch > max_p) max_p = n.pitch;
    }
    int span = static_cast<int>(max_p) - static_cast<int>(min_p);
    int target_span = static_cast<int>(kPedalHi) - static_cast<int>(kPedalLo);
    if (span > target_span) return false;  // Subject wider than pedal range.

    // Try all octave shifts.
    for (int shift = -48; shift <= 48; shift += 12) {
      int lo = static_cast<int>(min_p) + shift;
      int hi = static_cast<int>(max_p) + shift;
      if (lo >= kPedalLo && hi <= kPedalHi) return true;
    }
    return false;
  };

  bool subject_fits = fitsInPedalRange(subject.notes);
  bool answer_fits = fitsInPedalRange(answer.notes);

  PedalMode result = (subject_fits && answer_fits) ? PedalMode::TruePedal
                                                    : PedalMode::ManualBass;
  fprintf(stderr, "determinePedalMode: subject_fits=%d answer_fits=%d -> %s\n",
          subject_fits, answer_fits,
          result == PedalMode::TruePedal ? "TruePedal" : "ManualBass");
  return result;
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
  for (auto iter = notes.rbegin(); iter != notes.rend(); ++iter) {
    if (iter->voice == voice && iter->start_tick < before_tick) {
      last_pitch = iter->pitch;
      break;
    }
  }
  return last_pitch;
}

/// @brief Build a VerticalSnapshot from the last pitch of each voice at a given tick.
/// @param notes All fugue notes generated so far.
/// @param tick Current tick position.
/// @param num_voices Total number of voices in the fugue.
/// @return Snapshot with last-sounding pitch per voice (0 if silent).
static VerticalSnapshot buildSnapshot(
    const std::vector<NoteEvent>& notes, Tick tick, uint8_t num_voices) {
  VerticalSnapshot snap;
  snap.num_voices = num_voices;
  for (uint8_t vid = 0; vid < num_voices && vid < VerticalSnapshot::kMaxVoices; ++vid) {
    snap.pitches[vid] = extractVoiceLastPitch(notes, tick, vid);
  }
  return snap;
}

/// @brief Build an InvariantSet from voice range for feasibility estimation.
/// @param voice_id Voice to check feasibility for.
/// @param num_voices Total number of voices.
/// @return InvariantSet with voice range bounds set.
static InvariantSet buildFeasibilityInvariants(VoiceId voice_id, uint8_t num_voices) {
  InvariantSet inv;
  auto [range_lo, range_hi] = getFugueVoiceRange(voice_id, num_voices);
  inv.voice_range_lo = range_lo;
  inv.voice_range_hi = range_hi;
  return inv;
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
  return clampPitch(best, 0, 127);
}

// ===========================================================================
// Coda generation: extracted helper functions
// ===========================================================================

/// @brief Solution candidate for V7->I->final cadence search.
struct CodaSolution {
  uint8_t v7[5] = {};
  uint8_t i_chord[5] = {};
  uint8_t final_chord[5] = {};
  int cost = INT32_MAX;
};

/// @brief Enforce strict descending pitch order across voices (top-down).
///
/// For each voice from 0 (soprano) downward, ensures each pitch is strictly
/// below the one above.  Preserves pitch class when possible via octave shifts,
/// falling back to a hard ceiling clamp when no same-PC option fits.
///
/// @param pitches Array of pitches to reorder in-place.
/// @param voice_count Number of active voices.
/// @param num_voices Total voices (for range lookup).
template <typename T>
static void projectStrictOrder(T* pitches, uint8_t voice_count, uint8_t num_voices) {
  int upper = INT_MAX;
  for (uint8_t vid = 0; vid < voice_count; ++vid) {
    int cur = static_cast<int>(pitches[vid]);
    auto [vlo, vhi] = getFugueVoiceRange(vid, num_voices);

    if (cur < upper) {
      upper = cur;
      continue;
    }

    // cur >= upper: voice crossing/unison.  Try same PC an octave below.
    int pitch_class = getPitchClass(static_cast<uint8_t>(cur));
    bool found = false;
    for (int cand = cur - 12; cand >= static_cast<int>(vlo); cand -= 12) {
      if (cand < upper &&
          getPitchClass(static_cast<uint8_t>(cand)) == pitch_class) {
        pitches[vid] = static_cast<T>(cand);
        upper = cand;
        found = true;
        break;
      }
    }
    if (found) continue;

    // Hard ceiling clamp (PC may change, but strict order is priority).
    int ceiling = std::min(upper - 1, static_cast<int>(vhi));
    if (ceiling >= static_cast<int>(vlo)) {
      pitches[vid] = static_cast<T>(ceiling);
      upper = ceiling;
    } else {
      // Extreme fallback: set to voice range low.
      pitches[vid] = static_cast<T>(vlo);
      upper = static_cast<int>(vlo) - 1;
    }
  }
}

/// @brief Build Stage 1 coda notes: head motif in voice 0 + held chord tones.
///
/// Voice 0 plays a rising-and-falling tonic motif (head fragment).
/// Voices 1+ hold tonic chord tones with voice-leading from last_pitches.
/// Includes voice-crossing fix, consonance check, and descending-order enforcement.
///
/// @param start_tick Coda start position.
/// @param duration Total coda duration.
/// @param key Musical key.
/// @param num_voices Number of voices.
/// @param is_minor True for minor key.
/// @param last_pitches Previous pitch per voice (nullable).
/// @param stage1_dur Output: computed Stage 1 duration.
/// @return Vector of Stage 1 notes.
static std::vector<NoteEvent> buildCodaChordNotes(Tick start_tick, Tick duration,
                                                  Key key, uint8_t num_voices,
                                                  bool is_minor,
                                                  const uint8_t* last_pitches,
                                                  Tick& stage1_dur) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  stage1_dur = bar_dur * 2;
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

    for (uint8_t idx = 0; idx < count; ++idx) {
      uint8_t voice_idx = 1 + idx;
      auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
      uint8_t target_pitch;

      if (last_pitches && last_pitches[voice_idx] > 0) {
        // Voice-leading: find nearest chord tone to previous pitch.
        uint8_t prev = last_pitches[voice_idx];
        int best_dist = 999;
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[idx], vlo, vhi);
        for (int pitch_class : chord_pcs) {
          uint8_t cand = nearestPitchWithPC(prev, pitch_class, 7);
          int dist = std::abs(static_cast<int>(cand) - static_cast<int>(prev));
          if (dist < best_dist) {
            best_dist = dist;
            target_pitch = cand;
          }
        }
        target_pitch = clampPitch(static_cast<int>(target_pitch), vlo, vhi);
      } else {
        // Fallback: use fixed offsets.
        target_pitch = clampPitch(tonic_pitch + stage1_offsets[idx], vlo, vhi);
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

    // Fix Stage 1 voice crossing: ensure higher voice indices have lower pitches.
    // Stage 1 held chords are sustained tones, so pitch reordering is safe.
    // IMPORTANT: preserve voice-leading proximity to last_pitches to avoid
    // large jumps at coda entry (inner voices max 7st, outer voices max 12st).
    {
      // Collect held chord notes (voice >= 1, start_tick == start_tick).
      std::vector<size_t> held_indices;
      for (size_t idx = 0; idx < notes.size(); ++idx) {
        if (notes[idx].voice >= 1 && notes[idx].start_tick == start_tick &&
            notes[idx].duration == stage1_dur) {
          held_indices.push_back(idx);
        }
      }
      // Use voice 0's maximum pitch during Stage 1 as upper bound.
      // The motif rises to base_tonic+7 and falls back, so held chords should
      // be below the peak (not the last note) to avoid forced large jumps.
      uint8_t voice0_max_pitch = 0;
      for (const auto& nev : notes) {
        if (nev.voice == 0 && nev.pitch > voice0_max_pitch) {
          voice0_max_pitch = nev.pitch;
        }
      }

      // Fix voice crossing while preserving voice-leading proximity.
      // Greedy top-down: for each voice (1, 2, ...), find the closest chord
      // tone to last_pitches that is also <= upper_bound (pitch of voice above).
      // This avoids the sort-then-reassign approach that can break proximity
      // by assigning a wrong pitch class to a voice.
      uint8_t upper_bound = (voice0_max_pitch > 0) ? voice0_max_pitch : 127;
      for (size_t idx = 0; idx < held_indices.size(); ++idx) {
        size_t note_idx = held_indices[idx];
        uint8_t voice_idx = notes[note_idx].voice;
        auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
        uint8_t current_pitch = notes[note_idx].pitch;

        // If already within bounds, keep it.
        if (current_pitch <= upper_bound && current_pitch >= vlo) {
          upper_bound = current_pitch;
          continue;
        }

        // Re-find the best chord tone: within voice range, <= upper_bound,
        // and closest to last_pitches[voice_idx] (or current pitch as fallback).
        uint8_t ref_pitch = (last_pitches && last_pitches[voice_idx] > 0)
                                ? last_pitches[voice_idx]
                                : current_pitch;
        int best_dist = 999;
        uint8_t best_pitch = clampPitch(static_cast<int>(current_pitch), vlo,
                                        std::min(static_cast<int>(upper_bound),
                                                 static_cast<int>(vhi)));
        for (int pitch_class : chord_pcs) {
          // Scan octaves within the voice range.
          int base = pitch_class;
          while (base < vlo) base += 12;
          for (int cur = base; cur <= vhi && cur <= static_cast<int>(upper_bound); cur += 12) {
            if (cur < vlo) continue;
            int dist = std::abs(cur - static_cast<int>(ref_pitch));
            if (dist < best_dist) {
              best_dist = dist;
              best_pitch = static_cast<uint8_t>(cur);
            }
          }
        }
        notes[note_idx].pitch = best_pitch;
        upper_bound = best_pitch;
      }

      // Final enforcement: bubble-sort to guarantee strict descending order.
      // The greedy pass above can miss edge cases when voice-leading pulls
      // a higher voice below a lower voice's chord tone.
      for (size_t idx = 0; idx + 1 < held_indices.size(); ++idx) {
        for (size_t jdx = idx + 1; jdx < held_indices.size(); ++jdx) {
          if (notes[held_indices[jdx]].pitch >= notes[held_indices[idx]].pitch) {
            std::swap(notes[held_indices[idx]].pitch,
                      notes[held_indices[jdx]].pitch);
          }
        }
      }

      // Override lowest voice to tonic pedal point (Bach coda convention).
      // Choose the octave of tonic closest to last_pitches to avoid large jumps.
      VoiceId lowest = num_voices - 1;
      auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest, num_voices);
      int bass_tonic = 36 + static_cast<int>(key);  // C2 default
      while (bass_tonic > static_cast<int>(ped_hi)) bass_tonic -= 12;
      while (bass_tonic < static_cast<int>(ped_lo)) bass_tonic += 12;
      if (last_pitches && last_pitches[lowest] > 0) {
        int prev = static_cast<int>(last_pitches[lowest]);
        int best = bass_tonic;
        int best_dist = std::abs(prev - bass_tonic);
        for (int oct : {-12, 12, -24, 24}) {
          int cand = bass_tonic + oct;
          if (cand < static_cast<int>(ped_lo) || cand > static_cast<int>(ped_hi)) continue;
          int dist = std::abs(prev - cand);
          if (dist < best_dist) {
            best_dist = dist;
            best = cand;
          }
        }
        bass_tonic = best;
      }
      for (auto& note : notes) {
        if (note.voice == lowest && note.start_tick == start_tick &&
            note.duration == stage1_dur) {
          note.pitch = clampPitch(bass_tonic, ped_lo, ped_hi);
          break;
        }
      }
    }

    // Consonance check: ensure held chord tones (voices 1+) are consonant
    // with voice 0's first note on the strong beat.  If dissonant, try
    // octave shifts of the offending voice within its register, preserving
    // the chord degree.  Only apply to the first beat (start_tick).
    if (!notes.empty()) {
      // Find voice 0's first note pitch.
      uint8_t voice0_first_pitch = 0;
      for (const auto& note : notes) {
        if (note.voice == 0 && note.start_tick == start_tick) {
          voice0_first_pitch = note.pitch;
          break;
        }
      }

      if (voice0_first_pitch > 0) {
        for (size_t idx = 0; idx < notes.size(); ++idx) {
          if (notes[idx].voice == 0) continue;
          if (notes[idx].start_tick != start_tick) continue;

          int diff = std::abs(static_cast<int>(notes[idx].pitch) -
                              static_cast<int>(voice0_first_pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (interval_util::isConsonance(simple) && diff >= 3) continue;

          // Dissonant or too close: try octave shifts.
          auto [vlo, vhi] = getFugueVoiceRange(notes[idx].voice, num_voices);
          int orig_pc = getPitchClass(notes[idx].pitch);
          uint8_t best_pitch = notes[idx].pitch;
          int best_cost = INT32_MAX;

          for (int shift = -36; shift <= 36; shift += 12) {
            if (shift == 0) continue;
            int cand = static_cast<int>(notes[idx].pitch) + shift;
            if (cand < vlo || cand > vhi) continue;
            if (getPitchClass(static_cast<uint8_t>(cand)) != orig_pc) continue;

            // Check consonance with voice 0.
            int cand_diff = std::abs(cand - static_cast<int>(voice0_first_pitch));
            int cand_simple = interval_util::compoundToSimple(cand_diff);
            if (!interval_util::isConsonance(cand_simple) || cand_diff < 3) continue;

            // Check no voice crossing with adjacent voices.
            bool crosses = false;
            for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
              if (jdx == idx || notes[jdx].start_tick != start_tick) continue;
              if (notes[jdx].voice == notes[idx].voice) continue;
              if (notes[jdx].voice < notes[idx].voice && cand > notes[jdx].pitch) {
                crosses = true;
                break;
              }
              if (notes[jdx].voice > notes[idx].voice && cand < notes[jdx].pitch) {
                crosses = true;
                break;
              }
            }
            if (crosses) continue;

            // Cost: voice-leading distance from original + imperfect consonance bonus.
            int cost = std::abs(shift);
            if (cand_simple == 3 || cand_simple == 4 ||
                cand_simple == 8 || cand_simple == 9) {
              cost -= 6;  // Prefer imperfect consonances.
            }
            if (cost < best_cost) {
              best_cost = cost;
              best_pitch = static_cast<uint8_t>(cand);
            }
          }

          // If octave shift didn't help, try +-3rd and +-6th within chord.
          if (best_cost == INT32_MAX) {
            int tonic_pc_local = getPitchClass(static_cast<uint8_t>(tonic_pitch));
            int third_pc_local = (tonic_pitch + chord_third) % 12;
            int fifth_pc_local = (tonic_pitch + 7) % 12;
            int chord_pcs_local[] = {tonic_pc_local, third_pc_local, fifth_pc_local};

            for (int ct_pc : chord_pcs_local) {
              if (ct_pc == orig_pc) continue;  // Same degree, already tried octave shifts.
              // Find nearest pitch with this chord PC.
              for (int oct = -24; oct <= 24; oct += 12) {
                int base = static_cast<int>(notes[idx].pitch) + oct;
                int target = base - (base % 12) + ct_pc;
                if (target < base - 6) target += 12;
                if (target > base + 6) target -= 12;
                if (target < vlo || target > vhi) continue;

                // Check consonance with voice 0.
                int cand_diff = std::abs(target - static_cast<int>(voice0_first_pitch));
                int cand_simple = interval_util::compoundToSimple(cand_diff);
                if (!interval_util::isConsonance(cand_simple) || cand_diff < 3) continue;

                // Check no crossing.
                bool crosses = false;
                for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
                  if (jdx == idx || notes[jdx].start_tick != start_tick) continue;
                  if (notes[jdx].voice == notes[idx].voice) continue;
                  if (notes[jdx].voice < notes[idx].voice && target > notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                  if (notes[jdx].voice > notes[idx].voice && target < notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                }
                if (crosses) continue;

                // Voice-leading cost from original position.
                int cost = std::abs(target - static_cast<int>(notes[idx].pitch));
                if (cand_simple == 3 || cand_simple == 4 ||
                    cand_simple == 8 || cand_simple == 9) {
                  cost -= 3;
                }
                if (cost < best_cost) {
                  best_cost = cost;
                  best_pitch = static_cast<uint8_t>(target);
                }
              }
            }
          }

          if (best_pitch != notes[idx].pitch) {
            notes[idx].pitch = best_pitch;
          }
        }
      }
    }

    // Inter-voice consonance among held chord tones: ensure all pairs
    // have at least a minor 3rd spacing and form consonant intervals.
    // This is a soft fix -- only adjusts obviously dissonant pairs via
    // octave shift of the more flexible voice.
    for (size_t idx = 0; idx < notes.size(); ++idx) {
      if (notes[idx].voice == 0 || notes[idx].start_tick != start_tick) continue;
      for (size_t jdx = idx + 1; jdx < notes.size(); ++jdx) {
        if (notes[jdx].voice == 0 || notes[jdx].start_tick != start_tick) continue;
        int diff = std::abs(static_cast<int>(notes[idx].pitch) -
                            static_cast<int>(notes[jdx].pitch));
        if (diff >= 3) continue;  // Sufficient spacing.
        // Too close (unison or minor 2nd): shift the lower-priority voice.
        size_t fix_idx = (notes[idx].voice > notes[jdx].voice) ? idx : jdx;
        int fix_shift =
            (notes[fix_idx].pitch < notes[fix_idx == idx ? jdx : idx].pitch) ? -12 : 12;
        int shifted = static_cast<int>(notes[fix_idx].pitch) + fix_shift;
        auto [flo, fhi] = getFugueVoiceRange(notes[fix_idx].voice, num_voices);
        if (shifted >= flo && shifted <= fhi) {
          notes[fix_idx].pitch = static_cast<uint8_t>(shifted);
        }
      }
    }

    // Final strict descending order enforcement.
    // Consonance adjustments above can break the ordering established by
    // the earlier bubble sort.  Re-sort and resolve equal pitches.
    {
      std::vector<size_t> held_final;
      for (size_t idx = 0; idx < notes.size(); ++idx) {
        if (notes[idx].voice >= 1 && notes[idx].start_tick == start_tick &&
            notes[idx].duration == stage1_dur) {
          held_final.push_back(idx);
        }
      }
      std::sort(held_final.begin(), held_final.end(),
                [&notes](size_t lhs, size_t rhs) {
                  return notes[lhs].voice < notes[rhs].voice;
                });
      // Descending sort by pitch.
      for (size_t idx = 0; idx + 1 < held_final.size(); ++idx) {
        for (size_t jdx = idx + 1; jdx < held_final.size(); ++jdx) {
          if (notes[held_final[jdx]].pitch >= notes[held_final[idx]].pitch) {
            std::swap(notes[held_final[idx]].pitch, notes[held_final[jdx]].pitch);
          }
        }
      }
      // Resolve equal pitches: shift each voice strictly below the previous.
      // Use smaller steps (semitones) first, then octave shift, to avoid
      // large voice-leading jumps.
      for (size_t idx = 1; idx < held_final.size(); ++idx) {
        if (notes[held_final[idx]].pitch >= notes[held_final[idx - 1]].pitch) {
          auto [vlo, vhi] = getFugueVoiceRange(notes[held_final[idx]].voice, num_voices);
          uint8_t target = notes[held_final[idx - 1]].pitch;
          // Try nearest chord tone below target.
          int best = -1;
          for (int pitch_class : chord_pcs) {
            for (int cur = static_cast<int>(target) - 1; cur >= vlo; cur--) {
              if (cur % 12 == pitch_class) {
                if (best < 0 || cur > best) best = cur;
                break;
              }
            }
          }
          if (best >= vlo && best < static_cast<int>(target)) {
            notes[held_final[idx]].pitch = static_cast<uint8_t>(best);
          }
        }
      }
    }

  }

  return notes;
}

/// @brief Generate pitch candidates for a voice given target pitch classes.
///
/// Searches +-2 octaves from prev_pitch for pitches matching any target PC
/// within the given range.  Results are deduplicated and sorted.
///
/// @param prev_pitch Reference pitch for proximity (0 = use range_lo).
/// @param pcs Array of target pitch classes.
/// @param num_pcs Number of entries in pcs.
/// @param range_lo Voice range lower bound.
/// @param range_hi Voice range upper bound.
/// @return Sorted, deduplicated candidate pitches.
static std::vector<uint8_t> generateCadenceCandidates(uint8_t prev_pitch, const int* pcs,
                                                      int num_pcs, uint8_t range_lo,
                                                      uint8_t range_hi) {
  std::vector<uint8_t> cands;
  for (int pc_idx = 0; pc_idx < num_pcs; ++pc_idx) {
    int pitch_class = pcs[pc_idx];
    // Search +-2 octaves from prev_pitch.
    for (int oct = -24; oct <= 24; oct += 12) {
      int base = (prev_pitch > 0) ? static_cast<int>(prev_pitch) + oct
                                  : static_cast<int>(range_lo) + oct;
      // Find nearest pitch with target PC.
      int target = base - (base % 12) + pitch_class;
      if (target < base - 6) target += 12;
      if (target > base + 6) target -= 12;
      if (target >= static_cast<int>(range_lo) &&
          target <= static_cast<int>(range_hi)) {
        cands.push_back(static_cast<uint8_t>(target));
      }
    }
  }
  // Deduplicate.
  std::sort(cands.begin(), cands.end());
  cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
  return cands;
}

/// @brief Limit candidates by distance from reference pitch, keeping top N.
/// @param cands Candidate list (modified in-place).
/// @param ref Reference pitch for distance calculation.
/// @param max_n Maximum candidates to keep.
static void limitCadenceCandidates(std::vector<uint8_t>& cands, uint8_t ref, size_t max_n) {
  if (cands.size() <= max_n) return;
  std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
    return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
           std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
  });
  cands.resize(max_n);
}

/// @brief Search for optimal V7->I cadence via scored candidate enumeration.
///
/// Stage 2: Searches outer voice candidates first, then enumerates inner voice
/// combinations.  Scores solutions by voice-leading distance, resolution quality,
/// voice crossing, and parallel 5ths/8ths.
/// Stage 3: Emits final sustained tonic chord from the best solution.
/// Falls back to fixed offsets if no valid solution is found.
///
/// @param chord_notes Stage 1 notes (for voice-leading reference).
/// @param start_tick Coda start position.
/// @param duration Total coda duration.
/// @param key Musical key.
/// @param num_voices Number of voices.
/// @param is_minor True for minor key.
/// @param stage1_dur Duration of Stage 1 (bars 1-2).
/// @return Vector of Stage 2 and Stage 3 notes.
static std::vector<NoteEvent> searchCodaCadence(const std::vector<NoteEvent>& chord_notes,
                                                Tick start_tick, Tick duration,
                                                Key key, uint8_t num_voices,
                                                bool is_minor, Tick stage1_dur) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  if (duration <= stage1_dur) return notes;

  Tick stage2_start = start_tick + stage1_dur;
  Tick stage2_dur = std::min(bar_dur, duration - stage1_dur);
  Tick half_bar = stage2_dur / 2;

  // Collect Stage 1 end pitches for voice-leading reference.
  // Voice 0: last note of head motif; others: held chord tone.
  uint8_t stage1_end[5] = {0, 0, 0, 0, 0};
  for (const auto& nev : chord_notes) {
    if (nev.voice < 5) {
      // For voice 0, we want the last note; for others, the held note.
      if (nev.voice == 0) {
        Tick note_end = nev.start_tick + nev.duration;
        if (note_end <= stage2_start + 1) {
          stage1_end[0] = nev.pitch;
        }
      } else {
        stage1_end[nev.voice] = nev.pitch;
      }
    }
  }

  // Dominant and tonic pitch classes for V7 and I chords.
  int dom_root = (tonic_pitch + 7) % 12;       // G
  int dom_third = (tonic_pitch + 11) % 12;      // B (leading tone)
  int dom_fifth = (tonic_pitch + 14) % 12;      // D
  int dom_seventh = (tonic_pitch + 17) % 12;    // F (7th)
  int tonic_root = tonic_pitch % 12;            // C
  int picardy_third = (tonic_pitch + 4) % 12;   // E (always major for Picardy)
  int res_third_pc = is_minor ? picardy_third : ((tonic_pitch + 4) % 12);
  int tonic_fifth = (tonic_pitch + 7) % 12;     // G

  int v7_pcs[] = {dom_root, dom_third, dom_fifth, dom_seventh};
  int i_pcs[] = {tonic_root, res_third_pc, tonic_fifth};

  uint8_t voice_count = std::min(num_voices, static_cast<uint8_t>(5));
  uint8_t sop = 0;
  uint8_t bass = voice_count - 1;

  // === Outer voices first ===
  auto [sop_lo, sop_hi] = getFugueVoiceRange(sop, num_voices);
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass, num_voices);

  // V7 candidates for soprano and bass (limited to top 3 by proximity).
  auto sop_v7_cands = generateCadenceCandidates(stage1_end[sop], v7_pcs, 4, sop_lo, sop_hi);
  limitCadenceCandidates(sop_v7_cands, stage1_end[sop], 3);
  auto bass_v7_cands = generateCadenceCandidates(stage1_end[bass], v7_pcs, 4, bass_lo, bass_hi);
  limitCadenceCandidates(bass_v7_cands, stage1_end[bass], 3);
  // Prefer bass on dominant root.
  {
    std::vector<uint8_t> root_first;
    std::vector<uint8_t> others;
    for (auto cur : bass_v7_cands) {
      if (getPitchClass(cur) == dom_root)
        root_first.push_back(cur);
      else
        others.push_back(cur);
    }
    root_first.insert(root_first.end(), others.begin(), others.end());
    bass_v7_cands = root_first;
  }

  // Score a 3-chord sequence for all voices.
  auto scoreSolution = [&](const CodaSolution& sol) -> int {
    int cost = 0;

    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      // Voice-leading distance: stage1->V7, V7->I, I->final.
      int d1 = std::abs(static_cast<int>(sol.v7[vid]) - static_cast<int>(stage1_end[vid]));
      int d2 = std::abs(static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]));
      int d3 = std::abs(static_cast<int>(sol.final_chord[vid]) -
                        static_cast<int>(sol.i_chord[vid]));
      cost += 10 * (d1 + d2 + d3);

      // Excessive leap penalty (>12st).
      if (d1 > 12) cost += 200 * (d1 - 12);
      if (d2 > 12) cost += 200 * (d2 - 12);
      if (d3 > 12) cost += 200 * (d3 - 12);
    }

    // Leading-tone resolution: any voice with V7 leading tone should resolve up by semitone.
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      if (getPitchClass(sol.v7[vid]) == dom_third) {
        int resolution = static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]);
        if (resolution != 1) cost += 50;  // Leading tone must resolve up by semitone.
      }
    }

    // Seventh resolution: any voice with V7 seventh should resolve down by step.
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      if (getPitchClass(sol.v7[vid]) == dom_seventh) {
        int resolution = static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]);
        if (resolution != -1 && resolution != -2) cost += 50;
      }
    }

    // Contrary outer voice motion bonus (V7->I: soprano up, bass down or vice versa).
    {
      int sop_motion = static_cast<int>(sol.i_chord[sop]) - static_cast<int>(sol.v7[sop]);
      int bass_motion = static_cast<int>(sol.i_chord[bass]) - static_cast<int>(sol.v7[bass]);
      if ((sop_motion > 0 && bass_motion < 0) || (sop_motion < 0 && bass_motion > 0)) {
        cost -= 10;
      }
    }

    // Voice crossing/unison check -- instant rejection.
    for (int chord_idx = 0; chord_idx < 3; ++chord_idx) {
      const uint8_t* pitches = (chord_idx == 0) ? sol.v7
                             : (chord_idx == 1) ? sol.i_chord
                             : sol.final_chord;
      for (uint8_t vid = 0; vid + 1 < voice_count; ++vid) {
        if (pitches[vid] <= pitches[vid + 1]) {
          return INT32_MAX;  // Voice crossing or unison: reject.
        }
      }
    }

    // Parallel perfect 5ths/octaves check between consecutive chords.
    auto checkParallels = [&](const uint8_t* chord_a, const uint8_t* chord_b) -> int {
      int pen = 0;
      for (uint8_t va = 0; va < voice_count; ++va) {
        for (uint8_t vb = va + 1; vb < voice_count; ++vb) {
          int interval_a = std::abs(static_cast<int>(chord_a[va]) -
                                     static_cast<int>(chord_a[vb]));
          int interval_b = std::abs(static_cast<int>(chord_b[va]) -
                                     static_cast<int>(chord_b[vb]));
          int simple_a = interval_util::compoundToSimple(interval_a);
          int simple_b = interval_util::compoundToSimple(interval_b);
          // Parallel perfect unisons, 5ths, or octaves.
          if ((simple_a == 0 || simple_a == 7) &&
              (simple_b == 0 || simple_b == 7) &&
              simple_a == simple_b) {
            // Check both voices move in same direction.
            int motion_a = static_cast<int>(chord_b[va]) - static_cast<int>(chord_a[va]);
            int motion_b = static_cast<int>(chord_b[vb]) - static_cast<int>(chord_a[vb]);
            if (motion_a != 0 && motion_b != 0 &&
                ((motion_a > 0) == (motion_b > 0))) {
              return INT32_MAX;  // Parallel 5/8: reject.
            }
          }
        }
      }
      return pen;
    };

    int par1 = checkParallels(sol.v7, sol.i_chord);
    if (par1 == INT32_MAX) return INT32_MAX;
    cost += par1;
    int par2 = checkParallels(sol.i_chord, sol.final_chord);
    if (par2 == INT32_MAX) return INT32_MAX;
    cost += par2;

    // Hidden 5/8 on outer voices (penalty, not rejection).
    auto checkHidden58 = [&](const uint8_t* chord_a, const uint8_t* chord_b) -> int {
      int sop_m = static_cast<int>(chord_b[sop]) - static_cast<int>(chord_a[sop]);
      int bass_m = static_cast<int>(chord_b[bass]) - static_cast<int>(chord_a[bass]);
      if (sop_m != 0 && bass_m != 0 && ((sop_m > 0) == (bass_m > 0))) {
        int interval = std::abs(static_cast<int>(chord_b[sop]) -
                                 static_cast<int>(chord_b[bass]));
        int simple = interval_util::compoundToSimple(interval);
        if (simple == 0 || simple == 7) {
          return 100;  // Hidden 5/8 penalty.
        }
      }
      return 0;
    };

    cost += checkHidden58(sol.v7, sol.i_chord);
    cost += checkHidden58(sol.i_chord, sol.final_chord);

    // 4-3 suspension bonus: voice holds from V7 to I chord.
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      if (sol.v7[vid] == sol.i_chord[vid]) {
        // Check if held note forms a 4th resolving to 3rd with any lower voice.
        for (uint8_t vb = vid + 1; vb < voice_count; ++vb) {
          int interval_v7 = std::abs(static_cast<int>(sol.v7[vid]) -
                                      static_cast<int>(sol.v7[vb]));
          int simple = interval_util::compoundToSimple(interval_v7);
          if (simple == 5) {  // Perfect 4th
            cost -= 20;  // 4-3 suspension bonus.
          }
        }
      }
    }

    return cost;
  };

  CodaSolution best;

  // I chord PCs for the final chord (Picardy third for minor).
  int final_pcs[] = {tonic_root, picardy_third, tonic_fifth};

  // Search: iterate outer voice candidates, then fill inner voices.
  for (auto sop_v7 : sop_v7_cands) {
    // I chord: soprano resolves leading tone -> tonic (if leading tone),
    // otherwise nearest I chord tone.
    std::vector<uint8_t> sop_i_cands;
    if (getPitchClass(sop_v7) == dom_third) {
      // Leading tone must resolve up by semitone.
      uint8_t resolved = sop_v7 + 1;
      if (resolved >= sop_lo && resolved <= sop_hi) {
        sop_i_cands.push_back(resolved);
      }
    }
    if (sop_i_cands.empty()) {
      sop_i_cands = generateCadenceCandidates(sop_v7, i_pcs, 3, sop_lo, sop_hi);
    }
    limitCadenceCandidates(sop_i_cands, sop_v7, 2);

    for (auto bass_v7 : bass_v7_cands) {
      // Bass: V7 root -> tonic root (standard bass resolution).
      std::vector<uint8_t> bass_i_cands;
      if (getPitchClass(bass_v7) == dom_root) {
        // Dominant root -> tonic root (down P5 or up P4).
        for (int d_interval : {-7, 5, -19, 17}) {
          int cand = static_cast<int>(bass_v7) + d_interval;
          if (cand >= bass_lo && cand <= bass_hi &&
              getPitchClass(static_cast<uint8_t>(cand)) == tonic_root) {
            bass_i_cands.push_back(static_cast<uint8_t>(cand));
          }
        }
      }
      if (bass_i_cands.empty()) {
        bass_i_cands = generateCadenceCandidates(bass_v7, i_pcs, 3, bass_lo, bass_hi);
      }
      limitCadenceCandidates(bass_i_cands, bass_v7, 2);

      for (auto sop_i : sop_i_cands) {
        for (auto bass_i : bass_i_cands) {
          // Quick crossing check on outer voices.
          if (sop_v7 < bass_v7 || sop_i < bass_i) continue;

          // Final chord candidates for outer voices (limited to top 2).
          auto sop_final_cands = generateCadenceCandidates(sop_i, final_pcs, 3, sop_lo, sop_hi);
          limitCadenceCandidates(sop_final_cands, sop_i, 2);
          auto bass_final_cands =
              generateCadenceCandidates(bass_i, final_pcs, 3, bass_lo, bass_hi);
          limitCadenceCandidates(bass_final_cands, bass_i, 2);

          for (auto sop_final : sop_final_cands) {
            for (auto bass_final : bass_final_cands) {
              if (sop_final < bass_final) continue;

              // === Inner voices: enumerate all combinations ===
              if (voice_count <= 2) {
                CodaSolution sol;
                sol.v7[sop] = sop_v7;
                sol.v7[bass] = bass_v7;
                sol.i_chord[sop] = sop_i;
                sol.i_chord[bass] = bass_i;
                sol.final_chord[sop] = sop_final;
                sol.final_chord[bass] = bass_final;
                sol.cost = scoreSolution(sol);
                if (sol.cost < best.cost) best = sol;
                continue;
              }

              // Generate inner voice candidates.
              struct InnerCands {
                std::vector<uint8_t> v7;
                std::vector<uint8_t> i_chord;
                std::vector<uint8_t> final_chord;
              };
              std::vector<InnerCands> inner(voice_count - 2);

              for (uint8_t inner_idx = 0; inner_idx < voice_count - 2; ++inner_idx) {
                uint8_t voice_idx = 1 + inner_idx;
                auto [vlo, vhi] = getFugueVoiceRange(voice_idx, num_voices);
                inner[inner_idx].v7 =
                    generateCadenceCandidates(stage1_end[voice_idx], v7_pcs, 4, vlo, vhi);
                inner[inner_idx].i_chord = generateCadenceCandidates(0, i_pcs, 3, vlo, vhi);
                inner[inner_idx].final_chord =
                    generateCadenceCandidates(0, final_pcs, 3, vlo, vhi);

                // Limit candidates to keep search space manageable.
                // Sort by distance from previous pitch and keep top 5.
                auto limitByDist = [](std::vector<uint8_t>& cands, uint8_t ref, int max_n) {
                  if (ref == 0 || static_cast<int>(cands.size()) <= max_n) return;
                  std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
                    return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
                           std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
                  });
                  cands.resize(static_cast<size_t>(max_n));
                };
                limitByDist(inner[inner_idx].v7, stage1_end[voice_idx], 2);
                uint8_t v7_ref = inner[inner_idx].v7.empty() ? stage1_end[voice_idx]
                                                              : inner[inner_idx].v7[0];
                limitByDist(inner[inner_idx].i_chord, v7_ref, 2);
                uint8_t i_ref = inner[inner_idx].i_chord.empty()
                                    ? v7_ref
                                    : inner[inner_idx].i_chord[0];
                limitByDist(inner[inner_idx].final_chord, i_ref, 2);
              }

              // Enumerate inner voice combinations.
              // For 3 voices: 1 inner voice; 4 voices: 2 inner; 5 voices: 3 inner.
              uint8_t n_inner = voice_count - 2;

              // Recursive lambda to enumerate inner voice assignments.
              std::function<void(uint8_t, CodaSolution&)> enumerate;
              enumerate = [&](uint8_t depth, CodaSolution& partial) {
                if (depth == n_inner) {
                  partial.cost = scoreSolution(partial);
                  if (partial.cost < best.cost) best = partial;
                  return;
                }
                uint8_t voice_idx = 1 + depth;
                for (auto pv7 : inner[depth].v7) {
                  // Early pruning: skip if voice-leading distance alone
                  // already exceeds best known cost.
                  int d1 = std::abs(static_cast<int>(pv7) -
                                     static_cast<int>(stage1_end[voice_idx]));
                  if (10 * d1 >= best.cost) continue;
                  for (auto p_i : inner[depth].i_chord) {
                    for (auto p_f : inner[depth].final_chord) {
                      partial.v7[voice_idx] = pv7;
                      partial.i_chord[voice_idx] = p_i;
                      partial.final_chord[voice_idx] = p_f;
                      enumerate(depth + 1, partial);
                    }
                  }
                }
              };

              CodaSolution partial;
              partial.v7[sop] = sop_v7;
              partial.v7[bass] = bass_v7;
              partial.i_chord[sop] = sop_i;
              partial.i_chord[bass] = bass_i;
              partial.final_chord[sop] = sop_final;
              partial.final_chord[bass] = bass_final;
              enumerate(0, partial);
            }
          }
        }
      }
    }
  }

  // Emit notes from the best solution (search or fallback).
  if (best.cost < INT32_MAX) {
    // Layer 2 safety net: enforce strict descending pitch order.
    projectStrictOrder(best.v7, voice_count, num_voices);
    projectStrictOrder(best.i_chord, voice_count, num_voices);
    projectStrictOrder(best.final_chord, voice_count, num_voices);

    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      NoteEvent note;
      note.start_tick = stage2_start;
      note.duration = half_bar;
      note.pitch = best.v7[vid];
      note.velocity = kOrganVelocity;
      note.voice = vid;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }

    // Emit I chord notes (second half of Stage 2).
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      NoteEvent note;
      note.start_tick = stage2_start + half_bar;
      note.duration = stage2_dur - half_bar;
      note.pitch = best.i_chord[vid];
      note.velocity = kOrganVelocity;
      note.voice = vid;
      note.source = BachNoteSource::Coda;
      notes.push_back(note);
    }

    // Emit Stage 3 final tonic chord.
    Tick stage2_actual = stage2_dur;
    Tick stage3_start = start_tick + stage1_dur + stage2_actual;
    if (stage3_start < start_tick + duration) {
      Tick stage3_dur = start_tick + duration - stage3_start;
      for (uint8_t vid = 0; vid < voice_count; ++vid) {
        NoteEvent note;
        note.start_tick = stage3_start;
        note.duration = stage3_dur;
        note.pitch = best.final_chord[vid];
        note.velocity = kOrganVelocity;
        note.voice = vid;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }
  } else {
    // Fallback: emit simple V7-I if optimization found no valid solution.
    int dom_pitch = tonic_pitch + 7;
    int res_third = is_minor ? 3 : 4;

    // Layer 1: Voice-count-specific offsets.
    // 3-voice (and 2-voice): LT/7th/root for V7 -- omit 5th to avoid crossing.
    int dom7_offsets[5];
    int tonic_offsets_fb[5];
    int final_offsets_fb[5];
    uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));

    if (num_voices <= 3) {
      // V7: LT(soprano), 7th(inner), root(bass) -- 5th omitted.
      dom7_offsets[0] = 4;    // B4 (leading tone)
      dom7_offsets[1] = -2;   // F4 (7th)
      dom7_offsets[2] = -12;  // G3 (root)
      // I: tonic(sop), 3rd(inner), root(bass).
      tonic_offsets_fb[0] = 12;         // C5
      tonic_offsets_fb[1] = res_third;  // E4/Eb4
      tonic_offsets_fb[2] = 0;          // C4
      // Final: tonic(sop), 3rd(inner, Picardy), root(bass).
      final_offsets_fb[0] = 12;  // C5
      final_offsets_fb[1] = 4;   // E4 (always major for Picardy)
      final_offsets_fb[2] = 0;   // C4
    } else {
      // 4-5 voice: original offsets.
      int dom7_src[] = {4, 7, -2, -12, -17};
      int tonic_src[] = {12, 7, res_third, 0, -12};
      int final_src[] = {12, 7, 4, 0, -12};
      for (uint8_t vid = 0; vid < count; ++vid) {
        dom7_offsets[vid] = dom7_src[vid];
        tonic_offsets_fb[vid] = tonic_src[vid];
        final_offsets_fb[vid] = final_src[vid];
      }
    }

    // Compute fallback pitches into arrays.
    int v7_pitches[5] = {};
    int i_pitches[5] = {};
    int final_pitches[5] = {};
    for (uint8_t vid = 0; vid < count; ++vid) {
      auto [vlo, vhi] = getFugueVoiceRange(vid, num_voices);
      v7_pitches[vid] = static_cast<int>(clampPitch(dom_pitch + dom7_offsets[vid], vlo, vhi));
      i_pitches[vid] = static_cast<int>(
          clampPitch(tonic_pitch + tonic_offsets_fb[vid], vlo, vhi));
      final_pitches[vid] = static_cast<int>(
          clampPitch(tonic_pitch + final_offsets_fb[vid], vlo, vhi));
    }

    // Harsh dissonance guard: check each V7 pitch against the tonic bass
    // (pedal from Stage 1). If m2(1)/TT(6)/M7(11) is formed, try octave
    // shifts to find a safe register that avoids the clash.
    int tonic_bass = static_cast<int>(
        clampPitch(tonic_pitch, bass_lo, bass_hi));
    for (uint8_t vid = 0; vid < count; ++vid) {
      int simple = interval_util::compoundToSimple(
          absoluteInterval(static_cast<uint8_t>(v7_pitches[vid]),
                           static_cast<uint8_t>(tonic_bass)));
      if (simple == 1 || simple == 6 || simple == 11) {
        auto [range_lo, range_hi] = getFugueVoiceRange(vid, num_voices);
        for (int shift : {12, -12, 24, -24}) {
          int alt = v7_pitches[vid] + shift;
          if (alt < range_lo || alt > range_hi) continue;
          int alt_simple = interval_util::compoundToSimple(
              absoluteInterval(static_cast<uint8_t>(alt),
                               static_cast<uint8_t>(tonic_bass)));
          if (alt_simple != 1 && alt_simple != 6 && alt_simple != 11) {
            v7_pitches[vid] = alt;
            break;
          }
        }
      }
    }

    // Layer 2: Top-down greedy projection -- ensure strict descending order.
    projectStrictOrder(v7_pitches, count, num_voices);
    projectStrictOrder(i_pitches, count, num_voices);
    projectStrictOrder(final_pitches, count, num_voices);

    // Emit V7 and I chord notes.
    for (uint8_t vid = 0; vid < count; ++vid) {
      NoteEvent v7_note;
      v7_note.start_tick = stage2_start;
      v7_note.duration = half_bar;
      v7_note.pitch = static_cast<uint8_t>(v7_pitches[vid]);
      v7_note.velocity = kOrganVelocity;
      v7_note.voice = vid;
      v7_note.source = BachNoteSource::Coda;
      notes.push_back(v7_note);

      NoteEvent i_note;
      i_note.start_tick = stage2_start + half_bar;
      i_note.duration = stage2_dur - half_bar;
      i_note.pitch = static_cast<uint8_t>(i_pitches[vid]);
      i_note.velocity = kOrganVelocity;
      i_note.voice = vid;
      i_note.source = BachNoteSource::Coda;
      notes.push_back(i_note);
    }

    // Stage 3 fallback.
    Tick stage3_start = start_tick + stage1_dur + stage2_dur;
    if (stage3_start < start_tick + duration) {
      Tick stage3_dur = start_tick + duration - stage3_start;
      for (uint8_t vid = 0; vid < count; ++vid) {
        NoteEvent note;
        note.start_tick = stage3_start;
        note.duration = stage3_dur;
        note.pitch = static_cast<uint8_t>(final_pitches[vid]);
        note.velocity = kOrganVelocity;
        note.voice = vid;
        note.source = BachNoteSource::Coda;
        notes.push_back(note);
      }
    }
  }

  return notes;
}

/// @brief Diagnostic verification pass for coda voice crossings.
///
/// Logs warnings to stderr for any remaining voice crossing violations
/// in Stage 2/3 chords.  Does not modify notes.
///
/// @param notes All coda notes to check.
/// @param start_tick Coda start position.
/// @param duration Total coda duration.
/// @param num_voices Number of voices.
/// @param stage1_dur Duration of Stage 1.
static void verifyCodaCrossings(const std::vector<NoteEvent>& notes,
                                Tick start_tick, Tick duration,
                                uint8_t num_voices, Tick stage1_dur) {
  Tick bar_dur = kTicksPerBar;
  Tick stage2_start = start_tick + stage1_dur;
  Tick stage2_dur = std::min(bar_dur, duration - stage1_dur);
  Tick half_bar = stage2_dur / 2;

  auto checkCrossing = [&](const char* label, Tick tick) {
    uint8_t pitches_at_tick[5] = {0, 0, 0, 0, 0};
    for (const auto& nev : notes) {
      if (nev.start_tick <= tick && tick < nev.start_tick + nev.duration && nev.voice < 5) {
        pitches_at_tick[nev.voice] = nev.pitch;
      }
    }
    uint8_t count = std::min(num_voices, static_cast<uint8_t>(5));
    for (uint8_t vid = 0; vid + 1 < count; ++vid) {
      if (pitches_at_tick[vid] > 0 && pitches_at_tick[vid + 1] > 0 &&
          pitches_at_tick[vid] <= pitches_at_tick[vid + 1]) {
        std::fprintf(stderr,
                     "[createCodaNotes] WARNING: %s voice crossing v%u(%u) < v%u(%u)\n",
                     label, vid, pitches_at_tick[vid], vid + 1, pitches_at_tick[vid + 1]);
      }
    }
  };

  if (duration > stage1_dur) {
    checkCrossing("V7", stage2_start);
    checkCrossing("I", stage2_start + half_bar);
    Tick stage3_start = start_tick + stage1_dur + stage2_dur;
    if (stage3_start < start_tick + duration) {
      checkCrossing("final", stage3_start);
    }
  }
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
/// @param last_pitches Previous pitch per voice (nullable).
/// @return Vector of coda notes.
std::vector<NoteEvent> createCodaNotes(Tick start_tick, Tick duration,
                                       Key key, uint8_t num_voices,
                                       bool is_minor = false,
                                       const uint8_t* last_pitches = nullptr) {
  // Stage 1: Head motif + held chord tones with voice-crossing fixes.
  Tick stage1_dur = 0;
  auto chord_notes = buildCodaChordNotes(start_tick, duration, key, num_voices,
                                         is_minor, last_pitches, stage1_dur);

  // Stage 2-3: V7->I cadence search + final tonic chord.
  auto cadence_notes = searchCodaCadence(chord_notes, start_tick, duration, key,
                                         num_voices, is_minor, stage1_dur);

  // Merge Stage 1 and Stage 2-3 notes.
  std::vector<NoteEvent> notes;
  notes.reserve(chord_notes.size() + cadence_notes.size());
  notes.insert(notes.end(), chord_notes.begin(), chord_notes.end());
  notes.insert(notes.end(), cadence_notes.begin(), cadence_notes.end());

  // Verification pass: log any remaining voice-crossing violations (diagnostic only).
  verifyCodaCrossings(notes, start_tick, duration, num_voices, stage1_dur);

  return notes;
}

/// @brief Select how many voices should be active for an episode.
///
/// Based on BWV578 analysis: 3-voice texture dominates. Episodes in the
/// develop phase should typically use num_voices-1 active voices.
///
/// @param num_voices Total voice count.
/// @param phase_pos Current fugue phase position (0.0-1.0).
/// @param density_target Texture density target.
/// @param rng Random number generator.
/// @return Number of active voices for this episode.
uint8_t selectEpisodeVoiceCount(uint8_t num_voices, float phase_pos,
                                const TextureDensityTarget& density_target,
                                std::mt19937& rng,
                                bool post_entry = false) {
  if (num_voices <= 2) return num_voices;  // Can't reduce below 2.

  // In develop phase (0.25-0.70): mostly N-1 voices.
  float target = density_target.develop_density;
  if (phase_pos >= 0.70f) {
    target = density_target.stretto_density;
  }

  uint8_t target_voices = static_cast<uint8_t>(
      std::round(static_cast<float>(num_voices) * target));
  if (target_voices < 2) target_voices = 2;
  if (target_voices > num_voices) target_voices = num_voices;

  // D3: Musical-trigger tutti probability.
  // Post-entry episodes: 50% (subject re-entry warrants full texture).
  // Develop phase (< 0.70): 40% (raised from 25% for density).
  // Stretto phase (>= 0.70): 15% (near-tutti already via density target).
  float tutti_prob;
  if (post_entry) {
    tutti_prob = 0.50f;
  } else if (phase_pos < 0.70f) {
    tutti_prob = 0.40f;
  } else {
    tutti_prob = 0.15f;
  }
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  if (dist(rng) < tutti_prob) {
    return num_voices;
  }
  return target_voices;
}

/// @brief Select which voice to rest during an episode.
///
/// Rotates resting voice among inner voices only (outer voices are excluded).
/// Avoids selecting the same rest voice as the previous episode (alternation).
/// Avoids resting the entry voice (if in a middle entry section).
///
/// @param num_voices Total voice count.
/// @param episode_idx Episode index (for rotation).
/// @param entry_voice Voice with the current subject entry (-1 if none).
/// @param prev_rest Previous rest voice (num_voices = no previous).
/// @return Voice ID to rest, or num_voices if no rest.
uint8_t selectRestingVoice(uint8_t num_voices, int episode_idx,
                           int entry_voice = -1,
                           uint8_t prev_rest = 255) {
  if (num_voices <= 2) return num_voices;  // No rest possible.

  // Build list of eligible inner voices (exclude voice 0 and voice num_voices-1).
  // For 3-voice fugues, only voice 1 is inner.
  // For 4-voice fugues, voices 1 and 2 are inner.
  // For 5-voice fugues, voices 1, 2, and 3 are inner.
  uint8_t inner_count = (num_voices >= 3) ? (num_voices - 2) : 0;
  if (inner_count == 0) return num_voices;  // No inner voices available.

  // Rotate among inner voices, offset by episode index.
  uint8_t candidate = static_cast<uint8_t>(1 + (episode_idx % inner_count));

  // Alternation constraint: avoid consecutive same rest voice.
  if (candidate == prev_rest && inner_count > 1) {
    candidate = static_cast<uint8_t>(1 + ((episode_idx + 1) % inner_count));
  }

  // Skip entry voice if applicable.
  if (static_cast<int>(candidate) == entry_voice && inner_count > 1) {
    candidate = static_cast<uint8_t>(1 + ((episode_idx + 1) % inner_count));
    // Double-check alternation after entry voice skip.
    if (candidate == prev_rest && inner_count > 2) {
      candidate = static_cast<uint8_t>(1 + ((episode_idx + 2) % inner_count));
    }
  }

  return candidate;
}

/// @brief Apply FortPhase-dependent rest voice processing.
///
/// Augments rest voice notes to create held-tone texture (no note erasure).
/// BWV578 reference: avg_active ~2.66, with 3+ voices 67% of time.
/// Erasure-based approaches drop avg_active too much; augmentation-only
/// keeps all voices present while creating textural contrast.
///
/// Phase-dependent augmentation:
///   - Kernel:      Keep as-is (original rhythm for episode opening).
///   - Sequence:    Duration x2 (capped at half note) for held-tone effect.
///   - Dissolution: Duration x3 (capped at whole note) for sustained exit.
void applyFortPhaseRestProcessing(std::vector<NoteEvent>& notes,
                                  uint8_t rest_voice,
                                  Tick episode_start,
                                  Tick episode_duration,
                                  const FortspinnungGrammar& grammar) {
  Tick kernel_end = episode_start +
      static_cast<Tick>(static_cast<float>(episode_duration) * grammar.kernel_ratio);
  Tick sequence_end = kernel_end +
      static_cast<Tick>(static_cast<float>(episode_duration) * grammar.sequence_ratio);

  for (auto& note : notes) {
    if (note.voice != rest_voice) continue;
    if (note.start_tick < kernel_end) {
      // Kernel: keep original rhythm.
      continue;
    }
    if (note.start_tick < sequence_end) {
      // Sequence: augment duration x2, cap at half note.
      constexpr Tick kMaxSeq = duration::kHalfNote;  // 960 ticks
      note.duration = std::min(note.duration * 2, kMaxSeq);
    } else {
      // Dissolution: augment duration x3, cap at whole note.
      constexpr Tick kMaxDiss = duration::kWholeNote;  // 1920 ticks
      note.duration = std::min(note.duration * 3, kMaxDiss);
    }
  }
}

// ===========================================================================
// Pipeline step 1: Build material
// ===========================================================================

/// @brief Generate and validate subject, answer, countersubject,
///        constraint profile, and motif pool.
///
/// After basic subject generation and validation (via SubjectValidator),
/// runs FeasibilityHarness checks to gate subject quality:
///   1. findBestAssignment() — optimal octave placement for voice separation.
///   2. runMicroSim() — micro-exposition simulation for counterpoint feasibility.
///   3. verifyPair() — subject x answer obligation conflict detection.
///
/// If any check fails, the subject is regenerated with a different seed
/// (up to kMaxFeasibilityRetries attempts). On exhaustion, the best
/// attempt is used as a fallback (warning emitted to stderr).
///
/// @param config Fugue configuration.
/// @return All pre-computed material for the fugue.
FugueMaterial buildMaterial(const FugueConfig& config) {
  FugueMaterial mat;

  uint8_t num_voices = clampVoiceCount(config.num_voices);
  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);

  // --- Baseline: build material with the original seed (no feasibility gating). ---
  // This is the fallback if all feasibility attempts fail, preserving
  // identical output to the pre-feasibility-harness pipeline.
  mat.subject = generateValidSubject(config, mat.subject_attempts);
  if (mat.subject.notes.empty()) {
    return mat;
  }

  mat.constraint_profile = analyzeObligations(
      mat.subject.notes, config.key, config.is_minor);
  mat.answer = generateAnswer(mat.subject, config.answer_type, policy.preferred_answer);
  mat.countersubject = generateCountersubject(
      mat.subject, config.seed + 1000, 5, config.archetype);
  if (num_voices >= 4) {
    mat.countersubject_2 = generateSecondCountersubject(
        mat.subject, mat.countersubject, config.seed + 5000, 5, config.archetype);
  }
  mat.motif_pool.build(mat.subject.notes, mat.countersubject.notes,
                       mat.subject.character);

  // --- Feasibility gating: run harness checks to find a better subject. ---
  // Each attempt generates a subject, applies optimal octave placement,
  // then validates with micro-sim and pair verification.
  // If any attempt passes, its material replaces the baseline.
  for (int feasibility_attempt = 0; feasibility_attempt < kMaxFeasibilityRetries;
       ++feasibility_attempt) {
    // Vary the base seed for each feasibility retry to explore different subjects.
    // Use a large prime multiplier to avoid seed overlap with generateValidSubject's
    // internal retry seeds (which use 1000003u increments).
    FugueConfig attempt_config = config;
    if (feasibility_attempt > 0) {
      attempt_config.seed =
          config.seed + static_cast<uint32_t>(feasibility_attempt) * 7919u;
    }

    // Step 1: Generate subject with SubjectValidator retry logic.
    FugueMaterial candidate;
    candidate.subject = generateValidSubject(attempt_config, candidate.subject_attempts);
    if (candidate.subject.notes.empty()) {
      continue;
    }

    // Step 2: Analyze subject obligations for constraint-driven generation.
    candidate.constraint_profile = analyzeObligations(
        candidate.subject.notes, attempt_config.key, attempt_config.is_minor);

    // Step 3: Find optimal octave placement via voice assignment search.
    VoiceAssignment assignment = findBestAssignment(
        candidate.subject, candidate.constraint_profile, attempt_config);

    if (assignment.start_octave_offset != 0) {
      int semitone_shift = static_cast<int>(assignment.start_octave_offset) * 12;
      for (auto& note : candidate.subject.notes) {
        int shifted = static_cast<int>(note.pitch) + semitone_shift;
        note.pitch = static_cast<uint8_t>(std::clamp(shifted, 0, 127));
      }
      // Re-analyze obligations after pitch shift (contour/register may change).
      candidate.constraint_profile = analyzeObligations(
          candidate.subject.notes, attempt_config.key, attempt_config.is_minor);
    }

    // Step 4: Run micro-exposition simulation for counterpoint feasibility.
    MicroSimResult sim_result = runMicroSim(
        candidate.subject, attempt_config, kPipelineMicroSimTrials);

    // Step 5: Generate answer and verify subject x answer pair compatibility.
    candidate.answer = generateAnswer(
        candidate.subject, attempt_config.answer_type, policy.preferred_answer);

    SubjectConstraintProfile answer_profile = analyzeObligations(
        candidate.answer.notes, candidate.answer.key, attempt_config.is_minor);

    PairVerificationResult pair_result = verifyPair(
        candidate.constraint_profile, answer_profile,
        static_cast<int>(candidate.subject.length_ticks));

    // Accept immediately if both micro-sim and pair verification pass.
    if (sim_result.feasible() && pair_result.feasible()) {
      // Complete the candidate material before returning.
      candidate.countersubject = generateCountersubject(
          candidate.subject, attempt_config.seed + 1000, 5, attempt_config.archetype);
      if (num_voices >= 4) {
        candidate.countersubject_2 = generateSecondCountersubject(
            candidate.subject, candidate.countersubject,
            attempt_config.seed + 5000, 5, attempt_config.archetype);
      }
      candidate.motif_pool.build(candidate.subject.notes,
                                 candidate.countersubject.notes,
                                 candidate.subject.character);
      return candidate;
    }

    // Log the failure reason for diagnostics.
    if (!sim_result.feasible()) {
      fprintf(stderr,
              "buildMaterial: feasibility attempt %d/%d failed micro-sim "
              "(success_rate=%.2f, critical=%d, bottleneck=%d, overlap=%.2f)\n",
              feasibility_attempt + 1, kMaxFeasibilityRetries,
              sim_result.success_rate(),
              sim_result.num_critical_violations,
              sim_result.num_bottleneck,
              sim_result.avg_register_overlap);
    }
    if (!pair_result.feasible()) {
      fprintf(stderr,
              "buildMaterial: feasibility attempt %d/%d failed pair verification "
              "(conflicts=%zu, cadence_conflict=%.2f)\n",
              feasibility_attempt + 1, kMaxFeasibilityRetries,
              pair_result.conflicts.size(),
              pair_result.cadence_conflict_score);
    }
  }

  // Exhausted all retries: fall back to baseline material (original seed,
  // no octave shift) to preserve backward-compatible output.
  fprintf(stderr,
          "buildMaterial: feasibility retries exhausted, using baseline material\n");
  return mat;
}

// ===========================================================================
// Pipeline step 2: Plan structure
// ===========================================================================

/// @brief Create tonal plan, modulation plan, section layout, and energy curve.
/// @param config Fugue configuration.
/// @param material Pre-computed material from buildMaterial().
/// @return Complete structural plan for the fugue.
FuguePlan planStructure(const FugueConfig& config, const FugueMaterial& material) {
  FuguePlan plan;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // Estimate total duration using structural formula.
  Tick expo_ticks = static_cast<Tick>(num_voices) * material.subject.length_ticks;
  Tick episode_bars_tick = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick develop_ticks = (episode_bars_tick * static_cast<Tick>(config.develop_pairs) * 2) +
                       material.subject.length_ticks * static_cast<Tick>(config.develop_pairs);
  Tick return_ep_ticks = kTicksPerBar * static_cast<Tick>(config.episode_bars);
  Tick pedal_ticks = kTicksPerBar * kDominantPedalBars;
  Tick stretto_ticks = material.subject.length_ticks * 2;
  Tick coda_ticks = kTicksPerBar * kCodaBars;
  plan.estimated_duration = expo_ticks + develop_ticks + return_ep_ticks + pedal_ticks +
                            stretto_ticks + coda_ticks;
  Tick min_duration = kTicksPerBar * kMinFugueBars;
  if (plan.estimated_duration < min_duration) {
    plan.estimated_duration = min_duration;
  }

  // Create modulation plan.
  if (config.has_modulation_plan) {
    plan.modulation_plan = config.modulation_plan;
  } else {
    plan.modulation_plan = config.is_minor
        ? ModulationPlan::createForMinor(config.key)
        : ModulationPlan::createForMajor(config.key);
  }

  // Generate structure-aligned tonal plan.
  plan.tonal_plan = generateStructureAlignedTonalPlan(
      config, plan.modulation_plan, material.subject.length_ticks,
      plan.estimated_duration);

  // Create beat-resolution harmonic timeline.
  plan.detailed_timeline = plan.tonal_plan.toDetailedTimeline(plan.estimated_duration);

  // Plan section layout (exposition + develop pairs + return + pedal + stretto + coda).
  Tick current_tick = 0;

  // Section: Exposition (Establish).
  {
    PlannedSection expo_section;
    expo_section.type = SectionType::Exposition;
    expo_section.phase = FuguePhase::Establish;
    expo_section.key = config.key;
    expo_section.start_tick = current_tick;
    expo_section.end_tick = current_tick + expo_ticks;
    expo_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(expo_section);
    current_tick = expo_section.end_tick;
  }

  // Sections: Develop pairs (Episode + MiddleEntry).
  Key prev_key = config.key;
  for (int pair_idx = 0; pair_idx < config.develop_pairs; ++pair_idx) {
    int ep_bars = config.episode_bars;
    if (pair_idx % 2 != 0) {
      ep_bars += 1;
    }
    Tick episode_duration = kTicksPerBar * static_cast<Tick>(ep_bars);
    Key target_key = plan.modulation_plan.getTargetKey(pair_idx, config.key);

    // Episode section.
    PlannedSection ep_section;
    ep_section.type = SectionType::Episode;
    ep_section.phase = FuguePhase::Develop;
    ep_section.key = target_key;
    ep_section.prev_key = prev_key;
    ep_section.start_tick = current_tick;
    ep_section.end_tick = current_tick + episode_duration;
    ep_section.episode_index = pair_idx;
    ep_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(ep_section);
    current_tick += episode_duration;

    // MiddleEntry section.
    PlannedSection me_section;
    me_section.type = SectionType::MiddleEntry;
    me_section.phase = FuguePhase::Develop;
    me_section.key = target_key;
    me_section.prev_key = prev_key;
    me_section.start_tick = current_tick;
    me_section.end_tick = current_tick + material.subject.length_ticks;
    me_section.entry_voice = static_cast<VoiceId>(pair_idx % num_voices);
    me_section.companion_needed = true;
    me_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(me_section);
    current_tick += material.subject.length_ticks;

    prev_key = target_key;
  }

  // Section: Return Episode (transition back to home key).
  {
    Tick return_ep_duration = kTicksPerBar * static_cast<Tick>(config.episode_bars);
    PlannedSection return_section;
    return_section.type = SectionType::Episode;
    return_section.phase = FuguePhase::Develop;
    return_section.key = config.key;
    return_section.prev_key = prev_key;
    return_section.start_tick = current_tick;
    return_section.end_tick = current_tick + return_ep_duration;
    return_section.episode_index = config.develop_pairs;  // Next index after develop pairs
    return_section.energy_level = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
    plan.sections.push_back(return_section);
    current_tick += return_ep_duration;
    prev_key = config.key;
  }

  // Section: Stretto (Resolve).
  {
    PlannedSection stretto_section;
    stretto_section.type = SectionType::Stretto;
    stretto_section.phase = FuguePhase::Resolve;
    stretto_section.key = config.key;
    stretto_section.start_tick = current_tick;
    stretto_section.end_tick = current_tick + stretto_ticks;
    stretto_section.energy_level = FugueEnergyCurve::getLevel(
        current_tick, plan.estimated_duration);
    plan.sections.push_back(stretto_section);
    current_tick += stretto_ticks;
  }

  // Section: Coda (Resolve).
  {
    PlannedSection coda_section;
    coda_section.type = SectionType::Coda;
    coda_section.phase = FuguePhase::Resolve;
    coda_section.key = config.key;
    coda_section.start_tick = current_tick;
    coda_section.end_tick = current_tick + coda_ticks;
    coda_section.energy_level = 1.0f;
    plan.sections.push_back(coda_section);
  }

  return plan;
}

// ===========================================================================
// Pipeline step 3: Generate sections
// ===========================================================================

/// @brief Iterate planned sections and call existing sub-generators.
/// @param config Fugue configuration.
/// @param material Pre-computed material.
/// @param plan Structural plan.
/// @param structure Output FugueStructure for section tracking.
/// @return All generated notes across all sections.
std::vector<NoteEvent> generateSections(
    const FugueConfig& config, const FugueMaterial& material,
    const FuguePlan& plan, FugueStructure& structure) {
  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // 1. Initialize counterpoint infrastructure.
  CounterpointState cp_state;
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);
  CollisionResolver cp_resolver;
  cp_resolver.setHarmonicTimeline(&plan.detailed_timeline);

  // Pipeline-level constraint state for cross-section distribution tracking.
  ConstraintState pipeline_cs;
  pipeline_cs.gravity.melodic_model = &kFugueUpperMarkov;
  pipeline_cs.gravity.vertical_table = &kFugueVerticalTable;
  pipeline_cs.total_duration = plan.estimated_duration;
  // Collect cadence positions from the plan.
  for (const auto& sec : plan.sections) {
    if (sec.type == SectionType::Coda || sec.type == SectionType::Stretto) {
      pipeline_cs.cadence_ticks.push_back(sec.start_tick);
    }
  }

  // Previous episode exit state for chaining constraint state across episodes.
  // Carries forward gravity accumulator and distribution data so that consecutive
  // episodes share a continuous statistical context.
  std::optional<ConstraintState> prev_episode_exit;

  // Helper: record section notes in pipeline accumulator.
  auto record_notes = [&pipeline_cs](const std::vector<NoteEvent>& notes, Key key) {
    for (const auto& nev : notes) {
      int degree = 0;
      scale_util::pitchToScaleDegree(nev.pitch, key, ScaleType::Major, degree);
      pipeline_cs.accumulator.recordNote(nev.duration, degree);
    }
  };

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto [lo, hi] = getFugueVoiceRange(vid, num_voices);
    cp_state.registerVoice(vid, lo, hi);
  }
  cp_state.setKey(config.key);

  // 2. Tracking state.
  std::vector<NoteEvent> all_notes;
  Tick current_tick = 0;
  uint32_t entries_seen_mask = 0;

  // RNGs for false entry and density decisions.
  std::mt19937 false_entry_rng(config.seed + 9999u);
  std::mt19937 density_rng(config.seed + 8888u);

  // Character-based false entry probability.
  float false_entry_prob = 0.0f;
  switch (material.subject.character) {
    case SubjectCharacter::Restless: false_entry_prob = 0.40f; break;
    case SubjectCharacter::Playful:  false_entry_prob = 0.30f; break;
    case SubjectCharacter::Noble:    false_entry_prob = 0.15f; break;
    case SubjectCharacter::Severe:   false_entry_prob = 0.10f; break;
  }
  false_entry_prob = std::clamp(
      false_entry_prob + rng::rollFloat(false_entry_rng, -0.05f, 0.05f),
      0.0f, 1.0f);

  VoiceId lowest_voice = num_voices - 1;

  // Rest voice alternation tracking (Modify C).
  uint8_t prev_rest_voice = num_voices;  // Sentinel: no previous rest.

  // FortspinnungGrammar for FortPhase-based rest processing (Modify A).
  FortspinnungGrammar fort_grammar = getFortspinnungGrammar(material.subject.character);

  // 3. Section-by-section generation.
  for (size_t sec_idx = 0; sec_idx < plan.sections.size(); ++sec_idx) {
    const auto& section = plan.sections[sec_idx];

    switch (section.type) {
      case SectionType::Exposition: {
        Exposition expo = buildExposition(
            material.subject, material.answer, material.countersubject,
            config, config.seed,
            cp_state, cp_rules, cp_resolver,
            plan.detailed_timeline, plan.estimated_duration);
        structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                             0, expo.total_ticks, config.key);
        auto expo_notes = expo.allNotes();
        all_notes.insert(all_notes.end(), expo_notes.begin(), expo_notes.end());
        record_notes(expo_notes, config.key);
        current_tick = expo.total_ticks;
        break;
      }

      case SectionType::Episode: {
        Tick episode_duration = section.end_tick - section.start_tick;
        uint32_t pair_seed_base = config.seed +
            static_cast<uint32_t>(section.episode_index) * 2000u + 2000u;

        pipeline_cs.gravity.phase = FuguePhase::Develop;
        pipeline_cs.gravity.energy = section.energy_level;

        // Feasibility check: estimate solution space before episode generation.
        // If the leading voice has zero viable candidates, temporarily relax
        // constraints (widen voice range) to prevent generation deadlock.
        bool ranges_widened = false;
        {
          FeasibilityEstimator estimator;
          VerticalSnapshot snap = buildSnapshot(all_notes, current_tick, num_voices);
          InvariantSet feas_inv = buildFeasibilityInvariants(0, num_voices);
          auto feasibility = estimator.estimateWithCascade(
              cp_state, cp_rules, cp_resolver,
              0,  // voice 0 (leading voice)
              current_tick, kTicksPerBeat, feas_inv, snap);
          if (feasibility.min_choices == 0) {
            fprintf(stderr,
                    "Warning: episode %d at tick %u has zero feasible choices "
                    "for voice 0, widening voice range\n",
                    section.episode_index, current_tick);
            // Relax constraints: widen the registered voice range by a 5th
            // in each direction so the collision resolver has more room.
            for (uint8_t vid = 0; vid < num_voices; ++vid) {
              auto [cur_lo, cur_hi] = getFugueVoiceRange(vid, num_voices);
              uint8_t relaxed_lo = cur_lo >= 7 ? cur_lo - 7 : 0;
              uint8_t relaxed_hi = cur_hi <= 120 ? cur_hi + 7 : 127;
              cp_state.registerVoice(vid, relaxed_lo, relaxed_hi);
            }
            ranges_widened = true;
          }
        }

        // Extract per-voice last pitches for episode voice-leading continuity.
        uint8_t ep_last_pitches[6] = {};
        for (uint8_t vid = 0; vid < num_voices && vid < 6; ++vid) {
          ep_last_pitches[vid] = extractVoiceLastPitch(
              all_notes, current_tick, vid);
        }

        ConstraintState episode_exit;
        Episode episode = generateFortspinnungEpisode(
            material.subject, material.motif_pool,
            current_tick, episode_duration,
            section.prev_key, section.key, num_voices, pair_seed_base,
            section.episode_index, section.energy_level,
            cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
            0, &pipeline_cs.accumulator,
            prev_episode_exit ? &*prev_episode_exit : nullptr,
            &episode_exit, ep_last_pitches);
        // Chain exit state: update pipeline accumulator and store for next episode.
        pipeline_cs.accumulator = episode_exit.accumulator;
        prev_episode_exit = std::move(episode_exit);

        // Texture density management: FortPhase-dependent rest voice processing.
        // D3: Detect post-entry context for musical-trigger tutti.
        bool post_entry = (sec_idx > 0 &&
            plan.sections[sec_idx - 1].type == SectionType::MiddleEntry);
        float ep_phase_pos = static_cast<float>(current_tick) /
            static_cast<float>(plan.estimated_duration);
        uint8_t ep_active = selectEpisodeVoiceCount(
            num_voices, ep_phase_pos, config.density_target, density_rng,
            post_entry);
        if (ep_active < num_voices) {
          uint8_t rest_voice = selectRestingVoice(
              num_voices, section.episode_index, -1, prev_rest_voice);
          // FortPhase-dependent processing: Kernel=keep, Sequence=augment+thin,
          // Dissolution=erase. Replaces full erasure for more natural texture.
          applyFortPhaseRestProcessing(episode.notes, rest_voice,
                                       current_tick, episode_duration,
                                       fort_grammar);
          prev_rest_voice = rest_voice;
        }

        structure.addSection(SectionType::Episode, FuguePhase::Develop,
                             current_tick, current_tick + episode_duration,
                             section.key);
        all_notes.insert(all_notes.end(),
                         episode.notes.begin(), episode.notes.end());
        record_notes(episode.notes, section.key);

        // Restore original voice ranges if they were widened for feasibility.
        if (ranges_widened) {
          for (uint8_t vid = 0; vid < num_voices; ++vid) {
            auto [orig_lo, orig_hi] = getFugueVoiceRange(vid, num_voices);
            cp_state.registerVoice(vid, orig_lo, orig_hi);
          }
        }

        current_tick += episode_duration;
        break;
      }

      case SectionType::MiddleEntry: {
        Key target_key = section.key;

        // Count how many MiddleEntry sections precede this one for pair index.
        int me_count = 0;
        for (size_t prev_sec = 0; prev_sec < sec_idx; ++prev_sec) {
          if (plan.sections[prev_sec].type == SectionType::MiddleEntry) ++me_count;
        }
        int pair_idx = me_count;

        // Voice selection: rotation + unseen priority + bass forcing.
        uint8_t entry_voice;
        {
          uint8_t bass_voice = num_voices - 1;
          bool bass_has_entry = (entries_seen_mask & (1u << bass_voice)) != 0;
          int threshold = std::max(1, config.develop_pairs * 2 / 3);
          if (!bass_has_entry && pair_idx >= threshold && num_voices >= 4) {
            entry_voice = bass_voice;
          } else {
            uint8_t candidate = static_cast<uint8_t>(pair_idx % num_voices);
            for (uint8_t vid = 0; vid < num_voices; ++vid) {
              uint8_t check = (candidate + vid) % num_voices;
              if ((entries_seen_mask & (1u << check)) == 0) {
                candidate = check;
                break;
              }
            }
            entry_voice = candidate;
          }
          entries_seen_mask |= (1u << entry_voice);
        }

        // False entry decision.
        std::uniform_real_distribution<float> false_dist(0.0f, 1.0f);
        bool use_false_entry = (pair_idx > 0) &&
            (false_dist(false_entry_rng) < false_entry_prob);

        uint8_t entry_last = extractVoiceLastPitch(
            all_notes, current_tick, entry_voice);
        float me_phase_pos = static_cast<float>(current_tick) /
            static_cast<float>(plan.estimated_duration);

        MiddleEntry middle_entry = use_false_entry
            ? generateFalseEntry(material.subject, target_key, current_tick,
                                 entry_voice, num_voices, 3, me_phase_pos)
            : generateMiddleEntry(material.subject, target_key,
                                  current_tick, entry_voice, num_voices,
                                  cp_state, cp_rules, cp_resolver,
                                  plan.detailed_timeline, entry_last,
                                  me_phase_pos);

        Tick middle_end = middle_entry.end_tick;
        if (middle_end <= current_tick) {
          middle_end = current_tick + material.subject.length_ticks;
          if (middle_end <= current_tick) {
            middle_end = current_tick + kTicksPerBar * 2;
          }
        }

        structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                             current_tick, middle_end, target_key);
        all_notes.insert(all_notes.end(),
                         middle_entry.notes.begin(), middle_entry.notes.end());
        record_notes(middle_entry.notes, target_key);

        // C2: Countersubject placement in middle entries.
        // In Bach practice, the countersubject accompanies each subject entry
        // after the exposition. 85% placement for same-form entries.
        if (!material.countersubject.notes.empty() && !use_false_entry) {
          std::uniform_real_distribution<float> cs_dist(0.0f, 1.0f);
          if (cs_dist(density_rng) < 0.85f) {
            // CS voice: adjacent to entry, prefer upper voice, avoid bass.
            uint8_t cs_voice;
            if (entry_voice == 0) {
              cs_voice = 1;
            } else {
              cs_voice = entry_voice - 1;
            }
            // Don't collide with bass voice for 4+ voices.
            if (cs_voice >= num_voices - 1 && num_voices >= 4) {
              cs_voice = static_cast<uint8_t>(
                  (entry_voice + 1) % (num_voices - 1));
            }

            auto cs_notes = adaptCSToKey(
                material.countersubject.notes, target_key);
            auto [cs_lo, cs_hi] = getFugueVoiceRange(cs_voice, num_voices);
            int cs_shift = fitToRegister(cs_notes, cs_lo, cs_hi);

            // Independence check: CS start pitch != subject start (mod 12).
            bool independent = true;
            if (!cs_notes.empty() && !middle_entry.notes.empty()) {
              uint8_t cs_start = static_cast<uint8_t>(clampPitch(
                  static_cast<int>(cs_notes[0].pitch) + cs_shift, 0, 127));
              uint8_t subj_start = middle_entry.notes[0].pitch;
              if ((cs_start % 12) == (subj_start % 12)) {
                independent = false;
              }
            }

            if (independent) {
              for (auto& note : cs_notes) {
                note.start_tick += current_tick;
                note.pitch = static_cast<uint8_t>(clampPitch(
                    static_cast<int>(note.pitch) + cs_shift, 0, 127));
                note.voice = cs_voice;
                note.source = BachNoteSource::Countersubject;
              }
              // Trim notes that exceed the middle entry boundary.
              cs_notes.erase(
                  std::remove_if(cs_notes.begin(), cs_notes.end(),
                                 [middle_end](const NoteEvent& n) {
                                   return n.start_tick >= middle_end;
                                 }),
                  cs_notes.end());
              all_notes.insert(all_notes.end(),
                               cs_notes.begin(), cs_notes.end());
              record_notes(cs_notes, target_key);
            }
          }
        }

        // Companion counterpoint for non-entry voices.
        if (section.companion_needed) {
          Tick me_duration = middle_end - current_tick;
          if (me_duration > 0 && num_voices >= 2) {
            uint32_t companion_seed = config.seed +
                static_cast<uint32_t>(pair_idx) * 2000u + 2500u;
            float me_energy = FugueEnergyCurve::getLevel(
                current_tick, plan.estimated_duration);
            pipeline_cs.gravity.phase = FuguePhase::Develop;
            pipeline_cs.gravity.energy = me_energy;
            // Extract per-voice last pitches for companion voice-leading continuity.
            uint8_t comp_last_pitches[6] = {};
            for (uint8_t vid = 0; vid < num_voices && vid < 6; ++vid) {
              comp_last_pitches[vid] = extractVoiceLastPitch(
                  all_notes, current_tick, vid);
            }

            ConstraintState companion_exit;
            Episode companion = generateFortspinnungEpisode(
                material.subject, material.motif_pool,
                current_tick, me_duration,
                target_key, target_key, num_voices,
                companion_seed, pair_idx, me_energy,
                cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
                0, &pipeline_cs.accumulator,
                prev_episode_exit ? &*prev_episode_exit : nullptr,
                &companion_exit, comp_last_pitches);
            // Chain exit state from companion episode.
            pipeline_cs.accumulator = companion_exit.accumulator;
            prev_episode_exit = std::move(companion_exit);
            // Remove entry voice notes from companion.
            companion.notes.erase(
                std::remove_if(companion.notes.begin(), companion.notes.end(),
                               [entry_voice](const NoteEvent& evt) {
                                 return evt.voice == entry_voice;
                               }),
                companion.notes.end());
            // Additional voice rest for texture variety (4+ voices).
            float ep_phase_pos2 = static_cast<float>(current_tick) /
                static_cast<float>(plan.estimated_duration);
            uint8_t ep_active2 = selectEpisodeVoiceCount(
                num_voices, ep_phase_pos2, config.density_target, density_rng);
            if (ep_active2 < num_voices && num_voices >= 4) {
              uint8_t companion_rest = selectRestingVoice(
                  num_voices, pair_idx + 100, entry_voice, prev_rest_voice);
              if (companion_rest != entry_voice) {
                Tick me_dur = middle_end - current_tick;
                applyFortPhaseRestProcessing(companion.notes, companion_rest,
                                             current_tick, me_dur,
                                             fort_grammar);
                prev_rest_voice = companion_rest;
              }
            }
            all_notes.insert(all_notes.end(),
                             companion.notes.begin(), companion.notes.end());
            record_notes(companion.notes, target_key);
          }
        }

        current_tick = middle_end;
        break;
      }

      case SectionType::Stretto: {
        // Dominant pedal (4 bars before stretto).
        Tick pedal_duration = kTicksPerBar * kDominantPedalBars;
        {
          auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest_voice, num_voices);
          uint8_t tonic_for_pedal = tonicBassPitchForVoices(config.key,
                                                             num_voices);
          uint8_t dominant_pitch = clampPitch(
              static_cast<int>(tonic_for_pedal) + interval::kPerfect5th,
              ped_lo, ped_hi);

          removeLowestVoiceNotes(all_notes, lowest_voice,
                                 current_tick, current_tick + pedal_duration);
          auto dominant_pedal = generatePedalPoint(
              dominant_pitch, current_tick, pedal_duration, lowest_voice);
          all_notes.insert(all_notes.end(),
                           dominant_pedal.begin(), dominant_pedal.end());
          record_notes(dominant_pedal, config.key);

          // Register pedal notes in counterpoint state.
          for (const auto& note : dominant_pedal) {
            cp_state.addNote(note.voice, note);
          }

          // Upper voice episode over pedal.
          uint8_t upper_voices = num_voices > 1 ? num_voices - 1 : 1;
          float pedal_energy = FugueEnergyCurve::getLevel(
              current_tick, plan.estimated_duration);
          pipeline_cs.gravity.phase = FuguePhase::Resolve;
          pipeline_cs.gravity.energy = pedal_energy;
          // Extract per-voice last pitches for pedal episode voice-leading continuity.
          uint8_t pedal_last_pitches[6] = {};
          for (uint8_t vid = 0; vid < upper_voices && vid < 6; ++vid) {
            pedal_last_pitches[vid] = extractVoiceLastPitch(
                all_notes, current_tick, vid);
          }

          ConstraintState pedal_exit;
          Episode pedal_episode = generateFortspinnungEpisode(
              material.subject, material.motif_pool,
              current_tick, pedal_duration,
              config.key, config.key, upper_voices,
              config.seed + static_cast<uint32_t>(
                  config.develop_pairs + 1) * 2000u + 7000u,
              config.develop_pairs + 1, pedal_energy,
              cp_state, cp_rules, cp_resolver, plan.detailed_timeline,
              dominant_pitch, &pipeline_cs.accumulator,
              prev_episode_exit ? &*prev_episode_exit : nullptr,
              &pedal_exit, pedal_last_pitches);
          // Chain exit state from pedal episode.
          pipeline_cs.accumulator = pedal_exit.accumulator;
          prev_episode_exit = std::move(pedal_exit);

          // Texture reduction: rest one upper voice during pedal episode
          // to prevent tutti saturation (4+ voices).
          if (upper_voices >= 3) {
            uint8_t pedal_rest = static_cast<uint8_t>(
                (config.develop_pairs + 2) % upper_voices);
            pedal_episode.notes.erase(
                std::remove_if(pedal_episode.notes.begin(),
                               pedal_episode.notes.end(),
                               [pedal_rest](const NoteEvent& evt) {
                                 return evt.voice == pedal_rest;
                               }),
                pedal_episode.notes.end());
          }

          all_notes.insert(all_notes.end(),
                           pedal_episode.notes.begin(),
                           pedal_episode.notes.end());
          record_notes(pedal_episode.notes, config.key);
          current_tick += pedal_duration;
        }

        // Stretto body.
        uint8_t stretto_last[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          stretto_last[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
        }
        Stretto stretto = generateStretto(
            material.subject, config.key, current_tick,
            num_voices, config.seed + 4000,
            material.subject.character,
            cp_state, cp_rules, cp_resolver,
            plan.detailed_timeline, stretto_last,
            plan.estimated_duration);
        Tick stretto_end = stretto.end_tick;
        if (stretto_end <= current_tick) {
          stretto_end = current_tick + kTicksPerBar * 2;
        }
        structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                             current_tick, stretto_end, config.key);
        auto stretto_notes = stretto.allNotes();
        all_notes.insert(all_notes.end(),
                         stretto_notes.begin(), stretto_notes.end());
        record_notes(stretto_notes, config.key);
        current_tick = stretto_end;
        break;
      }

      case SectionType::Coda: {
        Tick coda_duration = kTicksPerBar * kCodaBars;
        structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                             current_tick, current_tick + coda_duration,
                             config.key);
        // Voice-leading: extract last pitches for smooth coda transitions.
        uint8_t coda_last_pitches[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          coda_last_pitches[vid] = extractVoiceLastPitch(
              all_notes, current_tick, vid);
        }
        auto coda_notes = createCodaNotes(
            current_tick, coda_duration, config.key, num_voices,
            config.is_minor, coda_last_pitches);
        all_notes.insert(all_notes.end(),
                         coda_notes.begin(), coda_notes.end());
        record_notes(coda_notes, config.key);

        // Tonic pedal in coda Stage 3 (last bar).
        {
          uint8_t tonic_pitch = tonicBassPitchForVoices(
              config.key, num_voices);
          Tick pedal_start = current_tick + kTicksPerBar * 3;
          Tick pedal_dur = current_tick + coda_duration - pedal_start;
          removeLowestVoiceNotes(all_notes, lowest_voice,
                                 pedal_start,
                                 current_tick + coda_duration);
          auto tonic_pedal = generatePedalPoint(
              tonic_pitch, pedal_start, pedal_dur, lowest_voice);
          all_notes.insert(all_notes.end(),
                           tonic_pedal.begin(), tonic_pedal.end());
        }

        current_tick += coda_duration;
        break;
      }
    }
  }

  return all_notes;
}

// ===========================================================================
// Pipeline step 4: Finalize — extracted sweep functions
// ===========================================================================

/// @brief Insert cadential coverage and approach notes into the fugue.
static void insertCadenceNotes(
    std::vector<NoteEvent>& all_notes,
    FugueStructure& structure,
    const FugueConfig& config,
    const FuguePlan& plan,
    uint8_t num_voices) {
  CadenceDetectionConfig cadence_config;
  cadence_config.max_bars_without_cadence = 16;
  cadence_config.scan_window_bars = 8;
  cadence_config.deceptive_cadence_probability = 0.20f;

  ensureCadentialCoverage(
      all_notes, structure, config.key, config.is_minor,
      num_voices - 1, num_voices,
      plan.estimated_duration,
      config.seed + 66666u, cadence_config);

  KeySignature home_key_sig;
  home_key_sig.tonic = config.key;
  home_key_sig.is_minor = config.is_minor;
  CadencePlan cadence_plan = CadencePlan::createForFugue(
      structure, home_key_sig, config.is_minor);

  applyCadenceApproachToVoices(
      all_notes, cadence_plan, config.key, config.is_minor,
      num_voices, config.seed + 88888u);
}

/// @brief Adjust episode material notes on strong beats that form dissonant
/// intervals with other simultaneously-sounding notes.  Lighter than the old
/// 11-pass post-validation but catches the worst offenders.
static void enforceStrongBeatConsonance(
    std::vector<NoteEvent>& all_notes,
    const FugueConfig& config,
    const FuguePlan& plan,
    uint8_t num_voices) {
  (void)config;       // reserved for future scale-aware consonance checks
  (void)num_voices;   // reserved for future voice-count-dependent thresholds
  Tick total_ticks = plan.estimated_duration;
  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    // Collect indices of notes active at this beat.
    std::vector<size_t> active;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].start_tick <= beat &&
          all_notes[idx].start_tick + all_notes[idx].duration > beat) {
        active.push_back(idx);
      }
    }
    if (active.size() < 2) continue;

    // Check episode material notes starting on this beat.
    for (size_t a : active) {
      if (all_notes[a].source != BachNoteSource::EpisodeMaterial &&
          all_notes[a].source != BachNoteSource::FreeCounterpoint) {
        continue;
      }
      if (all_notes[a].start_tick != beat) continue;

      bool has_dissonance = false;
      for (size_t b : active) {
        if (a == b) continue;
        int diff = std::abs(static_cast<int>(all_notes[a].pitch) -
                            static_cast<int>(all_notes[b].pitch));
        int simple = interval_util::compoundToSimple(diff);
        if (!interval_util::isConsonance(simple) && diff > 0 && diff < 36) {
          has_dissonance = true;
          break;
        }
      }
      if (!has_dissonance) continue;

      // Collect previous-beat pitches per voice for parallel checking.
      Tick prev_beat = (beat >= kTicksPerBeat) ? beat - kTicksPerBeat : 0;
      std::map<uint8_t, uint8_t> prev_pitches;
      if (beat > 0) {
        for (const auto& n : all_notes) {
          if (n.start_tick <= prev_beat &&
              n.start_tick + n.duration > prev_beat) {
            prev_pitches[n.voice] = n.pitch;
          }
        }
      }
      uint8_t this_voice = all_notes[a].voice;

      // Try small adjustments (±1..±3 semitones) to find consonance.
      // Reject candidates that create parallel P5 or P8.
      uint8_t orig = all_notes[a].pitch;
      for (int delta : {1, -1, 2, -2, 3, -3}) {
        int cand = static_cast<int>(orig) + delta;
        if (cand < 0 || cand > 127) continue;
        bool ok = true;
        for (size_t b : active) {
          if (a == b) continue;
          int diff = std::abs(cand - static_cast<int>(all_notes[b].pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple) && diff > 0 && diff < 36) {
            ok = false;
            break;
          }
          // Parallel check: if new interval is P1, P5, or P8, and the
          // same voice pair had the same interval class on the previous
          // beat, reject this candidate.
          if (simple == 0 || simple == 7) {
            uint8_t other_voice = all_notes[b].voice;
            auto it_this = prev_pitches.find(this_voice);
            auto it_other = prev_pitches.find(other_voice);
            if (it_this != prev_pitches.end() &&
                it_other != prev_pitches.end()) {
              int prev_diff = std::abs(
                  static_cast<int>(it_this->second) -
                  static_cast<int>(it_other->second));
              int prev_simple = interval_util::compoundToSimple(prev_diff);
              if (prev_simple == simple) {
                ok = false;
                break;
              }
            }
          }
        }
        if (ok) {
          all_notes[a].pitch = static_cast<uint8_t>(cand);
          break;
        }
      }
    }
  }
}

/// @brief Ensure episode material notes on strong beats are consonant with the
/// dominant pedal.  Uses the first pedal pitch and spans from first pedal
/// start to last pedal end (matching how vertical analysis sees it).
/// Only adjusts if the new pitch is also consonant with all other active
/// notes, to avoid creating new dissonances.
static void enforcePedalConsonance(std::vector<NoteEvent>& all_notes) {
  Tick first_pedal_start = 0, last_pedal_end = 0;
  uint8_t first_pedal_pitch = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::PedalPoint) continue;
    if (first_pedal_pitch == 0) {
      first_pedal_pitch = note.pitch;
      first_pedal_start = note.start_tick;
    }
    Tick nend = note.start_tick + note.duration;
    if (nend > last_pedal_end) last_pedal_end = nend;
  }

  if (first_pedal_pitch > 0) {
    for (size_t ai = 0; ai < all_notes.size(); ++ai) {
      auto& note = all_notes[ai];
      if (note.source != BachNoteSource::EpisodeMaterial) continue;
      if (note.start_tick < first_pedal_start ||
          note.start_tick >= last_pedal_end) {
        continue;
      }
      if (note.start_tick % kTicksPerBeat != 0) continue;

      int diff = std::abs(static_cast<int>(note.pitch) -
                          static_cast<int>(first_pedal_pitch));
      int simple = diff % 12;
      bool consonant = (simple == 0 || simple == 3 || simple == 4 ||
                        simple == 7 || simple == 8 || simple == 9);
      if (consonant) continue;

      // Collect other active notes at this tick for cross-check.
      std::vector<uint8_t> others;
      for (size_t bi = 0; bi < all_notes.size(); ++bi) {
        if (bi == ai) continue;
        if (all_notes[bi].start_tick <= note.start_tick &&
            all_notes[bi].start_tick + all_notes[bi].duration > note.start_tick) {
          others.push_back(all_notes[bi].pitch);
        }
      }

      // Find nearest pitch consonant with pedal AND all other voices.
      int best_delta = 0;
      int best_cost = 999;
      for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
        int cand = static_cast<int>(note.pitch) + delta;
        if (cand < 0 || cand > 127) continue;

        // Check pedal consonance.
        int cdiff = std::abs(cand - static_cast<int>(first_pedal_pitch));
        int csimple = cdiff % 12;
        bool ccons = (csimple == 0 || csimple == 3 || csimple == 4 ||
                      csimple == 7 || csimple == 8 || csimple == 9);
        if (!ccons) continue;

        // Check consonance with all other active voices.
        bool all_ok = true;
        for (uint8_t op : others) {
          int odiff = std::abs(cand - static_cast<int>(op));
          int osimple = interval_util::compoundToSimple(odiff);
          if (!interval_util::isConsonance(osimple) && odiff > 0 && odiff < 36) {
            all_ok = false;
            break;
          }
        }
        if (all_ok && std::abs(delta) < best_cost) {
          best_delta = delta;
          best_cost = std::abs(delta);
        }
      }
      if (best_delta != 0) {
        note.pitch = static_cast<uint8_t>(
            static_cast<int>(note.pitch) + best_delta);
      }
    }
  }
}

/// @brief Scan consecutive beats for parallel P5/P8 between voice pairs and
/// repair melodic tritone leaps on strong beats.
/// Repairs by adjusting Flexible-protection notes with small pitch shifts
/// snapped to scale.  Outer voice pairs (soprano-bass) are checked first.
static void repairParallelPerfectsAndTritones(
    std::vector<NoteEvent>& all_notes,
    const FugueConfig& config,
    const FuguePlan& plan,
    uint8_t num_voices) {
  constexpr int kMaxParallelRepairs = 16;
  ScaleType repair_scale =
      config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = plan.estimated_duration;

  // Sort by voice then start_tick for voice-based lookup.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.voice != rhs.voice) return lhs.voice < rhs.voice;
              return lhs.start_tick < rhs.start_tick;
            });

  // Helper: find the note sounding in a given voice at a given tick.
  // Returns pointer to the NoteEvent, or nullptr if none.
  auto findNoteAtTick = [&](VoiceId voice, Tick tick) -> NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.voice != voice) continue;
      if (note.start_tick > tick) break;  // sorted: no further matches
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick <= tick && note_end > tick) {
        // Skip very short passing tones (less than an eighth note).
        if (note.duration >= kTicksPerBeat / 2) {
          return &note;
        }
      }
    }
    return nullptr;
  };

  // Build ordered voice pairs: outer pairs first (0, N-1), then inner.
  std::vector<std::pair<VoiceId, VoiceId>> voice_pairs;
  voice_pairs.reserve(num_voices * (num_voices - 1) / 2);
  // Outer pair first (soprano vs bass).
  if (num_voices >= 2) {
    voice_pairs.push_back({0, static_cast<VoiceId>(num_voices - 1)});
  }
  // Remaining pairs, outer to inner.
  for (VoiceId vi = 0; vi < num_voices; ++vi) {
    for (VoiceId vj = vi + 1; vj < num_voices; ++vj) {
      if (vi == 0 && vj == num_voices - 1) continue;  // already added
      voice_pairs.push_back({vi, vj});
    }
  }

  int pre_count = 0;
  int outer_count = 0;
  int repair_count = 0;

  // Scan consecutive beat pairs.
  for (Tick beat = kTicksPerBeat; beat < total_ticks; beat += kTicksPerBeat) {
    Tick prev_beat = beat - kTicksPerBeat;

    for (const auto& [vi, vj] : voice_pairs) {
      NoteEvent* prev_note_a = findNoteAtTick(vi, prev_beat);
      NoteEvent* prev_note_b = findNoteAtTick(vj, prev_beat);
      NoteEvent* curr_note_a = findNoteAtTick(vi, beat);
      NoteEvent* curr_note_b = findNoteAtTick(vj, beat);

      if (!prev_note_a || !prev_note_b || !curr_note_a || !curr_note_b) {
        continue;
      }

      int prev_a = static_cast<int>(prev_note_a->pitch);
      int prev_b = static_cast<int>(prev_note_b->pitch);
      int curr_a = static_cast<int>(curr_note_a->pitch);
      int curr_b = static_cast<int>(curr_note_b->pitch);

      // Check same-direction motion (parallel or similar).
      int motion_a = curr_a - prev_a;
      int motion_b = curr_b - prev_b;
      if (motion_a == 0 || motion_b == 0) continue;  // oblique = ok
      if ((motion_a > 0) != (motion_b > 0)) continue;  // contrary = ok

      // Check parallel perfect interval (same interval class on both beats).
      int prev_ic = std::abs(prev_a - prev_b) % 12;
      int curr_ic = std::abs(curr_a - curr_b) % 12;
      if (prev_ic != curr_ic) continue;  // different interval classes
      if (prev_ic != 0 && prev_ic != 7) continue;  // not P1/P8 or P5

      pre_count++;
      // Only repair outer pair (soprano-bass) parallels — CRITICAL violations.
      // Inner pairs: preserve natural parallel rate (~0.84/100beats z-score).
      {
        bool is_outer = (vi == 0 && vj == static_cast<VoiceId>(num_voices - 1));
        if (!is_outer) continue;  // inner pairs: maintain natural rate
        outer_count++;
      }

      // Determine which note to modify: must be Flexible.
      NoteEvent* target = nullptr;
      int other_pitch = 0;
      int prev_voice_pitch = 0;
      if (getProtectionLevel(curr_note_b->source) == ProtectionLevel::Flexible) {
        target = curr_note_b;
        other_pitch = curr_a;
        prev_voice_pitch = prev_b;
      } else if (getProtectionLevel(curr_note_a->source) == ProtectionLevel::Flexible) {
        target = curr_note_a;
        other_pitch = curr_b;
        prev_voice_pitch = prev_a;
      }
      if (!target) continue;  // both Immutable

      // Generate candidates: ±1, ±2 semitones, snapped to scale.
      int best_pitch = -1;
      int best_leap_cost = INT_MAX;
      for (int delta : {-1, 1, -2, 2}) {
        int raw_cand = static_cast<int>(target->pitch) + delta;
        if (raw_cand < 0 || raw_cand > 127) continue;
        int cand = static_cast<int>(scale_util::nearestScaleTone(
            static_cast<uint8_t>(raw_cand), config.key, repair_scale));

        // Vertical safety: new interval should not be dissonant.
        int new_ic = std::abs(cand - other_pitch) % 12;
        // Reject P1/m2/M2/tritone (0, 1, 2, 6).
        if (new_ic == 0 || new_ic == 1 || new_ic == 2 || new_ic == 6) continue;

        // Verify the repair does not recreate a parallel perfect interval.
        int prev_other_pitch = (target == curr_note_a) ? prev_b : prev_a;
        int prev_pair_ic = std::abs(prev_voice_pitch - prev_other_pitch) % 12;
        int new_pair_ic = std::abs(cand - other_pitch) % 12;
        if ((new_pair_ic == 0 || new_pair_ic == 7) &&
            new_pair_ic == prev_pair_ic) {
          continue;  // would still be parallel perfect
        }

        // Melodic continuity: leap should not increase too much.
        int leap_before = std::abs(cand - prev_voice_pitch);
        int original_leap = std::abs(static_cast<int>(target->pitch) - prev_voice_pitch);
        if (leap_before > original_leap + 2) continue;

        if (leap_before < best_leap_cost) {
          best_leap_cost = leap_before;
          best_pitch = cand;
        }
      }

      if (best_pitch >= 0) {
        target->pitch = static_cast<uint8_t>(best_pitch);
        target->modified_by |= static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
        repair_count++;
      }
    }
  }

  // --- Melodic tritone repair ---
  // Scan each voice for consecutive notes forming a tritone leap (6 semitones)
  // on a strong beat. Adjust Flexible notes with ±1 semitone shift.
  for (VoiceId vid = 0; vid < num_voices; ++vid) {
    // Collect indices of notes in this voice, sorted by start_tick.
    std::vector<size_t> voice_indices;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == vid) {
        voice_indices.push_back(idx);
      }
    }
    // Already sorted by voice then start_tick from earlier sort.

    for (size_t pos = 1; pos < voice_indices.size(); ++pos) {
      if (repair_count >= kMaxParallelRepairs) break;

      size_t curr_idx = voice_indices[pos];
      size_t prev_idx = voice_indices[pos - 1];
      auto& curr_note = all_notes[curr_idx];
      auto& prev_note = all_notes[prev_idx];

      int leap = std::abs(static_cast<int>(curr_note.pitch) -
                          static_cast<int>(prev_note.pitch));
      if (leap != 6) continue;  // not a tritone

      // Only repair on strong beats (bar start or beat start).
      if (curr_note.start_tick % kTicksPerBar >= kTicksPerBeat) continue;

      // Only modify Flexible notes.
      if (getProtectionLevel(curr_note.source) != ProtectionLevel::Flexible) {
        continue;
      }

      // Try ±1 candidates, snap to scale.
      int best_cand = -1;
      int best_cost = INT_MAX;
      for (int delta : {-1, 1}) {
        int raw = static_cast<int>(curr_note.pitch) + delta;
        if (raw < 0 || raw > 127) continue;
        int cand = static_cast<int>(scale_util::nearestScaleTone(
            static_cast<uint8_t>(raw), config.key, repair_scale));
        int new_leap = std::abs(cand - static_cast<int>(prev_note.pitch));
        if (new_leap == 6) continue;  // still tritone

        // Check continuity with next note if exists.
        if (pos + 1 < voice_indices.size()) {
          int next_pitch = static_cast<int>(all_notes[voice_indices[pos + 1]].pitch);
          int next_leap = std::abs(cand - next_pitch);
          int orig_next = std::abs(static_cast<int>(curr_note.pitch) - next_pitch);
          if (next_leap > orig_next + 2) continue;
        }

        if (new_leap < best_cost) {
          best_cost = new_leap;
          best_cand = cand;
        }
      }

      if (best_cand >= 0) {
        curr_note.pitch = static_cast<uint8_t>(best_cand);
        curr_note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
        repair_count++;
      }
    }
  }

  fprintf(stderr, "P7.d sweep: %d parallels found (%d outer), %d repaired\n",
          pre_count, outer_count, repair_count);
}

/// @brief Scan each beat for adjacent voice pairs where the higher-numbered
/// voice (lower register) sounds a higher pitch than the lower-numbered voice
/// (higher register).  Repairs by adjusting the Flexible note with the
/// smallest diatonic pitch shift that resolves the crossing.
/// Limit: 8 repairs per section to protect melodic linearity.
static void repairVoiceCrossings(
    std::vector<NoteEvent>& all_notes,
    const FugueConfig& config,
    const FuguePlan& plan,
    uint8_t num_voices) {
  constexpr int kMaxCrossingRepairsPerSection = 8;
  ScaleType crossing_scale =
      config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = plan.estimated_duration;

  // Notes are already sorted by voice then start_tick from the parallel sweep.
  // Reuse the same findNoteAtTick pattern.
  auto findNoteForVoice = [&](VoiceId voice, Tick tick) -> NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.voice != voice) continue;
      if (note.start_tick > tick) break;  // sorted: no further matches
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick <= tick && note_end > tick) {
        return &note;
      }
    }
    return nullptr;
  };

  // Helper: find the previous note in the same voice before a given tick.
  auto findPrevNote = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    const NoteEvent* best = nullptr;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice) continue;
      if (note.start_tick >= tick) break;  // sorted
      best = &note;
    }
    return best;
  };

  // Helper: find the next note in the same voice after a given tick.
  auto findNextNote = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice) continue;
      if (note.start_tick > tick) return &note;
    }
    return nullptr;
  };

  // Track section boundaries from the plan for per-section repair limits.
  // Build a flat list of section start ticks.
  std::vector<Tick> section_starts;
  section_starts.reserve(plan.sections.size() + 1);
  for (const auto& sec : plan.sections) {
    section_starts.push_back(sec.start_tick);
  }
  section_starts.push_back(total_ticks);  // sentinel

  int total_crossing_repairs = 0;
  int total_crossings_found = 0;

  // Process per-section to enforce the per-section repair limit.
  // Coda/Stretto sections use CrossingPolicy::Reject semantics: unlimited repairs.
  for (size_t sec_idx = 0; sec_idx < plan.sections.size(); ++sec_idx) {
    Tick sec_start = plan.sections[sec_idx].start_tick;
    Tick sec_end = (sec_idx + 1 < plan.sections.size())
                       ? plan.sections[sec_idx + 1].start_tick
                       : total_ticks;
    int section_repairs = 0;
    bool conclude_policy =
        plan.sections[sec_idx].type == SectionType::Coda ||
        plan.sections[sec_idx].type == SectionType::Stretto;
    int max_repairs = conclude_policy ? INT_MAX : kMaxCrossingRepairsPerSection;

    for (Tick beat = sec_start; beat < sec_end; beat += kTicksPerBeat) {
      if (section_repairs >= max_repairs) break;

      // Check adjacent voice pairs (vi, vi+1).
      for (VoiceId vi = 0; vi + 1 < num_voices; ++vi) {
        if (section_repairs >= max_repairs) break;

        VoiceId vj = vi + 1;
        NoteEvent* note_upper = findNoteForVoice(vi, beat);
        NoteEvent* note_lower = findNoteForVoice(vj, beat);
        if (!note_upper || !note_lower) continue;

        int pitch_upper = static_cast<int>(note_upper->pitch);
        int pitch_lower = static_cast<int>(note_lower->pitch);

        // Crossing: higher-numbered voice (lower register) has higher pitch.
        if (pitch_upper >= pitch_lower) continue;

        // 2-beat lookahead: skip temporary crossings (matches Python validator).
        // Crossings that resolve within 2 beats are INFO (0pts) in scoring.
        bool resolves_soon = false;
        for (int ahead = 1; ahead <= 2; ++ahead) {
          Tick future = beat + kTicksPerBeat * ahead;
          if (future >= sec_end) break;
          NoteEvent* fu = findNoteForVoice(vi, future);
          NoteEvent* fl = findNoteForVoice(vj, future);
          if (fu && fl &&
              static_cast<int>(fu->pitch) >= static_cast<int>(fl->pitch)) {
            resolves_soon = true;
            break;
          }
        }
        if (resolves_soon) continue;

        total_crossings_found++;

        // Determine which note to modify based on ProtectionLevel.
        bool upper_flex =
            getProtectionLevel(note_upper->source) == ProtectionLevel::Flexible;
        bool lower_flex =
            getProtectionLevel(note_lower->source) == ProtectionLevel::Flexible;

        if (!upper_flex && !lower_flex) continue;  // both Immutable: skip

        // Prefer modifying the inner voice (higher-numbered = lower register).
        // If only one is Flexible, modify that one.
        NoteEvent* target = nullptr;
        const NoteEvent* anchor = nullptr;
        bool moving_upper = false;
        if (upper_flex && lower_flex) {
          // Prefer inner voice (lower = vj) to preserve soprano line.
          target = note_lower;
          anchor = note_upper;
          moving_upper = false;
        } else if (lower_flex) {
          target = note_lower;
          anchor = note_upper;
          moving_upper = false;
        } else {
          target = note_upper;
          anchor = note_lower;
          moving_upper = true;
        }

        int target_pitch = static_cast<int>(target->pitch);
        int anchor_pitch = static_cast<int>(anchor->pitch);

        // Find previous and next notes for melodic continuity check.
        const NoteEvent* prev = findPrevNote(target->voice, target->start_tick);
        const NoteEvent* next = findNextNote(target->voice,
                                             target->start_tick + target->duration - 1);

        int orig_prev_leap = prev
            ? std::abs(target_pitch - static_cast<int>(prev->pitch))
            : 0;
        int orig_next_leap = next
            ? std::abs(target_pitch - static_cast<int>(next->pitch))
            : 0;

        // Generate candidates: target.pitch +/- 1..12 semitones, snapped to scale.
        int best_cand = -1;
        int best_cost = INT_MAX;

        for (int delta : {1,  -1,  2,  -2,  3,  -3,  4,  -4,  5,  -5,  6,  -6,
                          7,  -7,  8,  -8,  9,  -9, 10, -10, 11, -11, 12, -12}) {
          int raw = target_pitch + delta;
          if (raw < 0 || raw > 127) continue;

          int cand = static_cast<int>(scale_util::nearestScaleTone(
              static_cast<uint8_t>(raw), config.key, crossing_scale));
          if (cand < 0 || cand > 127) continue;

          // Check: does the new pitch resolve the crossing?
          // If moving the upper voice (vi), cand must be >= anchor (lower voice pitch).
          // If moving the lower voice (vj), cand must be <= anchor (upper voice pitch).
          if (moving_upper) {
            if (cand < anchor_pitch) continue;  // still crossed
          } else {
            if (cand > anchor_pitch) continue;  // still crossed
          }

          // Check: vertical interval with the other voice is not unison/m2/tritone.
          int interval = std::abs(cand - anchor_pitch) % 12;
          if (interval == 1 || interval == 2 || interval == 6) continue;
          // Allow unison (interval == 0) only if voices are widely separated in register.

          // Check: melodic jump to prev/next note <= original jump + 4 semitones.
          if (prev) {
            int new_prev_leap = std::abs(cand - static_cast<int>(prev->pitch));
            if (new_prev_leap > orig_prev_leap + 4) continue;
          }
          if (next) {
            int new_next_leap = std::abs(cand - static_cast<int>(next->pitch));
            if (new_next_leap > orig_next_leap + 4) continue;
          }

          // Cost: distance from original pitch + melodic distortion.
          int pitch_cost = std::abs(cand - target_pitch);
          int melodic_penalty = 0;
          if (prev) {
            int new_leap = std::abs(cand - static_cast<int>(prev->pitch));
            melodic_penalty += (new_leap > orig_prev_leap) ? (new_leap - orig_prev_leap) : 0;
          }
          int cost = pitch_cost * 2 + melodic_penalty;
          if (cost < best_cost) {
            best_cost = cost;
            best_cand = cand;
          }
        }

        if (best_cand >= 0) {
          target->pitch = static_cast<uint8_t>(best_cand);
          target->modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
          section_repairs++;
          total_crossing_repairs++;
        }
      }
    }
  }

  fprintf(stderr, "Voice crossing sweep: %d crossings found, %d repaired\n",
          total_crossings_found, total_crossing_repairs);
}

// ===========================================================================
// Pipeline step 4: Finalize
// ===========================================================================

/// @brief Minimal post-processing and track assembly.
/// @param config Fugue configuration.
/// @param material Pre-computed material.
/// @param plan Structural plan.
/// @param structure Fugue structure with sections.
/// @param all_notes All generated notes (moved in).
/// @return Finalized FugueResult with tracks and structure.
FugueResult finalize(
    const FugueConfig& config, const FugueMaterial& material,
    const FuguePlan& plan, FugueStructure& structure,
    std::vector<NoteEvent> all_notes) {
  FugueResult result;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // --- Cadence insertion ---
  insertCadenceNotes(all_notes, structure, config, plan, num_voices);

  // --- Within-voice overlap removal ---
  finalizeFormNotes(all_notes, num_voices);

  // --- Strong-beat consonance enforcement ---
  enforceStrongBeatConsonance(all_notes, config, plan, num_voices);

  // --- Pedal consonance enforcement ---
  enforcePedalConsonance(all_notes);

  // --- Parallel perfect interval + melodic tritone repair ---
  repairParallelPerfectsAndTritones(all_notes, config, plan, num_voices);

  // --- Voice crossing repair ---
  repairVoiceCrossings(all_notes, config, plan, num_voices);

  // --- Create organ tracks + assign + sort ---
  result.tracks = createOrganTracks(num_voices);
  assignNotesToTracks(all_notes, result.tracks);
  sortTrackNotes(result.tracks);

  // --- Bar-resolution timeline ---
  {
    HarmonicTimeline bar_timeline;
    struct Region {
      Key key;
      Tick start;
      Tick end;
    };
    std::vector<Region> regions;
    const auto& mods = plan.tonal_plan.modulations;
    Tick total_ticks = plan.estimated_duration;
    if (mods.empty()) {
      regions.push_back({config.key, 0, total_ticks});
    } else {
      if (mods[0].tick > 0) {
        regions.push_back({config.key, 0, mods[0].tick});
      }
      for (size_t idx = 0; idx < mods.size(); ++idx) {
        Tick rstart = mods[idx].tick;
        Tick rend = (idx + 1 < mods.size()) ? mods[idx + 1].tick : total_ticks;
        if (rend > rstart) {
          regions.push_back({mods[idx].target_key, rstart, rend});
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
      for (const auto& evt : rtl.events()) {
        HarmonicEvent offset_ev = evt;
        offset_ev.tick += region.start;
        offset_ev.end_tick += region.start;
        bar_timeline.addEvent(offset_ev);
      }
    }
    result.timeline = bar_timeline;
  }

  // --- Harmonic rhythm factors ---
  {
    auto cadence_ticks_vec = extractCadenceTicks(
        CadencePlan::createForFugue(
            structure,
            KeySignature{config.key, config.is_minor},
            config.is_minor));
    applyRhythmFactors(result.timeline.mutableEvents(),
                        plan.estimated_duration, cadence_ticks_vec);
  }

  // --- Picardy third ---
  if (config.enable_picardy && config.is_minor) {
    KeySignature home_key_sig;
    home_key_sig.tonic = config.key;
    home_key_sig.is_minor = config.is_minor;
    for (auto& track : result.tracks) {
      applyPicardyToFinalChord(track.notes, home_key_sig,
                               plan.estimated_duration - kTicksPerBar);
    }
  }

  // --- Extended registration ---
  {
    auto ext_plan = createExtendedRegistrationPlan(
        structure.sections, plan.estimated_duration);
    applyExtendedRegistrationPlan(result.tracks, ext_plan);
  }

  // --- Re-sort tracks after Picardy and registration modifications ---
  sortTrackNotes(result.tracks);

  // --- Populate result fields ---
  result.success = true;
  result.structure = std::move(structure);
  result.attempts = material.subject_attempts;
  result.generation_timeline = plan.detailed_timeline;

  return result;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

FugueResult generateFuguePipeline(const FugueConfig& config) {
  // Step 1: Build material.
  FugueMaterial material = buildMaterial(config);
  if (material.subject.notes.empty()) {
    FugueResult result;
    result.error_message = "Failed to generate a valid subject";
    return result;
  }

  // Step 1b: Resolve pedal mode (immutable for this seed).
  FugueConfig resolved_config = config;
  resolved_config.pedal_mode = determinePedalMode(
      config, material.subject, material.answer);

  // Step 2: Plan structure.
  FuguePlan plan = planStructure(resolved_config, material);

  // Step 3: Generate sections.
  FugueStructure structure;
  std::vector<NoteEvent> all_notes = generateSections(
      resolved_config, material, plan, structure);

  // Step 4: Finalize.
  return finalize(resolved_config, material, plan, structure, std::move(all_notes));
}

}  // namespace bach
