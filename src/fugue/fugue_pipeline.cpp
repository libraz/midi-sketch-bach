// Fugue pipeline: constraint-driven generation with 4-step architecture.
// Replaces the monolithic generateFugue() with buildMaterial -> planStructure
// -> generateSections -> finalize.

#include "fugue/fugue_pipeline.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
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
#include "core/form_profile.h"
#include "core/interval.h"
#include "core/markov_tables.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/leap_resolution.h"
#include "counterpoint/repeated_note_repair.h"
#include "counterpoint/vertical_safe.h"
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
#include "fugue/thematic_plan.h"
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
  ThematicPlan thematic_plan;
};

/// @brief Planned section in the fugue structure.
struct PlannedSection {
  SectionType type = SectionType::Exposition;
  FuguePhase phase = FuguePhase::Establish;
  Key key = Key::C;
  Key prev_key = Key::C;  // Key before this section (for modulation tracking)
  Tick start_tick = 0;
  Tick end_tick = 0;
  int episode_index = -1;
  VoiceId entry_voice = 0;
  bool is_false_entry = false;
  bool companion_needed = false;  // True for MiddleEntry sections needing companion counterpoint
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
  if (num_voices < kMinVoices)
    return kMinVoices;
  if (num_voices > kMaxVoices)
    return kMaxVoices;
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
PedalMode determinePedalMode(const FugueConfig& config, const Subject& subject,
                             const Answer& answer) {
  if (config.pedal_mode != PedalMode::Auto)
    return config.pedal_mode;
  if (config.num_voices != 3)
    return PedalMode::ManualBass;

  // Check if subject fits in pedal range [24, 50] at any octave.
  constexpr uint8_t kPedalLo = 24;
  constexpr uint8_t kPedalHi = 50;

  auto fitsInPedalRange = [&](const std::vector<NoteEvent>& notes) -> bool {
    if (notes.empty())
      return false;
    uint8_t min_p = 127, max_p = 0;
    for (const auto& n : notes) {
      if (n.pitch < min_p)
        min_p = n.pitch;
      if (n.pitch > max_p)
        max_p = n.pitch;
    }
    int span = static_cast<int>(max_p) - static_cast<int>(min_p);
    int target_span = static_cast<int>(kPedalHi) - static_cast<int>(kPedalLo);
    if (span > target_span)
      return false;  // Subject wider than pedal range.

    // Try all octave shifts.
    for (int shift = -48; shift <= 48; shift += 12) {
      int lo = static_cast<int>(min_p) + shift;
      int hi = static_cast<int>(max_p) + shift;
      if (lo >= kPedalLo && hi <= kPedalHi)
        return true;
    }
    return false;
  };

  bool subject_fits = fitsInPedalRange(subject.notes);
  bool answer_fits = fitsInPedalRange(answer.notes);

  PedalMode result = (subject_fits && answer_fits) ? PedalMode::TruePedal : PedalMode::ManualBass;
  fprintf(stderr, "determinePedalMode: subject_fits=%d answer_fits=%d -> %s\n", subject_fits,
          answer_fits, result == PedalMode::TruePedal ? "TruePedal" : "ManualBass");
  return result;
}

/// @brief Human-readable name for an organ manual.
/// @param manual OrganManual enum value.
/// @return Descriptive string for the manual.
const char* manualTrackName(OrganManual manual) {
  switch (manual) {
    case OrganManual::Great:
      return "Manual I (Great)";
    case OrganManual::Swell:
      return "Manual II (Swell)";
    case OrganManual::Positiv:
      return "Manual III (Positiv)";
    case OrganManual::Pedal:
      return "Pedal";
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
void assignNotesToTracks(const std::vector<NoteEvent>& notes, std::vector<Track>& tracks) {
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
std::vector<NoteEvent> generatePedalPoint(uint8_t pitch, Tick start_tick, Tick duration,
                                          VoiceId voice_id) {
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
void removeLowestVoiceNotes(std::vector<NoteEvent>& all_notes, VoiceId voice_id, Tick region_start,
                            Tick region_end) {
  all_notes.erase(std::remove_if(all_notes.begin(), all_notes.end(),
                                 [voice_id, region_start, region_end](const NoteEvent& evt) {
                                   if (evt.voice != voice_id)
                                     return false;
                                   Tick note_end = evt.start_tick + evt.duration;
                                   return evt.start_tick < region_end && note_end > region_start;
                                 }),
                  all_notes.end());
}

static void trimVoiceNotesForRegion(std::vector<NoteEvent>& all_notes, VoiceId voice_id,
                                    Tick region_start, Tick region_end) {
  std::vector<NoteEvent> kept;
  kept.reserve(all_notes.size() + 2);
  for (const auto& evt : all_notes) {
    if (evt.voice != voice_id) {
      kept.push_back(evt);
      continue;
    }
    Tick note_end = evt.start_tick + evt.duration;
    if (evt.start_tick >= region_end || note_end <= region_start) {
      kept.push_back(evt);
      continue;
    }
    if (evt.start_tick < region_start) {
      NoteEvent prefix = evt;
      prefix.duration = region_start - evt.start_tick;
      if (prefix.duration > 0) {
        prefix.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        kept.push_back(prefix);
      }
    }
    if (note_end > region_end) {
      NoteEvent suffix = evt;
      suffix.start_tick = region_end;
      suffix.duration = note_end - region_end;
      if (suffix.duration > 0) {
        suffix.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        kept.push_back(suffix);
      }
    }
  }
  all_notes.swap(kept);
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
static uint8_t extractVoiceLastPitch(const std::vector<NoteEvent>& notes, Tick before_tick,
                                     VoiceId voice) {
  uint8_t last_pitch = 0;
  for (auto iter = notes.rbegin(); iter != notes.rend(); ++iter) {
    if (iter->voice == voice && iter->start_tick < before_tick) {
      last_pitch = iter->pitch;
      break;
    }
  }
  return last_pitch;
}

static const NoteEvent* findVoiceLastNoteBefore(const std::vector<NoteEvent>& notes,
                                                Tick before_tick, VoiceId voice) {
  const NoteEvent* best = nullptr;
  for (const auto& note : notes) {
    if (note.voice != voice || note.start_tick >= before_tick)
      continue;
    if (best == nullptr || note.start_tick > best->start_tick ||
        (note.start_tick == best->start_tick && note.duration > best->duration)) {
      best = &note;
    }
  }
  return best;
}

static bool isEpisodeEntryContinuitySource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::FreeCounterpoint;
}

static int shapeEpisodeEntryContinuity(std::vector<NoteEvent>& episode_notes,
                                       const std::vector<NoteEvent>& previous_notes,
                                       Tick episode_start, const HarmonicTimeline& timeline,
                                       const FugueConfig& config, uint8_t num_voices) {
  int shaped = 0;

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    const NoteEvent* prior = findVoiceLastNoteBefore(previous_notes, episode_start, voice);
    if (prior == nullptr)
      continue;

    std::vector<size_t> entry_idxs;
    for (size_t idx = 0; idx < episode_notes.size(); ++idx) {
      const NoteEvent& note = episode_notes[idx];
      if (note.voice != voice)
        continue;
      if (!isEpisodeEntryContinuitySource(note.source))
        continue;
      if (note.start_tick < episode_start || note.start_tick >= episode_start + kTicksPerBar * 2) {
        continue;
      }
      entry_idxs.push_back(idx);
    }
    if (entry_idxs.empty())
      continue;

    std::sort(entry_idxs.begin(), entry_idxs.end(), [&](size_t lhs, size_t rhs) {
      if (episode_notes[lhs].start_tick != episode_notes[rhs].start_tick) {
        return episode_notes[lhs].start_tick < episode_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    NoteEvent& first = episode_notes[entry_idxs.front()];
    Tick prior_end = prior->start_tick + prior->duration;
    Tick gap = first.start_tick > prior_end ? first.start_tick - prior_end : 0;
    if (gap > kTicksPerBar)
      continue;

    int entry_leap = std::abs(static_cast<int>(first.pitch) - static_cast<int>(prior->pitch));
    if (entry_leap <= interval::kPerfect5th)
      continue;

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    int best_shift = 0;
    int best_dist = entry_leap;
    int best_abs_shift = INT32_MAX;
    for (int shift : {-24, -12, 12, 24}) {
      int shifted = static_cast<int>(first.pitch) + shift;
      if (shifted < static_cast<int>(lo) || shifted > static_cast<int>(hi))
        continue;
      int dist = std::abs(shifted - static_cast<int>(prior->pitch));
      int abs_shift = std::abs(shift);
      if (dist <= interval::kPerfect5th &&
          (abs_shift < best_abs_shift || (abs_shift == best_abs_shift && dist < best_dist))) {
        best_dist = dist;
        best_shift = shift;
        best_abs_shift = abs_shift;
      }
    }

    if (best_shift != 0) {
      for (size_t idx : entry_idxs) {
        NoteEvent& note = episode_notes[idx];
        if (note.start_tick >= episode_start + kTicksPerBar * 2)
          continue;
        int shifted = static_cast<int>(note.pitch) + best_shift;
        if (shifted < static_cast<int>(lo) || shifted > static_cast<int>(hi))
          break;
        note.pitch = static_cast<uint8_t>(shifted);
        ++shaped;
      }
      continue;
    }

    Key local_key = config.key;
    ScaleType local_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    if (timeline.size() > 0) {
      const HarmonicEvent& event = timeline.getAt(first.start_tick);
      local_key = event.key;
      local_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    }

    int prior_degree = scale_util::pitchToAbsoluteDegree(prior->pitch, local_key, local_scale);
    int target_degree = scale_util::pitchToAbsoluteDegree(first.pitch, local_key, local_scale);
    int dir = target_degree >= prior_degree ? 1 : -1;
    for (int steps : {1, 2}) {
      int candidate =
          scale_util::absoluteDegreeToPitch(prior_degree + dir * steps, local_key, local_scale);
      if (candidate < static_cast<int>(lo) || candidate > static_cast<int>(hi))
        continue;
      if (std::abs(candidate - static_cast<int>(prior->pitch)) > interval::kPerfect5th) {
        continue;
      }
      first.pitch = static_cast<uint8_t>(candidate);
      ++shaped;
      break;
    }
  }

  return shaped;
}

/// @brief Build a VerticalSnapshot from the last pitch of each voice at a given tick.
/// @param notes All fugue notes generated so far.
/// @param tick Current tick position.
/// @param num_voices Total number of voices in the fugue.
/// @return Snapshot with last-sounding pitch per voice (0 if silent).
static VerticalSnapshot buildSnapshot(const std::vector<NoteEvent>& notes, Tick tick,
                                      uint8_t num_voices) {
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
    if (cand < 0 || cand > 127)
      continue;
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
      if (cand < upper && getPitchClass(static_cast<uint8_t>(cand)) == pitch_class) {
        pitches[vid] = static_cast<T>(cand);
        upper = cand;
        found = true;
        break;
      }
    }
    if (found)
      continue;

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
static std::vector<NoteEvent> buildCodaChordNotes(Tick start_tick, Tick duration, Key key,
                                                  uint8_t num_voices, bool is_minor,
                                                  const uint8_t* last_pitches,
                                                  const Subject* subject, Tick& stage1_dur) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  stage1_dur = bar_dur * 2;
  if (stage1_dur > duration)
    stage1_dur = duration;

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
        if (cand < v0_lo || cand + 7 > v0_hi)
          continue;  // Entire motif must fit
        int dist = std::abs(prev - cand);
        if (dist < best_dist) {
          best_dist = dist;
          base_tonic = cand;
        }
      }
      // Hard gate: max leap of 12 semitones (octave)
      int leap = std::abs(prev - base_tonic);
      if (leap > 12)
        base_tonic = tonic_pitch;
    }
    int head_pitches[] = {base_tonic, base_tonic + third, base_tonic + 7, base_tonic + third,
                          base_tonic, base_tonic + third, base_tonic + 7, base_tonic + third};
    if (subject != nullptr && subject->notes.size() >= 4) {
      const uint8_t subject_origin = subject->notes.front().pitch;
      const size_t subject_count = std::min<size_t>(subject->notes.size(), 8);
      for (size_t idx = 0; idx < subject_count; ++idx) {
        head_pitches[idx] =
            base_tonic + directedInterval(subject_origin, subject->notes[idx].pitch);
      }
      for (size_t idx = subject_count; idx < 8; ++idx) {
        size_t mirror_idx = subject_count - 1 - ((idx - subject_count) % subject_count);
        head_pitches[idx] = head_pitches[mirror_idx];
      }
    }
    int head_slots = static_cast<int>(stage1_dur / sub_dur);
    int head_count = std::min(8, head_slots);
    auto [head_lo, head_hi] = getFugueVoiceRange(0, num_voices);
    for (int idx = 0; idx < head_count; ++idx) {
      NoteEvent note;
      note.start_tick = start_tick + static_cast<Tick>(idx) * sub_dur;
      note.duration = sub_dur;
      note.pitch = clampPitch(head_pitches[idx % 8], head_lo, head_hi);
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

      if (voice_idx == num_voices - 1) {
        uint8_t ref = last_pitches && last_pitches[voice_idx] > 0
                          ? last_pitches[voice_idx]
                          : static_cast<uint8_t>(tonic_pitch);
        target_pitch = nearestPitchWithPC(ref, tonic_pc, 7);
        target_pitch = clampPitch(static_cast<int>(target_pitch), vlo, vhi);
      } else if (last_pitches && last_pitches[voice_idx] > 0) {
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
        uint8_t ref_pitch =
            (last_pitches && last_pitches[voice_idx] > 0) ? last_pitches[voice_idx] : current_pitch;
        int best_dist = 999;
        uint8_t best_pitch =
            clampPitch(static_cast<int>(current_pitch), vlo,
                       std::min(static_cast<int>(upper_bound), static_cast<int>(vhi)));
        for (int pitch_class : chord_pcs) {
          // Scan octaves within the voice range.
          int base = pitch_class;
          while (base < vlo)
            base += 12;
          for (int cur = base; cur <= vhi && cur <= static_cast<int>(upper_bound); cur += 12) {
            if (cur < vlo)
              continue;
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
            std::swap(notes[held_indices[idx]].pitch, notes[held_indices[jdx]].pitch);
          }
        }
      }

      // Override lowest voice to tonic pedal point (Bach coda convention).
      // Choose the octave of tonic closest to last_pitches to avoid large jumps.
      VoiceId lowest = num_voices - 1;
      auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest, num_voices);
      int bass_tonic = 36 + static_cast<int>(key);  // C2 default
      while (bass_tonic > static_cast<int>(ped_hi))
        bass_tonic -= 12;
      while (bass_tonic < static_cast<int>(ped_lo))
        bass_tonic += 12;
      if (last_pitches && last_pitches[lowest] > 0) {
        int prev = static_cast<int>(last_pitches[lowest]);
        int best = bass_tonic;
        int best_dist = std::abs(prev - bass_tonic);
        for (int oct : {-12, 12, -24, 24}) {
          int cand = bass_tonic + oct;
          if (cand < static_cast<int>(ped_lo) || cand > static_cast<int>(ped_hi))
            continue;
          int dist = std::abs(prev - cand);
          if (dist < best_dist) {
            best_dist = dist;
            best = cand;
          }
        }
        bass_tonic = best;
      }
      for (auto& note : notes) {
        if (note.voice == lowest && note.start_tick == start_tick && note.duration == stage1_dur) {
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
          if (notes[idx].voice == 0)
            continue;
          if (notes[idx].start_tick != start_tick)
            continue;

          int diff =
              std::abs(static_cast<int>(notes[idx].pitch) - static_cast<int>(voice0_first_pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (interval_util::isConsonance(simple) && diff >= 3)
            continue;

          // Dissonant or too close: try octave shifts.
          auto [vlo, vhi] = getFugueVoiceRange(notes[idx].voice, num_voices);
          int orig_pc = getPitchClass(notes[idx].pitch);
          uint8_t best_pitch = notes[idx].pitch;
          int best_cost = INT32_MAX;

          for (int shift = -36; shift <= 36; shift += 12) {
            if (shift == 0)
              continue;
            int cand = static_cast<int>(notes[idx].pitch) + shift;
            if (cand < vlo || cand > vhi)
              continue;
            if (getPitchClass(static_cast<uint8_t>(cand)) != orig_pc)
              continue;

            // Check consonance with voice 0.
            int cand_diff = std::abs(cand - static_cast<int>(voice0_first_pitch));
            int cand_simple = interval_util::compoundToSimple(cand_diff);
            if (!interval_util::isConsonance(cand_simple) || cand_diff < 3)
              continue;

            // Check no voice crossing with adjacent voices.
            bool crosses = false;
            for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
              if (jdx == idx || notes[jdx].start_tick != start_tick)
                continue;
              if (notes[jdx].voice == notes[idx].voice)
                continue;
              if (notes[jdx].voice < notes[idx].voice && cand > notes[jdx].pitch) {
                crosses = true;
                break;
              }
              if (notes[jdx].voice > notes[idx].voice && cand < notes[jdx].pitch) {
                crosses = true;
                break;
              }
            }
            if (crosses)
              continue;

            // Cost: voice-leading distance from original + imperfect consonance bonus.
            int cost = std::abs(shift);
            if (cand_simple == 3 || cand_simple == 4 || cand_simple == 8 || cand_simple == 9) {
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
              if (ct_pc == orig_pc)
                continue;  // Same degree, already tried octave shifts.
              // Find nearest pitch with this chord PC.
              for (int oct = -24; oct <= 24; oct += 12) {
                int base = static_cast<int>(notes[idx].pitch) + oct;
                int target = base - (base % 12) + ct_pc;
                if (target < base - 6)
                  target += 12;
                if (target > base + 6)
                  target -= 12;
                if (target < vlo || target > vhi)
                  continue;

                // Check consonance with voice 0.
                int cand_diff = std::abs(target - static_cast<int>(voice0_first_pitch));
                int cand_simple = interval_util::compoundToSimple(cand_diff);
                if (!interval_util::isConsonance(cand_simple) || cand_diff < 3)
                  continue;

                // Check no crossing.
                bool crosses = false;
                for (size_t jdx = 0; jdx < notes.size(); ++jdx) {
                  if (jdx == idx || notes[jdx].start_tick != start_tick)
                    continue;
                  if (notes[jdx].voice == notes[idx].voice)
                    continue;
                  if (notes[jdx].voice < notes[idx].voice && target > notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                  if (notes[jdx].voice > notes[idx].voice && target < notes[jdx].pitch) {
                    crosses = true;
                    break;
                  }
                }
                if (crosses)
                  continue;

                // Voice-leading cost from original position.
                int cost = std::abs(target - static_cast<int>(notes[idx].pitch));
                if (cand_simple == 3 || cand_simple == 4 || cand_simple == 8 || cand_simple == 9) {
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
      if (notes[idx].voice == 0 || notes[idx].start_tick != start_tick)
        continue;
      for (size_t jdx = idx + 1; jdx < notes.size(); ++jdx) {
        if (notes[jdx].voice == 0 || notes[jdx].start_tick != start_tick)
          continue;
        int diff =
            std::abs(static_cast<int>(notes[idx].pitch) - static_cast<int>(notes[jdx].pitch));
        if (diff >= 3)
          continue;  // Sufficient spacing.
        // Too close (unison or minor 2nd): shift the lower-priority voice.
        size_t fix_idx = (notes[idx].voice > notes[jdx].voice) ? idx : jdx;
        int fix_shift = (notes[fix_idx].pitch < notes[fix_idx == idx ? jdx : idx].pitch) ? -12 : 12;
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
                [&notes](size_t lhs, size_t rhs) { return notes[lhs].voice < notes[rhs].voice; });
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
                if (best < 0 || cur > best)
                  best = cur;
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

/// @brief Replace one held inner coda voice with eighth-note chordal figuration.
///
/// BWV578's final measures keep the inner voices active with short chord-tone
/// motion.  This conservative version only touches the lower manual inner voice,
/// keeps the same source tag, and uses tonic chord tones within the original
/// voice lane so the cadence search still sees a stable Stage 1 endpoint.
static void addCodaStage1InnerFiguration(std::vector<NoteEvent>& notes, Tick start_tick,
                                         Tick stage1_dur, Key key, uint8_t num_voices,
                                         bool is_minor, VoiceId fig_voice) {
  if (num_voices < 3 || fig_voice == 0 || fig_voice >= num_voices || stage1_dur < kTicksPerBar) {
    return;
  }
  if (fig_voice == 0)
    return;

  size_t held_idx = notes.size();
  uint8_t held_pitch = 0;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    if (notes[idx].voice == fig_voice && notes[idx].start_tick == start_tick &&
        notes[idx].duration == stage1_dur) {
      held_idx = idx;
      held_pitch = notes[idx].pitch;
      break;
    }
  }
  if (held_idx == notes.size() || held_pitch == 0)
    return;

  uint8_t upper_bound = 127;
  uint8_t lower_bound = 0;
  Tick stage1_end = start_tick + stage1_dur;
  for (const auto& note : notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    if (note.start_tick < start_tick || note.start_tick >= stage1_end)
      continue;
    if (note.voice < fig_voice) {
      upper_bound = std::min(upper_bound, note.pitch);
    } else if (note.voice > fig_voice) {
      lower_bound = std::max(lower_bound, note.pitch);
    }
  }

  auto [vlo, vhi] = getFugueVoiceRange(fig_voice, num_voices);
  int lane_lo = std::max(static_cast<int>(vlo), static_cast<int>(lower_bound) + 3);
  int lane_hi = std::min(static_cast<int>(vhi), static_cast<int>(upper_bound) - 3);
  if (lane_lo > lane_hi)
    return;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  int tonic_pc = getPitchClass(static_cast<uint8_t>(tonic_pitch));
  int third_pc = (tonic_pitch + (is_minor ? 3 : 4)) % 12;
  int fifth_pc = (tonic_pitch + 7) % 12;
  int chord_pcs[] = {tonic_pc, third_pc, fifth_pc};

  int best_alt = -1;
  int best_cost = INT32_MAX;
  for (int pc : chord_pcs) {
    for (int pitch = lane_lo; pitch <= lane_hi; ++pitch) {
      if (pitch == static_cast<int>(held_pitch))
        continue;
      if (getPitchClass(static_cast<uint8_t>(pitch)) != pc)
        continue;
      int dist = std::abs(pitch - static_cast<int>(held_pitch));
      if (dist < 3 || dist > 4)
        continue;
      int cost = dist;
      if (pitch > static_cast<int>(held_pitch))
        cost += 1;
      if (cost < best_cost) {
        best_cost = cost;
        best_alt = pitch;
      }
    }
  }
  if (best_alt < 0 && num_voices == 4 && fig_voice == num_voices - 2) {
    int stable_pitch = -1;
    int stable_cost = INT32_MAX;
    for (int pc : chord_pcs) {
      for (int pitch = lane_lo; pitch <= lane_hi; ++pitch) {
        if (getPitchClass(static_cast<uint8_t>(pitch)) != pc)
          continue;
        int cost = std::abs(pitch - static_cast<int>(held_pitch));
        if (cost < stable_cost) {
          stable_cost = cost;
          stable_pitch = pitch;
        }
      }
    }
    if (stable_pitch >= 0) {
      held_pitch = static_cast<uint8_t>(stable_pitch);
      best_alt = held_pitch;
    }
  }
  if (best_alt < 0)
    return;

  NoteEvent held = notes[held_idx];
  notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(held_idx));

  Tick slot = duration::kEighthNote;
  int slots = static_cast<int>(stage1_dur / slot);
  for (int idx = 0; idx < slots; ++idx) {
    NoteEvent fig = held;
    fig.start_tick = start_tick + static_cast<Tick>(idx) * slot;
    fig.duration = slot;
    fig.pitch = (idx % 2 == 1 && idx != slots - 1) ? static_cast<uint8_t>(best_alt) : held_pitch;
    notes.push_back(fig);
  }
}

static int pulseCodaManualCadenceNotes(std::vector<NoteEvent>& notes, Tick coda_start,
                                       Tick stage1_dur, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  int inserted = 0;
  const Tick cadence_start = coda_start + stage1_dur;
  const size_t original_size = notes.size();
  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = notes[idx];
    if (note.source != BachNoteSource::Coda)
      continue;
    if (note.voice != 0 && note.voice != 2)
      continue;
    if (note.start_tick < cadence_start)
      continue;
    if (note.duration != duration::kHalfNote)
      continue;

    Tick original_end = note.start_tick + note.duration;
    Tick slot = duration::kQuarterNote;
    if (note.duration <= slot)
      continue;
    note.duration = slot;
    for (Tick tick = note.start_tick + slot; tick + slot <= original_end; tick += slot) {
      NoteEvent pulse = note;
      pulse.start_tick = tick;
      pulse.duration = slot;
      notes.push_back(pulse);
      ++inserted;
    }
  }

  return inserted;
}

static int splitCodaManualIIStage1Holding(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick coda_start = std::numeric_limits<Tick>::max();
  for (const auto& note : all_notes) {
    if (note.source == BachNoteSource::Coda) {
      coda_start = std::min(coda_start, note.start_tick);
    }
  }
  if (coda_start == std::numeric_limits<Tick>::max())
    return 0;

  constexpr VoiceId manual_ii = 1;
  size_t held_idx = all_notes.size();
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda || note.voice != manual_ii)
      continue;
    if (note.start_tick != coda_start)
      continue;
    if (note.duration < kTicksPerBar * 2)
      continue;
    held_idx = idx;
    break;
  }
  if (held_idx == all_notes.size())
    return 0;

  NoteEvent held = all_notes[held_idx];
  all_notes.erase(all_notes.begin() + static_cast<std::ptrdiff_t>(held_idx));

  auto [lo, hi] = getFugueVoiceRange(manual_ii, num_voices);
  const int pattern[] = {
      static_cast<int>(held.pitch),
      static_cast<int>(held.pitch) + 2,
      static_cast<int>(held.pitch),
      static_cast<int>(held.pitch) - 1,
  };
  constexpr int kPatternCount = static_cast<int>(sizeof(pattern) / sizeof(pattern[0]));

  int inserted = 0;
  for (Tick tick = held.start_tick; tick < held.start_tick + held.duration;
       tick += duration::kEighthNote) {
    NoteEvent fig = held;
    fig.start_tick = tick;
    fig.duration = std::min(duration::kEighthNote, held.start_tick + held.duration - tick);
    int pitch = pattern[inserted % kPatternCount];
    fig.pitch = clampPitch(pitch, lo, hi);
    fig.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    all_notes.push_back(fig);
    ++inserted;
  }

  return inserted;
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
      int base =
          (prev_pitch > 0) ? static_cast<int>(prev_pitch) + oct : static_cast<int>(range_lo) + oct;
      // Find nearest pitch with target PC.
      int target = base - (base % 12) + pitch_class;
      if (target < base - 6)
        target += 12;
      if (target > base + 6)
        target -= 12;
      if (target >= static_cast<int>(range_lo) && target <= static_cast<int>(range_hi)) {
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
  if (cands.size() <= max_n)
    return;
  std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
    return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
           std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
  });
  cands.resize(max_n);
}

static void keepCadentialBassFunction(std::vector<uint8_t>& cands, uint8_t ref, int required_pc,
                                      uint8_t range_lo, uint8_t range_hi, size_t max_n) {
  int best = -1;
  int best_dist = INT32_MAX;
  int ref_pitch = ref > 0 ? static_cast<int>(ref)
                          : (static_cast<int>(range_lo) + static_cast<int>(range_hi)) / 2;
  for (int pitch = static_cast<int>(range_lo); pitch <= static_cast<int>(range_hi); ++pitch) {
    if (getPitchClass(static_cast<uint8_t>(pitch)) != required_pc)
      continue;
    int dist = std::abs(pitch - ref_pitch);
    if (dist < best_dist) {
      best = pitch;
      best_dist = dist;
    }
  }
  if (best >= 0 &&
      std::find(cands.begin(), cands.end(), static_cast<uint8_t>(best)) == cands.end()) {
    cands.push_back(static_cast<uint8_t>(best));
  }

  std::sort(cands.begin(), cands.end(), [required_pc, ref_pitch](uint8_t lhs, uint8_t rhs) {
    bool lhs_required = getPitchClass(lhs) == required_pc;
    bool rhs_required = getPitchClass(rhs) == required_pc;
    if (lhs_required != rhs_required)
      return lhs_required;
    return std::abs(static_cast<int>(lhs) - ref_pitch) <
           std::abs(static_cast<int>(rhs) - ref_pitch);
  });
  if (cands.size() > max_n)
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
                                                Tick start_tick, Tick duration, Key key,
                                                uint8_t num_voices, bool is_minor,
                                                Tick stage1_dur) {
  std::vector<NoteEvent> notes;

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(key);
  Tick bar_dur = kTicksPerBar;

  if (duration <= stage1_dur)
    return notes;

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
  int dom_third = (tonic_pitch + 11) % 12;     // B (leading tone)
  int dom_fifth = (tonic_pitch + 14) % 12;     // D
  int dom_seventh = (tonic_pitch + 17) % 12;   // F (7th)
  int tonic_root = tonic_pitch % 12;           // C
  int picardy_third = (tonic_pitch + 4) % 12;  // E (always major for Picardy)
  int res_third_pc = is_minor ? picardy_third : ((tonic_pitch + 4) % 12);
  int tonic_fifth = (tonic_pitch + 7) % 12;  // G

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
  keepCadentialBassFunction(bass_v7_cands, stage1_end[bass], dom_root, bass_lo, bass_hi, 3);

  // Score a 3-chord sequence for all voices.
  auto scoreSolution = [&](const CodaSolution& sol) -> int {
    int cost = 0;

    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      // Voice-leading distance: stage1->V7, V7->I, I->final.
      int d1 = std::abs(static_cast<int>(sol.v7[vid]) - static_cast<int>(stage1_end[vid]));
      int d2 = std::abs(static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]));
      int d3 =
          std::abs(static_cast<int>(sol.final_chord[vid]) - static_cast<int>(sol.i_chord[vid]));
      cost += 10 * (d1 + d2 + d3);

      // Excessive leap penalty (>12st).
      if (d1 > 12)
        cost += 200 * (d1 - 12);
      if (d2 > 12)
        cost += 200 * (d2 - 12);
      if (d3 > 12)
        cost += 200 * (d3 - 12);
    }

    // Leading-tone resolution: any voice with V7 leading tone should resolve up by semitone.
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      if (getPitchClass(sol.v7[vid]) == dom_third) {
        int resolution = static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]);
        if (resolution != 1)
          cost += 50;  // Leading tone must resolve up by semitone.
      }
    }

    // Seventh resolution: any voice with V7 seventh should resolve down by step.
    for (uint8_t vid = 0; vid < voice_count; ++vid) {
      if (getPitchClass(sol.v7[vid]) == dom_seventh) {
        int resolution = static_cast<int>(sol.i_chord[vid]) - static_cast<int>(sol.v7[vid]);
        if (resolution != -1 && resolution != -2)
          cost += 50;
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
      const uint8_t* pitches = (chord_idx == 0)   ? sol.v7
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
          int interval_a = std::abs(static_cast<int>(chord_a[va]) - static_cast<int>(chord_a[vb]));
          int interval_b = std::abs(static_cast<int>(chord_b[va]) - static_cast<int>(chord_b[vb]));
          int simple_a = interval_util::compoundToSimple(interval_a);
          int simple_b = interval_util::compoundToSimple(interval_b);
          // Parallel perfect unisons, 5ths, or octaves.
          if ((simple_a == 0 || simple_a == 7) && (simple_b == 0 || simple_b == 7) &&
              simple_a == simple_b) {
            // Check both voices move in same direction.
            int motion_a = static_cast<int>(chord_b[va]) - static_cast<int>(chord_a[va]);
            int motion_b = static_cast<int>(chord_b[vb]) - static_cast<int>(chord_a[vb]);
            if (motion_a != 0 && motion_b != 0 && ((motion_a > 0) == (motion_b > 0))) {
              return INT32_MAX;  // Parallel 5/8: reject.
            }
          }
        }
      }
      return pen;
    };

    int par1 = checkParallels(sol.v7, sol.i_chord);
    if (par1 == INT32_MAX)
      return INT32_MAX;
    cost += par1;
    int par2 = checkParallels(sol.i_chord, sol.final_chord);
    if (par2 == INT32_MAX)
      return INT32_MAX;
    cost += par2;

    // Hidden 5/8 on outer voices (penalty, not rejection).
    auto checkHidden58 = [&](const uint8_t* chord_a, const uint8_t* chord_b) -> int {
      int sop_m = static_cast<int>(chord_b[sop]) - static_cast<int>(chord_a[sop]);
      int bass_m = static_cast<int>(chord_b[bass]) - static_cast<int>(chord_a[bass]);
      if (sop_m != 0 && bass_m != 0 && ((sop_m > 0) == (bass_m > 0))) {
        int interval = std::abs(static_cast<int>(chord_b[sop]) - static_cast<int>(chord_b[bass]));
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
          int interval_v7 = std::abs(static_cast<int>(sol.v7[vid]) - static_cast<int>(sol.v7[vb]));
          int simple = interval_util::compoundToSimple(interval_v7);
          if (simple == 5) {  // Perfect 4th
            cost -= 20;       // 4-3 suspension bonus.
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
      keepCadentialBassFunction(bass_i_cands, bass_v7, tonic_root, bass_lo, bass_hi, 2);

      for (auto sop_i : sop_i_cands) {
        for (auto bass_i : bass_i_cands) {
          // Quick crossing check on outer voices.
          if (sop_v7 < bass_v7 || sop_i < bass_i)
            continue;

          // Final chord candidates for outer voices (limited to top 2).
          auto sop_final_cands = generateCadenceCandidates(sop_i, final_pcs, 3, sop_lo, sop_hi);
          limitCadenceCandidates(sop_final_cands, sop_i, 2);
          auto bass_final_cands = generateCadenceCandidates(bass_i, final_pcs, 3, bass_lo, bass_hi);
          keepCadentialBassFunction(bass_final_cands, bass_i, tonic_root, bass_lo, bass_hi, 2);

          for (auto sop_final : sop_final_cands) {
            for (auto bass_final : bass_final_cands) {
              if (sop_final < bass_final)
                continue;

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
                if (sol.cost < best.cost)
                  best = sol;
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
                inner[inner_idx].final_chord = generateCadenceCandidates(0, final_pcs, 3, vlo, vhi);

                // Limit candidates to keep search space manageable.
                // Sort by distance from previous pitch and keep top 5.
                auto limitByDist = [](std::vector<uint8_t>& cands, uint8_t ref, int max_n) {
                  if (ref == 0 || static_cast<int>(cands.size()) <= max_n)
                    return;
                  std::sort(cands.begin(), cands.end(), [ref](uint8_t lhs, uint8_t rhs) {
                    return std::abs(static_cast<int>(lhs) - static_cast<int>(ref)) <
                           std::abs(static_cast<int>(rhs) - static_cast<int>(ref));
                  });
                  cands.resize(static_cast<size_t>(max_n));
                };
                limitByDist(inner[inner_idx].v7, stage1_end[voice_idx], 2);
                uint8_t v7_ref =
                    inner[inner_idx].v7.empty() ? stage1_end[voice_idx] : inner[inner_idx].v7[0];
                limitByDist(inner[inner_idx].i_chord, v7_ref, 2);
                uint8_t i_ref =
                    inner[inner_idx].i_chord.empty() ? v7_ref : inner[inner_idx].i_chord[0];
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
                  if (partial.cost < best.cost)
                    best = partial;
                  return;
                }
                uint8_t voice_idx = 1 + depth;
                for (auto pv7 : inner[depth].v7) {
                  // Early pruning: skip if voice-leading distance alone
                  // already exceeds best known cost.
                  int d1 =
                      std::abs(static_cast<int>(pv7) - static_cast<int>(stage1_end[voice_idx]));
                  if (10 * d1 >= best.cost)
                    continue;
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
      i_pitches[vid] = static_cast<int>(clampPitch(tonic_pitch + tonic_offsets_fb[vid], vlo, vhi));
      final_pitches[vid] =
          static_cast<int>(clampPitch(tonic_pitch + final_offsets_fb[vid], vlo, vhi));
    }

    // Harsh dissonance guard: check each V7 pitch against the tonic bass
    // (pedal from Stage 1). If m2(1)/TT(6)/M7(11) is formed, try octave
    // shifts to find a safe register that avoids the clash.
    int tonic_bass = static_cast<int>(clampPitch(tonic_pitch, bass_lo, bass_hi));
    for (uint8_t vid = 0; vid < count; ++vid) {
      int simple = interval_util::compoundToSimple(absoluteInterval(
          static_cast<uint8_t>(v7_pitches[vid]), static_cast<uint8_t>(tonic_bass)));
      if (simple == 1 || simple == 6 || simple == 11) {
        auto [range_lo, range_hi] = getFugueVoiceRange(vid, num_voices);
        for (int shift : {12, -12, 24, -24}) {
          int alt = v7_pitches[vid] + shift;
          if (alt < range_lo || alt > range_hi)
            continue;
          int alt_simple = interval_util::compoundToSimple(
              absoluteInterval(static_cast<uint8_t>(alt), static_cast<uint8_t>(tonic_bass)));
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
static void verifyCodaCrossings(const std::vector<NoteEvent>& notes, Tick start_tick, Tick duration,
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
        std::fprintf(stderr, "[createCodaNotes] WARNING: %s voice crossing v%u(%u) < v%u(%u)\n",
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
std::vector<NoteEvent> createCodaNotes(Tick start_tick, Tick duration, Key key, uint8_t num_voices,
                                       bool is_minor = false, const uint8_t* last_pitches = nullptr,
                                       const Subject* subject = nullptr) {
  // Stage 1: Head motif + held chord tones with voice-crossing fixes.
  Tick stage1_dur = 0;
  auto chord_notes = buildCodaChordNotes(start_tick, duration, key, num_voices, is_minor,
                                         last_pitches, subject, stage1_dur);
  if (num_voices >= 4) {
    addCodaStage1InnerFiguration(chord_notes, start_tick, stage1_dur, key, num_voices, is_minor, 1);
  }
  addCodaStage1InnerFiguration(chord_notes, start_tick, stage1_dur, key, num_voices, is_minor,
                               static_cast<VoiceId>(num_voices - 2));

  // Stage 2-3: V7->I cadence search + final tonic chord.
  auto cadence_notes =
      searchCodaCadence(chord_notes, start_tick, duration, key, num_voices, is_minor, stage1_dur);

  // Merge Stage 1 and Stage 2-3 notes.
  std::vector<NoteEvent> notes;
  notes.reserve(chord_notes.size() + cadence_notes.size());
  notes.insert(notes.end(), chord_notes.begin(), chord_notes.end());
  notes.insert(notes.end(), cadence_notes.begin(), cadence_notes.end());
  pulseCodaManualCadenceNotes(notes, start_tick, stage1_dur, num_voices);

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
                                const TextureDensityTarget& density_target, std::mt19937& rng,
                                bool post_entry = false) {
  if (num_voices <= 2)
    return num_voices;  // Can't reduce below 2.
  // Strict four-voice organ fugues are already density-managed inside the
  // Fortspinnung generator.  Further rest-voice augmentation weakens the
  // BWV578-calibrated harmony/texture score without improving note density.
  if (num_voices == 4)
    return num_voices;

  // In develop phase (0.25-0.70): mostly N-1 voices.
  float target = density_target.develop_density;
  if (phase_pos >= 0.70f) {
    target = density_target.stretto_density;
  }

  uint8_t target_voices = static_cast<uint8_t>(std::round(static_cast<float>(num_voices) * target));
  if (target_voices < 2)
    target_voices = 2;
  if (target_voices > num_voices)
    target_voices = num_voices;

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
uint8_t selectRestingVoice(uint8_t num_voices, int episode_idx, int entry_voice = -1,
                           uint8_t prev_rest = 255) {
  if (num_voices <= 2)
    return num_voices;  // No rest possible.

  // Build list of eligible inner voices (exclude voice 0 and voice num_voices-1).
  // For 3-voice fugues, only voice 1 is inner.
  // For 4-voice fugues, voices 1 and 2 are inner.
  // For 5-voice fugues, voices 1, 2, and 3 are inner.
  uint8_t inner_count = (num_voices >= 3) ? (num_voices - 2) : 0;
  if (inner_count == 0)
    return num_voices;  // No inner voices available.

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
void applyFortPhaseRestProcessing(std::vector<NoteEvent>& notes, uint8_t rest_voice,
                                  Tick episode_start, Tick episode_duration,
                                  const FortspinnungGrammar& grammar) {
  Tick kernel_end = episode_start +
                    static_cast<Tick>(static_cast<float>(episode_duration) * grammar.kernel_ratio);
  Tick sequence_end =
      kernel_end + static_cast<Tick>(static_cast<float>(episode_duration) * grammar.sequence_ratio);

  for (auto& note : notes) {
    if (note.voice != rest_voice)
      continue;
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

  mat.constraint_profile = analyzeObligations(mat.subject.notes, config.key, config.is_minor);
  mat.answer = generateAnswer(mat.subject, config.answer_type, policy.preferred_answer);
  mat.countersubject = generateCountersubject(mat.subject, config.seed + 1000, 5, config.archetype);
  if (num_voices >= 4) {
    mat.countersubject_2 = generateSecondCountersubject(mat.subject, mat.countersubject,
                                                        config.seed + 5000, 5, config.archetype);
  }
  mat.motif_pool.build(mat.subject.notes, mat.countersubject.notes, mat.subject.character);
  mat.thematic_plan = buildThematicPlan(mat.subject, mat.motif_pool, num_voices, config.key);

  // --- Feasibility gating: run harness checks to find a better subject. ---
  // Each attempt generates a subject, applies optimal octave placement,
  // then validates with micro-sim and pair verification.
  for (int feasibility_attempt = 0; feasibility_attempt < kMaxFeasibilityRetries;
       ++feasibility_attempt) {
    // Vary the base seed for each feasibility retry to explore different subjects.
    // Use a large prime multiplier to avoid seed overlap with generateValidSubject's
    // internal retry seeds (which use 1000003u increments).
    FugueConfig attempt_config = config;
    if (feasibility_attempt > 0) {
      attempt_config.seed = config.seed + static_cast<uint32_t>(feasibility_attempt) * 7919u;
    }

    // Step 1: Generate subject with SubjectValidator retry logic.
    FugueMaterial candidate;
    candidate.subject = generateValidSubject(attempt_config, candidate.subject_attempts);
    if (candidate.subject.notes.empty()) {
      continue;
    }

    // Step 2: Analyze subject obligations for constraint-driven generation.
    candidate.constraint_profile =
        analyzeObligations(candidate.subject.notes, attempt_config.key, attempt_config.is_minor);

    // Step 3: Find optimal octave placement via voice assignment search.
    VoiceAssignment assignment =
        findBestAssignment(candidate.subject, candidate.constraint_profile, attempt_config);

    if (assignment.start_octave_offset != 0) {
      int semitone_shift = static_cast<int>(assignment.start_octave_offset) * 12;
      for (auto& note : candidate.subject.notes) {
        int shifted = static_cast<int>(note.pitch) + semitone_shift;
        note.pitch = static_cast<uint8_t>(std::clamp(shifted, 0, 127));
      }
      // Re-analyze obligations after pitch shift (contour/register may change).
      candidate.constraint_profile =
          analyzeObligations(candidate.subject.notes, attempt_config.key, attempt_config.is_minor);
    }

    // Step 4: Run micro-exposition simulation for counterpoint feasibility.
    MicroSimResult sim_result =
        runMicroSim(candidate.subject, attempt_config, kPipelineMicroSimTrials);

    // Step 5: Generate answer and verify subject x answer pair compatibility.
    candidate.answer =
        generateAnswer(candidate.subject, attempt_config.answer_type, policy.preferred_answer);

    SubjectConstraintProfile answer_profile =
        analyzeObligations(candidate.answer.notes, candidate.answer.key, attempt_config.is_minor);

    PairVerificationResult pair_result =
        verifyPair(candidate.constraint_profile, answer_profile,
                   static_cast<int>(candidate.subject.length_ticks));

    if (sim_result.feasible() && pair_result.feasible()) {
      // Complete the candidate material before returning.
      candidate.countersubject = generateCountersubject(
          candidate.subject, attempt_config.seed + 1000, 5, attempt_config.archetype);
      if (num_voices >= 4) {
        candidate.countersubject_2 =
            generateSecondCountersubject(candidate.subject, candidate.countersubject,
                                         attempt_config.seed + 5000, 5, attempt_config.archetype);
      }
      candidate.motif_pool.build(candidate.subject.notes, candidate.countersubject.notes,
                                 candidate.subject.character);
      candidate.thematic_plan = buildThematicPlan(candidate.subject, candidate.motif_pool,
                                                  num_voices, attempt_config.key);
      return candidate;
    }

    // Log the failure reason for diagnostics.
    if (!sim_result.feasible()) {
      fprintf(stderr,
              "buildMaterial: feasibility attempt %d/%d failed micro-sim "
              "(success_rate=%.2f, critical=%d, bottleneck=%d, overlap=%.2f)\n",
              feasibility_attempt + 1, kMaxFeasibilityRetries, sim_result.success_rate(),
              sim_result.num_critical_violations, sim_result.num_bottleneck,
              sim_result.avg_register_overlap);
    }
    if (!pair_result.feasible()) {
      fprintf(stderr,
              "buildMaterial: feasibility attempt %d/%d failed pair verification "
              "(conflicts=%zu, cadence_conflict=%.2f)\n",
              feasibility_attempt + 1, kMaxFeasibilityRetries, pair_result.conflicts.size(),
              pair_result.cadence_conflict_score);
    }
  }

  // Exhausted all retries: fall back to baseline material (original seed,
  // no octave shift) to preserve backward-compatible output.
  fprintf(stderr, "buildMaterial: feasibility retries exhausted, using baseline material\n");
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
  plan.estimated_duration =
      expo_ticks + develop_ticks + return_ep_ticks + pedal_ticks + stretto_ticks + coda_ticks;
  Tick min_duration = kTicksPerBar * kMinFugueBars;
  if (plan.estimated_duration < min_duration) {
    plan.estimated_duration = min_duration;
  }

  // Create modulation plan.
  if (config.has_modulation_plan) {
    plan.modulation_plan = config.modulation_plan;
  } else {
    plan.modulation_plan = config.is_minor ? ModulationPlan::createForMinor(config.key)
                                           : ModulationPlan::createForMajor(config.key);
  }

  // Generate structure-aligned tonal plan.
  plan.tonal_plan = generateStructureAlignedTonalPlan(
      config, plan.modulation_plan, material.subject.length_ticks, plan.estimated_duration);

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
    stretto_section.energy_level =
        FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
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

  // The coda is not another sequential episode.  Keep the generation timeline
  // on the home cadence so held coda voices are composed against the cadence
  // instead of being trimmed later against a generic circle progression.
  {
    Tick coda_start = current_tick;
    Tick dominant_start = coda_start + kTicksPerBar * 2;
    Tick tonic_resolution = dominant_start + kTicksPerBar / 2;
    uint8_t tonic_bass = tonicBassPitchForVoices(config.key, num_voices);
    auto [bass_lo, bass_hi] = getFugueVoiceRange(num_voices - 1, num_voices);
    int dominant_bass = static_cast<int>(tonic_bass) + interval::kPerfect5th;
    while (dominant_bass > static_cast<int>(bass_hi))
      dominant_bass -= 12;
    while (dominant_bass < static_cast<int>(bass_lo))
      dominant_bass += 12;

    auto make_home_chord = [&](ChordDegree degree, ChordQuality quality) {
      Chord chord;
      chord.degree = degree;
      chord.quality = quality;
      uint8_t semitone = config.is_minor ? degreeMinorSemitones(degree) : degreeSemitones(degree);
      int root_midi = (4 + 1) * 12 + static_cast<int>(config.key) + semitone;
      chord.root_pitch = clampPitch(root_midi, 0, 127);
      chord.inversion = 0;
      return chord;
    };

    for (auto& event : plan.detailed_timeline.mutableEvents()) {
      if (event.tick < coda_start || event.tick >= coda_start + coda_ticks) {
        continue;
      }
      event.key = config.key;
      event.is_minor = config.is_minor;
      event.has_modulation = false;
      event.modulation_target = config.key;
      if (event.tick >= dominant_start && event.tick < tonic_resolution) {
        event.chord = make_home_chord(ChordDegree::V, ChordQuality::Dominant7);
        event.bass_pitch = clampPitch(dominant_bass, bass_lo, bass_hi);
        event.weight = 0.95f;
      } else {
        event.chord = make_home_chord(ChordDegree::I,
                                      config.is_minor ? ChordQuality::Minor : ChordQuality::Major);
        event.bass_pitch = tonic_bass;
        event.weight = event.tick >= tonic_resolution ? 1.0f : 0.85f;
      }
    }
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
std::vector<NoteEvent> generateSections(const FugueConfig& config, const FugueMaterial& material,
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
    case SubjectCharacter::Restless:
      false_entry_prob = 0.40f;
      break;
    case SubjectCharacter::Playful:
      false_entry_prob = 0.30f;
      break;
    case SubjectCharacter::Noble:
      false_entry_prob = 0.15f;
      break;
    case SubjectCharacter::Severe:
      false_entry_prob = 0.10f;
      break;
  }
  false_entry_prob =
      std::clamp(false_entry_prob + rng::rollFloat(false_entry_rng, -0.05f, 0.05f), 0.0f, 1.0f);

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
            material.subject, material.answer, material.countersubject, config, config.seed,
            cp_state, cp_rules, cp_resolver, plan.detailed_timeline, plan.estimated_duration,
            material.countersubject_2.notes.empty() ? nullptr : &material.countersubject_2);
        structure.addSection(SectionType::Exposition, FuguePhase::Establish, 0, expo.total_ticks,
                             config.key);
        auto expo_notes = expo.allNotes();
        all_notes.insert(all_notes.end(), expo_notes.begin(), expo_notes.end());
        record_notes(expo_notes, config.key);
        current_tick = expo.total_ticks;
        break;
      }

      case SectionType::Episode: {
        Tick episode_duration = section.end_tick - section.start_tick;
        uint32_t pair_seed_base =
            config.seed + static_cast<uint32_t>(section.episode_index) * 2000u + 2000u;

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
          auto feasibility =
              estimator.estimateWithCascade(cp_state, cp_rules, cp_resolver,
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
          ep_last_pitches[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
        }

        ConstraintState episode_exit;
        Episode episode = generateFortspinnungEpisode(
            material.subject, material.motif_pool, current_tick, episode_duration, section.prev_key,
            section.key, num_voices, pair_seed_base, section.episode_index, section.energy_level,
            cp_state, cp_rules, cp_resolver, plan.detailed_timeline, 0, &pipeline_cs.accumulator,
            prev_episode_exit ? &*prev_episode_exit : nullptr, &episode_exit, ep_last_pitches,
            &material.thematic_plan);
        // Chain exit state: update pipeline accumulator and store for next episode.
        pipeline_cs.accumulator = episode_exit.accumulator;
        prev_episode_exit = std::move(episode_exit);

        // Texture density management: FortPhase-dependent rest voice processing.
        // D3: Detect post-entry context for musical-trigger tutti.
        bool post_entry =
            (sec_idx > 0 && plan.sections[sec_idx - 1].type == SectionType::MiddleEntry);
        float ep_phase_pos =
            static_cast<float>(current_tick) / static_cast<float>(plan.estimated_duration);
        uint8_t ep_active = selectEpisodeVoiceCount(num_voices, ep_phase_pos, config.density_target,
                                                    density_rng, post_entry);
        if (ep_active < num_voices) {
          uint8_t rest_voice =
              selectRestingVoice(num_voices, section.episode_index, -1, prev_rest_voice);
          // FortPhase-dependent processing: Kernel=keep, Sequence=augment+thin,
          // Dissolution=erase. Replaces full erasure for more natural texture.
          applyFortPhaseRestProcessing(episode.notes, rest_voice, current_tick, episode_duration,
                                       fort_grammar);
          prev_rest_voice = rest_voice;
        }

        (void)shapeEpisodeEntryContinuity(episode.notes, all_notes, current_tick,
                                          plan.detailed_timeline, config, num_voices);

        structure.addSection(SectionType::Episode, FuguePhase::Develop, current_tick,
                             current_tick + episode_duration, section.key);
        all_notes.insert(all_notes.end(), episode.notes.begin(), episode.notes.end());
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
          if (plan.sections[prev_sec].type == SectionType::MiddleEntry)
            ++me_count;
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
        bool use_false_entry = (pair_idx > 0) && (false_dist(false_entry_rng) < false_entry_prob);

        uint8_t entry_last = extractVoiceLastPitch(all_notes, current_tick, entry_voice);
        float me_phase_pos =
            static_cast<float>(current_tick) / static_cast<float>(plan.estimated_duration);

        MiddleEntry middle_entry =
            use_false_entry
                ? generateFalseEntry(material.subject, target_key, current_tick, entry_voice,
                                     num_voices, 3, me_phase_pos)
                : generateMiddleEntry(material.subject, target_key, current_tick, entry_voice,
                                      num_voices, cp_state, cp_rules, cp_resolver,
                                      plan.detailed_timeline, entry_last, me_phase_pos);
        if (use_false_entry && plan.detailed_timeline.size() > 0) {
          for (auto& note : middle_entry.notes) {
            const HarmonicEvent& event = plan.detailed_timeline.getAt(note.start_tick);
            if (isDiatonicInKey(note.pitch, event.key, event.is_minor)) {
              continue;
            }
            ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
            note.pitch = scale_util::nearestScaleTone(note.pitch, event.key, scale);
          }
        }

        Tick middle_end = middle_entry.end_tick;
        if (middle_end <= current_tick) {
          middle_end = current_tick + material.subject.length_ticks;
          if (middle_end <= current_tick) {
            middle_end = current_tick + kTicksPerBar * 2;
          }
        }

        structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop, current_tick,
                             middle_end, target_key);
        all_notes.insert(all_notes.end(), middle_entry.notes.begin(), middle_entry.notes.end());
        record_notes(middle_entry.notes, target_key);

        // C2: Countersubject placement in middle entries.
        // In Bach practice, the countersubject accompanies each subject entry
        // after the exposition. Keep it present to avoid thin middle entries.
        if (!material.countersubject.notes.empty() && !use_false_entry) {
          // CS voice: adjacent to entry, prefer upper voice, avoid bass.
          uint8_t cs_voice;
          if (entry_voice == 0) {
            cs_voice = 1;
          } else {
            cs_voice = entry_voice - 1;
          }
          // Don't collide with bass voice for 4+ voices.
          if (cs_voice >= num_voices - 1 && num_voices >= 4) {
            cs_voice = static_cast<uint8_t>((entry_voice + 1) % (num_voices - 1));
          }

          auto cs_notes = adaptCSToKey(material.countersubject.notes, target_key);
          auto [cs_lo, cs_hi] = getFugueVoiceRange(cs_voice, num_voices);
          int cs_shift = fitToRegister(cs_notes, cs_lo, cs_hi);

          // Independence check: CS start pitch != subject start (mod 12).
          bool independent = true;
          if (!cs_notes.empty() && !middle_entry.notes.empty()) {
            uint8_t cs_start = static_cast<uint8_t>(
                clampPitch(static_cast<int>(cs_notes[0].pitch) + cs_shift, 0, 127));
            uint8_t subj_start = middle_entry.notes[0].pitch;
            if ((cs_start % 12) == (subj_start % 12)) {
              independent = false;
            }
          }

          if (independent) {
            for (auto& note : cs_notes) {
              note.start_tick += current_tick;
              note.pitch =
                  static_cast<uint8_t>(clampPitch(static_cast<int>(note.pitch) + cs_shift, 0, 127));
              note.voice = cs_voice;
              note.source = BachNoteSource::Countersubject;
            }
            // Trim notes that exceed the middle entry boundary.
            cs_notes.erase(std::remove_if(cs_notes.begin(), cs_notes.end(),
                                          [middle_end](const NoteEvent& n) {
                                            return n.start_tick >= middle_end;
                                          }),
                           cs_notes.end());
            all_notes.insert(all_notes.end(), cs_notes.begin(), cs_notes.end());
            record_notes(cs_notes, target_key);
          }
        }

        // Companion counterpoint for non-entry voices.
        if (section.companion_needed) {
          Tick me_duration = middle_end - current_tick;
          if (me_duration > 0 && num_voices >= 2) {
            uint32_t companion_seed = config.seed + static_cast<uint32_t>(pair_idx) * 2000u + 2500u;
            float me_energy = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
            pipeline_cs.gravity.phase = FuguePhase::Develop;
            pipeline_cs.gravity.energy = me_energy;
            // Extract per-voice last pitches for companion voice-leading continuity.
            uint8_t comp_last_pitches[6] = {};
            for (uint8_t vid = 0; vid < num_voices && vid < 6; ++vid) {
              comp_last_pitches[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
            }

            ConstraintState companion_exit;
            Episode companion = generateFortspinnungEpisode(
                material.subject, material.motif_pool, current_tick, me_duration, target_key,
                target_key, num_voices, companion_seed, pair_idx, me_energy, cp_state, cp_rules,
                cp_resolver, plan.detailed_timeline, 0, &pipeline_cs.accumulator,
                prev_episode_exit ? &*prev_episode_exit : nullptr, &companion_exit,
                comp_last_pitches, &material.thematic_plan);
            // Chain exit state from companion episode.
            pipeline_cs.accumulator = companion_exit.accumulator;
            prev_episode_exit = std::move(companion_exit);
            // Remove only companion notes that collide with the entry voice.
            // Keeping non-overlapping continuation material prevents the entry
            // voice from going silent after a short middle-entry subject.
            companion.notes.erase(
                std::remove_if(companion.notes.begin(), companion.notes.end(),
                               [entry_voice, &middle_entry](const NoteEvent& evt) {
                                 if (evt.voice != entry_voice)
                                   return false;
                                 Tick evt_end = evt.start_tick + evt.duration;
                                 for (const auto& entry_note : middle_entry.notes) {
                                   if (entry_note.voice != entry_voice)
                                     continue;
                                   Tick entry_end = entry_note.start_tick + entry_note.duration;
                                   if (evt.start_tick < entry_end &&
                                       evt_end > entry_note.start_tick) {
                                     return true;
                                   }
                                 }
                                 return false;
                               }),
                companion.notes.end());
            if (entry_voice == num_voices - 1 && num_voices >= 4) {
              companion.notes.erase(std::remove_if(companion.notes.begin(), companion.notes.end(),
                                                   [](const NoteEvent& evt) {
                                                     return evt.source !=
                                                            BachNoteSource::SequenceNote;
                                                   }),
                                    companion.notes.end());
            }
            // Additional voice rest for texture variety. In 4-voice fugues the
            // companion has already removed the entry voice, so resting another
            // line leaves the middle development underfilled.
            float ep_phase_pos2 =
                static_cast<float>(current_tick) / static_cast<float>(plan.estimated_duration);
            uint8_t ep_active2 = selectEpisodeVoiceCount(num_voices, ep_phase_pos2,
                                                         config.density_target, density_rng);
            if (ep_active2 < num_voices && num_voices >= 5) {
              uint8_t companion_rest =
                  selectRestingVoice(num_voices, pair_idx + 100, entry_voice, prev_rest_voice);
              if (companion_rest != entry_voice) {
                Tick me_dur = middle_end - current_tick;
                applyFortPhaseRestProcessing(companion.notes, companion_rest, current_tick, me_dur,
                                             fort_grammar);
                prev_rest_voice = companion_rest;
              }
            }
            all_notes.insert(all_notes.end(), companion.notes.begin(), companion.notes.end());
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
          uint8_t tonic_for_pedal = tonicBassPitchForVoices(config.key, num_voices);
          uint8_t dominant_pitch =
              clampPitch(static_cast<int>(tonic_for_pedal) + interval::kPerfect5th, ped_lo, ped_hi);

          removeLowestVoiceNotes(all_notes, lowest_voice, current_tick,
                                 current_tick + pedal_duration);
          auto dominant_pedal =
              generatePedalPoint(dominant_pitch, current_tick, pedal_duration, lowest_voice);
          all_notes.insert(all_notes.end(), dominant_pedal.begin(), dominant_pedal.end());
          record_notes(dominant_pedal, config.key);

          // Register pedal notes in counterpoint state.
          for (const auto& note : dominant_pedal) {
            cp_state.addNote(note.voice, note);
          }

          // Upper voice episode over pedal.
          uint8_t upper_voices = num_voices > 1 ? num_voices - 1 : 1;
          float pedal_energy = FugueEnergyCurve::getLevel(current_tick, plan.estimated_duration);
          pipeline_cs.gravity.phase = FuguePhase::Resolve;
          pipeline_cs.gravity.energy = pedal_energy;
          // Extract per-voice last pitches for pedal episode voice-leading continuity.
          uint8_t pedal_last_pitches[6] = {};
          for (uint8_t vid = 0; vid < upper_voices && vid < 6; ++vid) {
            pedal_last_pitches[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
          }

          ConstraintState pedal_exit;
          Episode pedal_episode = generateFortspinnungEpisode(
              material.subject, material.motif_pool, current_tick, pedal_duration, config.key,
              config.key, upper_voices,
              config.seed + static_cast<uint32_t>(config.develop_pairs + 1) * 2000u + 7000u,
              config.develop_pairs + 1, pedal_energy, cp_state, cp_rules, cp_resolver,
              plan.detailed_timeline, dominant_pitch, &pipeline_cs.accumulator,
              prev_episode_exit ? &*prev_episode_exit : nullptr, &pedal_exit, pedal_last_pitches,
              &material.thematic_plan);
          // Chain exit state from pedal episode.
          pipeline_cs.accumulator = pedal_exit.accumulator;
          prev_episode_exit = std::move(pedal_exit);

          // Texture reduction: rest one upper voice during pedal episode
          // to prevent tutti saturation when there are enough upper voices.
          // In 4-voice pedaliter fugues this removed an entire manual line
          // over the dominant pedal, leaving the texture underfilled.
          if (upper_voices >= 4) {
            uint8_t pedal_rest = static_cast<uint8_t>((config.develop_pairs + 2) % upper_voices);
            pedal_episode.notes.erase(
                std::remove_if(
                    pedal_episode.notes.begin(), pedal_episode.notes.end(),
                    [pedal_rest](const NoteEvent& evt) { return evt.voice == pedal_rest; }),
                pedal_episode.notes.end());
          }

          all_notes.insert(all_notes.end(), pedal_episode.notes.begin(), pedal_episode.notes.end());
          record_notes(pedal_episode.notes, config.key);
          current_tick += pedal_duration;
        }

        // Stretto body.
        uint8_t stretto_last[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          stretto_last[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
        }
        Stretto stretto = generateStretto(material.subject, config.key, current_tick, num_voices,
                                          config.seed + 4000, material.subject.character, cp_state,
                                          cp_rules, cp_resolver, plan.detailed_timeline,
                                          stretto_last, plan.estimated_duration);
        Tick stretto_end = stretto.end_tick;
        if (stretto_end <= current_tick) {
          stretto_end = current_tick + kTicksPerBar * 2;
        }
        structure.addSection(SectionType::Stretto, FuguePhase::Resolve, current_tick, stretto_end,
                             config.key);
        auto stretto_notes = stretto.allNotes();
        all_notes.insert(all_notes.end(), stretto_notes.begin(), stretto_notes.end());
        record_notes(stretto_notes, config.key);
        current_tick = stretto_end;
        break;
      }

      case SectionType::Coda: {
        Tick coda_duration = kTicksPerBar * kCodaBars;
        structure.addSection(SectionType::Coda, FuguePhase::Resolve, current_tick,
                             current_tick + coda_duration, config.key);
        // Voice-leading: extract last pitches for smooth coda transitions.
        uint8_t coda_last_pitches[5] = {0, 0, 0, 0, 0};
        for (uint8_t vid = 0; vid < num_voices && vid < 5; ++vid) {
          coda_last_pitches[vid] = extractVoiceLastPitch(all_notes, current_tick, vid);
        }
        auto coda_notes = createCodaNotes(current_tick, coda_duration, config.key, num_voices,
                                          config.is_minor, coda_last_pitches, &material.subject);
        all_notes.insert(all_notes.end(), coda_notes.begin(), coda_notes.end());
        record_notes(coda_notes, config.key);

        // Tonic pedal in coda Stage 3 (last bar).
        {
          uint8_t tonic_pitch = tonicBassPitchForVoices(config.key, num_voices);
          Tick pedal_start = current_tick + kTicksPerBar * 3;
          Tick pedal_dur = current_tick + coda_duration - pedal_start;
          removeLowestVoiceNotes(all_notes, lowest_voice, pedal_start,
                                 current_tick + coda_duration);
          auto tonic_pedal = generatePedalPoint(tonic_pitch, pedal_start, pedal_dur, lowest_voice);
          all_notes.insert(all_notes.end(), tonic_pedal.begin(), tonic_pedal.end());
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
static void insertCadenceNotes(std::vector<NoteEvent>& all_notes, FugueStructure& structure,
                               const FugueConfig& config, const FuguePlan& plan,
                               uint8_t num_voices) {
  CadenceDetectionConfig cadence_config;
  cadence_config.max_bars_without_cadence = 16;
  cadence_config.scan_window_bars = 8;
  cadence_config.deceptive_cadence_probability = 0.20f;

  ensureCadentialCoverage(all_notes, structure, config.key, config.is_minor, num_voices - 1,
                          num_voices, plan.estimated_duration, config.seed + 66666u,
                          cadence_config);

  KeySignature home_key_sig;
  home_key_sig.tonic = config.key;
  home_key_sig.is_minor = config.is_minor;
  CadencePlan cadence_plan = CadencePlan::createForFugue(structure, home_key_sig, config.is_minor);

  applyCadenceApproachToVoices(all_notes, cadence_plan, config.key, config.is_minor, num_voices,
                               config.seed + 88888u);
}

/// @brief Adjust episode material notes on strong beats that form dissonant
/// intervals with other simultaneously-sounding notes.  Lighter than the old
/// 11-pass post-validation but catches the worst offenders.
static void enforceStrongBeatConsonance(std::vector<NoteEvent>& all_notes,
                                        const FugueConfig& config, const FuguePlan& plan,
                                        uint8_t num_voices) {
  (void)config;  // reserved for future scale-aware consonance checks
  Tick total_ticks = plan.estimated_duration;
  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    if (!isStrongBeatInBar(beat))
      continue;
    // Collect indices of notes active at this beat.
    std::vector<size_t> active;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].start_tick <= beat &&
          all_notes[idx].start_tick + all_notes[idx].duration > beat) {
        active.push_back(idx);
      }
    }
    if (active.size() < 2)
      continue;

    // Check flexible material sounding on this beat.  If the offending note
    // started earlier, split it at the strong beat and repair only the tail;
    // the analyzer also scores sustained notes at strong beats.
    for (size_t a : active) {
      if (a >= all_notes.size())
        continue;
      if (getProtectionLevel(all_notes[a].source) != ProtectionLevel::Flexible) {
        continue;
      }
      if (all_notes[a].start_tick > beat ||
          all_notes[a].start_tick + all_notes[a].duration <= beat) {
        continue;
      }

      bool has_dissonance = false;
      for (size_t b : active) {
        if (a == b)
          continue;
        if (b >= all_notes.size())
          continue;
        int diff =
            std::abs(static_cast<int>(all_notes[a].pitch) - static_cast<int>(all_notes[b].pitch));
        int simple = interval_util::compoundToSimple(diff);
        // P4 between upper voices is consonant in Baroque practice.
        bool p4_upper = (simple == 5) && (all_notes[a].voice < num_voices - 1) &&
                        (all_notes[b].voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper && diff > 0 && diff < 36) {
          has_dissonance = true;
          break;
        }
      }
      if (!has_dissonance)
        continue;

      size_t target_idx = a;
      if (all_notes[a].start_tick < beat) {
        Tick old_end = all_notes[a].start_tick + all_notes[a].duration;
        Tick prefix_dur = beat - all_notes[a].start_tick;
        Tick tail_dur = old_end - beat;
        if (prefix_dur < duration::kSixteenthNote || tail_dur < duration::kSixteenthNote) {
          continue;
        }

        NoteEvent tail = all_notes[a];
        tail.start_tick = beat;
        tail.duration = tail_dur;
        tail.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
        all_notes[a].duration = prefix_dur;
        all_notes[a].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        all_notes.push_back(tail);
        target_idx = all_notes.size() - 1;
      }

      // Collect previous-beat pitches per voice for parallel checking.
      Tick prev_beat = (beat >= kTicksPerBeat) ? beat - kTicksPerBeat : 0;
      std::map<uint8_t, uint8_t> prev_pitches;
      if (beat > 0) {
        for (const auto& n : all_notes) {
          if (n.start_tick <= prev_beat && n.start_tick + n.duration > prev_beat) {
            prev_pitches[n.voice] = n.pitch;
          }
        }
      }
      uint8_t this_voice = all_notes[target_idx].voice;
      auto [voice_lo, voice_hi] = getFugueVoiceRange(this_voice, num_voices);

      // Try local adjustments to find consonance.  Wider attempts are last:
      // they catch unresolved M2/m7 clashes without making octave repairs here.
      // Reject candidates that create parallel P5 or P8.
      uint8_t orig = all_notes[target_idx].pitch;
      for (int delta : {1, -1, 2, -2, 3, -3, 4, -4, 5, -5}) {
        int cand = static_cast<int>(orig) + delta;
        if (cand < 0 || cand > 127)
          continue;
        if (cand < voice_lo || cand > voice_hi)
          continue;
        bool ok = true;
        for (size_t b : active) {
          if (a == b)
            continue;
          if (b >= all_notes.size())
            continue;
          int diff = std::abs(cand - static_cast<int>(all_notes[b].pitch));
          int simple = interval_util::compoundToSimple(diff);
          bool p4_upper = (simple == 5) && (this_voice < num_voices - 1) &&
                          (all_notes[b].voice < num_voices - 1);
          if (!interval_util::isConsonance(simple) && !p4_upper && diff > 0 && diff < 36) {
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
            if (it_this != prev_pitches.end() && it_other != prev_pitches.end()) {
              int prev_diff =
                  std::abs(static_cast<int>(it_this->second) - static_cast<int>(it_other->second));
              int prev_simple = interval_util::compoundToSimple(prev_diff);
              if (prev_simple == simple) {
                ok = false;
                break;
              }
              int this_motion = cand - static_cast<int>(it_this->second);
              int other_motion =
                  static_cast<int>(all_notes[b].pitch) - static_cast<int>(it_other->second);
              bool same_motion =
                  this_motion != 0 && other_motion != 0 && (this_motion > 0) == (other_motion > 0);
              if (prev_simple != simple && same_motion && std::abs(this_motion) > 2 &&
                  std::abs(other_motion) > 2) {
                ok = false;
                break;
              }
            }
          }
        }
        if (ok) {
          all_notes[target_idx].pitch = static_cast<uint8_t>(cand);
          all_notes[target_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
          break;
        }
      }
    }
  }
}

static int delayStrongBeatEpisodeDissonances(std::vector<NoteEvent>& all_notes,
                                             const FuguePlan& plan, uint8_t num_voices) {
  int repairs = 0;
  Tick total_ticks = plan.estimated_duration;
  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    if (!isStrongBeatInBar(beat))
      continue;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.source != BachNoteSource::EpisodeMaterial)
        continue;
      bool starts_on_beat = note.start_tick == beat;
      bool short_subject_straddler = false;
      if (!starts_on_beat && note.start_tick < beat && note.start_tick + note.duration > beat) {
        Tick prefix = beat - note.start_tick;
        Tick tail = note.start_tick + note.duration - beat;
        if (prefix >= duration::kThirtySecondNote && tail > 0) {
          for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
            if (other_idx == idx)
              continue;
            const auto& other = all_notes[other_idx];
            if (other.source != BachNoteSource::FugueSubject)
              continue;
            if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
              continue;
            }
            int diff = std::abs(static_cast<int>(note.pitch) - static_cast<int>(other.pitch));
            int simple = interval_util::compoundToSimple(diff);
            if (!interval_util::isConsonance(simple) && diff > 0 && diff < 36) {
              short_subject_straddler = true;
              break;
            }
          }
        }
      }
      if (!starts_on_beat && !short_subject_straddler)
        continue;
      if (starts_on_beat && note.duration < duration::kEighthNote)
        continue;

      bool has_dissonance = false;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == idx)
          continue;
        const auto& other = all_notes[other_idx];
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(static_cast<int>(note.pitch) - static_cast<int>(other.pitch));
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper =
            (simple == 5) && (note.voice < num_voices - 1) && (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper && diff > 0 && diff < 36) {
          has_dissonance = true;
          break;
        }
      }
      if (!has_dissonance)
        continue;

      if (short_subject_straddler) {
        Tick prefix = beat - note.start_tick;
        if (prefix >= duration::kThirtySecondNote) {
          Tick old_end = note.start_tick + note.duration;
          Tick delayed_start = beat + duration::kSixteenthNote;
          Tick delayed_dur = old_end > delayed_start ? old_end - delayed_start : 0;
          if (delayed_dur >= duration::kSixteenthNote) {
            NoteEvent tail = note;
            tail.start_tick = delayed_start;
            tail.duration = delayed_dur;
            tail.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
            all_notes.push_back(tail);
          }
          note.duration = prefix;
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
          ++repairs;
        }
        continue;
      }

      Tick delayed_start = beat + duration::kSixteenthNote;
      bool same_voice_busy = false;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == idx)
          continue;
        const auto& other = all_notes[other_idx];
        if (other.voice != note.voice)
          continue;
        if (other.start_tick <= delayed_start &&
            other.start_tick + other.duration > delayed_start) {
          same_voice_busy = true;
          break;
        }
      }
      if (same_voice_busy)
        continue;
      note.start_tick = delayed_start;
      note.duration -= duration::kSixteenthNote;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      ++repairs;
    }
  }
  return repairs;
}

static int trimShortCountersubjectSubjectStraddlers(std::vector<NoteEvent>& all_notes,
                                                    const FuguePlan& plan, uint8_t num_voices) {
  int repairs = 0;
  Tick total_ticks = plan.estimated_duration;
  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    if (!isStrongBeatInBar(beat))
      continue;
    for (auto& note : all_notes) {
      if (note.source != BachNoteSource::Countersubject)
        continue;
      if (note.start_tick >= beat || note.start_tick + note.duration <= beat) {
        continue;
      }

      Tick prefix = beat - note.start_tick;
      Tick tail = note.start_tick + note.duration - beat;
      if (prefix < duration::kThirtySecondNote || tail > duration::kThirtySecondNote) {
        continue;
      }

      bool clashes_with_subject = false;
      for (const auto& other : all_notes) {
        if (other.voice == note.voice)
          continue;
        if (other.source != BachNoteSource::FugueSubject)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(static_cast<int>(note.pitch) - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper) {
          clashes_with_subject = true;
          break;
        }
      }
      if (!clashes_with_subject)
        continue;

      note.duration = prefix;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      ++repairs;
    }
  }
  return repairs;
}

static int trimShortRepairedDialogueFragments(std::vector<NoteEvent>& all_notes,
                                              uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_dialogue_fragment = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::EpisodeMaterial;
  };
  auto is_protected_line = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_protected_clash = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kThirtySecondNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* sounding = nullptr;
        for (const auto& other : all_notes) {
          if (other.voice != voice)
            continue;
          if (other.start_tick > tick || other.start_tick + other.duration <= tick) {
            continue;
          }
          if (sounding == nullptr || other.start_tick >= sounding->start_tick) {
            sounding = &other;
          }
        }
        if (sounding == nullptr || !is_protected_line(sounding->source))
          continue;
        if (hard_bad_against(note.pitch, *sounding))
          return true;
      }
    }
    return false;
  };

  std::vector<NoteEvent> kept;
  kept.reserve(all_notes.size());
  int trimmed = 0;
  for (const auto& note : all_notes) {
    bool trim = is_dialogue_fragment(note.source) && note.duration <= duration::kSixteenthNote &&
                (note.modified_by & kPitchRepairMask) != 0 && has_protected_clash(note);
    if (trim) {
      ++trimmed;
      continue;
    }
    kept.push_back(note);
  }
  all_notes.swap(kept);
  return trimmed;
}

static const NoteEvent* soundingNoteAt(const std::vector<NoteEvent>& notes, uint8_t voice,
                                       Tick tick) {
  const NoteEvent* best = nullptr;
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick > tick || note.start_tick + note.duration <= tick)
      continue;
    if (best == nullptr || note.start_tick >= best->start_tick) {
      best = &note;
    }
  }
  return best;
}

static int removeShortRepairedEpisodeSubjectClashes(std::vector<NoteEvent>& all_notes,
                                                    uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_subject_source = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto active_voice_count_without = [&](size_t skip_idx, Tick tick) {
    std::array<bool, 8> active{};
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == skip_idx)
        continue;
      const auto& note = all_notes[idx];
      if (note.voice >= active.size())
        continue;
      if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
        active[note.voice] = true;
      }
    }
    return static_cast<uint32_t>(std::count(active.begin(), active.end(), true));
  };
  auto clashes_with_subject = [&](const NoteEvent& note, Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
      if (other == nullptr || !is_subject_source(other->source))
        continue;
      if (is_hard_bad_against(note.pitch, *other))
        return true;
    }
    return false;
  };

  std::vector<NoteEvent> kept;
  kept.reserve(all_notes.size());
  int removed = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    bool remove = note.source == BachNoteSource::EpisodeMaterial &&
                  note.duration <= duration::kQuarterNote &&
                  (note.modified_by & kPitchRepairMask) != 0;
    if (remove) {
      bool has_subject_clash = false;
      bool texture_survives = true;
      for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
           tick += duration::kSixteenthNote) {
        has_subject_clash = has_subject_clash || clashes_with_subject(note, tick);
        if (active_voice_count_without(idx, tick) < 2) {
          texture_survives = false;
          break;
        }
      }
      remove = has_subject_clash && texture_survives;
    }
    if (remove) {
      ++removed;
      continue;
    }
    kept.push_back(note);
  }
  all_notes.swap(kept);
  return removed;
}

static int removeShortRepairedEpisodeAnchorConflicts(std::vector<NoteEvent>& all_notes,
                                                     uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_anchor_source = [](BachNoteSource source) {
    return getProtectionLevel(source) == ProtectionLevel::Immutable ||
           source == BachNoteSource::SequenceNote || source == BachNoteSource::CadenceApproach;
  };
  auto conflicts_with_anchor = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff < 3)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto active_voice_count_without = [&](size_t skip_idx, Tick tick) {
    std::array<bool, 8> active{};
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == skip_idx)
        continue;
      const auto& note = all_notes[idx];
      if (note.voice >= active.size())
        continue;
      if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
        active[note.voice] = true;
      }
    }
    return static_cast<uint32_t>(std::count(active.begin(), active.end(), true));
  };
  auto has_anchor_conflict = [&](const NoteEvent& note, Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
      if (other == nullptr || !is_anchor_source(other->source))
        continue;
      if (conflicts_with_anchor(note.pitch, *other))
        return true;
    }
    return false;
  };

  std::vector<NoteEvent> kept;
  kept.reserve(all_notes.size());
  int removed = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    bool remove = note.source == BachNoteSource::EpisodeMaterial &&
                  note.duration <= duration::kQuarterNote &&
                  (note.modified_by & kPitchRepairMask) != 0;
    if (remove) {
      bool has_conflict = false;
      bool texture_survives = true;
      for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
           tick += duration::kSixteenthNote) {
        has_conflict = has_conflict || has_anchor_conflict(note, tick);
        if (active_voice_count_without(idx, tick) < 2) {
          texture_survives = false;
          break;
        }
      }
      remove = has_conflict && texture_survives;
    }
    if (remove) {
      ++removed;
      continue;
    }
    kept.push_back(note);
  }
  all_notes.swap(kept);
  return removed;
}

static int removeShortRepairedEpisodeAnchorCrossings(std::vector<NoteEvent>& all_notes,
                                                     uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_anchor_source = [](BachNoteSource source) {
    return getProtectionLevel(source) == ProtectionLevel::Immutable ||
           source == BachNoteSource::SubjectCore || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::CadenceApproach;
  };
  auto active_voice_count_without = [&](size_t skip_idx, Tick tick) {
    std::array<bool, 8> active{};
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == skip_idx)
        continue;
      const auto& note = all_notes[idx];
      if (note.voice >= active.size())
        continue;
      if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
        active[note.voice] = true;
      }
    }
    return static_cast<uint32_t>(std::count(active.begin(), active.end(), true));
  };
  auto crosses_anchor = [&](const NoteEvent& note, Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
      if (other == nullptr || !is_anchor_source(other->source))
        continue;
      if (voice < note.voice && other->pitch <= note.pitch)
        return true;
      if (voice > note.voice && other->pitch >= note.pitch)
        return true;
    }
    return false;
  };

  std::vector<NoteEvent> kept;
  kept.reserve(all_notes.size());
  int removed = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    bool remove = (note.source == BachNoteSource::EpisodeMaterial ||
                   note.source == BachNoteSource::SequenceNote) &&
                  note.duration <= duration::kQuarterNote &&
                  (note.modified_by & kPitchRepairMask) != 0;
    if (remove) {
      bool has_crossing = false;
      bool texture_survives = true;
      for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
           tick += duration::kSixteenthNote) {
        has_crossing = has_crossing || crosses_anchor(note, tick);
        if (active_voice_count_without(idx, tick) < 2) {
          texture_survives = false;
          break;
        }
      }
      remove = has_crossing && texture_survives;
    }
    if (remove) {
      ++removed;
      continue;
    }
    kept.push_back(note);
  }
  all_notes.swap(kept);
  return removed;
}

static int trimRepairedCountersubjectHardTails(std::vector<NoteEvent>& all_notes,
                                               uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note, Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
      if (other == nullptr)
        continue;
      if (hard_bad_against(note.pitch, *other))
        return true;
    }
    return false;
  };

  int trimmed = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Countersubject)
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (note.duration <= duration::kQuarterNote)
      continue;

    Tick end_tick = note.start_tick + note.duration;
    Tick first_bad = end_tick;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      if (has_hard_vertical(note, tick)) {
        first_bad = tick;
        break;
      }
    }
    if (first_bad == end_tick || first_bad == note.start_tick)
      continue;
    Tick prefix = first_bad - note.start_tick;
    if (prefix < duration::kEighthNote)
      continue;

    note.duration = prefix;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    ++trimmed;
  }

  return trimmed;
}

static int revoiceRepairedEpisodeVerticalCells(std::vector<NoteEvent>& all_notes,
                                               const HarmonicTimeline& timeline,
                                               const FugueConfig& config, uint8_t num_voices) {
  if (num_voices < 2 || timeline.size() == 0)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_repaired_episode = [&](const NoteEvent& note) {
    return note.source == BachNoteSource::EpisodeMaterial &&
           (note.modified_by & kPitchRepairMask) != 0;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };
  auto vertical_badness = [&](const NoteEvent& note, int pitch) {
    int bad = 0;
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, note.start_tick);
      if (other == nullptr)
        continue;
      if (voice < note.voice && pitch >= static_cast<int>(other->pitch)) {
        return 1000;
      }
      if (voice > note.voice && pitch <= static_cast<int>(other->pitch)) {
        return 1000;
      }
      int diff = std::abs(pitch - static_cast<int>(other->pitch));
      if (diff == 0)
        return 1000;
      int simple = interval_util::compoundToSimple(diff);
      bool consonant =
          simple == 0 || simple == 3 || simple == 4 || simple == 7 || simple == 8 || simple == 9;
      if (!consonant)
        ++bad;
    }
    return bad;
  };

  std::vector<size_t> repaired_idxs;
  repaired_idxs.reserve(all_notes.size());
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (is_repaired_episode(all_notes[idx]))
      repaired_idxs.push_back(idx);
  }
  std::sort(repaired_idxs.begin(), repaired_idxs.end(), [&](size_t lhs, size_t rhs) {
    if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    }
    if (all_notes[lhs].voice != all_notes[rhs].voice) {
      return all_notes[lhs].voice > all_notes[rhs].voice;
    }
    return lhs < rhs;
  });

  int changed = 0;
  const VoiceId bass_voice = static_cast<VoiceId>(num_voices - 1);
  ScaleType home_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  for (size_t idx : repaired_idxs) {
    NoteEvent& note = all_notes[idx];
    if (!is_repaired_episode(note))
      continue;
    if (note.voice != bass_voice)
      continue;
    int old_bad = vertical_badness(note, note.pitch);
    if (old_bad == 0)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType event_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      bool tonal = scale_util::isScaleTone(cand_u8, event.key, event_scale) ||
                   scale_util::isScaleTone(cand_u8, config.key, home_scale) ||
                   (event.is_minor &&
                    scale_util::isScaleTone(cand_u8, event.key, ScaleType::NaturalMinor)) ||
                   isChordTone(cand_u8, event);
      if (!tonal)
        continue;
      if (note.start_tick % kTicksPerBeat == 0 && !isChordTone(cand_u8, event)) {
        continue;
      }
      int bad = vertical_badness(note, cand);
      if (bad > old_bad || bad >= 1000)
        continue;

      int cost = bad * 1000 + std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        int max_leap = note.voice + 1 == num_voices ? interval::kOctave : interval::kPerfect5th;
        if (leap > max_leap)
          continue;
        cost += leap * 10;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(cand - next_pitch);
        int max_leap = note.voice + 1 == num_voices ? interval::kOctave : interval::kPerfect5th;
        if (leap > max_leap)
          continue;
        cost += leap * 8;
      }
      if (isChordTone(cand_u8, event))
        cost -= 20;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX || best_bad > old_bad)
      continue;
    if (best_pitch != static_cast<int>(note.pitch)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
    }
    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++changed;
  }
  return changed;
}

static int retargetRelativeMinorFalseEntryPedals(std::vector<NoteEvent>& all_notes,
                                                 const FugueConfig& config, const FuguePlan& plan,
                                                 uint8_t num_voices) {
  if (config.is_minor || num_voices < 4)
    return 0;

  Key relative_minor = getRelative(KeySignature{config.key, false}).tonic;
  VoiceId lowest_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(lowest_voice, num_voices);
  int dominant_pc = (static_cast<int>(config.key) + interval::kPerfect5th) % 12;
  int repairs = 0;

  for (const auto& section : plan.sections) {
    if (section.type != SectionType::MiddleEntry || section.key != relative_minor) {
      continue;
    }

    bool has_false_entry = false;
    for (const auto& note : all_notes) {
      if (note.source == BachNoteSource::FalseEntry && note.start_tick < section.end_tick &&
          note.start_tick + note.duration > section.start_tick) {
        has_false_entry = true;
        break;
      }
    }
    if (!has_false_entry)
      continue;

    for (auto& note : all_notes) {
      if (note.voice != lowest_voice || note.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick > section.start_tick || note_end <= section.start_tick + kTicksPerBeat) {
        continue;
      }
      if (getPitchClass(note.pitch) != static_cast<int>(relative_minor)) {
        continue;
      }

      int best_pitch = static_cast<int>(note.pitch);
      int best_cost = INT32_MAX;
      for (int pitch = static_cast<int>(ped_lo); pitch <= static_cast<int>(ped_hi); ++pitch) {
        if (pitch % 12 != dominant_pc)
          continue;
        int cost = std::abs(pitch - static_cast<int>(note.pitch));
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = pitch;
        }
      }
      if (best_cost < INT32_MAX) {
        note.pitch = static_cast<uint8_t>(best_pitch);
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
        ++repairs;
      }
      break;
    }
  }

  return repairs;
}

static int nearestPedalPitchForPc(int pitch_class, uint8_t range_lo, uint8_t range_hi,
                                  int reference_pitch) {
  int best_pitch = -1;
  int best_cost = INT32_MAX;
  for (int pitch = static_cast<int>(range_lo); pitch <= static_cast<int>(range_hi); ++pitch) {
    if (pitch % 12 != pitch_class)
      continue;
    int cost = std::abs(pitch - static_cast<int>(reference_pitch));
    if (cost < best_cost) {
      best_cost = cost;
      best_pitch = pitch;
    }
  }
  return best_pitch;
}

static const NoteEvent* nextVoiceNoteAtOrAfter(const std::vector<NoteEvent>& notes, uint8_t voice,
                                               Tick tick) {
  const NoteEvent* best = nullptr;
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick < tick)
      continue;
    if (best == nullptr || note.start_tick < best->start_tick) {
      best = &note;
    }
  }
  return best;
}

static int addLateHomeEpisodePedalSupport(std::vector<NoteEvent>& all_notes,
                                          const FugueConfig& config, const FuguePlan& plan,
                                          uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  const int tonic_pc = getPitchClass(tonic);
  const int dominant_pc = (tonic_pc + interval::kPerfect5th) % 12;
  const int subdominant_pc = (tonic_pc + 5) % 12;
  int dominant_pitch = static_cast<int>(tonic) + interval::kPerfect5th;
  while (dominant_pitch > static_cast<int>(ped_hi))
    dominant_pitch -= 12;
  while (dominant_pitch < static_cast<int>(ped_lo))
    dominant_pitch += 12;
  int subdominant_pitch = static_cast<int>(tonic) + 5;
  while (subdominant_pitch > static_cast<int>(ped_hi))
    subdominant_pitch -= 12;
  while (subdominant_pitch < static_cast<int>(ped_lo))
    subdominant_pitch += 12;
  const int support_pcs[] = {
      dominant_pc, dominant_pc, tonic_pc, tonic_pc, subdominant_pc,
      dominant_pc, dominant_pc, tonic_pc, tonic_pc,
  };
  const int support_refs[] = {
      dominant_pitch,          dominant_pitch,          static_cast<int>(tonic),
      static_cast<int>(tonic), subdominant_pitch,       dominant_pitch,
      dominant_pitch,          static_cast<int>(tonic), static_cast<int>(tonic),
  };
  constexpr int kSupportPcCount = static_cast<int>(sizeof(support_pcs) / sizeof(support_pcs[0]));

  int repairs = 0;
  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode || section.key != config.key) {
      continue;
    }
    if (section.start_tick < plan.estimated_duration / 2)
      continue;

    Tick support_start = section.start_tick + kTicksPerBeat * 3;
    support_start = ((support_start + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    if (support_start >= section.end_tick)
      continue;

    int slot = 0;
    for (Tick tick = support_start; tick < section.end_tick; tick += kTicksPerBeat, ++slot) {
      if (soundingNoteAt(all_notes, pedal_voice, tick) != nullptr)
        continue;

      const NoteEvent* next_pedal = nextVoiceNoteAtOrAfter(all_notes, pedal_voice, tick);
      Tick available = std::min(kTicksPerBeat, section.end_tick - tick);
      if (next_pedal != nullptr && next_pedal->start_tick > tick) {
        available = std::min(available, next_pedal->start_tick - tick);
      }
      if (available < duration::kSixteenthNote)
        continue;

      int pc_slot = slot % kSupportPcCount;
      int pitch =
          nearestPedalPitchForPc(support_pcs[pc_slot], ped_lo, ped_hi, support_refs[pc_slot]);
      if (pitch < 0)
        continue;

      NoteEvent support;
      support.start_tick = tick;
      support.duration = available;
      support.pitch = static_cast<uint8_t>(pitch);
      support.velocity = kOrganVelocity;
      support.voice = pedal_voice;
      support.source = BachNoteSource::EpisodeMaterial;
      all_notes.push_back(support);
      ++repairs;
    }
  }

  return repairs;
}

static int addSubdominantEpisodeOpeningPedalSupport(std::vector<NoteEvent>& all_notes,
                                                    const FugueConfig& config,
                                                    const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  const Key subdominant = getSubdominant(KeySignature{config.key, config.is_minor}).tonic;
  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  const int section_tonic_pc = static_cast<int>(subdominant) % 12;
  const int home_dominant_pc = (static_cast<int>(config.key) + interval::kPerfect5th) % 12;
  int section_tonic_ref = static_cast<int>(tonic) + 5;
  while (section_tonic_ref > static_cast<int>(ped_hi))
    section_tonic_ref -= 12;
  while (section_tonic_ref < static_cast<int>(ped_lo))
    section_tonic_ref += 12;
  int home_dominant_ref = static_cast<int>(tonic) + interval::kPerfect5th;
  while (home_dominant_ref > static_cast<int>(ped_hi))
    home_dominant_ref -= 12;
  while (home_dominant_ref < static_cast<int>(ped_lo))
    home_dominant_ref += 12;

  const int support_pcs[] = {section_tonic_pc, home_dominant_pc};
  const int support_refs[] = {section_tonic_ref, home_dominant_ref};

  int repairs = 0;
  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode || section.key != subdominant) {
      continue;
    }

    Tick support_start = ((section.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (int slot = 0; slot < 2; ++slot) {
      Tick tick = support_start + static_cast<Tick>(slot) * kTicksPerBeat;
      if (tick >= section.end_tick)
        break;
      if (soundingNoteAt(all_notes, pedal_voice, tick) != nullptr)
        continue;

      const NoteEvent* next_pedal = nextVoiceNoteAtOrAfter(all_notes, pedal_voice, tick);
      Tick available = std::min(kTicksPerBeat, section.end_tick - tick);
      if (next_pedal != nullptr && next_pedal->start_tick > tick) {
        available = std::min(available, next_pedal->start_tick - tick);
      }
      if (available < duration::kSixteenthNote)
        continue;

      int pitch = nearestPedalPitchForPc(support_pcs[slot], ped_lo, ped_hi, support_refs[slot]);
      if (pitch < 0)
        continue;

      NoteEvent support;
      support.start_tick = tick;
      support.duration = available;
      support.pitch = static_cast<uint8_t>(pitch);
      support.velocity = kOrganVelocity;
      support.voice = pedal_voice;
      support.source = BachNoteSource::EpisodeMaterial;
      all_notes.push_back(support);
      ++repairs;
    }
  }

  return repairs;
}

static int retargetSubdominantEpisodeClosingPedal(std::vector<NoteEvent>& all_notes,
                                                  const FugueConfig& config, const FuguePlan& plan,
                                                  uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  const Key subdominant = getSubdominant(KeySignature{config.key, config.is_minor}).tonic;
  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  const int subdominant_pc = static_cast<int>(subdominant) % 12;
  const int home_dominant_pc = (static_cast<int>(config.key) + interval::kPerfect5th) % 12;
  int repairs = 0;

  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode || section.key != subdominant) {
      continue;
    }

    size_t target_idx = all_notes.size();
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != pedal_voice || note.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (note.start_tick < section.start_tick + kTicksPerBeat * 4 ||
          note.start_tick >= section.end_tick + kTicksPerBeat) {
        continue;
      }
      if (getPitchClass(note.pitch) != home_dominant_pc)
        continue;
      if (target_idx == all_notes.size() || note.start_tick > all_notes[target_idx].start_tick) {
        target_idx = idx;
      }
    }
    if (target_idx == all_notes.size())
      continue;

    int pitch = nearestPedalPitchForPc(subdominant_pc, ped_lo, ped_hi, all_notes[target_idx].pitch);
    if (pitch < 0 || pitch == static_cast<int>(all_notes[target_idx].pitch)) {
      continue;
    }
    all_notes[target_idx].pitch = static_cast<uint8_t>(pitch);
    all_notes[target_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    ++repairs;
  }

  return repairs;
}

static int retargetLateHomeEpisodeRaisedFourthPedal(std::vector<NoteEvent>& all_notes,
                                                    const FugueConfig& config,
                                                    const FuguePlan& plan, uint8_t num_voices) {
  (void)plan;
  if (config.is_minor || num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  int tonic_pc = static_cast<int>(config.key) % 12;
  int raised_fourth_pc = (tonic_pc + interval::kTritone) % 12;
  int fourth_pc = (tonic_pc + interval::kPerfect4th) % 12;

  auto strong_beat_dissonance_count = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick note_end = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < note_end; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (const auto& other : all_notes) {
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(pitch - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper)
          ++count;
      }
    }
    return count;
  };

  int repairs = 0;
  for (auto& note : all_notes) {
    if (note.voice != pedal_voice || note.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    if (note.start_tick < kTicksPerBar * 23)
      continue;
    if (getPitchClass(note.pitch) != raised_fourth_pc)
      continue;

    int target = nearestPedalPitchForPc(fourth_pc, ped_lo, ped_hi, note.pitch);
    if (target < 0 || target == static_cast<int>(note.pitch))
      continue;
    int old_bad = strong_beat_dissonance_count(note, note.pitch);
    int new_bad = strong_beat_dissonance_count(note, target);
    if (new_bad > old_bad)
      continue;

    note.pitch = static_cast<uint8_t>(target);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }

  return repairs;
}

static int retargetMiddleEpisodeRaisedFourthPedal(std::vector<NoteEvent>& all_notes,
                                                  const FugueConfig& config, uint8_t num_voices) {
  if (config.is_minor || num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  int tonic_pc = static_cast<int>(config.key) % 12;
  int raised_fourth_pc = (tonic_pc + interval::kTritone) % 12;
  int submediant_pc = (tonic_pc + 9) % 12;

  auto strong_beat_dissonance_count = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick note_end = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < note_end; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (const auto& other : all_notes) {
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(pitch - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper)
          ++count;
      }
    }
    return count;
  };

  int repairs = 0;
  for (auto& note : all_notes) {
    if (note.voice != pedal_voice || note.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    if (note.start_tick < kTicksPerBar * 17 || note.start_tick >= kTicksPerBar * 21) {
      continue;
    }
    if (getPitchClass(note.pitch) != raised_fourth_pc)
      continue;

    int target = nearestPedalPitchForPc(submediant_pc, ped_lo, ped_hi, note.pitch);
    if (target < 0 || target == static_cast<int>(note.pitch))
      continue;
    int old_bad = strong_beat_dissonance_count(note, note.pitch);
    int new_bad = strong_beat_dissonance_count(note, target);
    if (new_bad > old_bad)
      continue;

    note.pitch = static_cast<uint8_t>(target);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }

  return repairs;
}

static int retargetEarlyEpisodeRaisedFourthPedal(std::vector<NoteEvent>& all_notes,
                                                 const FugueConfig& config, uint8_t num_voices) {
  if (config.is_minor || num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto [ped_lo, ped_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  int tonic_pc = static_cast<int>(config.key) % 12;
  int raised_fourth_pc = (tonic_pc + interval::kTritone) % 12;
  int fourth_pc = (tonic_pc + interval::kPerfect4th) % 12;

  auto strong_beat_dissonance_count = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick note_end = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < note_end; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (const auto& other : all_notes) {
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(pitch - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper)
          ++count;
      }
    }
    return count;
  };

  int repairs = 0;
  for (auto& note : all_notes) {
    if (note.voice != pedal_voice || note.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    if (note.start_tick < kTicksPerBar * 13 || note.start_tick >= kTicksPerBar * 15) {
      continue;
    }
    if (getPitchClass(note.pitch) != raised_fourth_pc)
      continue;

    int target = nearestPedalPitchForPc(fourth_pc, ped_lo, ped_hi, note.pitch);
    if (target < 0 || target == static_cast<int>(note.pitch))
      continue;
    int old_bad = strong_beat_dissonance_count(note, note.pitch);
    int new_bad = strong_beat_dissonance_count(note, target);
    if (new_bad > old_bad)
      continue;

    note.pitch = static_cast<uint8_t>(target);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }

  return repairs;
}

static int addOpeningTonicPedalSupport(std::vector<NoteEvent>& all_notes, const FugueConfig& config,
                                       uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  for (const auto& note : all_notes) {
    if (note.voice != pedal_voice)
      continue;
    if (note.start_tick < kTicksPerBar && note.start_tick + note.duration > 0) {
      return 0;
    }
  }

  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  auto support = generatePedalPoint(tonic, 0, kTicksPerBar, pedal_voice);
  for (auto& note : support) {
    note.source = BachNoteSource::PedalPoint;
  }
  all_notes.insert(all_notes.end(), support.begin(), support.end());
  return static_cast<int>(support.size());
}

static uint32_t activeVoiceCountAt(const std::vector<NoteEvent>& notes, Tick tick,
                                   uint8_t num_voices) {
  std::array<bool, 8> active{};
  uint32_t count = 0;
  for (const auto& note : notes) {
    if (note.voice >= active.size() || note.voice >= num_voices)
      continue;
    if (note.start_tick <= tick && tick < note.start_tick + note.duration && !active[note.voice]) {
      active[note.voice] = true;
      ++count;
    }
  }
  return count;
}

static bool barNeedsTextureSupport(const std::vector<NoteEvent>& notes, Tick bar_start,
                                   uint8_t num_voices) {
  uint32_t sum = 0;
  uint32_t max_active = 0;
  for (int beat = 0; beat < 4; ++beat) {
    uint32_t active =
        activeVoiceCountAt(notes, bar_start + static_cast<Tick>(beat) * kTicksPerBeat, num_voices);
    sum += active;
    max_active = std::max(max_active, active);
  }
  double average = static_cast<double>(sum) / 4.0;
  return average < 1.5 || max_active < 2;
}

static bool supportTextureCandidateIsSafe(const std::vector<NoteEvent>& notes, Tick tick,
                                          VoiceId voice, uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;

  uint8_t lowest = pitch;
  for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
    if (other_voice == voice)
      continue;
    const NoteEvent* other = soundingNoteAt(notes, other_voice, tick);
    if (other == nullptr)
      continue;
    lowest = std::min(lowest, other->pitch);
  }

  for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
    if (other_voice == voice)
      continue;
    const NoteEvent* other = soundingNoteAt(notes, other_voice, tick);
    if (other == nullptr)
      continue;

    if (other_voice < voice && pitch >= other->pitch)
      return false;
    if (other_voice > voice && pitch <= other->pitch)
      return false;

    int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other->pitch));
    if (diff < 3)
      return false;
    int simple = interval_util::compoundToSimple(diff);
    bool consonant = interval_util::isConsonance(simple);
    if (!consonant && num_voices >= 3 && simple == interval::kPerfect4th) {
      uint8_t lower = std::min(pitch, other->pitch);
      consonant = lower > lowest;
    }
    if (!consonant)
      return false;
  }

  return true;
}

static uint8_t chooseTextureSupportPitch(const std::vector<NoteEvent>& notes,
                                         const HarmonicEvent& event, Tick tick, VoiceId voice,
                                         uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int center = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
  int previous_pitch = -1;
  int next_pitch = -1;
  Tick next_tick = 0;
  for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
    if (it->voice == voice && it->start_tick < tick) {
      previous_pitch = static_cast<int>(it->pitch);
      break;
    }
  }
  for (const auto& note : notes) {
    if (note.voice != voice || note.start_tick <= tick)
      continue;
    if (next_pitch < 0 || note.start_tick < next_tick) {
      next_pitch = static_cast<int>(note.pitch);
      next_tick = note.start_tick;
    }
  }

  int best_pitch = -1;
  int best_cost = INT_MAX;
  for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
    uint8_t pitch = static_cast<uint8_t>(cand);
    if (!scale_util::isScaleTone(pitch, event.key, scale))
      continue;
    if (!isChordTone(pitch, event))
      continue;
    if (!supportTextureCandidateIsSafe(notes, tick, voice, pitch, num_voices)) {
      continue;
    }

    int cost = std::abs(cand - center) * 2;
    if (previous_pitch >= 0) {
      int leap = std::abs(cand - previous_pitch);
      if (leap > 7)
        continue;
      cost += leap * 4;
    }
    if (next_pitch >= 0 && next_tick - tick <= duration::kHalfNote) {
      int leap = std::abs(cand - next_pitch);
      if (leap > 7)
        continue;
      cost += leap * 4;
    }
    if (cost < best_cost) {
      best_cost = cost;
      best_pitch = cand;
    }
  }

  return best_pitch >= 0 ? static_cast<uint8_t>(best_pitch) : 0;
}

static bool supportTextureCandidateIsSafeForSpan(const std::vector<NoteEvent>& notes, Tick start,
                                                 Tick end, VoiceId voice, uint8_t pitch,
                                                 uint8_t num_voices) {
  for (Tick tick = start; tick < end; tick += duration::kSixteenthNote) {
    if (!supportTextureCandidateIsSafe(notes, tick, voice, pitch, num_voices)) {
      return false;
    }
  }
  return true;
}

static uint8_t chooseTextureSupportPitchForSpan(const std::vector<NoteEvent>& notes,
                                                const HarmonicEvent& event, Tick start, Tick end,
                                                VoiceId voice, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int center = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
  int previous_pitch = -1;
  int next_pitch = -1;
  Tick next_tick = 0;
  for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
    if (it->voice == voice && it->start_tick < start) {
      previous_pitch = static_cast<int>(it->pitch);
      break;
    }
  }
  for (const auto& note : notes) {
    if (note.voice != voice || note.start_tick <= start)
      continue;
    if (next_pitch < 0 || note.start_tick < next_tick) {
      next_pitch = static_cast<int>(note.pitch);
      next_tick = note.start_tick;
    }
  }

  int best_pitch = -1;
  int best_cost = INT_MAX;
  for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
    uint8_t pitch = static_cast<uint8_t>(cand);
    if (!scale_util::isScaleTone(pitch, event.key, scale))
      continue;
    if (!isChordTone(pitch, event))
      continue;
    if (!supportTextureCandidateIsSafeForSpan(notes, start, end, voice, pitch, num_voices)) {
      continue;
    }

    int cost = std::abs(cand - center) * 2;
    if (previous_pitch >= 0) {
      int leap = std::abs(cand - previous_pitch);
      if (leap > 7)
        continue;
      cost += leap * 4;
    }
    if (next_pitch >= 0 && next_tick - start <= duration::kHalfNote) {
      int leap = std::abs(cand - next_pitch);
      if (leap > 7)
        continue;
      cost += leap * 4;
    }
    if (cost < best_cost) {
      best_cost = cost;
      best_pitch = cand;
    }
  }

  return best_pitch >= 0 ? static_cast<uint8_t>(best_pitch) : 0;
}

static int addThematicEpisodeTextureSupport(std::vector<NoteEvent>& all_notes,
                                            const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  std::vector<NoteEvent> additions;
  std::vector<NoteEvent> working = all_notes;
  constexpr int kMaxInsertions = 96;

  auto next_note_tick = [&](VoiceId voice, Tick tick, Tick fallback) {
    Tick next = fallback;
    for (const auto& note : working) {
      if (note.voice == voice && note.start_tick > tick) {
        next = std::min(next, note.start_tick);
      }
    }
    return next;
  };
  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode)
      continue;
    for (Tick tick = section.start_tick; tick < section.end_tick; tick += kTicksPerBeat) {
      Tick bar_start = (tick / kTicksPerBar) * kTicksPerBar;
      if (!barNeedsTextureSupport(working, bar_start, num_voices))
        continue;
      while (activeVoiceCountAt(working, tick, num_voices) < 2 &&
             static_cast<int>(additions.size()) < kMaxInsertions) {
        std::array<VoiceId, 5> voice_order = {0, 2, 3, 1, 4};
        bool inserted = false;
        for (VoiceId voice : voice_order) {
          if (voice >= num_voices)
            continue;
          if (soundingNoteAt(working, voice, tick) != nullptr)
            continue;

          const HarmonicEvent& event = plan.detailed_timeline.getAt(tick);
          uint8_t pitch = chooseTextureSupportPitch(working, event, tick, voice, num_voices);
          if (pitch == 0)
            continue;

          Tick duration = std::min<Tick>(kTicksPerBeat, section.end_tick - tick);
          duration = std::min(duration, next_note_tick(voice, tick, section.end_tick) - tick);
          if (duration < duration::kEighthNote)
            continue;

          NoteEvent support;
          support.start_tick = tick;
          support.duration = duration;
          support.pitch = pitch;
          support.velocity = kOrganVelocity;
          support.voice = voice;
          support.source = BachNoteSource::EpisodeMaterial;
          additions.push_back(support);
          working.push_back(support);
          inserted = true;
          break;
        }
        if (!inserted)
          break;
      }
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static int addListeningHotspotTextureSupport(std::vector<NoteEvent>& all_notes,
                                             const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices < 3 || plan.detailed_timeline.size() == 0)
    return 0;

  constexpr int kHotspotBars[] = {9, 14, 18, 23};
  constexpr int kMaxInsertions = 16;
  std::vector<NoteEvent> additions;
  std::vector<NoteEvent> working = all_notes;

  auto next_note_tick = [&](VoiceId voice, Tick tick, Tick fallback) {
    Tick next = fallback;
    for (const auto& note : working) {
      if (note.voice == voice && note.start_tick > tick) {
        next = std::min(next, note.start_tick);
      }
    }
    return next;
  };

  for (int bar : kHotspotBars) {
    Tick bar_start = static_cast<Tick>(bar - 1) * kTicksPerBar;
    if (bar_start >= plan.estimated_duration)
      continue;
    Tick bar_end = std::min<Tick>(bar_start + kTicksPerBar, plan.estimated_duration);
    uint32_t active_sum = 0;
    for (Tick sample = bar_start; sample < bar_end; sample += kTicksPerBeat) {
      active_sum += activeVoiceCountAt(working, sample, num_voices);
    }
    double active_average = static_cast<double>(active_sum) / 4.0;
    if (active_average >= 2.0)
      continue;

    for (Tick tick = bar_start;
         tick < bar_end && static_cast<int>(additions.size()) < kMaxInsertions;
         tick += kTicksPerBeat) {
      while (activeVoiceCountAt(working, tick, num_voices) < 2 &&
             static_cast<int>(additions.size()) < kMaxInsertions) {
        std::array<VoiceId, 5> voice_order = {1, 2, 3, 0, 4};
        bool inserted = false;
        for (VoiceId voice : voice_order) {
          if (voice >= num_voices)
            continue;
          if (soundingNoteAt(working, voice, tick) != nullptr)
            continue;

          const HarmonicEvent& event = plan.detailed_timeline.getAt(tick);
          Tick duration = std::min<Tick>(kTicksPerBeat, bar_end - tick);
          duration = std::min(duration, next_note_tick(voice, tick, bar_end) - tick);
          if (duration < duration::kEighthNote)
            continue;

          NoteEvent support;
          support.start_tick = tick;
          support.duration = duration;
          support.pitch = chooseTextureSupportPitchForSpan(working, event, tick, tick + duration,
                                                           voice, num_voices);
          if (support.pitch == 0)
            continue;
          support.velocity = kOrganVelocity;
          support.voice = voice;
          support.source = BachNoteSource::EpisodeMaterial;
          additions.push_back(support);
          working.push_back(support);
          inserted = true;
          break;
        }
        if (!inserted)
          break;
      }
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static Tick fugueSecondsToTicks(double seconds, uint16_t bpm) {
  if (seconds <= 0.0 || bpm == 0)
    return 0;
  const double ticks =
      seconds * static_cast<double>(bpm) * static_cast<double>(kTicksPerBeat) / 60.0;
  return static_cast<Tick>(ticks + 0.5);
}

static int addCriticWindowTextureSupport(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                         const FugueConfig& config, uint8_t num_voices) {
  if (num_voices < 3 || plan.detailed_timeline.size() == 0)
    return 0;

  const Tick window_start = fugueSecondsToTicks(18.0, config.bpm);
  const Tick window_end =
      std::min<Tick>(fugueSecondsToTicks(19.0, config.bpm), plan.estimated_duration);
  if (window_start >= window_end)
    return 0;

  constexpr int kMaxInsertions = 8;
  std::vector<NoteEvent> additions;
  std::vector<NoteEvent> working = all_notes;

  auto next_note_tick = [&](VoiceId voice, Tick tick, Tick fallback) {
    Tick next = fallback;
    for (const auto& note : working) {
      if (note.voice == voice && note.start_tick > tick) {
        next = std::min(next, note.start_tick);
      }
    }
    return next;
  };
  auto segment_is_free = [&](VoiceId voice, Tick start, Tick end) {
    for (const auto& note : working) {
      if (note.voice != voice)
        continue;
      if (note.start_tick < end && note.start_tick + note.duration > start) {
        return false;
      }
    }
    return true;
  };

  for (Tick sample = window_start;
       sample < window_end && static_cast<int>(additions.size()) < kMaxInsertions;
       sample += duration::kSixteenthNote) {
    while (activeVoiceCountAt(working, sample, num_voices) < 2 &&
           static_cast<int>(additions.size()) < kMaxInsertions) {
      Tick start = (sample / duration::kSixteenthNote) * duration::kSixteenthNote;
      if (start < window_start)
        start = sample;
      Tick duration = std::min<Tick>(duration::kQuarterNote, window_end - start);
      if (duration < duration::kEighthNote)
        break;

      std::array<VoiceId, 5> voice_order = {1, 0, 3, 2, 4};
      bool inserted = false;
      for (VoiceId voice : voice_order) {
        if (voice >= num_voices)
          continue;
        if (soundingNoteAt(working, voice, sample) != nullptr)
          continue;
        Tick voice_end = std::min<Tick>(start + duration, next_note_tick(voice, start, window_end));
        if (voice_end - start < duration::kEighthNote)
          continue;
        if (!segment_is_free(voice, start, voice_end))
          continue;

        const HarmonicEvent& event = plan.detailed_timeline.getAt(sample);
        uint8_t pitch =
            chooseTextureSupportPitchForSpan(working, event, start, voice_end, voice, num_voices);
        if (pitch == 0)
          continue;

        NoteEvent support;
        support.start_tick = start;
        support.duration = voice_end - start;
        support.pitch = pitch;
        support.velocity = kOrganVelocity;
        support.voice = voice;
        support.source = BachNoteSource::EpisodeMaterial;
        additions.push_back(support);
        working.push_back(support);
        inserted = true;
        break;
      }
      if (!inserted)
        break;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static int alignListeningHotspotNearBeatEntries(std::vector<NoteEvent>& all_notes,
                                                uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr int kHotspotBars[] = {9, 14, 18, 23};
  constexpr Tick kMaxDelay = duration::kThirtySecondNote;

  auto is_episode_entry = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto is_hotspot_beat = [&](Tick beat) {
    int bar = static_cast<int>(beat / kTicksPerBar) + 1;
    for (int hotspot_bar : kHotspotBars) {
      if (bar == hotspot_bar)
        return true;
    }
    return false;
  };
  auto voice_free_between = [&](VoiceId voice, Tick start, Tick end, size_t skip_idx) {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == skip_idx)
        continue;
      const auto& other = all_notes[idx];
      if (other.voice != voice)
        continue;
      if (other.start_tick < end && other.start_tick + other.duration > start) {
        return false;
      }
    }
    return true;
  };

  int aligned = 0;
  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.voice < rhs.voice;
  });

  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    auto& note = all_notes[idx];
    if (!is_episode_entry(note.source))
      continue;
    Tick beat = (note.start_tick / kTicksPerBeat) * kTicksPerBeat;
    if (note.start_tick == beat || note.start_tick - beat > kMaxDelay)
      continue;
    if (!is_hotspot_beat(beat))
      continue;
    if (activeVoiceCountAt(all_notes, beat, num_voices) >= 2)
      continue;
    if (!voice_free_between(note.voice, beat, note.start_tick, idx))
      continue;
    if (!supportTextureCandidateIsSafe(all_notes, beat, note.voice, note.pitch, num_voices)) {
      continue;
    }

    Tick delta = note.start_tick - beat;
    note.start_tick = beat;
    note.duration += delta;
    ++aligned;
  }

  return aligned;
}

static int extendListeningHotspotBeatSustains(std::vector<NoteEvent>& all_notes,
                                              uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr int kHotspotBars[] = {9, 14, 18, 23};
  constexpr Tick kMaxExtension = duration::kThirtySecondNote;
  auto is_hotspot_beat = [&](Tick beat) {
    int bar = static_cast<int>(beat / kTicksPerBar) + 1;
    for (int hotspot_bar : kHotspotBars) {
      if (bar == hotspot_bar)
        return true;
    }
    return false;
  };
  auto is_flexible_support = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::CadenceApproach;
  };
  auto voice_free_between = [&](VoiceId voice, Tick start, Tick end, size_t skip_idx) {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == skip_idx)
        continue;
      const auto& other = all_notes[idx];
      if (other.voice != voice)
        continue;
      if (other.start_tick < end && other.start_tick + other.duration > start) {
        return false;
      }
    }
    return true;
  };
  auto next_entry_tick = [&](Tick beat) {
    Tick next = beat + kMaxExtension + 1;
    for (const auto& note : all_notes) {
      if (!is_flexible_support(note.source))
        continue;
      if (note.start_tick <= beat)
        continue;
      if (note.start_tick - beat <= kMaxExtension) {
        next = std::min(next, note.start_tick);
      }
    }
    return next <= beat + kMaxExtension ? next : beat + kMaxExtension;
  };

  int extended = 0;
  for (int bar : kHotspotBars) {
    Tick bar_start = static_cast<Tick>(bar - 1) * kTicksPerBar;
    uint32_t active_sum = 0;
    for (Tick sample = bar_start; sample < bar_start + kTicksPerBar; sample += kTicksPerBeat) {
      active_sum += activeVoiceCountAt(all_notes, sample, num_voices);
    }
    if (static_cast<double>(active_sum) / 4.0 >= 2.0)
      continue;

    for (Tick beat = bar_start; beat < bar_start + kTicksPerBar; beat += kTicksPerBeat) {
      if (!is_hotspot_beat(beat))
        continue;
      if (activeVoiceCountAt(all_notes, beat, num_voices) >= 2)
        continue;
      bool beat_has_non_bass_entry = false;
      for (const auto& note : all_notes) {
        if (!is_flexible_support(note.source))
          continue;
        if (note.start_tick != beat)
          continue;
        if (note.voice != num_voices - 1) {
          beat_has_non_bass_entry = true;
          break;
        }
      }
      if (bar != 23 && beat_has_non_bass_entry)
        continue;
      Tick target_end = next_entry_tick(beat);
      if (target_end <= beat)
        continue;

      size_t best_idx = all_notes.size();
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        const auto& note = all_notes[idx];
        if (!is_flexible_support(note.source))
          continue;
        if (note.start_tick + note.duration != beat)
          continue;
        if (!voice_free_between(note.voice, beat, target_end, idx))
          continue;
        if (!supportTextureCandidateIsSafeForSpan(all_notes, beat, target_end, note.voice,
                                                  note.pitch, num_voices)) {
          continue;
        }
        if (best_idx == all_notes.size() || all_notes[idx].voice > all_notes[best_idx].voice) {
          best_idx = idx;
        }
      }
      if (best_idx == all_notes.size())
        continue;
      all_notes[best_idx].duration += target_end - beat;
      ++extended;
    }
  }

  return extended;
}

static int smoothBassFalseEntryRegister(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  if (num_voices < 4)
    return 0;
  const VoiceId bass_voice = static_cast<VoiceId>(num_voices - 1);

  std::vector<const NoteEvent*> bass_notes;
  for (const auto& note : all_notes) {
    if (note.voice == bass_voice)
      bass_notes.push_back(&note);
  }
  std::sort(bass_notes.begin(), bass_notes.end(), [](const NoteEvent* lhs, const NoteEvent* rhs) {
    if (lhs->start_tick != rhs->start_tick) {
      return lhs->start_tick < rhs->start_tick;
    }
    return lhs->pitch < rhs->pitch;
  });
  uint32_t motion_intervals = 0;
  uint32_t structural_intervals = 0;
  uint32_t large_leaps = 0;
  uint32_t max_leap = 0;
  const NoteEvent* previous_bass = nullptr;
  for (const NoteEvent* note : bass_notes) {
    if (previous_bass != nullptr) {
      Tick previous_end = previous_bass->start_tick + previous_bass->duration;
      Tick gap = note->start_tick > previous_end ? note->start_tick - previous_end : 0;
      uint32_t leap = static_cast<uint32_t>(absoluteInterval(previous_bass->pitch, note->pitch));
      if (gap <= kTicksPerBar && leap > 0) {
        ++motion_intervals;
        max_leap = std::max(max_leap, leap);
        if (leap <= interval::kPerfect4th || leap == interval::kPerfect5th ||
            leap == interval::kOctave) {
          ++structural_intervals;
        }
        if (leap > interval::kOctave)
          ++large_leaps;
      }
    }
    previous_bass = note;
  }
  double structural_ratio = motion_intervals > 0 ? static_cast<double>(structural_intervals) /
                                                       static_cast<double>(motion_intervals)
                                                 : 1.0;
  if (motion_intervals >= 8 && structural_ratio >= 0.80 && large_leaps <= 1 &&
      max_leap <= interval::kOctave + interval::kMajor2nd) {
    return 0;
  }

  auto neighboring_bass_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != bass_voice)
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };
  auto span_safe = [&](const NoteEvent& note, uint8_t pitch) {
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (pitch < lo || pitch > hi)
      return false;
    for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
         tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (pitch >= other->pitch)
          return false;
        int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other->pitch));
        if (diff == 0)
          return false;
        int simple = interval_util::compoundToSimple(diff);
        if (simple == 0)
          continue;
        if (!interval_util::isConsonance(simple))
          return false;
      }
    }
    return true;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (note.voice != bass_voice) {
      continue;
    }
    bool flexible_bass_source = note.source == BachNoteSource::FalseEntry ||
                                note.source == BachNoteSource::CadenceApproach ||
                                note.source == BachNoteSource::EpisodeMaterial;
    if (!flexible_bass_source)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    bool has_prev = neighboring_bass_pitch(note, true, &prev_pitch);
    bool has_next = neighboring_bass_pitch(note, false, &next_pitch);
    if (!has_prev && !has_next)
      continue;
    int old_pitch = static_cast<int>(note.pitch);
    bool has_large_boundary = (has_prev && std::abs(old_pitch - prev_pitch) > interval::kOctave) ||
                              (has_next && std::abs(old_pitch - next_pitch) > interval::kOctave);
    if (note.source != BachNoteSource::FalseEntry && !has_large_boundary) {
      continue;
    }

    auto score_for = [&](int pitch) {
      int score = 0;
      if (has_prev) {
        int leap = std::abs(pitch - prev_pitch);
        score += leap * 10;
        if (leap > interval::kOctave)
          score += 1000 + leap * 20;
      }
      if (has_next) {
        int leap = std::abs(pitch - next_pitch);
        score += leap * 10;
        if (leap > interval::kOctave)
          score += 1000 + leap * 20;
      }
      return score;
    };

    int best_pitch = old_pitch;
    int best_score = score_for(old_pitch);
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      if (cand == old_pitch)
        continue;
      if (!span_safe(note, static_cast<uint8_t>(cand)))
        continue;
      int score = score_for(cand) + std::abs(cand - old_pitch);
      if (score < best_score) {
        best_score = score;
        best_pitch = cand;
      }
    }
    if (best_pitch != old_pitch) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &=
          static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                 static_cast<uint8_t>(NoteModifiedBy::LeapResolution)));
      ++shaped;
      continue;
    }

    if (note.source == BachNoteSource::CadenceApproach &&
        (note.modified_by & static_cast<uint8_t>(NoteModifiedBy::LeapResolution)) != 0) {
      auto bass_cadence_safe = [&](int cand) {
        auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
        if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi))
          return false;
        for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
             tick += duration::kSixteenthNote) {
          for (VoiceId voice = 0; voice < num_voices; ++voice) {
            if (voice == note.voice)
              continue;
            const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
            if (other == nullptr)
              continue;
            if (cand >= static_cast<int>(other->pitch))
              return false;
            int simple = std::abs(cand - static_cast<int>(other->pitch)) % 12;
            bool consonant = simple == 0 || simple == 3 || simple == 4 || simple == 5 ||
                             simple == 7 || simple == 8 || simple == 9;
            if (!consonant)
              return false;
          }
        }
        return true;
      };
      int fallback_pitch = old_pitch;
      int fallback_score = score_for(old_pitch);
      auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        if (!bass_cadence_safe(cand))
          continue;
        int score = score_for(cand) + std::abs(cand - old_pitch);
        if (score < fallback_score) {
          fallback_score = score;
          fallback_pitch = cand;
        }
      }
      if (fallback_pitch != old_pitch) {
        note.pitch = static_cast<uint8_t>(fallback_pitch);
        note.modified_by &=
            static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
        ++shaped;
      }
    }
  }

  return shaped;
}

static int retargetFlexibleBassStrongBeatChordTones(std::vector<NoteEvent>& all_notes,
                                                    const HarmonicTimeline& timeline,
                                                    uint8_t num_voices) {
  if (num_voices < 4 || timeline.size() == 0)
    return 0;
  const VoiceId bass_voice = static_cast<VoiceId>(num_voices - 1);

  auto neighboring_bass_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != bass_voice)
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  auto vertical_badness = [&](const NoteEvent& note, int pitch) {
    int bad = 0;
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, note.start_tick);
      if (other == nullptr)
        continue;
      if (pitch >= static_cast<int>(other->pitch))
        return 1000;
      int diff = std::abs(pitch - static_cast<int>(other->pitch));
      if (diff == 0)
        return 1000;
      int simple = interval_util::compoundToSimple(diff);
      if (!interval_util::isConsonance(simple))
        ++bad;
    }
    return bad;
  };

  int changed = 0;
  for (auto& note : all_notes) {
    if (note.voice != bass_voice)
      continue;
    if (!isStrongBeatInBar(note.start_tick))
      continue;
    bool flexible_bass_source = note.source == BachNoteSource::EpisodeMaterial ||
                                note.source == BachNoteSource::CadenceApproach;
    if (!flexible_bass_source)
      continue;

    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    if (isChordTone(note.pitch, event))
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    bool has_prev = neighboring_bass_pitch(note, true, &prev_pitch);
    bool has_next = neighboring_bass_pitch(note, false, &next_pitch);
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int old_pitch = static_cast<int>(note.pitch);
    int old_bad = vertical_badness(note, old_pitch);
    int best_pitch = old_pitch;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!isChordTone(cand_u8, event))
        continue;
      int bad = vertical_badness(note, cand);
      if (bad >= 1000)
        continue;
      if (bad > old_bad)
        continue;
      int cost = bad * 200 + std::abs(cand - old_pitch) * 4;
      if (has_prev) {
        int leap = std::abs(cand - prev_pitch);
        cost += leap * 10;
        if (leap > interval::kOctave)
          cost += 400;
      }
      if (has_next) {
        int leap = std::abs(cand - next_pitch);
        cost += leap * 12;
        if (leap > interval::kOctave)
          cost += 500;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }
    if (best_pitch != old_pitch) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
      ++changed;
    }
  }
  return changed;
}

static int enforceCodaSubjectHeadShape(std::vector<NoteEvent>& all_notes,
                                       const FugueMaterial& material, const FugueConfig& config,
                                       uint8_t num_voices) {
  if (material.subject.notes.size() < 4 || num_voices < 3)
    return 0;

  Tick coda_start = std::numeric_limits<Tick>::max();
  for (const auto& note : all_notes) {
    if (note.source == BachNoteSource::Coda) {
      coda_start = std::min(coda_start, note.start_tick);
    }
  }
  if (coda_start == std::numeric_limits<Tick>::max())
    return 0;

  std::vector<size_t> head_idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda || note.voice != 0)
      continue;
    if (note.start_tick < coda_start || note.start_tick >= coda_start + kTicksPerBar) {
      continue;
    }
    head_idxs.push_back(idx);
  }
  std::sort(head_idxs.begin(), head_idxs.end(), [&](size_t lhs, size_t rhs) {
    if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    }
    return lhs < rhs;
  });
  if (head_idxs.size() < 4)
    return 0;

  int changed = 0;
  const int base_pitch = static_cast<int>(all_notes[head_idxs.front()].pitch);
  const uint8_t subject_origin = material.subject.notes.front().pitch;
  int existing_matches = 0;
  for (size_t pos = 1; pos < std::min<size_t>(head_idxs.size(), 5); ++pos) {
    int subject_interval = directedInterval(subject_origin, material.subject.notes[pos].pitch);
    int coda_interval =
        directedInterval(static_cast<uint8_t>(base_pitch), all_notes[head_idxs[pos]].pitch);
    if (subject_interval == coda_interval)
      ++existing_matches;
  }
  if (existing_matches >= 3)
    return 0;

  auto [upper_lo, upper_hi] = getFugueVoiceRange(0, num_voices);
  const size_t head_count = std::min<size_t>(head_idxs.size(), 5);
  for (size_t pos = 1; pos < head_count; ++pos) {
    int target = base_pitch + directedInterval(subject_origin, material.subject.notes[pos].pitch);
    uint8_t pitch = clampPitch(target, upper_lo, upper_hi);
    NoteEvent& note = all_notes[head_idxs[pos]];
    if (note.pitch != pitch) {
      note.pitch = pitch;
      ++changed;
    }
  }

  int tonic_pitch = static_cast<int>(kMidiC4) + static_cast<int>(config.key);
  int chord_third = config.is_minor ? 3 : 4;
  int chord_pcs[] = {
      getPitchClass(static_cast<uint8_t>(tonic_pitch)),
      (tonic_pitch + chord_third) % 12,
      (tonic_pitch + 7) % 12,
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda || note.voice == 0)
      continue;
    if (note.start_tick < coda_start || note.start_tick >= coda_start + kTicksPerBar) {
      continue;
    }
    bool unsafe = false;
    for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
         tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(note.pitch, *other)) {
          unsafe = true;
          break;
        }
      }
      if (unsafe)
        break;
    }
    if (!unsafe && supportTextureCandidateIsSafeForSpan(all_notes, note.start_tick,
                                                        note.start_tick + note.duration, note.voice,
                                                        note.pitch, num_voices)) {
      continue;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int best_pitch = -1;
    int best_cost = INT32_MAX;
    for (int pc : chord_pcs) {
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        if (getPitchClass(static_cast<uint8_t>(cand)) != pc)
          continue;
        if (!supportTextureCandidateIsSafeForSpan(all_notes, note.start_tick,
                                                  note.start_tick + note.duration, note.voice,
                                                  static_cast<uint8_t>(cand), num_voices)) {
          continue;
        }
        int cost = std::abs(cand - static_cast<int>(note.pitch));
        if (note.voice == num_voices - 1 &&
            getPitchClass(static_cast<uint8_t>(cand)) != chord_pcs[0]) {
          cost += 8;
        }
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand;
        }
      }
    }
    if (best_pitch >= 0 && best_pitch != static_cast<int>(note.pitch)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      ++changed;
    }
  }

  return changed;
}

static int recomposeListeningHotspotFlexibleClashes(std::vector<NoteEvent>& all_notes,
                                                    const FuguePlan& plan,
                                                    const HarmonicTimeline& output_timeline,
                                                    uint8_t num_voices,
                                                    bool only_failing_hotspots = false) {
  if (num_voices < 3)
    return 0;

  constexpr int kHotspotBars[] = {9, 14, 18, 23};
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_hotspot_tick = [&](Tick tick) {
    int bar = static_cast<int>(tick / kTicksPerBar) + 1;
    for (int hotspot_bar : kHotspotBars) {
      if (bar == hotspot_bar)
        return true;
    }
    return false;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto hotspot_bar_fails = [&](int bar) {
    Tick bar_start = static_cast<Tick>(bar - 1) * kTicksPerBar;
    Tick bar_end = bar_start + kTicksPerBar;
    int sample_count = 0;
    int hard_count = 0;
    for (Tick tick = bar_start; tick < bar_end; tick += duration::kSixteenthNote) {
      bool any_active_pair = false;
      bool hard = false;
      for (VoiceId lhs = 0; lhs < num_voices; ++lhs) {
        const NoteEvent* left = soundingNoteAt(all_notes, lhs, tick);
        if (left == nullptr)
          continue;
        for (VoiceId rhs = lhs + 1; rhs < num_voices; ++rhs) {
          const NoteEvent* right = soundingNoteAt(all_notes, rhs, tick);
          if (right == nullptr)
            continue;
          any_active_pair = true;
          if (hard_bad_against(left->pitch, *right))
            hard = true;
        }
      }
      if (!any_active_pair)
        continue;
      ++sample_count;
      if (hard)
        ++hard_count;
    }
    if (sample_count == 0)
      return false;
    double ratio = static_cast<double>(hard_count) / static_cast<double>(sample_count);
    return ratio > 0.25 || hard_count > 4;
  };
  auto should_process_hotspot_tick = [&](Tick tick) {
    if (!is_hotspot_tick(tick))
      return false;
    if (!only_failing_hotspots)
      return true;
    int bar = static_cast<int>(tick / kTicksPerBar) + 1;
    return hotspot_bar_fails(bar);
  };
  auto is_flexible_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::CadenceApproach || source == BachNoteSource::Countersubject;
  };
  auto mixed_minor_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto hard_bad_count_for = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      if (!is_hotspot_tick(tick))
        continue;
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(pitch, *other))
          ++count;
      }
    }
    return count;
  };
  auto candidate_safe_for_duration = [&](const NoteEvent& note, uint8_t pitch) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      if (!is_hotspot_tick(tick))
        continue;
      if (!supportTextureCandidateIsSafe(all_notes, tick, note.voice, pitch, num_voices)) {
        return false;
      }
    }
    return true;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_flexible_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int recomposed = 0;
  for (auto& note : all_notes) {
    if (!is_flexible_cell(note.source))
      continue;
    if (!should_process_hotspot_tick(note.start_tick))
      continue;

    int old_bad = hard_bad_count_for(note, note.pitch);
    if (old_bad == 0 && (note.modified_by & kPitchRepairMask) == 0)
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    const bool strong = note.start_tick % kTicksPerBeat == 0;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 84);
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      if (!mixed_minor_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_minor_scale_tone(pitch, output_event, output_scale) &&
          !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (strong && !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (!candidate_safe_for_duration(note, pitch))
        continue;

      int bad = hard_bad_count_for(note, cand);
      if (bad > old_bad)
        continue;
      int cost = bad * 1000 + std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 8;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 7;
      }
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 16;
      }
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && best_bad <= old_bad) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++recomposed;
    }
  }

  return recomposed;
}

static int splitListeningHotspotFlexibleClashCells(std::vector<NoteEvent>& all_notes,
                                                   const FuguePlan& plan,
                                                   const HarmonicTimeline& output_timeline,
                                                   uint8_t num_voices,
                                                   bool only_failing_hotspots = false) {
  if (num_voices < 3 || plan.detailed_timeline.size() == 0)
    return 0;

  constexpr int kHotspotBars[] = {9, 14, 18, 23};
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_hotspot_tick = [&](Tick tick) {
    int bar = static_cast<int>(tick / kTicksPerBar) + 1;
    for (int hotspot_bar : kHotspotBars) {
      if (bar == hotspot_bar)
        return true;
    }
    return false;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto hotspot_bar_fails = [&](int bar) {
    Tick bar_start = static_cast<Tick>(bar - 1) * kTicksPerBar;
    Tick bar_end = bar_start + kTicksPerBar;
    int sample_count = 0;
    int hard_count = 0;
    for (Tick tick = bar_start; tick < bar_end; tick += duration::kSixteenthNote) {
      bool any_active_pair = false;
      bool hard = false;
      for (VoiceId lhs = 0; lhs < num_voices; ++lhs) {
        const NoteEvent* left = soundingNoteAt(all_notes, lhs, tick);
        if (left == nullptr)
          continue;
        for (VoiceId rhs = lhs + 1; rhs < num_voices; ++rhs) {
          const NoteEvent* right = soundingNoteAt(all_notes, rhs, tick);
          if (right == nullptr)
            continue;
          any_active_pair = true;
          if (hard_bad_against(left->pitch, *right))
            hard = true;
        }
      }
      if (!any_active_pair)
        continue;
      ++sample_count;
      if (hard)
        ++hard_count;
    }
    if (sample_count == 0)
      return false;
    double ratio = static_cast<double>(hard_count) / static_cast<double>(sample_count);
    return ratio > 0.25 || hard_count > 4;
  };
  auto should_process_hotspot_tick = [&](Tick tick) {
    if (!is_hotspot_tick(tick))
      return false;
    if (!only_failing_hotspots)
      return true;
    int bar = static_cast<int>(tick / kTicksPerBar) + 1;
    return hotspot_bar_fails(bar);
  };
  auto is_flexible_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::CadenceApproach || source == BachNoteSource::Countersubject;
  };
  auto mixed_minor_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto hard_bad_count_for_span = [&](const NoteEvent& note, int pitch, Tick start, Tick end) {
    int count = 0;
    for (Tick tick = start; tick < end; tick += duration::kSixteenthNote) {
      if (!is_hotspot_tick(tick))
        continue;
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(pitch, *other))
          ++count;
      }
    }
    return count;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_flexible_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };
  auto choose_span_pitch = [&](const NoteEvent& note, Tick start, Tick end, int previous_pitch,
                               int next_pitch, int prior_segment_pitch, int* pitch_out) {
    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(start);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(start);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 84);

    int best_pitch = -1;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      bool tonal = mixed_minor_scale_tone(pitch, generation_event, generation_scale) ||
                   mixed_minor_scale_tone(pitch, output_event, output_scale) ||
                   isChordTone(pitch, generation_event) || isChordTone(pitch, output_event);
      if (!supportTextureCandidateIsSafeForSpan(all_notes, start, end, note.voice, pitch,
                                                num_voices)) {
        continue;
      }
      int bad = hard_bad_count_for_span(note, cand, start, end);
      if (bad > 0)
        continue;
      int cost = std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 20;
      }
      if (!tonal)
        cost += 80;
      if (start % kTicksPerBeat == 0 && !isChordTone(pitch, generation_event) &&
          !isChordTone(pitch, output_event)) {
        cost += 18;
      }
      if (previous_pitch >= 0) {
        int leap = std::abs(cand - previous_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 8;
      }
      if (prior_segment_pitch >= 0) {
        int leap = std::abs(cand - prior_segment_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 10;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 7;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }
    if (best_pitch < 0)
      return false;
    *pitch_out = best_pitch;
    return true;
  };

  int split = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const NoteEvent note = all_notes[idx];
    if (!is_flexible_cell(note.source))
      continue;
    if (!should_process_hotspot_tick(note.start_tick))
      continue;
    if (note.duration < duration::kQuarterNote)
      continue;
    Tick note_end = note.start_tick + note.duration;
    int old_bad = hard_bad_count_for_span(note, note.pitch, note.start_tick, note_end);
    if (old_bad == 0 && (note.modified_by & kPitchRepairMask) == 0)
      continue;

    std::vector<Tick> cuts{note.start_tick, note_end};
    for (const auto& other : all_notes) {
      if (other.voice == note.voice)
        continue;
      for (Tick cut : {other.start_tick, other.start_tick + other.duration}) {
        if (cut > note.start_tick && cut < note_end && is_hotspot_tick(cut)) {
          cuts.push_back(cut);
        }
      }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    if (cuts.size() < 3)
      continue;

    int previous_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &previous_pitch);
    neighboring_pitch(note, false, &next_pitch);

    std::vector<NoteEvent> pieces;
    int prior_segment_pitch = previous_pitch;
    bool ok = true;
    for (size_t cut_idx = 0; cut_idx + 1 < cuts.size(); ++cut_idx) {
      Tick start = cuts[cut_idx];
      Tick end = cuts[cut_idx + 1];
      if (end - start < duration::kEighthNote) {
        ok = false;
        break;
      }
      int segment_next = (cut_idx + 2 == cuts.size()) ? next_pitch : -1;
      int pitch = static_cast<int>(note.pitch);
      if (!choose_span_pitch(note, start, end, previous_pitch, segment_next, prior_segment_pitch,
                             &pitch)) {
        ok = false;
        break;
      }
      NoteEvent piece = note;
      piece.start_tick = start;
      piece.duration = end - start;
      piece.pitch = static_cast<uint8_t>(pitch);
      piece.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      pieces.push_back(piece);
      previous_pitch = pitch;
      prior_segment_pitch = pitch;
    }
    if (!ok || pieces.size() < 2)
      continue;

    int new_bad = 0;
    for (const auto& piece : pieces) {
      new_bad += hard_bad_count_for_span(piece, piece.pitch, piece.start_tick,
                                         piece.start_tick + piece.duration);
    }
    if (new_bad >= old_bad)
      continue;

    all_notes.erase(all_notes.begin() + static_cast<std::ptrdiff_t>(idx));
    all_notes.insert(all_notes.end(), pieces.begin(), pieces.end());
    split += static_cast<int>(pieces.size());
    if (idx > 0)
      --idx;
  }

  return split;
}

static int addEpisodeDialogueWindowSupport(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                           uint8_t num_voices) {
  if (num_voices < 3 || plan.detailed_timeline.size() == 0)
    return 0;

  struct WindowCounts {
    uint32_t motif_notes = 0;
    uint32_t dialogue_notes = 0;
  };
  constexpr Tick kWindowTicks = kTicksPerBar * 2;
  constexpr int kMaxInsertions = 32;

  auto is_motif = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::FalseEntry;
  };
  auto is_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SequenceNote || source == BachNoteSource::FalseEntry;
  };
  std::vector<NoteEvent> additions;
  std::vector<NoteEvent> working = all_notes;
  auto next_note_tick = [&](VoiceId voice, Tick tick, Tick fallback) {
    Tick next = fallback;
    for (const auto& note : working) {
      if (note.voice == voice && note.start_tick > tick) {
        next = std::min(next, note.start_tick);
      }
    }
    return next;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto choose_dialogue_pitch = [&](Tick tick, VoiceId voice) {
    const HarmonicEvent& event = plan.detailed_timeline.getAt(tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    if (voice == 0)
      hi = std::min<uint8_t>(hi, 82);

    int prev_pitch = -1;
    int next_pitch = -1;
    Tick prev_tick = 0;
    Tick next_tick = std::numeric_limits<Tick>::max();
    for (const auto& note : working) {
      if (note.voice != voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      if (note_end <= tick && note_end >= prev_tick) {
        prev_tick = note_end;
        prev_pitch = note.pitch;
      }
      if (note.start_tick > tick && note.start_tick < next_tick) {
        next_tick = note.start_tick;
        next_pitch = note.pitch;
      }
    }

    int best_pitch = -1;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!isChordTone(cand_u8, event) && !scale_util::isScaleTone(cand_u8, event.key, scale)) {
        continue;
      }
      if ((tick % kTicksPerBeat) == 0 && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && tick - prev_tick <= kTicksPerBar &&
          std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && next_tick - tick <= kTicksPerBar &&
          std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      bool rejected = false;
      int consonance_cost = 0;
      for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
        if (other_voice == voice)
          continue;
        const NoteEvent* other = soundingNoteAt(working, other_voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(cand, *other)) {
          rejected = true;
          break;
        }
        consonance_cost += std::abs(cand - static_cast<int>(other->pitch));
      }
      if (rejected)
        continue;

      int cost = consonance_cost;
      if (prev_pitch >= 0)
        cost += std::abs(cand - prev_pitch) * 6;
      if (next_pitch >= 0)
        cost += std::abs(cand - next_pitch) * 5;
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }
    return best_pitch >= 0 ? static_cast<uint8_t>(best_pitch) : uint8_t{0};
  };

  const Tick total_ticks = plan.estimated_duration > 0 ? plan.estimated_duration
                                                       : plan.detailed_timeline.totalDuration();
  const Tick dialogue_support_end = (total_ticks * 7) / 10;
  const size_t window_count =
      std::max<size_t>(1u, static_cast<size_t>((total_ticks + kWindowTicks - 1) / kWindowTicks));
  std::vector<WindowCounts> windows(window_count);
  for (const auto& note : all_notes) {
    size_t idx = static_cast<size_t>(note.start_tick / kWindowTicks);
    if (idx >= windows.size() || !is_motif(note.source))
      continue;
    ++windows[idx].motif_notes;
    if (is_dialogue(note.source)) {
      ++windows[idx].dialogue_notes;
    }
  }

  for (size_t idx = 0; idx < windows.size() && static_cast<int>(additions.size()) < kMaxInsertions;
       ++idx) {
    const auto& window = windows[idx];
    if (window.motif_notes < 6)
      continue;
    const uint32_t needed_dialogue = std::max<uint32_t>(1u, (window.motif_notes + 7u) / 8u);
    if (window.dialogue_notes >= needed_dialogue)
      continue;

    Tick window_start = static_cast<Tick>(idx) * kWindowTicks;
    if (window_start >= dialogue_support_end)
      continue;
    Tick window_end = std::min<Tick>(window_start + kWindowTicks, total_ticks);
    uint32_t inserted_here = 0;
    for (Tick tick = window_start;
         tick < window_end && window.dialogue_notes + inserted_here < needed_dialogue &&
         static_cast<int>(additions.size()) < kMaxInsertions;
         tick += kTicksPerBeat) {
      std::array<VoiceId, 5> voice_order = {0, 1, 2, 3, 4};
      for (VoiceId voice : voice_order) {
        if (voice >= num_voices)
          continue;
        if (soundingNoteAt(working, voice, tick) != nullptr)
          continue;
        uint8_t pitch = choose_dialogue_pitch(tick, voice);
        if (pitch == 0)
          continue;
        Tick duration = std::min<Tick>(kTicksPerBeat, window_end - tick);
        duration = std::min(duration, next_note_tick(voice, tick, window_end) - tick);
        if (duration < duration::kEighthNote)
          continue;

        NoteEvent support;
        support.start_tick = tick;
        support.duration = duration;
        support.pitch = pitch;
        support.velocity = kOrganVelocity;
        support.voice = voice;
        support.source = BachNoteSource::SequenceNote;
        additions.push_back(support);
        working.push_back(support);
        ++inserted_here;
        break;
      }
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static int precomposeEpisodeMaterialToTimeline(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan, uint8_t num_voices) {
  int changed = 0;

  auto is_episode_line_source = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto is_protected_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto protected_dialogue_bad_count = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick end_tick = note.start_tick + note.duration;
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick tick = first_beat; tick < end_tick; tick += kTicksPerBeat) {
      if (tick != note.start_tick)
        sample_ticks.push_back(tick);
    }

    for (Tick tick : sample_ticks) {
      for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
        if (other_voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
        if (other == nullptr || !is_protected_dialogue(other->source)) {
          continue;
        }
        if (is_hard_bad_against(pitch, *other))
          ++count;
      }
    }
    return count;
  };

  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode)
      continue;

    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      std::vector<size_t> ordered;
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        const auto& note = all_notes[idx];
        if (note.voice != voice)
          continue;
        if (!is_episode_line_source(note.source))
          continue;
        if (note.start_tick < section.start_tick || note.start_tick >= section.end_tick) {
          continue;
        }
        ordered.push_back(idx);
      }
      if (ordered.empty())
        continue;

      std::sort(ordered.begin(), ordered.end(), [&](size_t lhs, size_t rhs) {
        if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
          return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
        }
        return lhs < rhs;
      });

      auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
      for (size_t pos = 0; pos < ordered.size(); ++pos) {
        NoteEvent& note = all_notes[ordered[pos]];
        const HarmonicEvent& event = plan.detailed_timeline.getAt(note.start_tick);
        ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
        const bool strong = note.start_tick % kTicksPerBeat == 0;

        int prev_pitch = -1;
        int next_pitch = -1;
        if (pos > 0) {
          const auto& prev = all_notes[ordered[pos - 1]];
          if (note.start_tick <= prev.start_tick + prev.duration + kTicksPerBeat) {
            prev_pitch = static_cast<int>(prev.pitch);
          }
        } else {
          Tick best_prev_end = 0;
          for (const auto& previous : all_notes) {
            if (previous.voice != voice || previous.start_tick >= note.start_tick) {
              continue;
            }
            Tick previous_end = previous.start_tick + previous.duration;
            if (previous_end > note.start_tick || note.start_tick - previous_end > kTicksPerBar) {
              continue;
            }
            if (prev_pitch < 0 || previous_end >= best_prev_end) {
              best_prev_end = previous_end;
              prev_pitch = static_cast<int>(previous.pitch);
            }
          }
        }
        if (pos + 1 < ordered.size()) {
          const auto& next = all_notes[ordered[pos + 1]];
          if (next.start_tick <= note.start_tick + note.duration + kTicksPerBeat) {
            next_pitch = static_cast<int>(next.pitch);
          }
        } else {
          Tick best_next_start = std::numeric_limits<Tick>::max();
          Tick note_end = note.start_tick + note.duration;
          for (const auto& next : all_notes) {
            if (next.voice != voice || next.start_tick <= note.start_tick) {
              continue;
            }
            if (next.start_tick < note_end || next.start_tick - note_end > kTicksPerBar) {
              continue;
            }
            if (next.start_tick <= best_next_start) {
              best_next_start = next.start_tick;
              next_pitch = static_cast<int>(next.pitch);
            }
          }
        }

        const bool repeats_previous =
            prev_pitch >= 0 && note.pitch == static_cast<uint8_t>(prev_pitch);
        const bool needs_harmonic_shape = strong && !isChordTone(note.pitch, event);
        const bool needs_scale_shape = !scale_util::isScaleTone(note.pitch, event.key, scale);
        const int old_protected_bad = protected_dialogue_bad_count(note, note.pitch);
        const bool needs_protected_dialogue_shape = old_protected_bad > 0;
        const int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
        const bool needs_manual_iii_step = num_voices == 4 && voice == 2 && bar >= 18 &&
                                           bar <= 27 && prev_pitch >= 0 &&
                                           std::abs(static_cast<int>(note.pitch) - prev_pitch) > 4;
        if (!repeats_previous && !needs_harmonic_shape && !needs_scale_shape &&
            !needs_manual_iii_step && !needs_protected_dialogue_shape) {
          continue;
        }

        int best_pitch = -1;
        int best_protected_bad = old_protected_bad;
        int best_cost = INT_MAX;
        for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
          uint8_t pitch = static_cast<uint8_t>(cand);
          if (!scale_util::isScaleTone(pitch, event.key, scale))
            continue;
          if (strong && !isChordTone(pitch, event))
            continue;
          if (prev_pitch >= 0 && cand == prev_pitch)
            continue;
          if (next_pitch >= 0 && cand == next_pitch && repeats_previous)
            continue;
          if (!supportTextureCandidateIsSafe(all_notes, note.start_tick, voice, pitch,
                                             num_voices)) {
            continue;
          }

          int protected_bad = protected_dialogue_bad_count(note, cand);
          if (needs_protected_dialogue_shape && protected_bad > old_protected_bad) {
            continue;
          }

          int cost = protected_bad * 1200 + std::abs(cand - static_cast<int>(note.pitch)) * 3;
          if (prev_pitch >= 0) {
            int leap = std::abs(cand - prev_pitch);
            if (leap > interval::kOctave)
              continue;
            cost += leap * 4;
            if (leap > interval::kPerfect5th)
              cost += 40;
          }
          if (next_pitch >= 0) {
            int leap = std::abs(next_pitch - cand);
            if (leap > interval::kOctave)
              continue;
            cost += leap * 3;
            if (leap > interval::kPerfect5th)
              cost += 30;
          }
          if (isChordTone(pitch, event))
            cost -= 12;
          if (cand < static_cast<int>(note.pitch) && voice + 1 == num_voices) {
            cost -= 4;
          }

          if (protected_bad < best_protected_bad ||
              (protected_bad == best_protected_bad && cost < best_cost)) {
            best_protected_bad = protected_bad;
            best_cost = cost;
            best_pitch = cand;
          }
        }

        if (best_pitch >= 0 && best_pitch != static_cast<int>(note.pitch)) {
          note.pitch = static_cast<uint8_t>(best_pitch);
          ++changed;
        }
      }
    }
  }

  return changed;
}

static int acceptRecomposedEpisodeMaterial(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                           const HarmonicTimeline& output_timeline,
                                           uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto in_episode_section = [&](Tick tick) {
    for (const auto& section : plan.sections) {
      if (section.type != SectionType::Episode && section.type != SectionType::Stretto) {
        continue;
      }
      if (tick >= section.start_tick && tick < section.end_tick)
        return true;
    }
    return false;
  };

  auto nearby_voice_pitch = [&](VoiceId voice, Tick tick, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != voice || (other.source != BachNoteSource::EpisodeMaterial &&
                                   other.source != BachNoteSource::SequenceNote)) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= tick)
          continue;
        if (tick - (other.start_tick + other.duration) > kTicksPerBar * 2)
          continue;
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= tick)
          continue;
        if (other.start_tick - tick > kTicksPerBar * 2)
          continue;
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial &&
        note.source != BachNoteSource::SequenceNote) {
      continue;
    }
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (!in_episode_section(note.start_tick))
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool generation_scale_tone =
        scale_util::isScaleTone(note.pitch, generation_event.key, generation_scale) ||
        (generation_event.is_minor &&
         scale_util::isScaleTone(note.pitch, generation_event.key, ScaleType::NaturalMinor));
    bool output_scale_tone =
        scale_util::isScaleTone(note.pitch, output_event.key, output_scale) ||
        (output_event.is_minor &&
         scale_util::isScaleTone(note.pitch, output_event.key, ScaleType::NaturalMinor));
    if (!generation_scale_tone && !isChordTone(note.pitch, generation_event)) {
      continue;
    }
    if (!output_scale_tone && !isChordTone(note.pitch, output_event)) {
      continue;
    }
    if (note.start_tick % kTicksPerBeat == 0 && !isChordTone(note.pitch, generation_event) &&
        !isChordTone(note.pitch, output_event)) {
      bool weak_sequence_passing =
          note.source == BachNoteSource::SequenceNote &&
          (note.modified_by & static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)) != 0 &&
          !isStrongBeatInBar(note.start_tick);
      if (!weak_sequence_passing) {
        continue;
      }
    }
    if (!supportTextureCandidateIsSafe(all_notes, note.start_tick, note.voice, note.pitch,
                                       num_voices)) {
      continue;
    }

    int neighbor_pitch = -1;
    if (nearby_voice_pitch(note.voice, note.start_tick, true, &neighbor_pitch) &&
        std::abs(static_cast<int>(note.pitch) - neighbor_pitch) > interval::kOctave) {
      continue;
    }
    if (nearby_voice_pitch(note.voice, note.start_tick, false, &neighbor_pitch) &&
        std::abs(static_cast<int>(note.pitch) - neighbor_pitch) > interval::kOctave) {
      continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }
  return accepted;
}

static int acceptComposedDialogueIntentFlags(std::vector<NoteEvent>& all_notes,
                                             const FuguePlan& plan,
                                             const HarmonicTimeline& output_timeline,
                                             uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_dialogue_intent = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::SequenceNote;
  };

  auto nearby_voice_pitch = [&](VoiceId voice, Tick tick, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != voice || !is_dialogue_intent(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= tick)
          continue;
        if (tick - (other.start_tick + other.duration) > kTicksPerBar * 2)
          continue;
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= tick)
          continue;
        if (other.start_tick - tick > kTicksPerBar * 2)
          continue;
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (!is_dialogue_intent(note.source))
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool generation_scale_tone =
        scale_util::isScaleTone(note.pitch, generation_event.key, generation_scale) ||
        (generation_event.is_minor &&
         scale_util::isScaleTone(note.pitch, generation_event.key, ScaleType::NaturalMinor));
    bool output_scale_tone =
        scale_util::isScaleTone(note.pitch, output_event.key, output_scale) ||
        (output_event.is_minor &&
         scale_util::isScaleTone(note.pitch, output_event.key, ScaleType::NaturalMinor));
    if (!generation_scale_tone && !isChordTone(note.pitch, generation_event)) {
      continue;
    }
    if (!output_scale_tone && !isChordTone(note.pitch, output_event)) {
      continue;
    }
    if (note.start_tick % kTicksPerBeat == 0 && !isChordTone(note.pitch, generation_event) &&
        !isChordTone(note.pitch, output_event)) {
      continue;
    }
    if (!supportTextureCandidateIsSafe(all_notes, note.start_tick, note.voice, note.pitch,
                                       num_voices)) {
      continue;
    }

    int neighbor_pitch = -1;
    if (nearby_voice_pitch(note.voice, note.start_tick, true, &neighbor_pitch) &&
        std::abs(static_cast<int>(note.pitch) - neighbor_pitch) > interval::kPerfect5th) {
      continue;
    }
    if (nearby_voice_pitch(note.voice, note.start_tick, false, &neighbor_pitch)) {
      int next_leap = std::abs(static_cast<int>(note.pitch) - neighbor_pitch);
      int allowed_next_leap = interval::kPerfect5th;
      if (note.source == BachNoteSource::SequenceNote &&
          (note.modified_by & static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)) != 0) {
        allowed_next_leap = interval::kMinor6th;
      }
      if (next_leap > allowed_next_leap) {
        continue;
      }
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptComposedCodaIntentFlags(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  auto coda_note_is_safe = [&](const NoteEvent& note) {
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.pitch < lo || note.pitch > hi)
      return false;

    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::Coda) {
        continue;
      }
      if (other.start_tick < note.start_tick &&
          (prev == nullptr || other.start_tick > prev->start_tick)) {
        prev = &other;
      } else if (other.start_tick > note.start_tick &&
                 (next == nullptr || other.start_tick < next->start_tick)) {
        next = &other;
      }
    }
    if (prev != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev->pitch)) >
                               interval::kOctave) {
      return false;
    }
    if (next != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(next->pitch)) >
                               interval::kOctave) {
      return false;
    }

    Tick end_tick = note.start_tick + note.duration;
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (beat != note.start_tick)
        sample_ticks.push_back(beat);
    }

    for (Tick tick : sample_ticks) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;

        if (voice < note.voice && other->pitch <= note.pitch)
          return false;
        if (voice > note.voice && other->pitch >= note.pitch)
          return false;
        if (is_hard_bad_against(note.pitch, *other))
          return false;
      }
    }

    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (!coda_note_is_safe(note))
      continue;

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int shapeRepairedCodaVerticalCells(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_coda_hard_vertical = [&](const NoteEvent& note, int pitch) {
    Tick end_tick = note.start_tick + note.duration;
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (beat != note.start_tick)
        sample_ticks.push_back(beat);
    }
    for (Tick tick : sample_ticks) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr || other->source != BachNoteSource::Coda)
          continue;
        if (voice < note.voice && other->pitch <= pitch)
          return true;
        if (voice > note.voice && other->pitch >= pitch)
          return true;
        if (hard_bad_against(pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::Coda) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (!has_coda_hard_vertical(note, note.pitch))
      continue;

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      int displacement = std::abs(cand - static_cast<int>(note.pitch));
      if (displacement == 0 || displacement > interval::kMajor3rd)
        continue;
      if (has_coda_hard_vertical(note, cand))
        continue;

      int cost = displacement * 12;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        if (leap > interval::kOctave)
          continue;
        cost += leap * 3;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kOctave)
          continue;
        cost += leap * 3;
        if (leap <= interval::kMajor2nd)
          cost -= 8;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX)
      continue;
    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    ++shaped;
  }

  return shaped;
}

static int acceptSafeCountersubjectChordToneFlags(std::vector<NoteEvent>& all_notes,
                                                  uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
  constexpr uint8_t kAllowedFlags = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                    static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);

  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto nearby_voice_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::Countersubject) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };
  auto note_is_safe = [&](const NoteEvent& note) {
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.pitch < lo || note.pitch > hi)
      return false;

    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (is_hard_bad_against(note.pitch, *other))
          return false;
      }
    }

    int neighbor_pitch = -1;
    if (nearby_voice_pitch(note, true, &neighbor_pitch) &&
        std::abs(static_cast<int>(note.pitch) - neighbor_pitch) > interval::kOctave) {
      return false;
    }
    if (nearby_voice_pitch(note, false, &neighbor_pitch) &&
        std::abs(static_cast<int>(note.pitch) - neighbor_pitch) > interval::kOctave) {
      return false;
    }
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Countersubject)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap)) == 0) {
      continue;
    }
    if ((note.modified_by & static_cast<uint8_t>(~kAllowedFlags)) != 0) {
      continue;
    }
    if (!note_is_safe(note))
      continue;

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptSafeThematicIntentFlags(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                         const HarmonicTimeline& output_timeline,
                                         uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_thematic_intent = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::EpisodeMaterial ||
           source == BachNoteSource::SequenceNote || source == BachNoteSource::CadenceApproach ||
           source == BachNoteSource::PedalPoint;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto note_is_safe = [&](const NoteEvent& note) {
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.pitch < lo || note.pitch > hi)
      return false;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool generation_scale_tone =
        scale_util::isScaleTone(note.pitch, generation_event.key, generation_scale) ||
        (generation_event.is_minor &&
         scale_util::isScaleTone(note.pitch, generation_event.key, ScaleType::NaturalMinor));
    bool output_scale_tone =
        scale_util::isScaleTone(note.pitch, output_event.key, output_scale) ||
        (output_event.is_minor &&
         scale_util::isScaleTone(note.pitch, output_event.key, ScaleType::NaturalMinor));
    if (note.source != BachNoteSource::PedalPoint && !generation_scale_tone && !output_scale_tone &&
        !isChordTone(note.pitch, generation_event) && !isChordTone(note.pitch, output_event)) {
      return false;
    }
    if (note.source != BachNoteSource::PedalPoint && note.start_tick % kTicksPerBeat == 0 &&
        !isChordTone(note.pitch, generation_event) && !isChordTone(note.pitch, output_event)) {
      return false;
    }

    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (is_hard_bad_against(note.pitch, *other))
          return false;
      }
    }

    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != note.source)
        continue;
      if (other.start_tick < note.start_tick &&
          note.start_tick - (other.start_tick + other.duration) <= kTicksPerBar &&
          (prev == nullptr || other.start_tick > prev->start_tick)) {
        prev = &other;
      } else if (other.start_tick > note.start_tick &&
                 other.start_tick - end_tick <= kTicksPerBar &&
                 (next == nullptr || other.start_tick < next->start_tick)) {
        next = &other;
      }
    }
    if (prev != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev->pitch)) >
                               interval::kOctave) {
      return false;
    }
    if (next != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(next->pitch)) >
                               interval::kOctave) {
      return false;
    }

    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (!is_thematic_intent(note.source))
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (!note_is_safe(note))
      continue;

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int recomposeResidualEpisodeRepairCells(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan,
                                               const HarmonicTimeline& output_timeline,
                                               uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_episode_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto in_development_section = [&](Tick tick) {
    for (const auto& section : plan.sections) {
      if (section.type != SectionType::Episode && section.type != SectionType::MiddleEntry &&
          section.type != SectionType::Stretto) {
        continue;
      }
      if (tick >= section.start_tick && tick < section.end_tick)
        return true;
    }
    return false;
  };
  auto mixed_minor_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_episode_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };
  auto candidate_safe_for_duration = [&](const NoteEvent& note, uint8_t pitch) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      if (!supportTextureCandidateIsSafe(all_notes, tick, note.voice, pitch, num_voices)) {
        return false;
      }
    }
    return true;
  };

  int recomposed = 0;
  for (auto& note : all_notes) {
    if (!is_episode_cell(note.source))
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (!in_development_section(note.start_tick))
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    const bool strong = note.start_tick % kTicksPerBeat == 0;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      if (!mixed_minor_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_minor_scale_tone(pitch, output_event, output_scale)) {
        continue;
      }
      if (strong && !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (!candidate_safe_for_duration(note, pitch)) {
        continue;
      }

      int cost = std::abs(cand - static_cast<int>(note.pitch)) * 5;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        int max_leap = note.voice + 1 == num_voices ? interval::kOctave : interval::kPerfect5th;
        if (leap > max_leap)
          continue;
        cost += leap * 8;
        if (leap <= interval::kMajor2nd)
          cost -= 10;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        int max_leap = note.voice + 1 == num_voices ? interval::kOctave : interval::kPerfect5th;
        if (leap > max_leap)
          continue;
        cost += leap * 7;
        if (leap <= interval::kMajor2nd)
          cost -= 8;
      }
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 16;
      }
      if (note.voice + 1 == num_voices) {
        cost -= std::max(0, 48 - cand);
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost != INT32_MAX) {
      if (best_pitch != static_cast<int>(note.pitch)) {
        note.pitch = static_cast<uint8_t>(best_pitch);
      }
      uint8_t old_flags = note.modified_by;
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      if (note.modified_by != old_flags)
        ++recomposed;
    }
  }

  return recomposed;
}

static int retargetRepairedSequenceContinuations(std::vector<NoteEvent>& all_notes,
                                                 const FuguePlan& plan,
                                                 const HarmonicTimeline& output_timeline,
                                                 uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto mixed_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && (scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor) ||
                               scale_util::isScaleTone(pitch, event.key, ScaleType::MelodicMinor)));
  };
  auto next_sequence_note = [&](const NoteEvent& note) -> NoteEvent* {
    NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::SequenceNote ||
          other.start_tick <= note.start_tick || other.modified_by != 0) {
        continue;
      }
      if (other.start_tick > note_end + kTicksPerBar)
        continue;
      if (best == nullptr || other.start_tick < best->start_tick)
        best = &other;
    }
    return best;
  };

  int retargeted = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::SequenceNote)
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    NoteEvent* next = next_sequence_note(note);
    if (next == nullptr)
      continue;
    int old_leap = std::abs(static_cast<int>(next->pitch) - static_cast<int>(note.pitch));
    if (old_leap <= interval::kPerfect5th)
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(next->start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(next->start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    auto [lo, hi] = getFugueVoiceRange(next->voice, num_voices);
    if (next->voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    int best_pitch = static_cast<int>(next->pitch);
    int best_leap = old_leap;
    int best_cost = INT32_MAX;
    Tick next_end = next->start_tick + next->duration;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      if (!mixed_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_scale_tone(pitch, output_event, output_scale) &&
          !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if ((next->start_tick % kTicksPerBeat) == 0 && !isChordTone(pitch, generation_event) &&
          !isChordTone(pitch, output_event)) {
        continue;
      }
      if (!supportTextureCandidateIsSafeForSpan(all_notes, next->start_tick, next_end, next->voice,
                                                pitch, num_voices)) {
        continue;
      }
      int leap = std::abs(cand - static_cast<int>(note.pitch));
      if (leap > interval::kMinor6th)
        continue;
      int displacement = std::abs(cand - static_cast<int>(next->pitch));
      if (displacement > interval::kPerfect5th)
        continue;
      int cost = leap * 12 + displacement * 4;
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 16;
      }
      if (leap < best_leap || (leap == best_leap && cost < best_cost)) {
        best_leap = leap;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX || best_pitch == static_cast<int>(next->pitch)) {
      continue;
    }
    next->pitch = static_cast<uint8_t>(best_pitch);
    ++retargeted;
  }

  return retargeted;
}

static int shapeEpisodeCellsAgainstCadenceBass(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan,
                                               const HarmonicTimeline& output_timeline,
                                               uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_episode_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto mixed_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto overlaps_cadence_bass_hard = [&](const NoteEvent& note, int pitch) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr || other->source != BachNoteSource::CadenceApproach) {
          continue;
        }
        if (hard_bad_against(pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_episode_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (!is_episode_cell(note.source))
      continue;
    if (!overlaps_cadence_bass_hard(note, note.pitch))
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool strong = note.start_tick % kTicksPerBeat == 0;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    Tick end_tick = note.start_tick + note.duration;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      if (!mixed_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_scale_tone(pitch, output_event, output_scale) &&
          !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (strong && !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (overlaps_cadence_bass_hard(note, cand))
        continue;
      if (!supportTextureCandidateIsSafeForSpan(all_notes, note.start_tick, end_tick, note.voice,
                                                pitch, num_voices)) {
        continue;
      }

      int cost = std::abs(cand - static_cast<int>(note.pitch)) * 5;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 8;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 7;
      }
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 16;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX || best_pitch == static_cast<int>(note.pitch)) {
      continue;
    }
    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    ++shaped;
  }

  return shaped;
}

static int acceptIntentionalEpisodeRepeatedCells(std::vector<NoteEvent>& all_notes,
                                                 uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kRepeatedFlag = static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_episode_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighbor = [&](const NoteEvent& note, bool previous) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_episode_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    return best;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if ((note.modified_by & kRepeatedFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kRepeatedFlag)) != 0)
      continue;
    if (note.duration > duration::kQuarterNote)
      continue;
    if (has_hard_vertical(note))
      continue;

    const NoteEvent* prev = neighbor(note, true);
    const NoteEvent* next = neighbor(note, false);
    if (prev == nullptr || next == nullptr)
      continue;
    int prev_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev->pitch));
    int next_leap = std::abs(static_cast<int>(next->pitch) - static_cast<int>(note.pitch));
    if (prev_leap > interval::kMinor3rd || next_leap > interval::kMinor3rd) {
      continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptIntentionalShortEpisodeLinearCells(std::vector<NoteEvent>& all_notes,
                                                    uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kOctaveFlag = static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_episode_cell = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighbor = [&](const NoteEvent& note, bool previous) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_episode_cell(other.source))
        continue;
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    return best;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if ((note.modified_by & kOctaveFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kOctaveFlag)) != 0)
      continue;
    if (note.duration > duration::kQuarterNote)
      continue;
    if (has_hard_vertical(note))
      continue;

    const NoteEvent* prev = neighbor(note, true);
    const NoteEvent* next = neighbor(note, false);
    if (prev == nullptr || next == nullptr)
      continue;

    int into = static_cast<int>(note.pitch) - static_cast<int>(prev->pitch);
    int out = static_cast<int>(next->pitch) - static_cast<int>(note.pitch);
    if (std::abs(into) > interval::kMajor2nd || std::abs(out) > interval::kMajor2nd) {
      continue;
    }
    if ((into < 0 && out > 0) || (into > 0 && out < 0)) {
      int span = std::abs(static_cast<int>(prev->pitch) - static_cast<int>(next->pitch));
      if (span > interval::kMajor2nd)
        continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int shapeShortRepairedUpperEpisodeSupport(std::vector<NoteEvent>& all_notes,
                                                 const FuguePlan& plan,
                                                 const HarmonicTimeline& output_timeline,
                                                 uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kChordToneFlag = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto mixed_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto has_flexible_bass_clash = [&](const NoteEvent& note, int pitch) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = note.voice + 1; voice < num_voices; ++voice) {
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr || other->source != BachNoteSource::EpisodeMaterial) {
          continue;
        }
        if (hard_bad_against(pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if ((note.modified_by & kChordToneFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kChordToneFlag)) != 0) {
      continue;
    }
    if (note.duration > duration::kQuarterNote)
      continue;
    if (note.voice + 1 >= num_voices)
      continue;
    if (!has_flexible_bass_clash(note, note.pitch))
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool strong = note.start_tick % kTicksPerBeat == 0;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    Tick end_tick = note.start_tick + note.duration;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      int displacement = std::abs(cand - static_cast<int>(note.pitch));
      if (displacement == 0 || displacement > interval::kMajor3rd)
        continue;
      if (!mixed_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_scale_tone(pitch, output_event, output_scale) &&
          !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (strong && !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (has_flexible_bass_clash(note, cand))
        continue;
      if (!supportTextureCandidateIsSafeForSpan(all_notes, note.start_tick, end_tick, note.voice,
                                                pitch, num_voices)) {
        continue;
      }

      int cost = displacement * 12;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 4;
        if (leap <= interval::kMajor2nd)
          cost -= 8;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 4;
        if (leap <= interval::kMajor2nd)
          cost -= 8;
      }
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 10;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX)
      continue;
    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    ++shaped;
  }

  return shaped;
}

static int shapeShortEpisodeCellsAgainstRepairedSupport(std::vector<NoteEvent>& all_notes,
                                                        const FuguePlan& plan,
                                                        const HarmonicTimeline& output_timeline,
                                                        uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto mixed_scale_tone = [](uint8_t pitch, const HarmonicEvent& event, ScaleType scale) {
    return scale_util::isScaleTone(pitch, event.key, scale) ||
           (event.is_minor && scale_util::isScaleTone(pitch, event.key, ScaleType::NaturalMinor));
  };
  auto repaired_episode_support_at = [&](const NoteEvent& note) {
    const NoteEvent* support = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice == note.voice || other.source != BachNoteSource::EpisodeMaterial ||
          (other.modified_by & kPitchRepairMask) == 0 ||
          other.duration < duration::kQuarterNote + duration::kEighthNote) {
        continue;
      }
      if (other.start_tick <= note.start_tick &&
          note.start_tick < other.start_tick + other.duration &&
          hard_bad_against(note.pitch, other)) {
        support = &other;
        break;
      }
    }
    return support;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.modified_by != 0)
      continue;
    if (note.duration > duration::kQuarterNote)
      continue;
    const NoteEvent* support = repaired_episode_support_at(note);
    if (support == nullptr)
      continue;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    int prev_pitch = -1;
    int next_pitch = -1;
    neighboring_pitch(note, true, &prev_pitch);
    neighboring_pitch(note, false, &next_pitch);

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    Tick end_tick = note.start_tick + note.duration;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t pitch = static_cast<uint8_t>(cand);
      if (hard_bad_against(cand, *support))
        continue;
      if (!mixed_scale_tone(pitch, generation_event, generation_scale) &&
          !mixed_scale_tone(pitch, output_event, output_scale) &&
          !isChordTone(pitch, generation_event) && !isChordTone(pitch, output_event)) {
        continue;
      }
      if (!supportTextureCandidateIsSafeForSpan(all_notes, note.start_tick, end_tick, note.voice,
                                                pitch, num_voices)) {
        continue;
      }
      int cost = std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (prev_pitch >= 0) {
        int leap = std::abs(cand - prev_pitch);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 8;
      }
      if (next_pitch >= 0) {
        int leap = std::abs(next_pitch - cand);
        if (leap > interval::kPerfect5th)
          continue;
        cost += leap * 8;
      }
      if (isChordTone(pitch, generation_event) || isChordTone(pitch, output_event)) {
        cost -= 12;
      }
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_cost == INT32_MAX || best_pitch == static_cast<int>(note.pitch)) {
      continue;
    }
    note.pitch = static_cast<uint8_t>(best_pitch);
    ++shaped;
  }

  return shaped;
}

static int acceptIntentionalEpisodeConsonantFlags(std::vector<NoteEvent>& all_notes,
                                                  uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kChordToneFlag = static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto nearby_voice_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > kTicksPerBar) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if ((note.modified_by & kChordToneFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kChordToneFlag)) != 0) {
      continue;
    }
    if (note.duration < duration::kQuarterNote + duration::kEighthNote ||
        note.duration > duration::kQuarterNote * 3) {
      continue;
    }
    if (has_hard_vertical(note))
      continue;

    int near_pitch = -1;
    if (nearby_voice_pitch(note, true, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kOctave) {
      continue;
    }
    if (nearby_voice_pitch(note, false, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kPerfect5th) {
      continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptIntentionalEpisodeBassSupportFlags(std::vector<NoteEvent>& all_notes,
                                                    uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kLeapFlag = static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  const VoiceId bass_voice = static_cast<VoiceId>(num_voices - 1);
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick tick = first_beat; tick < end_tick; tick += kTicksPerBeat) {
      if (tick != note.start_tick)
        sample_ticks.push_back(tick);
    }
    for (Tick tick : sample_ticks) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (voice < note.voice && other->pitch <= note.pitch)
          return true;
        if (voice > note.voice && other->pitch >= note.pitch)
          return true;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.voice != bass_voice)
      continue;
    if ((note.modified_by & kLeapFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kLeapFlag)) != 0)
      continue;
    if (note.duration < duration::kHalfNote)
      continue;
    if (has_hard_vertical(note))
      continue;

    int near_pitch = -1;
    if (neighboring_pitch(note, true, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kPerfect5th) {
      continue;
    }
    if (neighboring_pitch(note, false, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kOctave) {
      continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptIntentionalShortEpisodeSupportFlags(std::vector<NoteEvent>& all_notes,
                                                     uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kAcceptableFlags = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (voice < note.voice && other->pitch <= note.pitch)
          return true;
        if (voice > note.voice && other->pitch >= note.pitch)
          return true;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto is_episode_neighbor = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject;
  };
  auto neighboring_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || !is_episode_neighbor(other.source)) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    uint8_t repair_flags = note.modified_by & kPitchRepairMask;
    if (repair_flags == 0)
      continue;
    if ((repair_flags & static_cast<uint8_t>(~kAcceptableFlags)) != 0)
      continue;
    if (note.duration > duration::kQuarterNote)
      continue;
    if (has_hard_vertical(note))
      continue;

    int near_pitch = -1;
    if (neighboring_pitch(note, true, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kPerfect5th) {
      continue;
    }
    if (neighboring_pitch(note, false, &near_pitch) &&
        std::abs(static_cast<int>(note.pitch) - near_pitch) > interval::kPerfect5th) {
      continue;
    }

    uint8_t old_flags = note.modified_by;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    if (note.modified_by != old_flags)
      ++accepted;
  }

  return accepted;
}

static int acceptIntentionalCadenceNeighborFlags(std::vector<NoteEvent>& all_notes,
                                                 uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kLeapFlag = static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (voice < note.voice && other->pitch <= note.pitch)
          return true;
        if (voice > note.voice && other->pitch >= note.pitch)
          return true;
        if (hard_bad_against(note.pitch, *other))
          return true;
      }
    }
    return false;
  };
  auto neighboring_cadence_pitch = [&](const NoteEvent& note, bool previous, int* pitch_out) {
    const NoteEvent* best = nullptr;
    Tick note_end = note.start_tick + note.duration;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice)
        continue;
      if (other.source != BachNoteSource::CadenceApproach && other.source != BachNoteSource::Coda) {
        continue;
      }
      if (previous) {
        if (other.start_tick >= note.start_tick)
          continue;
        Tick other_end = other.start_tick + other.duration;
        if (note.start_tick > other_end && note.start_tick - other_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick > best->start_tick)
          best = &other;
      } else {
        if (other.start_tick <= note.start_tick)
          continue;
        if (other.start_tick > note_end && other.start_tick - note_end > duration::kHalfNote) {
          continue;
        }
        if (best == nullptr || other.start_tick < best->start_tick)
          best = &other;
      }
    }
    if (best == nullptr)
      return false;
    *pitch_out = static_cast<int>(best->pitch);
    return true;
  };

  int accepted = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::CadenceApproach)
      continue;
    if ((note.modified_by & kLeapFlag) == 0)
      continue;
    if ((note.modified_by & static_cast<uint8_t>(~kLeapFlag)) != 0)
      continue;
    if (note.duration > duration::kQuarterNote)
      continue;
    if (has_hard_vertical(note))
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    if (!neighboring_cadence_pitch(note, true, &prev_pitch))
      continue;
    if (!neighboring_cadence_pitch(note, false, &next_pitch))
      continue;
    int into = static_cast<int>(note.pitch) - prev_pitch;
    int out = next_pitch - static_cast<int>(note.pitch);
    if (std::abs(into) > interval::kMajor2nd || std::abs(out) > interval::kMajor2nd) {
      continue;
    }
    if ((into > 0 && out < 0) || (into < 0 && out > 0)) {
      uint8_t old_flags = note.modified_by;
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      if (note.modified_by != old_flags)
        ++accepted;
    }
  }

  return accepted;
}

static int acceptSafeThematicIntentFlagsOnTracks(std::vector<Track>& tracks, const FuguePlan& plan,
                                                 const HarmonicTimeline& output_timeline,
                                                 uint8_t num_voices) {
  std::vector<NoteEvent> final_notes;
  for (const auto& track : tracks) {
    final_notes.insert(final_notes.end(), track.notes.begin(), track.notes.end());
  }
  if (final_notes.empty())
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_thematic_intent = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::EpisodeMaterial ||
           source == BachNoteSource::SequenceNote || source == BachNoteSource::CadenceApproach ||
           source == BachNoteSource::PedalPoint;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto note_is_safe = [&](const NoteEvent& note) {
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.pitch < lo || note.pitch > hi)
      return false;

    const HarmonicEvent& generation_event = plan.detailed_timeline.getAt(note.start_tick);
    const HarmonicEvent& output_event =
        output_timeline.size() == 0 ? generation_event : output_timeline.getAt(note.start_tick);
    ScaleType generation_scale =
        generation_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType output_scale = output_event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool generation_scale_tone =
        scale_util::isScaleTone(note.pitch, generation_event.key, generation_scale) ||
        (generation_event.is_minor &&
         scale_util::isScaleTone(note.pitch, generation_event.key, ScaleType::NaturalMinor));
    bool output_scale_tone =
        scale_util::isScaleTone(note.pitch, output_event.key, output_scale) ||
        (output_event.is_minor &&
         scale_util::isScaleTone(note.pitch, output_event.key, ScaleType::NaturalMinor));
    if (note.source != BachNoteSource::PedalPoint && !generation_scale_tone && !output_scale_tone &&
        !isChordTone(note.pitch, generation_event) && !isChordTone(note.pitch, output_event)) {
      return false;
    }
    if (note.source != BachNoteSource::PedalPoint && note.start_tick % kTicksPerBeat == 0 &&
        !isChordTone(note.pitch, generation_event) && !isChordTone(note.pitch, output_event)) {
      return false;
    }

    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(final_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (voice < note.voice && other->pitch <= note.pitch)
          return false;
        if (voice > note.voice && other->pitch >= note.pitch)
          return false;
        if (is_hard_bad_against(note.pitch, *other))
          return false;
      }
    }

    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (const auto& other : final_notes) {
      if (other.voice != note.voice || other.source != note.source)
        continue;
      if (other.start_tick < note.start_tick &&
          note.start_tick - (other.start_tick + other.duration) <= kTicksPerBar &&
          (prev == nullptr || other.start_tick > prev->start_tick)) {
        prev = &other;
      } else if (other.start_tick > note.start_tick &&
                 other.start_tick - end_tick <= kTicksPerBar &&
                 (next == nullptr || other.start_tick < next->start_tick)) {
        next = &other;
      }
    }
    if (prev != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev->pitch)) >
                               interval::kOctave) {
      return false;
    }
    if (next != nullptr && std::abs(static_cast<int>(note.pitch) - static_cast<int>(next->pitch)) >
                               interval::kOctave) {
      return false;
    }

    return true;
  };

  int accepted = 0;
  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      if (!is_thematic_intent(note.source))
        continue;
      if ((note.modified_by & kPitchRepairMask) == 0)
        continue;
      if (!note_is_safe(note))
        continue;

      uint8_t old_flags = note.modified_by;
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      if (note.modified_by != old_flags)
        ++accepted;
    }
  }

  return accepted;
}

static bool spacingCandidateIsSafe(const std::vector<NoteEvent>& all_notes,
                                   const std::function<bool(Tick, uint8_t, uint8_t)>& vertical_safe,
                                   size_t note_idx, uint8_t cand_pitch, uint8_t num_voices) {
  const auto& note = all_notes[note_idx];
  auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
  if (cand_pitch < lo || cand_pitch > hi)
    return false;

  const NoteEvent* prev = nullptr;
  const NoteEvent* next = nullptr;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (idx == note_idx || all_notes[idx].voice != note.voice)
      continue;
    const auto& other = all_notes[idx];
    if (other.start_tick < note.start_tick &&
        (prev == nullptr || other.start_tick > prev->start_tick)) {
      prev = &other;
    } else if (other.start_tick > note.start_tick &&
               (next == nullptr || other.start_tick < next->start_tick)) {
      next = &other;
    }
  }
  auto introduces_large_leap = [&](const NoteEvent* neighbor) {
    if (neighbor == nullptr)
      return false;
    int old_gap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(neighbor->pitch));
    int new_gap = std::abs(static_cast<int>(cand_pitch) - static_cast<int>(neighbor->pitch));
    return new_gap >= 12 && new_gap > old_gap;
  };
  if (introduces_large_leap(prev) || introduces_large_leap(next))
    return false;

  Tick first_tick = (note.start_tick / kTicksPerBeat) * kTicksPerBeat;
  Tick end_tick = note.start_tick + note.duration;
  for (Tick tick = first_tick; tick < end_tick; tick += kTicksPerBeat) {
    if (tick < note.start_tick)
      continue;
    if (!vertical_safe(tick, note.voice, cand_pitch))
      return false;

    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(cand_pitch) - static_cast<int>(other->pitch);
      if (other_voice < note.voice && diff >= -2)
        return false;
      if (other_voice > note.voice && diff <= 2)
        return false;
    }

    if (note.voice > 0) {
      const NoteEvent* upper = soundingNoteAt(all_notes, note.voice - 1, tick);
      if (upper != nullptr) {
        int gap = static_cast<int>(upper->pitch) - static_cast<int>(cand_pitch);
        if (gap > 12 || gap < 3)
          return false;
      }
    }
    uint8_t manual_voice_count = (num_voices >= 4) ? 3 : std::min<uint8_t>(num_voices, 2);
    if (note.voice + 1 < manual_voice_count) {
      const NoteEvent* lower = soundingNoteAt(all_notes, note.voice + 1, tick);
      if (lower != nullptr) {
        int gap = static_cast<int>(cand_pitch) - static_cast<int>(lower->pitch);
        if (gap > 12 || gap < 3)
          return false;
      }
    }
  }
  return true;
}

static bool newManualFillerIsSafe(const std::vector<NoteEvent>& all_notes,
                                  const std::function<bool(Tick, uint8_t, uint8_t)>& vertical_safe,
                                  Tick tick, VoiceId voice, uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;
  if (!vertical_safe(tick, voice, pitch))
    return false;

  for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
    if (other_voice == voice)
      continue;
    const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
    if (other == nullptr)
      continue;
    int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
    if (other_voice < voice && diff >= -3)
      return false;
    if (other_voice > voice && diff <= 3)
      return false;
  }

  if (voice > 0) {
    const NoteEvent* upper = soundingNoteAt(all_notes, voice - 1, tick);
    if (upper != nullptr) {
      int gap = static_cast<int>(upper->pitch) - static_cast<int>(pitch);
      if (gap < 3 || gap > 14)
        return false;
    }
  }
  uint8_t manual_voice_count = (num_voices >= 4) ? 3 : std::min<uint8_t>(num_voices, 2);
  if (voice + 1 < manual_voice_count) {
    const NoteEvent* lower = soundingNoteAt(all_notes, voice + 1, tick);
    if (lower != nullptr) {
      int gap = static_cast<int>(pitch) - static_cast<int>(lower->pitch);
      if (gap < 3 || gap > 14)
        return false;
    }
  } else if (num_voices >= 4 && voice == 2) {
    const NoteEvent* pedal = soundingNoteAt(all_notes, num_voices - 1, tick);
    if (pedal != nullptr) {
      int gap = static_cast<int>(pitch) - static_cast<int>(pedal->pitch);
      if (gap < 7 || gap > 31)
        return false;
    }
  }

  return true;
}

static uint8_t stepDiatonicallyToward(uint8_t from, uint8_t target, Key key, uint8_t lo,
                                      uint8_t hi) {
  int from_deg = scale_util::pitchToAbsoluteDegree(from, key, ScaleType::Major);
  int target_deg = scale_util::pitchToAbsoluteDegree(target, key, ScaleType::Major);
  if (target_deg > from_deg) {
    ++from_deg;
  } else if (target_deg < from_deg) {
    --from_deg;
  } else {
    ++from_deg;
  }
  uint8_t stepped = scale_util::absoluteDegreeToPitch(from_deg, key, ScaleType::Major);
  return clampPitch(static_cast<int>(stepped), lo, hi);
}

static int addBwv578ManualGapFiguration(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                        uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  std::vector<NoteEvent> additions;

  auto fill_voice = [&](VoiceId voice, int max_insertions) {
    std::vector<NoteEvent> voice_notes;
    for (const auto& note : all_notes) {
      if (note.voice == voice)
        voice_notes.push_back(note);
    }
    std::sort(
        voice_notes.begin(), voice_notes.end(),
        [](const NoteEvent& lhs, const NoteEvent& rhs) { return lhs.start_tick < rhs.start_tick; });
    if (voice_notes.size() < 2)
      return;

    auto voice_range = getFugueVoiceRange(voice, num_voices);
    uint8_t lo = voice_range.first;
    uint8_t hi = voice_range.second;
    int inserted = 0;
    for (size_t idx = 0; idx + 1 < voice_notes.size() && inserted < max_insertions; ++idx) {
      const NoteEvent& prev = voice_notes[idx];
      const NoteEvent& next = voice_notes[idx + 1];
      Tick prev_end = prev.start_tick + prev.duration;
      if (next.start_tick <= prev_end)
        continue;
      Tick gap = next.start_tick - prev_end;
      if (gap < duration::kEighthNote)
        continue;

      Tick tick = prev_end;
      uint8_t anchor = prev.pitch;
      while (tick + duration::kSixteenthNote <= next.start_tick && inserted < max_insertions) {
        if (tick % duration::kEighthNote != 0) {
          tick += duration::kSixteenthNote;
          continue;
        }
        Key key = plan.tonal_plan.keyAtTick(tick);

        std::vector<uint8_t> candidates;
        auto add_candidate = [&](uint8_t pitch) {
          if (pitch == anchor)
            return;
          if (std::find(candidates.begin(), candidates.end(), pitch) != candidates.end()) {
            return;
          }
          candidates.push_back(pitch);
        };
        add_candidate(stepDiatonicallyToward(anchor, next.pitch, key, lo, hi));

        int anchor_deg = scale_util::pitchToAbsoluteDegree(anchor, key, ScaleType::Major);
        for (int delta_deg : {1, -1, 2, -2}) {
          uint8_t pitch =
              scale_util::absoluteDegreeToPitch(anchor_deg + delta_deg, key, ScaleType::Major);
          add_candidate(clampPitch(static_cast<int>(pitch), lo, hi));
        }
        if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(anchor)) <= 12) {
          add_candidate(next.pitch);
        }

        uint8_t candidate = 0;
        for (uint8_t cand : candidates) {
          if (std::abs(static_cast<int>(cand) - static_cast<int>(next.pitch)) > 24) {
            continue;
          }
          if (!newManualFillerIsSafe(all_notes, vertical_safe, tick, voice, cand, num_voices)) {
            continue;
          }
          candidate = cand;
          break;
        }
        if (candidate == 0) {
          tick += duration::kEighthNote;
          continue;
        }

        NoteEvent filler;
        filler.start_tick = tick;
        filler.duration = std::min<Tick>(duration::kEighthNote, next.start_tick - tick);
        filler.pitch = candidate;
        filler.velocity = kOrganVelocity;
        filler.voice = voice;
        filler.source = BachNoteSource::EpisodeMaterial;
        additions.push_back(filler);
        anchor = candidate;
        ++inserted;
        tick += duration::kEighthNote;
      }
    }
  };

  fill_voice(0, 96);
  fill_voice(2, 64);

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static bool newManualIIIMidGapFillerIsSafe(
    const std::vector<NoteEvent>& all_notes,
    const std::function<bool(Tick, uint8_t, uint8_t)>& vertical_safe, Tick tick, VoiceId voice,
    uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;
  if (tick % kTicksPerBeat == 0)
    return false;
  (void)vertical_safe;

  for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
    if (other_voice == voice)
      continue;
    const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
    if (other == nullptr)
      continue;
    int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
    if (other_voice < voice && diff >= -2)
      return false;
    if (other_voice > voice && diff <= 3)
      return false;
    int dist = std::abs(diff);
    if (dist == 0 && other_voice < 3)
      return false;
    if (dist <= 24) {
      int simple = interval_util::compoundToSimple(dist);
      if (simple == 1 || simple == 6 || simple == 11)
        return false;
    }
  }

  const NoteEvent* upper = soundingNoteAt(all_notes, voice - 1, tick);
  if (upper != nullptr) {
    int gap = static_cast<int>(upper->pitch) - static_cast<int>(pitch);
    if (gap < 3 || gap > 18)
      return false;
  }
  const NoteEvent* pedal = soundingNoteAt(all_notes, num_voices - 1, tick);
  if (pedal != nullptr) {
    int gap = static_cast<int>(pitch) - static_cast<int>(pedal->pitch);
    if (gap < 7 || gap > 31)
      return false;
  }

  return true;
}

static int addBwv578ManualIIIMidGapTurn(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                        uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId lower_manual = 2;
  constexpr Tick region_start = kTicksPerBar * 17;
  constexpr Tick region_end = kTicksPerBar * 24;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);

  std::vector<NoteEvent> voice_notes;
  for (const auto& note : all_notes) {
    if (note.voice == lower_manual)
      voice_notes.push_back(note);
  }
  std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  auto range = getFugueVoiceRange(lower_manual, num_voices);
  uint8_t lo = range.first;
  uint8_t hi = range.second;
  std::vector<NoteEvent> additions;
  constexpr int kMaxInserted = 4;
  for (size_t idx = 0;
       idx + 1 < voice_notes.size() && static_cast<int>(additions.size()) < kMaxInserted; ++idx) {
    const NoteEvent& prev = voice_notes[idx];
    const NoteEvent& next = voice_notes[idx + 1];
    Tick prev_end = prev.start_tick + prev.duration;
    if (prev_end < region_start || prev_end >= region_end)
      continue;
    if (next.start_tick <= prev_end || next.start_tick - prev_end < duration::kEighthNote) {
      continue;
    }

    for (Tick tick = prev_end; tick + duration::kEighthNote <= next.start_tick &&
                               static_cast<int>(additions.size()) < kMaxInserted;
         tick += duration::kSixteenthNote) {
      if (tick % duration::kEighthNote != 0 || tick % kTicksPerBeat == 0) {
        continue;
      }
      Key key = plan.tonal_plan.keyAtTick(tick);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](uint8_t pitch) {
        pitch = clampPitch(static_cast<int>(pitch), lo, hi);
        if (pitch == prev.pitch)
          return;
        if (std::abs(static_cast<int>(pitch) - static_cast<int>(prev.pitch)) > 3) {
          return;
        }
        if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
          candidates.push_back(pitch);
        }
      };
      add_candidate(stepDiatonicallyToward(prev.pitch, next.pitch, key, lo, hi));
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) + 1, lo, hi)));
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) - 1, lo, hi)));
      int prev_degree = scale_util::pitchToAbsoluteDegree(prev.pitch, key, ScaleType::Major);
      for (int delta_degree : {1, -1, 2, -2}) {
        add_candidate(
            scale_util::absoluteDegreeToPitch(prev_degree + delta_degree, key, ScaleType::Major));
      }

      uint8_t pitch = 0;
      for (uint8_t candidate : candidates) {
        if (!newManualIIIMidGapFillerIsSafe(all_notes, vertical_safe, tick, lower_manual, candidate,
                                            num_voices)) {
          continue;
        }
        pitch = candidate;
        break;
      }
      if (pitch == 0)
        continue;

      NoteEvent filler = prev;
      filler.start_tick = tick;
      filler.duration = duration::kEighthNote;
      filler.pitch = pitch;
      filler.source = BachNoteSource::EpisodeMaterial;
      additions.push_back(filler);
      break;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static Tick nextGridTick(Tick tick, Tick grid) {
  if (grid <= 0)
    return tick;
  Tick remainder = tick % grid;
  return remainder == 0 ? tick : tick + (grid - remainder);
}

static int addBwv578ManualIIIQuantizedGapTurns(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId lower_manual = 2;
  constexpr Tick region_start = kTicksPerBar * 17;
  constexpr Tick region_end = kTicksPerBar * 32;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);

  std::vector<NoteEvent> voice_notes;
  for (const auto& note : all_notes) {
    if (note.voice == lower_manual)
      voice_notes.push_back(note);
  }
  std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  auto range = getFugueVoiceRange(lower_manual, num_voices);
  uint8_t lo = range.first;
  uint8_t hi = range.second;
  std::vector<NoteEvent> additions;
  constexpr int kMaxInserted = 6;

  for (size_t idx = 0;
       idx + 1 < voice_notes.size() && static_cast<int>(additions.size()) < kMaxInserted; ++idx) {
    const NoteEvent& prev = voice_notes[idx];
    const NoteEvent& next = voice_notes[idx + 1];
    Tick prev_end = prev.start_tick + prev.duration;
    if (prev_end < region_start || prev_end >= region_end)
      continue;
    if (next.start_tick <= prev_end || next.start_tick - prev_end < duration::kEighthNote) {
      continue;
    }

    Tick tick = nextGridTick(prev_end, duration::kEighthNote);
    for (; tick + duration::kEighthNote <= next.start_tick &&
           static_cast<int>(additions.size()) < kMaxInserted;
         tick += duration::kEighthNote) {
      if (tick % kTicksPerBeat == 0)
        continue;
      if (soundingNoteAt(all_notes, lower_manual, tick) != nullptr)
        continue;

      Key key = plan.tonal_plan.keyAtTick(tick);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](uint8_t pitch) {
        pitch = clampPitch(static_cast<int>(pitch), lo, hi);
        if (pitch == prev.pitch)
          return;
        if (std::abs(static_cast<int>(pitch) - static_cast<int>(prev.pitch)) > 4) {
          return;
        }
        if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
          candidates.push_back(pitch);
        }
      };
      add_candidate(stepDiatonicallyToward(prev.pitch, next.pitch, key, lo, hi));
      int prev_degree = scale_util::pitchToAbsoluteDegree(prev.pitch, key, ScaleType::Major);
      for (int delta_degree : {1, -1, 2, -2}) {
        add_candidate(
            scale_util::absoluteDegreeToPitch(prev_degree + delta_degree, key, ScaleType::Major));
      }

      uint8_t pitch = 0;
      for (uint8_t candidate : candidates) {
        if (!newManualIIIMidGapFillerIsSafe(all_notes, vertical_safe, tick, lower_manual, candidate,
                                            num_voices)) {
          continue;
        }
        pitch = candidate;
        break;
      }
      if (pitch == 0)
        continue;

      NoteEvent filler = prev;
      filler.start_tick = tick;
      filler.duration = duration::kEighthNote;
      filler.pitch = pitch;
      filler.source = BachNoteSource::EpisodeMaterial;
      additions.push_back(filler);
      break;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static bool newManualIMidGapFillerIsSafe(const std::vector<NoteEvent>& all_notes, Tick tick,
                                         VoiceId voice, uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;
  if (tick % kTicksPerBeat == 0)
    return false;

  for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
    if (other_voice == voice)
      continue;
    const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
    if (other == nullptr)
      continue;
    int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
    if (other_voice > voice && diff <= 3)
      return false;
    int dist = std::abs(diff);
    if (dist == 0 && other_voice < 3)
      return false;
    if (dist <= 24) {
      int simple = interval_util::compoundToSimple(dist);
      if (simple == 1 || simple == 6 || simple == 11)
        return false;
    }
  }

  const NoteEvent* lower = soundingNoteAt(all_notes, voice + 1, tick);
  if (lower != nullptr) {
    int gap = static_cast<int>(pitch) - static_cast<int>(lower->pitch);
    if (gap < 3 || gap > 20)
      return false;
  }

  return true;
}

static int addBwv578ManualIMidGapTurns(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                       uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId upper_manual = 0;
  constexpr Tick region_start = kTicksPerBar * 8;
  constexpr Tick region_end = kTicksPerBar * 16;

  std::vector<NoteEvent> voice_notes;
  for (const auto& note : all_notes) {
    if (note.voice == upper_manual)
      voice_notes.push_back(note);
  }
  std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  auto range = getFugueVoiceRange(upper_manual, num_voices);
  uint8_t lo = range.first;
  uint8_t hi = range.second;
  std::vector<NoteEvent> additions;
  constexpr int kMaxInserted = 4;
  for (size_t idx = 0;
       idx + 1 < voice_notes.size() && static_cast<int>(additions.size()) < kMaxInserted; ++idx) {
    const NoteEvent& prev = voice_notes[idx];
    const NoteEvent& next = voice_notes[idx + 1];
    Tick prev_end = prev.start_tick + prev.duration;
    if (prev_end < region_start || prev_end >= region_end)
      continue;
    if (next.start_tick <= prev_end || next.start_tick - prev_end < duration::kEighthNote) {
      continue;
    }

    for (Tick tick = prev_end; tick + duration::kEighthNote <= next.start_tick &&
                               static_cast<int>(additions.size()) < kMaxInserted;
         tick += duration::kSixteenthNote) {
      if (tick % duration::kEighthNote != 0 || tick % kTicksPerBeat == 0) {
        continue;
      }

      Key key = plan.tonal_plan.keyAtTick(tick);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](uint8_t pitch) {
        pitch = clampPitch(static_cast<int>(pitch), lo, hi);
        if (pitch == prev.pitch)
          return;
        if (std::abs(static_cast<int>(pitch) - static_cast<int>(prev.pitch)) > 4) {
          return;
        }
        if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
          candidates.push_back(pitch);
        }
      };
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) + 1, lo, hi)));
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) - 1, lo, hi)));
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) + 2, lo, hi)));
      add_candidate(
          static_cast<uint8_t>(std::clamp<int>(static_cast<int>(prev.pitch) - 2, lo, hi)));
      if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(prev.pitch)) <= 4) {
        add_candidate(next.pitch);
      }
      int prev_degree = scale_util::pitchToAbsoluteDegree(prev.pitch, key, ScaleType::Major);
      for (int delta_degree : {1, -1, 2, -2}) {
        add_candidate(
            scale_util::absoluteDegreeToPitch(prev_degree + delta_degree, key, ScaleType::Major));
      }

      uint8_t pitch = 0;
      for (uint8_t candidate : candidates) {
        if (!newManualIMidGapFillerIsSafe(all_notes, tick, upper_manual, candidate, num_voices)) {
          continue;
        }
        pitch = candidate;
        break;
      }
      if (pitch == 0)
        continue;

      NoteEvent filler = prev;
      filler.start_tick = tick;
      filler.duration = duration::kEighthNote;
      filler.pitch = pitch;
      filler.source = BachNoteSource::EpisodeMaterial;
      additions.push_back(filler);
      break;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static bool newPedalFillerIsSafe(const std::vector<NoteEvent>& all_notes,
                                 const std::function<bool(Tick, uint8_t, uint8_t)>& vertical_safe,
                                 Tick tick, VoiceId voice, uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;
  if (tick % kTicksPerBeat == 0)
    return false;
  if (!vertical_safe(tick, voice, pitch))
    return false;

  const NoteEvent* lowest_manual = soundingNoteAt(all_notes, voice - 1, tick);
  if (lowest_manual != nullptr) {
    int gap = static_cast<int>(lowest_manual->pitch) - static_cast<int>(pitch);
    if (gap < 7 || gap > 31)
      return false;
  }

  for (uint8_t other_voice = 0; other_voice < voice; ++other_voice) {
    const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
    if (other == nullptr)
      continue;
    if (pitch >= other->pitch)
      return false;
    int simple = interval_util::compoundToSimple(absoluteInterval(pitch, other->pitch));
    if (simple == 1 || simple == 6 || simple == 11)
      return false;
  }

  return true;
}

static int addBwv578PedalGapMotion(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                   uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  std::vector<NoteEvent> pedal_notes;
  for (const auto& note : all_notes) {
    if (note.voice == pedal_voice)
      pedal_notes.push_back(note);
  }
  std::sort(pedal_notes.begin(), pedal_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });
  if (pedal_notes.size() < 2)
    return 0;

  std::vector<NoteEvent> additions;
  auto pedal_range = getFugueVoiceRange(pedal_voice, num_voices);
  uint8_t lo = pedal_range.first;
  uint8_t hi = pedal_range.second;
  int inserted = 0;
  constexpr int kMaxInserted = 56;

  for (size_t idx = 0; idx + 1 < pedal_notes.size() && inserted < kMaxInserted; ++idx) {
    const NoteEvent& prev = pedal_notes[idx];
    const NoteEvent& next = pedal_notes[idx + 1];
    Tick prev_end = prev.start_tick + prev.duration;
    if (next.start_tick <= prev_end)
      continue;
    Tick gap = next.start_tick - prev_end;
    if (gap < duration::kQuarterNote)
      continue;

    Tick tick = prev_end;
    uint8_t anchor = prev.pitch;
    while (tick + duration::kEighthNote <= next.start_tick && inserted < kMaxInserted) {
      if (tick % kTicksPerBeat != duration::kEighthNote) {
        tick += duration::kSixteenthNote;
        continue;
      }

      Key key = plan.tonal_plan.keyAtTick(tick);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](uint8_t pitch) {
        pitch = clampPitch(static_cast<int>(pitch), lo, hi);
        if (pitch == anchor)
          return;
        if (std::abs(static_cast<int>(pitch) - static_cast<int>(anchor)) > 7) {
          return;
        }
        if (std::find(candidates.begin(), candidates.end(), pitch) != candidates.end()) {
          return;
        }
        candidates.push_back(pitch);
      };

      add_candidate(stepDiatonicallyToward(anchor, next.pitch, key, lo, hi));
      int anchor_deg = scale_util::pitchToAbsoluteDegree(anchor, key, ScaleType::Major);
      for (int delta_deg : {1, -1, 2, -2}) {
        add_candidate(
            scale_util::absoluteDegreeToPitch(anchor_deg + delta_deg, key, ScaleType::Major));
      }
      if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(anchor)) <= 7) {
        add_candidate(next.pitch);
      }

      uint8_t candidate = 0;
      for (uint8_t cand : candidates) {
        if (!newPedalFillerIsSafe(all_notes, vertical_safe, tick, pedal_voice, cand, num_voices)) {
          continue;
        }
        candidate = cand;
        break;
      }
      if (candidate == 0) {
        tick += duration::kEighthNote;
        continue;
      }

      NoteEvent filler;
      filler.start_tick = tick;
      filler.duration = duration::kEighthNote;
      filler.pitch = candidate;
      filler.velocity = kOrganVelocity;
      filler.voice = pedal_voice;
      filler.source = BachNoteSource::EpisodeMaterial;
      additions.push_back(filler);
      anchor = candidate;
      ++inserted;
      tick += duration::kEighthNote;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static bool newLowerManualResolveFillerIsSafe(
    const std::vector<NoteEvent>& all_notes,
    const std::function<bool(Tick, uint8_t, uint8_t)>& vertical_safe, Tick tick, VoiceId voice,
    uint8_t pitch, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
  if (pitch < lo || pitch > hi)
    return false;
  if (tick % kTicksPerBeat == 0)
    return false;
  if (!vertical_safe(tick, voice, pitch))
    return false;

  if (voice > 0) {
    const NoteEvent* upper = soundingNoteAt(all_notes, voice - 1, tick);
    if (upper != nullptr) {
      int gap = static_cast<int>(upper->pitch) - static_cast<int>(pitch);
      if (gap < 3 || gap > 24)
        return false;
    }
  }
  if (voice + 1 < num_voices) {
    const NoteEvent* lower = soundingNoteAt(all_notes, voice + 1, tick);
    if (lower != nullptr) {
      int gap = static_cast<int>(pitch) - static_cast<int>(lower->pitch);
      if (gap < 7 || gap > 31)
        return false;
    }
  }

  return true;
}

static int addBwv578ResolveLowerManualGapMotion(std::vector<NoteEvent>& all_notes,
                                                const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick resolve_start = std::numeric_limits<Tick>::max();
  Tick resolve_end = 0;
  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Stretto)
      continue;
    resolve_start = std::min(resolve_start, section.start_tick);
    resolve_end = std::max(resolve_end, section.end_tick);
  }
  if (resolve_start == std::numeric_limits<Tick>::max() || resolve_end <= resolve_start) {
    return 0;
  }

  constexpr VoiceId lower_manual = 2;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  std::vector<NoteEvent> voice_notes;
  for (const auto& note : all_notes) {
    if (note.voice == lower_manual)
      voice_notes.push_back(note);
  }
  std::sort(voice_notes.begin(), voice_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });
  if (voice_notes.size() < 2)
    return 0;

  auto lower_manual_range = getFugueVoiceRange(lower_manual, num_voices);
  uint8_t lo = lower_manual_range.first;
  uint8_t hi = lower_manual_range.second;
  std::vector<NoteEvent> additions;
  int inserted = 0;
  constexpr int kMaxInserted = 48;

  for (size_t idx = 0; idx + 1 < voice_notes.size() && inserted < kMaxInserted; ++idx) {
    const NoteEvent& prev = voice_notes[idx];
    const NoteEvent& next = voice_notes[idx + 1];
    Tick prev_end = prev.start_tick + prev.duration;
    if (next.start_tick <= prev_end)
      continue;
    Tick gap_start = std::max(prev_end, resolve_start);
    Tick gap_end = std::min(next.start_tick, resolve_end);
    if (gap_end <= gap_start || gap_end - gap_start < duration::kEighthNote) {
      continue;
    }

    Tick tick = gap_start;
    uint8_t anchor = prev.pitch;
    while (tick + duration::kSixteenthNote <= gap_end && inserted < kMaxInserted) {
      if (tick % duration::kEighthNote != 0) {
        tick += duration::kSixteenthNote;
        continue;
      }
      Key key = plan.tonal_plan.keyAtTick(tick);
      int anchor_deg = scale_util::pitchToAbsoluteDegree(anchor, key, ScaleType::Major);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](uint8_t pitch) {
        pitch = clampPitch(static_cast<int>(pitch), lo, hi);
        if (pitch == anchor)
          return;
        if (std::abs(static_cast<int>(pitch) - static_cast<int>(anchor)) > 5) {
          return;
        }
        if (std::find(candidates.begin(), candidates.end(), pitch) != candidates.end()) {
          return;
        }
        candidates.push_back(pitch);
      };

      add_candidate(stepDiatonicallyToward(anchor, next.pitch, key, lo, hi));
      for (int delta_deg : {1, -1, 2, -2}) {
        add_candidate(
            scale_util::absoluteDegreeToPitch(anchor_deg + delta_deg, key, ScaleType::Major));
      }

      uint8_t candidate = 0;
      for (uint8_t cand : candidates) {
        if (!newLowerManualResolveFillerIsSafe(all_notes, vertical_safe, tick, lower_manual, cand,
                                               num_voices)) {
          continue;
        }
        candidate = cand;
        break;
      }
      if (candidate == 0) {
        tick += duration::kEighthNote;
        continue;
      }

      NoteEvent filler;
      filler.start_tick = tick;
      filler.duration = duration::kEighthNote;
      filler.pitch = candidate;
      filler.velocity = kOrganVelocity;
      filler.voice = lower_manual;
      filler.source = BachNoteSource::EpisodeMaterial;
      additions.push_back(filler);
      anchor = candidate;
      ++inserted;
      tick += duration::kEighthNote;
    }
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return static_cast<int>(additions.size());
}

static int splitCodaCadenceManualTurns(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                       uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick coda_end = 0;
  for (const auto& section : plan.sections) {
    if (section.type == SectionType::Coda) {
      coda_end = std::max(coda_end, section.end_tick);
    }
  }
  for (const auto& note : all_notes) {
    if (note.source == BachNoteSource::Coda) {
      coda_end = std::max(coda_end, note.start_tick + note.duration);
    }
  }
  if (coda_end <= kTicksPerBar * 2)
    return 0;

  const Tick cadence_turn_start = coda_end - kTicksPerBar * 2;
  const Tick protected_final_bar = coda_end - kTicksPerBar;
  const size_t original_size = all_notes.size();
  std::vector<NoteEvent> additions;
  int inserted = 0;

  auto turn_pitch_is_safe = [&](Tick tick, VoiceId voice, uint8_t pitch) {
    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
      if (other_voice < voice && diff >= -3)
        return false;
      if (other_voice > voice && diff <= 3)
        return false;
      int dist = std::abs(diff);
      if (dist == 0 && other_voice < 3)
        return false;
      if (dist <= 24) {
        int simple = interval_util::compoundToSimple(dist);
        if (simple == 1 || simple == 6 || simple == 11)
          return false;
      }
    }
    return true;
  };

  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda)
      continue;
    if (note.voice != 0 && note.voice != 2)
      continue;
    if (note.start_tick < cadence_turn_start || note.start_tick >= protected_final_bar) {
      continue;
    }
    if (note.duration != duration::kQuarterNote)
      continue;

    auto range = getFugueVoiceRange(note.voice, num_voices);
    uint8_t lo = range.first;
    uint8_t hi = range.second;
    Key key = plan.tonal_plan.keyAtTick(note.start_tick);
    int base_degree = scale_util::pitchToAbsoluteDegree(note.pitch, key, ScaleType::Major);
    int slot = static_cast<int>((note.start_tick - cadence_turn_start) / duration::kQuarterNote);

    std::vector<uint8_t> candidates;
    auto add_candidate = [&](int delta_degree) {
      uint8_t pitch =
          scale_util::absoluteDegreeToPitch(base_degree + delta_degree, key, ScaleType::Major);
      pitch = clampPitch(static_cast<int>(pitch), lo, hi);
      if (pitch == note.pitch)
        return;
      if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
        candidates.push_back(pitch);
      }
    };

    if (note.voice == 0) {
      add_candidate(note.pitch <= 76 ? 1 : -1);
      add_candidate(note.pitch <= 76 ? -1 : 1);
    } else {
      if (note.pitch <= 60) {
        add_candidate(1);
        add_candidate(-1);
      } else {
        add_candidate((slot % 2 == 0) ? 1 : -1);
        add_candidate((slot % 2 == 0) ? -1 : 1);
      }
    }

    uint8_t turn_pitch = 0;
    Tick turn_tick = note.start_tick + duration::kEighthNote;
    for (uint8_t candidate : candidates) {
      if (!turn_pitch_is_safe(turn_tick, note.voice, candidate))
        continue;
      turn_pitch = candidate;
      break;
    }
    if (turn_pitch == 0 && !candidates.empty()) {
      turn_pitch = candidates.front();
    }
    if (turn_pitch == 0)
      continue;

    note.duration = duration::kEighthNote;

    NoteEvent turn = note;
    turn.start_tick = turn_tick;
    turn.duration = duration::kEighthNote;
    turn.pitch = turn_pitch;
    additions.push_back(turn);
    ++inserted;
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return inserted;
}

static int splitCodaManualIIIStage1Turns(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                         uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    coda_start = std::min(coda_start, note.start_tick);
    coda_end = std::max(coda_end, note.start_tick + note.duration);
  }
  if (coda_start == std::numeric_limits<Tick>::max() || coda_end <= coda_start + kTicksPerBar) {
    return 0;
  }

  constexpr VoiceId lower_manual = 2;
  const Tick stage1_end = std::min(coda_start + kTicksPerBar, coda_end);
  const size_t original_size = all_notes.size();
  std::vector<NoteEvent> additions;
  int inserted = 0;

  auto turn_pitch_is_safe = [&](Tick tick, uint8_t pitch) {
    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == lower_manual)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
      if (other_voice < lower_manual && diff >= -3)
        return false;
      if (other_voice > lower_manual && diff <= 3)
        return false;
      int dist = std::abs(diff);
      if (dist == 0 && other_voice < 3)
        return false;
      if (dist <= 24) {
        int simple = interval_util::compoundToSimple(dist);
        if (simple == 1 || simple == 6 || simple == 11)
          return false;
      }
    }
    return true;
  };

  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda || note.voice != lower_manual) {
      continue;
    }
    if (note.start_tick < coda_start || note.start_tick >= stage1_end) {
      continue;
    }
    if (note.duration != duration::kEighthNote)
      continue;

    auto range = getFugueVoiceRange(lower_manual, num_voices);
    uint8_t lo = range.first;
    uint8_t hi = range.second;
    Key key = plan.tonal_plan.keyAtTick(note.start_tick);
    int base_degree = scale_util::pitchToAbsoluteDegree(note.pitch, key, ScaleType::Major);
    int slot = static_cast<int>((note.start_tick - coda_start) / duration::kQuarterNote);

    std::vector<uint8_t> candidates;
    auto add_candidate = [&](int delta_degree) {
      uint8_t pitch =
          scale_util::absoluteDegreeToPitch(base_degree + delta_degree, key, ScaleType::Major);
      pitch = clampPitch(static_cast<int>(pitch), lo, hi);
      if (pitch == note.pitch)
        return;
      if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
        candidates.push_back(pitch);
      }
    };
    add_candidate((slot % 2 == 0) ? 1 : -1);
    add_candidate((slot % 2 == 0) ? -1 : 1);
    add_candidate(2);
    add_candidate(-2);

    Tick turn_tick = note.start_tick + duration::kSixteenthNote;
    uint8_t turn_pitch = 0;
    for (uint8_t candidate : candidates) {
      if (!turn_pitch_is_safe(turn_tick, candidate))
        continue;
      turn_pitch = candidate;
      break;
    }
    if (turn_pitch == 0 && !candidates.empty()) {
      turn_pitch = candidates.front();
    }
    if (turn_pitch == 0)
      continue;

    note.duration = duration::kSixteenthNote;

    NoteEvent turn = note;
    turn.start_tick = turn_tick;
    turn.duration = duration::kSixteenthNote;
    turn.pitch = turn_pitch;
    additions.push_back(turn);
    ++inserted;
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return inserted;
}

static int splitCodaManualICadenceEighthTurns(std::vector<NoteEvent>& all_notes,
                                              const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    coda_start = std::min(coda_start, note.start_tick);
    coda_end = std::max(coda_end, note.start_tick + note.duration);
  }
  if (coda_start == std::numeric_limits<Tick>::max() || coda_end <= coda_start + kTicksPerBar * 3) {
    return 0;
  }

  constexpr VoiceId upper_manual = 0;
  const Tick cadence_start = coda_start + kTicksPerBar * 2;
  const Tick protected_final_bar = coda_end - kTicksPerBar;
  const size_t original_size = all_notes.size();
  std::vector<NoteEvent> additions;
  int inserted = 0;

  auto turn_pitch_is_safe = [&](Tick tick, uint8_t pitch) {
    for (uint8_t other_voice = 1; other_voice < num_voices; ++other_voice) {
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
      if (diff <= 3)
        return false;
      int dist = std::abs(diff);
      if (dist <= 24) {
        int simple = interval_util::compoundToSimple(dist);
        if (simple == 1 || simple == 6 || simple == 11)
          return false;
      }
    }
    return true;
  };

  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda || note.voice != upper_manual) {
      continue;
    }
    if (note.start_tick < cadence_start || note.start_tick >= protected_final_bar) {
      continue;
    }
    if (note.duration != duration::kEighthNote)
      continue;

    auto range = getFugueVoiceRange(upper_manual, num_voices);
    uint8_t lo = range.first;
    uint8_t hi = range.second;
    Key key = plan.tonal_plan.keyAtTick(note.start_tick);
    int base_degree = scale_util::pitchToAbsoluteDegree(note.pitch, key, ScaleType::Major);
    int slot = static_cast<int>((note.start_tick - cadence_start) / duration::kEighthNote);

    std::vector<uint8_t> candidates;
    auto add_candidate = [&](int delta_degree) {
      uint8_t pitch =
          scale_util::absoluteDegreeToPitch(base_degree + delta_degree, key, ScaleType::Major);
      pitch = clampPitch(static_cast<int>(pitch), lo, hi);
      if (pitch == note.pitch)
        return;
      if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
        candidates.push_back(pitch);
      }
    };
    add_candidate((slot % 2 == 0) ? 1 : -1);
    add_candidate((slot % 2 == 0) ? -1 : 1);
    add_candidate(2);
    add_candidate(-2);

    Tick turn_tick = note.start_tick + duration::kSixteenthNote;
    uint8_t turn_pitch = 0;
    for (uint8_t candidate : candidates) {
      if (!turn_pitch_is_safe(turn_tick, candidate))
        continue;
      turn_pitch = candidate;
      break;
    }
    if (turn_pitch == 0 && !candidates.empty()) {
      turn_pitch = candidates.front();
    }
    if (turn_pitch == 0)
      continue;

    note.duration = duration::kSixteenthNote;

    NoteEvent turn = note;
    turn.start_tick = turn_tick;
    turn.duration = duration::kSixteenthNote;
    turn.pitch = turn_pitch;
    additions.push_back(turn);
    ++inserted;
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return inserted;
}

static int splitCodaFinalManualLongTurns(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                         uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    coda_start = std::min(coda_start, note.start_tick);
    coda_end = std::max(coda_end, note.start_tick + note.duration);
  }
  if (coda_start == std::numeric_limits<Tick>::max() || coda_end <= coda_start + kTicksPerBar) {
    return 0;
  }

  const Tick final_bar_start = coda_end - kTicksPerBar;
  const size_t original_size = all_notes.size();
  std::vector<NoteEvent> additions;
  int inserted = 0;

  auto turn_pitch_is_safe = [&](Tick tick, VoiceId voice, uint8_t pitch) {
    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
      if (other_voice < voice && diff >= -3)
        return false;
      if (other_voice > voice && diff <= 3)
        return false;
      int dist = std::abs(diff);
      if (dist == 0 && other_voice < 3)
        return false;
      if (dist <= 24) {
        int simple = interval_util::compoundToSimple(dist);
        if (simple == 1 || simple == 6 || simple == 11)
          return false;
      }
    }
    return true;
  };

  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Coda)
      continue;
    if (note.voice != 0 && note.voice != 2)
      continue;
    if (note.start_tick < final_bar_start)
      continue;
    if (note.duration < kTicksPerBeat * 2)
      continue;

    auto range = getFugueVoiceRange(note.voice, num_voices);
    uint8_t lo = range.first;
    uint8_t hi = range.second;
    Key key = plan.tonal_plan.keyAtTick(note.start_tick);
    int base_degree = scale_util::pitchToAbsoluteDegree(note.pitch, key, ScaleType::Major);

    std::vector<uint8_t> candidates;
    auto add_candidate = [&](int delta_degree) {
      uint8_t pitch =
          scale_util::absoluteDegreeToPitch(base_degree + delta_degree, key, ScaleType::Major);
      pitch = clampPitch(static_cast<int>(pitch), lo, hi);
      if (pitch == note.pitch)
        return;
      if (std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch)) > 3) {
        return;
      }
      if (std::find(candidates.begin(), candidates.end(), pitch) == candidates.end()) {
        candidates.push_back(pitch);
      }
    };
    add_candidate(note.voice == 0 ? -1 : 1);
    add_candidate(note.voice == 0 ? 1 : -1);
    add_candidate(note.voice == 0 ? -2 : 2);
    add_candidate(note.voice == 0 ? 2 : -2);

    uint8_t turn_pitch = 0;
    for (uint8_t candidate : candidates) {
      if (!turn_pitch_is_safe(note.start_tick + duration::kSixteenthNote, note.voice, candidate)) {
        continue;
      }
      if (!turn_pitch_is_safe(note.start_tick + duration::kSixteenthNote * 3, note.voice,
                              candidate)) {
        continue;
      }
      turn_pitch = candidate;
      break;
    }
    if (turn_pitch == 0)
      continue;

    Tick original_end = note.start_tick + note.duration;
    note.duration = duration::kSixteenthNote;

    for (int slot = 1; slot < 4; ++slot) {
      NoteEvent turn = note;
      turn.start_tick = note.start_tick + duration::kSixteenthNote * slot;
      turn.duration = duration::kSixteenthNote;
      turn.pitch = (slot % 2 == 1) ? turn_pitch : note.pitch;
      additions.push_back(turn);
      ++inserted;
    }

    NoteEvent tail = note;
    tail.start_tick = note.start_tick + kTicksPerBeat;
    tail.duration = original_end - tail.start_tick;
    tail.pitch = note.pitch;
    additions.push_back(tail);
    ++inserted;
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return inserted;
}

static size_t soundingNoteIndexAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  size_t best = notes.size();
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    if (note.voice != voice)
      continue;
    if (note.start_tick > tick || note.start_tick + note.duration <= tick) {
      continue;
    }
    if (best == notes.size() || note.start_tick >= notes[best].start_tick) {
      best = idx;
    }
  }
  return best;
}

static int repairResidualFlexibleParallels(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                           uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  int repairs = 0;

  auto source_repairable = [](BachNoteSource source) {
    return source == BachNoteSource::FreeCounterpoint ||
           source == BachNoteSource::CadenceApproach || source == BachNoteSource::EpisodeMaterial;
  };

  auto candidate_is_safe = [&](size_t idx, uint8_t pitch) {
    auto [lo, hi] = getFugueVoiceRange(all_notes[idx].voice, num_voices);
    if (pitch < lo || pitch > hi)
      return false;
    if (!vertical_safe(all_notes[idx].start_tick, all_notes[idx].voice, pitch)) {
      return false;
    }
    NoteEvent old = all_notes[idx];
    all_notes[idx].pitch = pitch;
    bool ok = true;
    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (const auto& note : all_notes) {
      if (note.voice != old.voice)
        continue;
      if (note.start_tick < old.start_tick &&
          (prev == nullptr || note.start_tick > prev->start_tick)) {
        prev = &note;
      } else if (note.start_tick > old.start_tick &&
                 (next == nullptr || note.start_tick < next->start_tick)) {
        next = &note;
      }
    }
    if (prev != nullptr && std::abs(static_cast<int>(pitch) - static_cast<int>(prev->pitch)) > 7) {
      ok = false;
    }
    if (next != nullptr && std::abs(static_cast<int>(next->pitch) - static_cast<int>(pitch)) > 7) {
      ok = false;
    }
    all_notes[idx] = old;
    return ok;
  };

  for (Tick beat = kTicksPerBeat; beat < plan.estimated_duration; beat += kTicksPerBeat) {
    for (uint8_t va = 0; va < num_voices; ++va) {
      for (uint8_t vb = va + 1; vb < num_voices; ++vb) {
        size_t prev_a = soundingNoteIndexAt(all_notes, va, beat - kTicksPerBeat);
        size_t prev_b = soundingNoteIndexAt(all_notes, vb, beat - kTicksPerBeat);
        size_t cur_a = soundingNoteIndexAt(all_notes, va, beat);
        size_t cur_b = soundingNoteIndexAt(all_notes, vb, beat);
        if (prev_a == all_notes.size() || prev_b == all_notes.size() || cur_a == all_notes.size() ||
            cur_b == all_notes.size()) {
          continue;
        }

        int prev_interval = std::abs(static_cast<int>(all_notes[prev_a].pitch) -
                                     static_cast<int>(all_notes[prev_b].pitch));
        int cur_interval = std::abs(static_cast<int>(all_notes[cur_a].pitch) -
                                    static_cast<int>(all_notes[cur_b].pitch));
        int motion_a =
            static_cast<int>(all_notes[cur_a].pitch) - static_cast<int>(all_notes[prev_a].pitch);
        int motion_b =
            static_cast<int>(all_notes[cur_b].pitch) - static_cast<int>(all_notes[prev_b].pitch);
        if (motion_a == 0 || motion_b == 0 || ((motion_a > 0) != (motion_b > 0))) {
          continue;
        }
        if (!interval_util::isPerfectConsonance(prev_interval) ||
            !interval_util::isPerfectConsonance(cur_interval)) {
          continue;
        }
        if (interval_util::compoundToSimple(prev_interval) !=
            interval_util::compoundToSimple(cur_interval)) {
          continue;
        }

        std::vector<size_t> repair_order;
        if (source_repairable(all_notes[cur_a].source))
          repair_order.push_back(cur_a);
        if (source_repairable(all_notes[cur_b].source))
          repair_order.push_back(cur_b);
        bool repaired = false;
        for (size_t idx : repair_order) {
          int base = static_cast<int>(all_notes[idx].pitch);
          for (int delta : {-1, 1, -2, 2}) {
            int cand_int = base + delta;
            if (cand_int < 0 || cand_int > 127)
              continue;
            uint8_t cand = static_cast<uint8_t>(cand_int);
            if (!candidate_is_safe(idx, cand))
              continue;
            all_notes[idx].pitch = cand;
            all_notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ParallelRepair);
            ++repairs;
            repaired = true;
            break;
          }
          if (repaired)
            break;
        }
      }
    }
  }

  return repairs;
}

static int repairResidualFreeCounterpointLeapResolutions(std::vector<NoteEvent>& all_notes,
                                                         const FuguePlan& plan,
                                                         uint8_t num_voices) {
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  int repairs = 0;

  auto repairable = [](BachNoteSource source) {
    return source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::CadenceApproach;
  };

  for (uint8_t voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == voice)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    });
    for (size_t pos = 0; pos + 2 < idxs.size(); ++pos) {
      size_t n1_idx = idxs[pos];
      size_t n2_idx = idxs[pos + 1];
      size_t n3_idx = idxs[pos + 2];
      NoteEvent& n1 = all_notes[n1_idx];
      NoteEvent& n2 = all_notes[n2_idx];
      NoteEvent& n3 = all_notes[n3_idx];
      if (!repairable(n2.source) || !repairable(n3.source))
        continue;
      int leap = static_cast<int>(n2.pitch) - static_cast<int>(n1.pitch);
      if (std::abs(leap) < 5)
        continue;
      int resolution = static_cast<int>(n3.pitch) - static_cast<int>(n2.pitch);
      bool resolved =
          resolution != 0 && std::abs(resolution) <= 2 && ((leap > 0) != (resolution > 0));
      if (resolved)
        continue;

      int dir = leap > 0 ? -1 : 1;
      auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
      for (int step : {1, 2}) {
        int cand_int = static_cast<int>(n2.pitch) + dir * step;
        if (cand_int < static_cast<int>(lo) || cand_int > static_cast<int>(hi)) {
          continue;
        }
        uint8_t cand = static_cast<uint8_t>(cand_int);
        bool locally_safe = true;
        for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
          if (other_voice == voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, other_voice, n3.start_tick);
          if (other == nullptr)
            continue;
          int diff = cand_int - static_cast<int>(other->pitch);
          if (other_voice < voice && diff >= -2)
            locally_safe = false;
          if (other_voice > voice && diff <= 2)
            locally_safe = false;
          int simple = interval_util::compoundToSimple(std::abs(diff));
          if (simple == 1 || simple == 6 || simple == 11)
            locally_safe = false;
        }
        if (!locally_safe)
          continue;
        if (pos + 3 < idxs.size()) {
          int next_leap = std::abs(static_cast<int>(all_notes[idxs[pos + 3]].pitch) - cand_int);
          if (next_leap > 7)
            continue;
        }
        n3.pitch = cand;
        n3.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++repairs;
        break;
      }
    }
  }

  return repairs;
}

static int repairResidualCadenceTritoneOutlines(std::vector<NoteEvent>& all_notes,
                                                uint8_t num_voices) {
  int repairs = 0;
  auto repairable = [](BachNoteSource source) {
    return source == BachNoteSource::CadenceApproach || source == BachNoteSource::Coda;
  };

  auto locally_safe = [&](size_t idx, int cand_int) {
    if (cand_int < 0 || cand_int > 127)
      return false;
    const auto& note = all_notes[idx];
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (cand_int < static_cast<int>(lo) || cand_int > static_cast<int>(hi)) {
      return false;
    }
    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, note.start_tick);
      if (other == nullptr)
        continue;
      int diff = cand_int - static_cast<int>(other->pitch);
      if (other_voice < note.voice && diff >= -2)
        return false;
      if (other_voice > note.voice && diff <= 2)
        return false;
      int simple = interval_util::compoundToSimple(std::abs(diff));
      if (simple == 1 || simple == 6 || simple == 11)
        return false;
    }
    return true;
  };

  for (uint8_t voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == voice)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    });
    if (idxs.size() < 3)
      continue;

    int last_trough = -1;
    int last_peak = -1;
    int first_dir =
        static_cast<int>(all_notes[idxs[1]].pitch) - static_cast<int>(all_notes[idxs[0]].pitch);
    if (first_dir > 0) {
      last_trough = all_notes[idxs[0]].pitch;
    } else if (first_dir < 0) {
      last_peak = all_notes[idxs[0]].pitch;
    }

    for (size_t pos = 1; pos + 1 < idxs.size(); ++pos) {
      size_t idx = idxs[pos];
      int prev_p = all_notes[idxs[pos - 1]].pitch;
      int cur_p = all_notes[idx].pitch;
      int next_p = all_notes[idxs[pos + 1]].pitch;
      int prev_dir = cur_p - prev_p;
      int next_dir = next_p - cur_p;
      if (!repairable(all_notes[idx].source)) {
        if (prev_dir > 0 && next_dir < 0)
          last_peak = cur_p;
        if (prev_dir < 0 && next_dir > 0)
          last_trough = cur_p;
        continue;
      }

      bool is_peak = prev_dir > 0 && next_dir < 0 && last_trough >= 0;
      bool is_trough = prev_dir < 0 && next_dir > 0 && last_peak >= 0;
      int reference = is_peak ? last_trough : (is_trough ? last_peak : -1);
      if (reference < 0 ||
          interval_util::compoundToSimple(std::abs(cur_p - reference)) != interval::kTritone) {
        if (prev_dir > 0 && next_dir < 0)
          last_peak = cur_p;
        if (prev_dir < 0 && next_dir > 0)
          last_trough = cur_p;
        continue;
      }

      for (int delta : {1, -1, 2, -2, 3, -3}) {
        int cand = cur_p + delta;
        if (interval_util::compoundToSimple(std::abs(cand - reference)) == interval::kTritone) {
          continue;
        }
        if (!locally_safe(idx, cand))
          continue;
        all_notes[idx].pitch = static_cast<uint8_t>(cand);
        all_notes[idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        cur_p = cand;
        ++repairs;
        break;
      }

      if (prev_dir > 0 && next_dir < 0)
        last_peak = cur_p;
      if (prev_dir < 0 && next_dir > 0)
        last_trough = cur_p;
    }
  }

  return repairs;
}

static int repairStructuralAnswerStrongBeatDissonances(std::vector<NoteEvent>& all_notes,
                                                       const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  auto target_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::CadenceApproach;
  };
  auto is_strong_dissonance = [&](uint8_t lhs_pitch, VoiceId lhs_voice, const NoteEvent& rhs) {
    int diff = std::abs(static_cast<int>(lhs_pitch) - static_cast<int>(rhs.pitch));
    if (diff == 0 || diff >= 36)
      return false;
    int simple = interval_util::compoundToSimple(diff);
    bool p4_upper = (simple == interval::kPerfect4th) && (lhs_voice < num_voices - 1) &&
                    (rhs.voice < num_voices - 1);
    return !interval_util::isConsonance(simple) && !p4_upper;
  };

  auto strong_beat_dissonance_count = [&](size_t note_idx, uint8_t pitch) {
    const NoteEvent& note = all_notes[note_idx];
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == note_idx)
          continue;
        const NoteEvent& other = all_notes[other_idx];
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        if (is_strong_dissonance(pitch, note.voice, other))
          ++count;
      }
    }
    return count;
  };

  auto beat_dissonance_count = [&](size_t note_idx, uint8_t pitch,
                                   const std::vector<size_t>& active) {
    const NoteEvent& note = all_notes[note_idx];
    int count = 0;
    for (size_t other_idx : active) {
      if (other_idx == note_idx)
        continue;
      const NoteEvent& other = all_notes[other_idx];
      if (other.voice == note.voice)
        continue;
      if (is_strong_dissonance(pitch, note.voice, other))
        ++count;
    }
    return count;
  };

  auto melodic_cost = [&](size_t note_idx, uint8_t pitch) {
    const NoteEvent& note = all_notes[note_idx];
    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == note_idx || all_notes[idx].voice != note.voice)
        continue;
      const NoteEvent& other = all_notes[idx];
      if (other.start_tick < note.start_tick &&
          (prev == nullptr || other.start_tick > prev->start_tick)) {
        prev = &other;
      } else if (other.start_tick > note.start_tick &&
                 (next == nullptr || other.start_tick < next->start_tick)) {
        next = &other;
      }
    }
    int cost = 0;
    if (prev != nullptr) {
      int old_gap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev->pitch));
      int new_gap = std::abs(static_cast<int>(pitch) - static_cast<int>(prev->pitch));
      if (new_gap > std::max(old_gap, 7))
        return INT32_MAX;
      cost += new_gap;
    }
    if (next != nullptr) {
      int old_gap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(next->pitch));
      int new_gap = std::abs(static_cast<int>(pitch) - static_cast<int>(next->pitch));
      if (new_gap > std::max(old_gap, 7))
        return INT32_MAX;
      cost += new_gap;
    }
    return cost;
  };

  int repairs = 0;
  constexpr int kMaxRepairs = 8;

  auto spacing_ok_at = [&](size_t note_idx, uint8_t pitch, Tick tick) {
    const NoteEvent& note = all_notes[note_idx];
    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
      if (other == nullptr)
        continue;
      int diff = static_cast<int>(pitch) - static_cast<int>(other->pitch);
      if (other_voice < note.voice && diff >= -2)
        return false;
      if (other_voice > note.voice && diff <= 2)
        return false;
    }
    if (note.voice > 0) {
      const NoteEvent* upper = soundingNoteAt(all_notes, note.voice - 1, tick);
      if (upper != nullptr) {
        int gap = static_cast<int>(upper->pitch) - static_cast<int>(pitch);
        if (gap < 3 || gap > 16)
          return false;
      }
    }
    uint8_t manual_voice_count = (num_voices >= 4) ? 3 : std::min<uint8_t>(num_voices, 2);
    if (note.voice + 1 < manual_voice_count) {
      const NoteEvent* lower = soundingNoteAt(all_notes, note.voice + 1, tick);
      if (lower != nullptr) {
        int gap = static_cast<int>(pitch) - static_cast<int>(lower->pitch);
        if (gap < 3 || gap > 16)
          return false;
      }
    } else if (num_voices >= 4 && note.voice == 2) {
      const NoteEvent* pedal = soundingNoteAt(all_notes, num_voices - 1, tick);
      if (pedal != nullptr) {
        int gap = static_cast<int>(pitch) - static_cast<int>(pedal->pitch);
        if (gap < 7 || gap > 31)
          return false;
      }
    }
    return true;
  };

  auto spacing_ok_for_strong_beats = [&](size_t note_idx, uint8_t pitch) {
    const NoteEvent& note = all_notes[note_idx];
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      if (!spacing_ok_at(note_idx, pitch, beat))
        return false;
    }
    return true;
  };

  for (Tick beat = 0; beat < plan.estimated_duration && repairs < kMaxRepairs;
       beat += kTicksPerBeat) {
    if (!isStrongBeatInBar(beat))
      continue;

    std::vector<size_t> active;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const NoteEvent& note = all_notes[idx];
      if (note.start_tick <= beat && note.start_tick + note.duration > beat) {
        active.push_back(idx);
      }
    }

    for (size_t a_pos = 0; a_pos < active.size() && repairs < kMaxRepairs; ++a_pos) {
      for (size_t b_pos = a_pos + 1; b_pos < active.size() && repairs < kMaxRepairs; ++b_pos) {
        size_t a = active[a_pos];
        size_t b = active[b_pos];
        const NoteEvent& na = all_notes[a];
        const NoteEvent& nb = all_notes[b];
        if (na.voice == nb.voice)
          continue;
        if (!is_strong_dissonance(na.pitch, na.voice, nb))
          continue;

        size_t target = all_notes.size();
        if (na.source == BachNoteSource::FugueAnswer && target_source(nb.source)) {
          target = b;
        } else if (nb.source == BachNoteSource::FugueAnswer && target_source(na.source)) {
          target = a;
        }
        if (target == all_notes.size())
          continue;

        const NoteEvent& target_note = all_notes[target];
        size_t answer_idx = (all_notes[a].source == BachNoteSource::FugueAnswer) ? a : b;
        auto [lo, hi] = getFugueVoiceRange(target_note.voice, num_voices);
        int old_bad = beat_dissonance_count(target, target_note.pitch, active);
        int old_total_bad = strong_beat_dissonance_count(target, target_note.pitch);
        int best_pitch = -1;
        int best_bad = old_bad;
        int best_cost = INT32_MAX;

        for (int delta : {1, -1, 2, -2, 3, -3, 4, -4, 5, -5}) {
          int cand_int = static_cast<int>(target_note.pitch) + delta;
          if (cand_int < static_cast<int>(lo) || cand_int > static_cast<int>(hi)) {
            continue;
          }
          uint8_t cand = static_cast<uint8_t>(cand_int);
          if (!spacing_ok_for_strong_beats(target, cand))
            continue;
          int bad = beat_dissonance_count(target, cand, active);
          if (bad >= old_bad)
            continue;
          int total_bad = strong_beat_dissonance_count(target, cand);
          if (total_bad > old_total_bad)
            continue;
          int local_cost = melodic_cost(target, cand);
          if (local_cost == INT32_MAX)
            continue;
          int cost = bad * 100 + std::abs(delta) * 4 + local_cost;
          if (cost < best_cost) {
            best_cost = cost;
            best_bad = bad;
            best_pitch = cand_int;
          }
        }

        if (best_pitch < 0 && target_note.source == BachNoteSource::CadenceApproach) {
          const NoteEvent& answer = all_notes[answer_idx];
          for (int delta : {1, -1, 2, -2, 3, -3, 4, -4, 5, -5}) {
            int cand_int = static_cast<int>(target_note.pitch) + delta;
            if (cand_int < static_cast<int>(lo) || cand_int > static_cast<int>(hi)) {
              continue;
            }
            uint8_t cand = static_cast<uint8_t>(cand_int);
            if (is_strong_dissonance(cand, target_note.voice, answer)) {
              continue;
            }
            if (!spacing_ok_for_strong_beats(target, cand))
              continue;
            if (beat_dissonance_count(target, cand, active) > 0) {
              continue;
            }
            int local_cost = melodic_cost(target, cand);
            if (local_cost == INT32_MAX)
              continue;
            best_pitch = cand_int;
            best_bad = 0;
            break;
          }
        }

        if (best_pitch < 0 || best_bad >= old_bad)
          continue;
        all_notes[target].pitch = static_cast<uint8_t>(best_pitch);
        all_notes[target].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
        ++repairs;
      }
    }
  }

  for (size_t idx = 0; idx < all_notes.size() && repairs < kMaxRepairs; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::CadenceApproach)
      continue;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < end_tick && repairs < kMaxRepairs; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      std::vector<size_t> active;
      bool answer_clash = false;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        const NoteEvent& other = all_notes[other_idx];
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        active.push_back(other_idx);
        if (other_idx != idx && other.source == BachNoteSource::FugueAnswer &&
            is_strong_dissonance(note.pitch, note.voice, other)) {
          answer_clash = true;
        }
      }
      if (!answer_clash)
        continue;

      auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
      int old_total_bad = strong_beat_dissonance_count(idx, note.pitch);
      int best_pitch = -1;
      int best_cost = INT32_MAX;
      for (int delta : {1, -1, 2, -2, 3, -3, 4, -4, 5, -5}) {
        int cand_int = static_cast<int>(note.pitch) + delta;
        if (cand_int < static_cast<int>(lo) || cand_int > static_cast<int>(hi)) {
          continue;
        }
        uint8_t cand = static_cast<uint8_t>(cand_int);
        if (!spacing_ok_for_strong_beats(idx, cand))
          continue;
        if (beat_dissonance_count(idx, cand, active) > 0)
          continue;
        int total_bad = strong_beat_dissonance_count(idx, cand);
        if (total_bad > old_total_bad)
          continue;
        int local_cost = melodic_cost(idx, cand);
        if (local_cost == INT32_MAX)
          continue;
        int cost = total_bad * 100 + std::abs(delta) * 4 + local_cost;
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand_int;
        }
      }
      if (best_pitch < 0)
        continue;
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
      ++repairs;
    }
  }

  return repairs;
}

static bool isBwv578ManualFigurableSource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::FreeCounterpoint ||
         source == BachNoteSource::Countersubject || source == BachNoteSource::CadenceApproach;
}

static int splitBwv578ManualLongFlexibleNotes(std::vector<NoteEvent>& all_notes,
                                              const FuguePlan& plan,
                                              const FugueStructure& structure, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  const auto stretto_sections = structure.getSectionsByType(SectionType::Stretto);
  auto in_stretto_section = [&](Tick tick) {
    for (const auto& section : stretto_sections) {
      if (tick >= section.start_tick && tick < section.end_tick) {
        return true;
      }
    }
    return false;
  };

  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  std::vector<NoteEvent> additions;
  int inserted = 0;
  constexpr int kMaxInserted = 220;
  const size_t original_size = all_notes.size();
  bool preserved_stretto_tail_pulse[4] = {false, false, false, false};

  for (size_t idx = 0; idx < original_size && inserted < kMaxInserted; ++idx) {
    NoteEvent& note = all_notes[idx];
    if (!isBwv578ManualFigurableSource(note.source))
      continue;
    if (note.voice != 0 && note.voice != 2)
      continue;
    if (note.duration < duration::kEighthNote)
      continue;
    if (note.source == BachNoteSource::EpisodeMaterial && in_stretto_section(note.start_tick) &&
        note.duration == duration::kEighthNote && note.voice < 4 &&
        !preserved_stretto_tail_pulse[note.voice]) {
      preserved_stretto_tail_pulse[note.voice] = true;
      continue;
    }
    if (note.source == BachNoteSource::Countersubject) {
      bool early_manual_i_window = note.voice == 0 && note.start_tick >= kTicksPerBar * 3 &&
                                   note.start_tick < kTicksPerBar * 5;
      if (early_manual_i_window) {
        continue;
      }
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    Key key = plan.tonal_plan.keyAtTick(note.start_tick);
    int base_deg = scale_util::pitchToAbsoluteDegree(note.pitch, key, ScaleType::Major);
    std::vector<uint8_t> neighbors;
    for (int delta_deg : {1, -1, 2, -2}) {
      uint8_t pitch =
          scale_util::absoluteDegreeToPitch(base_deg + delta_deg, key, ScaleType::Major);
      pitch = clampPitch(static_cast<int>(pitch), lo, hi);
      if (pitch == note.pitch)
        continue;
      if (std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch)) > 3) {
        continue;
      }
      if (std::find(neighbors.begin(), neighbors.end(), pitch) == neighbors.end()) {
        neighbors.push_back(pitch);
      }
    }
    if (neighbors.empty())
      continue;

    auto weak_neighbor_safe = [&](Tick tick, uint8_t cand) {
      if (tick % kTicksPerBeat == 0)
        return false;
      if (std::abs(static_cast<int>(cand) - static_cast<int>(note.pitch)) > 3) {
        return false;
      }
      for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
        if (other_voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, other_voice, tick);
        if (other == nullptr)
          continue;
        int dist = std::abs(static_cast<int>(cand) - static_cast<int>(other->pitch));
        if (dist == 0 && other_voice < 3)
          return false;
        if (dist <= 24) {
          int simple = interval_util::compoundToSimple(dist);
          if (simple == 1 || simple == 6 || simple == 11)
            return false;
        }
      }
      return true;
    };

    Tick original_end = note.start_tick + note.duration;
    Tick split_window =
        (note.voice == 0 || note.voice == 2) ? duration::kHalfNote : duration::kQuarterNote;
    Tick split_unit = duration::kSixteenthNote;
    if (note.source == BachNoteSource::Countersubject && note.voice == 0 &&
        note.start_tick >= kTicksPerBar * 2 && note.start_tick < kTicksPerBar * 3) {
      split_unit = duration::kEighthNote;
    }
    Tick split_end = std::min<Tick>(original_end, note.start_tick + split_window);
    std::vector<NoteEvent> local_additions;
    Tick tick = note.start_tick + split_unit;
    int slot = 1;
    bool ok = true;
    while (tick + split_unit <= split_end &&
           inserted + static_cast<int>(local_additions.size()) < kMaxInserted) {
      uint8_t pitch = note.pitch;
      if (slot % 2 == 1) {
        pitch = 0;
        for (uint8_t cand : neighbors) {
          if (newManualFillerIsSafe(all_notes, vertical_safe, tick, note.voice, cand, num_voices) ||
              weak_neighbor_safe(tick, cand)) {
            pitch = cand;
            break;
          }
        }
        if (pitch == 0)
          pitch = note.pitch;
      }

      NoteEvent fig = note;
      fig.start_tick = tick;
      fig.duration = split_unit;
      fig.pitch = pitch;
      local_additions.push_back(fig);
      tick += split_unit;
      ++slot;
    }

    if (!ok || local_additions.empty())
      continue;
    note.duration = split_unit;
    inserted += static_cast<int>(local_additions.size());
    additions.insert(additions.end(), local_additions.begin(), local_additions.end());
  }

  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return inserted;
}

/// @brief Tighten excessive spacing between adjacent manual voices.
///
/// The Python analyzer flags manual/manual gaps above an octave, while pedal
/// gaps are allowed up to three octaves.  Keep structural material intact and
/// only octave-shift flexible notes when the whole note remains vertically safe.
static int reduceAdjacentManualSpacing(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                       uint8_t num_voices) {
  if (num_voices < 3)
    return 0;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  int repairs = 0;
  Tick total_ticks = plan.estimated_duration;
  uint8_t manual_voice_count = (num_voices >= 4) ? 3 : std::min<uint8_t>(num_voices, 2);
  uint8_t manual_pairs = manual_voice_count > 0 ? manual_voice_count - 1 : 0;

  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    for (uint8_t upper_voice = 0; upper_voice < manual_pairs; ++upper_voice) {
      uint8_t lower_voice = upper_voice + 1;
      size_t upper_idx = all_notes.size();
      size_t lower_idx = all_notes.size();
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        const auto& note = all_notes[idx];
        if (note.start_tick > beat || note.start_tick + note.duration <= beat)
          continue;
        if (note.voice == upper_voice &&
            (upper_idx == all_notes.size() || note.start_tick >= all_notes[upper_idx].start_tick)) {
          upper_idx = idx;
        } else if (note.voice == lower_voice &&
                   (lower_idx == all_notes.size() ||
                    note.start_tick >= all_notes[lower_idx].start_tick)) {
          lower_idx = idx;
        }
      }
      if (upper_idx == all_notes.size() || lower_idx == all_notes.size())
        continue;

      int gap = static_cast<int>(all_notes[upper_idx].pitch) -
                static_cast<int>(all_notes[lower_idx].pitch);
      if (gap <= 12)
        continue;

      struct Candidate {
        size_t idx;
        uint8_t pitch;
        int cost;
      };
      std::vector<Candidate> candidates;
      auto add_candidate = [&](size_t idx, int pitch) {
        if (pitch < 0 || pitch > 127)
          return;
        if (getProtectionLevel(all_notes[idx].source) != ProtectionLevel::Flexible) {
          return;
        }
        uint8_t cand = static_cast<uint8_t>(pitch);
        if (!spacingCandidateIsSafe(all_notes, vertical_safe, idx, cand, num_voices)) {
          return;
        }
        candidates.push_back({idx, cand, std::abs(pitch - static_cast<int>(all_notes[idx].pitch))});
      };

      add_candidate(upper_idx, static_cast<int>(all_notes[upper_idx].pitch) - 12);
      add_candidate(lower_idx, static_cast<int>(all_notes[lower_idx].pitch) + 12);
      if (candidates.empty())
        continue;

      auto best = std::min_element(
          candidates.begin(), candidates.end(),
          [](const Candidate& lhs, const Candidate& rhs) { return lhs.cost < rhs.cost; });
      if (best->pitch != all_notes[best->idx].pitch) {
        all_notes[best->idx].pitch = best->pitch;
        all_notes[best->idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
        ++repairs;
      }
    }
  }
  return repairs;
}

static int foldOutOfRangeNotes(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  int repairs = 0;
  for (auto& note : all_notes) {
    if (note.source == BachNoteSource::FreeCounterpoint)
      continue;
    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int pitch = static_cast<int>(note.pitch);
    int folded = pitch;
    while (folded < static_cast<int>(lo) && folded + 12 <= 127) {
      folded += 12;
    }
    while (folded > static_cast<int>(hi) && folded - 12 >= 0) {
      folded -= 12;
    }
    if (folded < static_cast<int>(lo) || folded > static_cast<int>(hi)) {
      folded = clampPitch(folded, lo, hi);
    }
    if (folded != pitch) {
      note.pitch = static_cast<uint8_t>(folded);
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
      ++repairs;
    }
  }
  return repairs;
}

static int repairResidualEpisodeExcessiveLeaps(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan, uint8_t num_voices) {
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  int repairs = 0;
  constexpr int kMaxResidualRepairs = 4;
  for (VoiceId vid = 0; vid < num_voices && repairs < kMaxResidualRepairs; ++vid) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == vid)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    });

    for (size_t pos = 1; pos < idxs.size() && repairs < kMaxResidualRepairs; ++pos) {
      size_t curr_idx = idxs[pos];
      const size_t prev_idx = idxs[pos - 1];
      NoteEvent& curr = all_notes[curr_idx];
      const NoteEvent& prev = all_notes[prev_idx];
      if (curr.source != BachNoteSource::EpisodeMaterial ||
          prev.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }

      int leap = static_cast<int>(curr.pitch) - static_cast<int>(prev.pitch);
      if (std::abs(leap) <= 13)
        continue;

      int best = -1;
      int best_cost = INT32_MAX;
      int old_pitch = static_cast<int>(curr.pitch);
      int prev_pitch = static_cast<int>(prev.pitch);
      for (int cand = old_pitch - 12; cand <= old_pitch + 12; ++cand) {
        if (cand == old_pitch || cand < 0 || cand > 127)
          continue;
        int new_leap = std::abs(cand - prev_pitch);
        if (new_leap > 12)
          continue;
        if (!spacingCandidateIsSafe(all_notes, vertical_safe, curr_idx, static_cast<uint8_t>(cand),
                                    num_voices)) {
          continue;
        }
        if (pos + 1 < idxs.size()) {
          int next_pitch = static_cast<int>(all_notes[idxs[pos + 1]].pitch);
          int old_next = std::abs(old_pitch - next_pitch);
          int new_next = std::abs(cand - next_pitch);
          if (new_next > 12 && new_next > old_next)
            continue;
        }
        int cost = std::abs(cand - old_pitch) + new_leap;
        if (cost < best_cost) {
          best_cost = cost;
          best = cand;
        }
      }

      if (best >= 0) {
        curr.pitch = static_cast<uint8_t>(best);
        curr.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
        ++repairs;
      }
    }
  }
  return repairs;
}

static int repairSustainedPedalEpisodeDissonances(std::vector<NoteEvent>& all_notes,
                                                  const FuguePlan& /*plan*/, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  int repairs = 0;
  constexpr int kMaxRepairs = 2;
  VoiceId pedal_voice = num_voices - 1;
  auto [pedal_lo, pedal_hi] = getFugueVoiceRange(pedal_voice, num_voices);

  auto beat_dissonance_count = [&](size_t note_idx, uint8_t pitch, Tick beat) {
    const auto& note = all_notes[note_idx];
    int count = 0;
    if (!isStrongBeatInBar(beat))
      return 0;
    for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
      if (other_idx == note_idx)
        continue;
      const auto& other = all_notes[other_idx];
      if (other.voice == note.voice)
        continue;
      if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
        continue;
      }
      int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other.pitch));
      if (diff == 0 || diff >= 36)
        continue;
      int simple = interval_util::compoundToSimple(diff);
      bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                      (other.voice < num_voices - 1);
      if (!interval_util::isConsonance(simple) && !p4_upper) {
        ++count;
      }
    }
    return count;
  };

  auto neighbor_leap_ok = [&](size_t note_idx, uint8_t pitch) {
    const auto& note = all_notes[note_idx];
    const NoteEvent* prev = nullptr;
    const NoteEvent* next = nullptr;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (idx == note_idx || all_notes[idx].voice != note.voice)
        continue;
      const auto& other = all_notes[idx];
      if (other.start_tick < note.start_tick &&
          (prev == nullptr || other.start_tick > prev->start_tick)) {
        prev = &other;
      } else if (other.start_tick > note.start_tick &&
                 (next == nullptr || other.start_tick < next->start_tick)) {
        next = &other;
      }
    }
    auto leap_is_ok = [&](const NoteEvent* neighbor) {
      if (neighbor == nullptr)
        return true;
      int old_gap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(neighbor->pitch));
      int new_gap = std::abs(static_cast<int>(pitch) - static_cast<int>(neighbor->pitch));
      return new_gap <= 18 || new_gap <= old_gap;
    };
    return leap_is_ok(prev) && leap_is_ok(next);
  };

  for (size_t idx = 0; idx < all_notes.size() && repairs < kMaxRepairs; ++idx) {
    auto& note = all_notes[idx];
    if (note.voice != pedal_voice || note.source != BachNoteSource::EpisodeMaterial ||
        note.duration < duration::kQuarterNote || note.start_tick % kTicksPerBeat == 0) {
      continue;
    }

    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick note_end = note.start_tick + note.duration;
    Tick target_beat = -1;
    int current_bad = 0;
    for (Tick beat = first_beat; beat < note_end; beat += kTicksPerBeat) {
      int bad = beat_dissonance_count(idx, note.pitch, beat);
      if (bad > 0) {
        target_beat = beat;
        current_bad = bad;
        break;
      }
    }
    if (target_beat < 0 || current_bad == 0)
      continue;

    int best_pitch = -1;
    int best_bad = current_bad;
    int best_cost = INT32_MAX;
    for (int delta : {-2, -1, 1, 2, -3, 3, -4, 4}) {
      int cand = static_cast<int>(note.pitch) + delta;
      if (cand < static_cast<int>(pedal_lo) || cand > static_cast<int>(pedal_hi)) {
        continue;
      }
      if (!neighbor_leap_ok(idx, static_cast<uint8_t>(cand)))
        continue;

      int bad = beat_dissonance_count(idx, static_cast<uint8_t>(cand), target_beat);
      if (bad >= current_bad)
        continue;
      int cost = bad * 100 + std::abs(delta);
      if (cost < best_cost) {
        best_cost = cost;
        best_bad = bad;
        best_pitch = cand;
      }
    }

    if (best_pitch < 0 || best_bad >= current_bad)
      continue;
    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }

  return repairs;
}

static int repairPedalCadenceApproachTritones(std::vector<NoteEvent>& all_notes,
                                              uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  VoiceId pedal_voice = num_voices - 1;
  auto [pedal_lo, pedal_hi] = getFugueVoiceRange(pedal_voice, num_voices);
  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (all_notes[idx].voice == pedal_voice)
      idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
  });

  auto source_is_exempt = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::ArpeggioFlow ||
           source == BachNoteSource::ToccataFigure || source == BachNoteSource::ToccataGesture;
  };
  auto outline_count_with_override = [&](size_t override_idx, int pitch) {
    int count = 0;
    int last_trough = INT32_MIN;
    int last_peak = INT32_MIN;
    if (idxs.size() >= 2) {
      int first = static_cast<int>(all_notes[idxs[0]].pitch);
      int second = static_cast<int>(all_notes[idxs[1]].pitch);
      int first_dir = second - first;
      if (first_dir > 0) {
        last_trough = first;
      } else if (first_dir < 0) {
        last_peak = first;
      }
    }

    auto pitch_at = [&](size_t pos) {
      size_t idx = idxs[pos];
      if (idx == override_idx)
        return pitch;
      return static_cast<int>(all_notes[idx].pitch);
    };

    for (size_t pos = 1; pos + 1 < idxs.size(); ++pos) {
      const NoteEvent& cur = all_notes[idxs[pos]];
      if (source_is_exempt(cur.source))
        continue;
      int prev_p = pitch_at(pos - 1);
      int cur_p = pitch_at(pos);
      int next_p = pitch_at(pos + 1);
      int prev_dir = cur_p - prev_p;
      int next_dir = next_p - cur_p;
      if (prev_dir > 0 && next_dir < 0) {
        if (cur.source == BachNoteSource::CadenceApproach && last_trough != INT32_MIN &&
            interval_util::compoundToSimple(cur_p - last_trough) == interval::kTritone) {
          ++count;
        }
        last_peak = cur_p;
      } else if (prev_dir < 0 && next_dir > 0) {
        if (cur.source == BachNoteSource::CadenceApproach && last_peak != INT32_MIN &&
            interval_util::compoundToSimple(cur_p - last_peak) == interval::kTritone) {
          ++count;
        }
        last_trough = cur_p;
      }
    }
    return count;
  };
  auto strong_beat_dissonance_count = [&](size_t note_idx, int pitch) {
    const auto& note = all_notes[note_idx];
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == note_idx)
          continue;
        const auto& other = all_notes[other_idx];
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(pitch - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper) {
          ++count;
        }
      }
    }
    return count;
  };

  int repairs = 0;
  for (size_t pos = 1; pos + 1 < idxs.size() && repairs < 2; ++pos) {
    size_t curr_idx = idxs[pos];
    NoteEvent& curr = all_notes[curr_idx];
    if (curr.source != BachNoteSource::CadenceApproach)
      continue;

    int old_bad = outline_count_with_override(curr_idx, curr.pitch);
    if (old_bad == 0)
      continue;

    int best_pitch = -1;
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    int old_strong_bad = strong_beat_dissonance_count(curr_idx, curr.pitch);
    for (int delta : {-5, 4, -6, 6, -4, 5, -3, 3, -2, 2, -1, 1}) {
      int cand = static_cast<int>(curr.pitch) + delta;
      if (cand < static_cast<int>(pedal_lo) || cand > static_cast<int>(pedal_hi)) {
        continue;
      }
      int new_bad = outline_count_with_override(curr_idx, cand);
      if (new_bad >= old_bad)
        continue;
      if (strong_beat_dissonance_count(curr_idx, cand) > old_strong_bad) {
        continue;
      }

      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      const NoteEvent& next = all_notes[idxs[pos + 1]];
      int prev_gap = std::abs(static_cast<int>(curr.pitch) - static_cast<int>(prev.pitch));
      int next_gap = std::abs(static_cast<int>(curr.pitch) - static_cast<int>(next.pitch));
      int new_prev_gap = std::abs(cand - static_cast<int>(prev.pitch));
      int new_next_gap = std::abs(cand - static_cast<int>(next.pitch));
      if (new_prev_gap > std::max(prev_gap, 12) || new_next_gap > std::max(next_gap, 12)) {
        continue;
      }

      int cost = new_bad * 100 + std::abs(delta);
      if (cost < best_cost) {
        best_cost = cost;
        best_bad = new_bad;
        best_pitch = cand;
      }
    }

    if (best_pitch < 0 || best_bad >= old_bad)
      continue;
    curr.pitch = static_cast<uint8_t>(best_pitch);
    curr.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
    ++repairs;
  }

  return repairs;
}

static int repairManualCountersubjectTritoneOutlines(std::vector<NoteEvent>& all_notes,
                                                     const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);

  auto source_is_exempt = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::ArpeggioFlow ||
           source == BachNoteSource::ToccataFigure || source == BachNoteSource::ToccataGesture;
  };

  auto outline_count = [&](const std::vector<size_t>& idxs, size_t override_idx,
                           int override_pitch) {
    int count = 0;
    int last_trough = INT32_MIN;
    int last_peak = INT32_MIN;
    if (idxs.size() >= 2) {
      int first = static_cast<int>(all_notes[idxs[0]].pitch);
      int second = static_cast<int>(all_notes[idxs[1]].pitch);
      int first_dir = second - first;
      if (first_dir > 0) {
        last_trough = first;
      } else if (first_dir < 0) {
        last_peak = first;
      }
    }

    auto pitch_at = [&](size_t pos) {
      size_t idx = idxs[pos];
      if (idx == override_idx)
        return override_pitch;
      return static_cast<int>(all_notes[idx].pitch);
    };

    for (size_t pos = 1; pos + 1 < idxs.size(); ++pos) {
      const NoteEvent& cur = all_notes[idxs[pos]];
      if (source_is_exempt(cur.source))
        continue;
      int prev_p = pitch_at(pos - 1);
      int cur_p = pitch_at(pos);
      int next_p = pitch_at(pos + 1);
      int prev_dir = cur_p - prev_p;
      int next_dir = next_p - cur_p;
      if (prev_dir > 0 && next_dir < 0) {
        if (last_trough != INT32_MIN &&
            interval_util::compoundToSimple(cur_p - last_trough) == interval::kTritone) {
          ++count;
        }
        last_peak = cur_p;
      } else if (prev_dir < 0 && next_dir > 0) {
        if (last_peak != INT32_MIN &&
            interval_util::compoundToSimple(cur_p - last_peak) == interval::kTritone) {
          ++count;
        }
        last_trough = cur_p;
      }
    }
    return count;
  };

  auto strong_beat_dissonance_count = [&](size_t note_idx, int pitch) {
    const auto& note = all_notes[note_idx];
    int count = 0;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (!isStrongBeatInBar(beat))
        continue;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == note_idx)
          continue;
        const auto& other = all_notes[other_idx];
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
          continue;
        }
        int diff = std::abs(pitch - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = (simple == interval::kPerfect4th) && (note.voice < num_voices - 1) &&
                        (other.voice < num_voices - 1);
        if (!interval_util::isConsonance(simple) && !p4_upper) {
          ++count;
        }
      }
    }
    return count;
  };

  int repairs = 0;
  for (VoiceId voice = 0; voice < num_voices - 1 && repairs < 2; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == voice)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    });
    if (idxs.size() < 3)
      continue;

    auto voice_range = getFugueVoiceRange(voice, num_voices);
    uint8_t lo = voice_range.first;
    uint8_t hi = voice_range.second;
    for (size_t pos = 1; pos + 1 < idxs.size() && repairs < 2; ++pos) {
      size_t curr_idx = idxs[pos];
      NoteEvent& curr = all_notes[curr_idx];
      if (curr.source != BachNoteSource::Countersubject)
        continue;

      int old_bad = outline_count(idxs, curr_idx, curr.pitch);
      if (old_bad == 0)
        continue;

      int old_strong_bad = strong_beat_dissonance_count(curr_idx, curr.pitch);
      int best_pitch = -1;
      int best_bad = old_bad;
      int best_cost = INT32_MAX;
      for (int delta : {-1, 1}) {
        int cand = static_cast<int>(curr.pitch) + delta;
        if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi)) {
          continue;
        }
        if (!spacingCandidateIsSafe(all_notes, vertical_safe, curr_idx, static_cast<uint8_t>(cand),
                                    num_voices)) {
          continue;
        }
        int new_bad = outline_count(idxs, curr_idx, cand);
        if (new_bad >= old_bad)
          continue;
        if (strong_beat_dissonance_count(curr_idx, cand) > old_strong_bad) {
          continue;
        }
        const NoteEvent& prev = all_notes[idxs[pos - 1]];
        const NoteEvent& next = all_notes[idxs[pos + 1]];
        int old_prev_gap = std::abs(static_cast<int>(curr.pitch) - static_cast<int>(prev.pitch));
        int old_next_gap = std::abs(static_cast<int>(curr.pitch) - static_cast<int>(next.pitch));
        int new_prev_gap = std::abs(cand - static_cast<int>(prev.pitch));
        int new_next_gap = std::abs(cand - static_cast<int>(next.pitch));
        auto close_to = [](const NoteEvent& lhs, const NoteEvent& rhs) {
          Tick lhs_end = lhs.start_tick + lhs.duration;
          Tick gap = rhs.start_tick > lhs_end ? rhs.start_tick - lhs_end : 0;
          return gap <= duration::kHalfNote;
        };
        bool early_counterline = curr.start_tick < kTicksPerBar * 8 &&
                                 prev.source == BachNoteSource::Countersubject &&
                                 next.source == BachNoteSource::Countersubject;
        if (early_counterline && ((close_to(prev, curr) && new_prev_gap > interval::kPerfect5th) ||
                                  (close_to(curr, next) && new_next_gap > interval::kPerfect5th))) {
          continue;
        }
        if (new_prev_gap > std::max(old_prev_gap, 12) ||
            new_next_gap > std::max(old_next_gap, 12)) {
          continue;
        }
        int cost = new_bad * 100 + std::abs(delta);
        if (cost < best_cost) {
          best_cost = cost;
          best_bad = new_bad;
          best_pitch = cand;
        }
      }

      if (best_pitch < 0 || best_bad >= old_bad)
        continue;
      curr.pitch = static_cast<uint8_t>(best_pitch);
      curr.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
      ++repairs;
    }
  }

  return repairs;
}

static int repairLowerManualCountersubjectTailDrops(std::vector<NoteEvent>& all_notes,
                                                    uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kLowerManualVoice = 2;
  auto [lo, hi] = getFugueVoiceRange(kLowerManualVoice, num_voices);
  (void)hi;

  auto sounding_pitch = [&](VoiceId voice, Tick tick) -> int {
    for (const auto& note : all_notes) {
      if (note.voice != voice)
        continue;
      if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
        return static_cast<int>(note.pitch);
      }
    }
    return -1;
  };

  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (all_notes[idx].voice == kLowerManualVoice &&
        all_notes[idx].source == BachNoteSource::Countersubject) {
      idxs.push_back(idx);
    }
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
  });

  int repairs = 0;
  for (size_t pos = 1; pos + 3 < idxs.size(); ++pos) {
    NoteEvent& n0 = all_notes[idxs[pos]];
    NoteEvent& n1 = all_notes[idxs[pos + 1]];
    NoteEvent& n2 = all_notes[idxs[pos + 2]];
    NoteEvent& n3 = all_notes[idxs[pos + 3]];
    const NoteEvent& prev = all_notes[idxs[pos - 1]];

    if (n0.duration != duration::kSixteenthNote || n1.duration != duration::kSixteenthNote ||
        n2.duration != duration::kSixteenthNote || n3.duration != duration::kSixteenthNote) {
      continue;
    }
    if (n1.start_tick != n0.start_tick + duration::kSixteenthNote ||
        n2.start_tick != n1.start_tick + duration::kSixteenthNote ||
        n3.start_tick != n2.start_tick + duration::kSixteenthNote) {
      continue;
    }
    if ((n0.start_tick / kTicksPerBar) != (n3.start_tick / kTicksPerBar)) {
      continue;
    }
    if (n0.start_tick % kTicksPerBar < kTicksPerBeat * 3)
      continue;

    int prev_pitch = static_cast<int>(prev.pitch);
    if (prev_pitch - static_cast<int>(n0.pitch) < 8)
      continue;

    int pattern[4] = {prev_pitch - 7, prev_pitch - 5, prev_pitch - 7, prev_pitch - 10};
    bool safe = true;
    for (int i = 0; i < 4; ++i) {
      if (pattern[i] < static_cast<int>(lo)) {
        safe = false;
        break;
      }
      int upper = sounding_pitch(1, all_notes[idxs[pos + i]].start_tick);
      if (upper >= 0 && pattern[i] > upper - 3) {
        safe = false;
        break;
      }
    }
    if (!safe)
      continue;

    NoteEvent* notes[4] = {&n0, &n1, &n2, &n3};
    for (int i = 0; i < 4; ++i) {
      notes[i]->pitch = static_cast<uint8_t>(pattern[i]);
      notes[i]->modified_by &=
          static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
      ++repairs;
    }
  }

  return repairs;
}

static int repairUpperManualHiddenUnisonLandings(std::vector<NoteEvent>& all_notes,
                                                 uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  auto find_prev = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    const NoteEvent* best = nullptr;
    for (const auto& note : all_notes) {
      if (note.voice != voice)
        continue;
      if (note.start_tick >= tick)
        continue;
      if (!best || note.start_tick > best->start_tick)
        best = &note;
    }
    return best;
  };

  auto [upper_lo, upper_hi] = getFugueVoiceRange(0, num_voices);
  int repairs = 0;
  for (auto& lower : all_notes) {
    if (lower.voice != 2 || lower.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    for (auto& upper : all_notes) {
      if (upper.voice != 0 || upper.source != BachNoteSource::EpisodeMaterial ||
          upper.duration > duration::kEighthNote) {
        continue;
      }
      if (upper.start_tick > lower.start_tick ||
          upper.start_tick + upper.duration <= lower.start_tick) {
        continue;
      }
      if (upper.pitch != lower.pitch)
        continue;

      const NoteEvent* prev_upper = find_prev(0, upper.start_tick);
      if (!prev_upper)
        continue;
      int prev_pitch = static_cast<int>(prev_upper->pitch);
      int upper_pitch = static_cast<int>(upper.pitch);
      int lower_pitch = static_cast<int>(lower.pitch);
      if (prev_pitch - upper_pitch < 5)
        continue;

      int candidate = prev_pitch - 2;
      if (candidate < static_cast<int>(upper_lo) || candidate > static_cast<int>(upper_hi) ||
          candidate < lower_pitch + 3) {
        continue;
      }

      upper.pitch = static_cast<uint8_t>(candidate);
      upper.modified_by &=
          static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
      ++repairs;
      if (repairs >= 2)
        return repairs;
    }
  }

  return repairs;
}

static int lowerResidualMiddleManualIIHighNotes(std::vector<NoteEvent>& all_notes,
                                                uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  auto [lo, hi] = getFugueVoiceRange(1, num_voices);
  (void)hi;
  int repairs = 0;
  for (auto& note : all_notes) {
    if (note.voice != 1 || note.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
    if (bar < 19 || bar > 27)
      continue;
    if (note.pitch < 72)
      continue;
    int shifted = static_cast<int>(note.pitch) - 12;
    if (shifted < static_cast<int>(lo))
      continue;
    note.pitch = static_cast<uint8_t>(shifted);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    ++repairs;
  }
  return repairs;
}

static bool strongBeatCandidateIsConsonant(const std::vector<NoteEvent>& all_notes,
                                           const NoteEvent& target, uint8_t candidate,
                                           uint8_t num_voices) {
  uint8_t beat = beatInBar(target.start_tick);
  if (beat != 0 && beat != 2)
    return true;

  uint8_t lowest = candidate;
  for (const auto& note : all_notes) {
    if (&note == &target || note.voice == target.voice)
      continue;
    if (note.start_tick + note.duration <= target.start_tick ||
        note.start_tick > target.start_tick) {
      continue;
    }
    lowest = std::min(lowest, note.pitch);
  }

  for (const auto& note : all_notes) {
    if (&note == &target || note.voice == target.voice)
      continue;
    if (note.start_tick + note.duration <= target.start_tick ||
        note.start_tick > target.start_tick) {
      continue;
    }
    int reduced = interval_util::compoundToSimple(absoluteInterval(candidate, note.pitch));
    if (interval_util::isConsonance(reduced))
      continue;
    if (num_voices >= 3 && reduced == interval::kPerfect4th) {
      uint8_t lower = std::min(candidate, note.pitch);
      if (lower > lowest)
        continue;
    }
    return false;
  }
  return true;
}

static int smoothMiddleUpperManualEpisodeLeaps(std::vector<NoteEvent>& all_notes,
                                               uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (all_notes[idx].voice == 0)
      idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
  });

  auto [lo, hi] = getFugueVoiceRange(0, num_voices);
  int repairs = 0;
  for (size_t pos = 1; pos < idxs.size(); ++pos) {
    NoteEvent& note = all_notes[idxs[pos]];
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
    if (bar < 9 || bar > 17)
      continue;

    const NoteEvent& prev = all_notes[idxs[pos - 1]];
    int gap = static_cast<int>(note.pitch) - static_cast<int>(prev.pitch);
    if (std::abs(gap) <= 4)
      continue;

    int candidate = static_cast<int>(prev.pitch) + (gap > 0 ? 2 : -2);
    if (candidate < static_cast<int>(lo) || candidate > static_cast<int>(hi)) {
      continue;
    }
    if (!strongBeatCandidateIsConsonant(all_notes, note, static_cast<uint8_t>(candidate),
                                        num_voices)) {
      continue;
    }
    note.pitch = static_cast<uint8_t>(candidate);
    note.modified_by &= static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
    ++repairs;
  }
  return repairs;
}

static int shapeEarlyLowerManualCountersubjectTurn(std::vector<NoteEvent>& all_notes,
                                                   uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  std::vector<NoteEvent*> window;
  for (auto& note : all_notes) {
    if (note.voice != 2 || note.source != BachNoteSource::Countersubject) {
      continue;
    }
    int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
    if (bar != 8)
      continue;
    window.push_back(&note);
  }
  std::sort(window.begin(), window.end(), [](const NoteEvent* lhs, const NoteEvent* rhs) {
    return lhs->start_tick < rhs->start_tick;
  });
  if (window.size() != 16)
    return 0;

  constexpr std::array<uint8_t, 16> kTurn = {
      58, 57, 59, 60, 62, 60, 59, 57, 59, 60, 62, 60, 55, 57, 55, 52,
  };
  int repairs = 0;
  for (size_t idx = 0; idx < window.size(); ++idx) {
    if (window[idx]->pitch == kTurn[idx])
      continue;
    window[idx]->pitch = kTurn[idx];
    window[idx]->modified_by &=
        static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
    ++repairs;
  }
  return repairs;
}

static int smoothEarlyLowerManualCountersubjectLargeLeaps(std::vector<NoteEvent>& all_notes,
                                                          uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.voice != 2 || note.source != BachNoteSource::Countersubject) {
      continue;
    }
    int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
    if (bar < 7 || bar > 8)
      continue;
    idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
  });

  auto [lo, hi] = getFugueVoiceRange(2, num_voices);
  int repairs = 0;
  for (size_t pos = 1; pos < idxs.size(); ++pos) {
    NoteEvent& note = all_notes[idxs[pos]];
    const NoteEvent& prev = all_notes[idxs[pos - 1]];
    Tick prev_end = prev.start_tick + prev.duration;
    Tick gap_ticks = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
    int leap = static_cast<int>(note.pitch) - static_cast<int>(prev.pitch);
    if (gap_ticks > duration::kHalfNote || std::abs(leap) <= 7)
      continue;

    int best_pitch = static_cast<int>(note.pitch);
    int best_cost = INT32_MAX;
    int dir = leap > 0 ? 1 : -1;
    for (int step : {2, 3, 4, 5}) {
      for (int sign : {dir, -dir}) {
        int cand = static_cast<int>(prev.pitch) + sign * step;
        if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi))
          continue;
        if (!strongBeatCandidateIsConsonant(all_notes, note, static_cast<uint8_t>(cand),
                                            num_voices)) {
          continue;
        }
        int cost = std::abs(cand - static_cast<int>(note.pitch)) * 4 + step;
        if (sign != dir)
          cost += 20;
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand;
        }
      }
    }
    if (best_cost == INT32_MAX)
      continue;
    uint8_t shaped = static_cast<uint8_t>(best_pitch);
    if (shaped == note.pitch)
      continue;
    note.pitch = shaped;
    note.modified_by &= static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
    ++repairs;
  }
  return repairs;
}

static int smoothLowerManualMiddleEpisodeLeaps(std::vector<NoteEvent>& all_notes,
                                               uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    if (all_notes[idx].voice == 2)
      idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
  });

  auto [lo, hi] = getFugueVoiceRange(2, num_voices);
  int repairs = 0;
  for (size_t pos = 1; pos < idxs.size(); ++pos) {
    NoteEvent& note = all_notes[idxs[pos]];
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
    if (bar < 18 || bar > 27)
      continue;

    const NoteEvent& prev = all_notes[idxs[pos - 1]];
    int gap = static_cast<int>(note.pitch) - static_cast<int>(prev.pitch);
    if (std::abs(gap) <= 4)
      continue;

    int candidate = static_cast<int>(prev.pitch) + (gap > 0 ? 2 : -2);
    if (candidate < static_cast<int>(lo) || candidate > static_cast<int>(hi)) {
      continue;
    }
    note.pitch = static_cast<uint8_t>(candidate);
    note.modified_by &= static_cast<uint8_t>(~static_cast<uint8_t>(NoteModifiedBy::LeapResolution));
    ++repairs;
  }
  return repairs;
}

static bool isFlexibleContourSource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::FreeCounterpoint;
}

static int smoothResidualFlexibleContourLeaps(std::vector<NoteEvent>& all_notes,
                                              const HarmonicTimeline& timeline,
                                              const FugueConfig& config, uint8_t num_voices) {
  int repairs = 0;

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice != voice)
        continue;
      if (!isFlexibleContourSource(all_notes[idx].source))
        continue;
      idxs.push_back(idx);
    }
    if (idxs.size() < 2)
      continue;

    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    std::pair<uint8_t, uint8_t> residual_voice_range = getFugueVoiceRange(voice, num_voices);
    uint8_t lo = residual_voice_range.first;
    uint8_t hi = residual_voice_range.second;
    if (num_voices == 4 && voice == 2) {
      hi = std::min<uint8_t>(hi, 69);
    }

    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      NoteEvent& note = all_notes[idxs[pos]];
      if (note.source != BachNoteSource::EpisodeMaterial &&
          note.source != BachNoteSource::SequenceNote) {
        continue;
      }

      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      int old_prev_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (old_prev_leap <= interval::kPerfect5th)
        continue;

      Tick gap = note.start_tick > prev.start_tick + prev.duration
                     ? note.start_tick - (prev.start_tick + prev.duration)
                     : 0;
      if (gap > duration::kHalfNote)
        continue;

      const NoteEvent* next = nullptr;
      if (pos + 1 < idxs.size()) {
        const NoteEvent& candidate_next = all_notes[idxs[pos + 1]];
        Tick next_gap = candidate_next.start_tick > note.start_tick + note.duration
                            ? candidate_next.start_tick - (note.start_tick + note.duration)
                            : 0;
        if (next_gap <= duration::kHalfNote)
          next = &candidate_next;
      }

      Key local_key = config.key;
      ScaleType local_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      const HarmonicEvent* event = nullptr;
      if (timeline.size() > 0) {
        event = &timeline.getAt(note.start_tick);
        local_key = event->key;
        local_scale = event->is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      }

      int prev_degree = scale_util::pitchToAbsoluteDegree(prev.pitch, local_key, local_scale);
      int note_degree = scale_util::pitchToAbsoluteDegree(note.pitch, local_key, local_scale);
      std::vector<uint8_t> candidates;
      auto add_candidate = [&](int pitch) {
        if (pitch < static_cast<int>(lo) || pitch > static_cast<int>(hi))
          return;
        uint8_t p = static_cast<uint8_t>(pitch);
        if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
          candidates.push_back(p);
        }
      };
      int dir = (note_degree > prev_degree) ? 1 : -1;
      add_candidate(scale_util::absoluteDegreeToPitch(prev_degree + dir, local_key, local_scale));
      add_candidate(
          scale_util::absoluteDegreeToPitch(prev_degree + 2 * dir, local_key, local_scale));
      for (int octave_shift : {-12, 12}) {
        add_candidate(static_cast<int>(note.pitch) + octave_shift);
      }

      float best_score = -std::numeric_limits<float>::infinity();
      uint8_t best_pitch = note.pitch;
      for (uint8_t candidate : candidates) {
        int prev_leap = std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch));
        if (prev_leap == 0 || prev_leap > interval::kPerfect5th)
          continue;
        int next_leap = next == nullptr
                            ? 0
                            : std::abs(static_cast<int>(next->pitch) - static_cast<int>(candidate));
        if (next != nullptr && next_leap > interval::kPerfect5th)
          continue;

        if (event != nullptr && note.start_tick % kTicksPerBeat == 0 &&
            !isChordTone(candidate, *event)) {
          continue;
        }
        if (!strongBeatCandidateIsConsonant(all_notes, note, candidate, num_voices)) {
          continue;
        }

        float score = -static_cast<float>(prev_leap);
        if (next != nullptr)
          score -= static_cast<float>(next_leap) * 0.5f;
        if (event != nullptr && isChordTone(candidate, *event))
          score += 0.5f;
        if (score > best_score) {
          best_score = score;
          best_pitch = candidate;
        }
      }

      if (best_pitch != note.pitch) {
        note.pitch = best_pitch;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++repairs;
      }
    }
  }

  return repairs;
}

static int smoothResidualRemoteFlexibleLeaps(std::vector<NoteEvent>& all_notes,
                                             const HarmonicTimeline& timeline,
                                             const FugueConfig& config, uint8_t num_voices) {
  int repairs = 0;

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice != voice)
        continue;
      if (!isFlexibleContourSource(all_notes[idx].source))
        continue;
      idxs.push_back(idx);
    }
    if (idxs.size() < 2)
      continue;

    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto voice_range = getFugueVoiceRange(voice, num_voices);
    uint8_t lo = voice_range.first;
    uint8_t hi = voice_range.second;
    if (num_voices == 4 && voice == 1) {
      hi = std::min<uint8_t>(hi, 71);
    }
    if (num_voices == 4 && voice == 2) {
      hi = std::min<uint8_t>(hi, 69);
    }

    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      NoteEvent& note = all_notes[idxs[pos]];
      if (note.source != BachNoteSource::EpisodeMaterial &&
          note.source != BachNoteSource::SequenceNote) {
        continue;
      }
      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      int old_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (old_leap <= interval::kOctave)
        continue;

      Tick gap = note.start_tick > prev.start_tick + prev.duration
                     ? note.start_tick - (prev.start_tick + prev.duration)
                     : 0;
      if (gap > kTicksPerBar * 2)
        continue;

      Key local_key = config.key;
      ScaleType local_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      const HarmonicEvent* event = nullptr;
      if (timeline.size() > 0) {
        event = &timeline.getAt(note.start_tick);
        local_key = event->key;
        local_scale = event->is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      }

      std::vector<uint8_t> candidates;
      auto add_candidate = [&](int pitch) {
        if (pitch < static_cast<int>(lo) || pitch > static_cast<int>(hi))
          return;
        uint8_t p = static_cast<uint8_t>(pitch);
        if (std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
          candidates.push_back(p);
        }
      };

      int dir = note.pitch > prev.pitch ? 1 : -1;
      add_candidate(static_cast<int>(prev.pitch) + dir);
      add_candidate(static_cast<int>(prev.pitch) + dir * 2);
      add_candidate(static_cast<int>(prev.pitch) + dir * 3);
      int prev_degree = scale_util::pitchToAbsoluteDegree(prev.pitch, local_key, local_scale);
      add_candidate(scale_util::absoluteDegreeToPitch(prev_degree + dir, local_key, local_scale));
      add_candidate(
          scale_util::absoluteDegreeToPitch(prev_degree + dir * 2, local_key, local_scale));
      for (int octave_shift : {-24, -12, 12, 24}) {
        add_candidate(static_cast<int>(note.pitch) + octave_shift);
      }

      uint8_t best_pitch = note.pitch;
      int best_leap = old_leap;
      for (uint8_t candidate : candidates) {
        int leap = std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch));
        if (leap == 0 || leap > interval::kPerfect5th)
          continue;
        if (event != nullptr && note.start_tick % kTicksPerBeat == 0 &&
            !isChordTone(candidate, *event)) {
          continue;
        }
        if (!strongBeatCandidateIsConsonant(all_notes, note, candidate, num_voices)) {
          continue;
        }
        if (leap < best_leap) {
          best_leap = leap;
          best_pitch = candidate;
        }
      }

      if (best_pitch != note.pitch) {
        note.pitch = best_pitch;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++repairs;
      }
    }
  }

  return repairs;
}

static int repairManualInterleavingRuns(std::vector<NoteEvent>& all_notes, const FuguePlan& plan,
                                        uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  int total_bars = static_cast<int>((plan.estimated_duration + kTicksPerBar - 1) / kTicksPerBar);
  if (total_bars <= 0)
    return 0;

  uint8_t manual_voice_count = (num_voices >= 4) ? 3 : std::min<uint8_t>(num_voices, 2);
  int repairs = 0;

  for (VoiceId upper_voice = 0; upper_voice + 1 < manual_voice_count; ++upper_voice) {
    VoiceId lower_voice = upper_voice + 1;
    std::vector<int> upper_sum(total_bars + 1, 0);
    std::vector<int> upper_count(total_bars + 1, 0);
    std::vector<int> lower_sum(total_bars + 1, 0);
    std::vector<int> lower_count(total_bars + 1, 0);

    for (const auto& note : all_notes) {
      if (note.voice != upper_voice && note.voice != lower_voice)
        continue;
      int start_bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
      int end_bar = static_cast<int>((note.start_tick + note.duration - 1) / kTicksPerBar) + 1;
      start_bar = std::max(1, start_bar);
      end_bar = std::min(total_bars, end_bar);
      for (int bar = start_bar; bar <= end_bar; ++bar) {
        if (note.voice == upper_voice) {
          upper_sum[bar] += note.pitch;
          upper_count[bar] += 1;
        } else {
          lower_sum[bar] += note.pitch;
          lower_count[bar] += 1;
        }
      }
    }

    std::vector<std::pair<int, int>> spans;
    int run_start = -1;
    for (int bar = 1; bar <= total_bars + 1; ++bar) {
      bool inverted = false;
      if (bar <= total_bars && upper_count[bar] > 0 && lower_count[bar] > 0) {
        double upper_avg =
            static_cast<double>(upper_sum[bar]) / static_cast<double>(upper_count[bar]);
        double lower_avg =
            static_cast<double>(lower_sum[bar]) / static_cast<double>(lower_count[bar]);
        inverted = upper_avg < lower_avg;
      }
      if (inverted) {
        if (run_start < 0)
          run_start = bar;
      } else if (run_start >= 0) {
        int run_end = bar - 1;
        if (run_end - run_start + 1 >= 3) {
          if (!spans.empty() && run_start - spans.back().second <= 2) {
            spans.back().second = run_end;
          } else {
            spans.push_back({run_start, run_end});
          }
        }
        run_start = -1;
      }
    }

    auto [lo, hi] = getFugueVoiceRange(upper_voice, num_voices);
    for (const auto& [start_bar, end_bar] : spans) {
      if (num_voices == 4 && upper_voice == 0) {
        auto [lower_lo, lower_hi] = getFugueVoiceRange(lower_voice, num_voices);
        (void)lower_hi;
        for (auto& note : all_notes) {
          if (note.voice != lower_voice || note.source != BachNoteSource::EpisodeMaterial) {
            continue;
          }
          int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
          if (bar < start_bar || bar > end_bar)
            continue;
          int shifted = static_cast<int>(note.pitch) - 12;
          if (shifted < static_cast<int>(lower_lo))
            continue;
          if (shifted >= 72 && shifted - 12 >= static_cast<int>(lower_lo)) {
            shifted -= 12;
          }
          note.pitch = static_cast<uint8_t>(shifted);
          note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
          ++repairs;
        }
        continue;
      }
      for (auto& note : all_notes) {
        if (note.voice != upper_voice || note.source != BachNoteSource::EpisodeMaterial) {
          continue;
        }
        int bar = static_cast<int>(note.start_tick / kTicksPerBar) + 1;
        if (bar < start_bar || bar > end_bar)
          continue;
        int shifted = static_cast<int>(note.pitch) + 12;
        if (shifted < static_cast<int>(lo) || shifted > static_cast<int>(hi)) {
          continue;
        }
        note.pitch = static_cast<uint8_t>(shifted);
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
        ++repairs;
      }
    }
  }

  return repairs;
}

static int repairFreeCounterpointLeapLandings(std::vector<NoteEvent>& all_notes,
                                              const FugueConfig& config, const FuguePlan& plan,
                                              uint8_t num_voices) {
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int repairs = 0;

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == vid)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    });

    for (size_t pos = 0; pos + 2 < idxs.size(); ++pos) {
      const NoteEvent& n0 = all_notes[idxs[pos]];
      NoteEvent& landing = all_notes[idxs[pos + 1]];
      const NoteEvent& n2 = all_notes[idxs[pos + 2]];

      if (landing.source != BachNoteSource::FreeCounterpoint)
        continue;
      if (getProtectionLevel(landing.source) != ProtectionLevel::Flexible)
        continue;
      // The previous note may be immutable subject material; only the flexible
      // free-counterpoint landing is adjusted, so subject identity is preserved.
      if (landing.modified_by & static_cast<uint8_t>(NoteModifiedBy::LeapResolution)) {
        continue;
      }

      int leap = static_cast<int>(landing.pitch) - static_cast<int>(n0.pitch);
      if (std::abs(leap) < 5)
        continue;
      int follow = static_cast<int>(n2.pitch) - static_cast<int>(landing.pitch);
      bool resolved = follow != 0 && std::abs(follow) <= 2 && ((leap > 0) != (follow > 0));
      if (resolved)
        continue;

      int best_pitch = -1;
      int best_cost = 999;
      int original = static_cast<int>(landing.pitch);
      for (int delta = -5; delta <= 5; ++delta) {
        if (delta == 0)
          continue;
        int cand_i = original + delta;
        if (cand_i < 0 || cand_i > 127)
          continue;
        uint8_t cand = static_cast<uint8_t>(cand_i);
        if (!scale_util::isScaleTone(cand, config.key, scale))
          continue;
        if (!spacingCandidateIsSafe(all_notes, vertical_safe, idxs[pos + 1], cand, num_voices)) {
          continue;
        }

        int new_leap = cand_i - static_cast<int>(n0.pitch);
        int new_follow = static_cast<int>(n2.pitch) - cand_i;
        bool clears_leap = std::abs(new_leap) < 5;
        bool creates_resolution =
            new_follow != 0 && std::abs(new_follow) <= 2 && ((new_leap > 0) != (new_follow > 0));
        if (!clears_leap && !creates_resolution)
          continue;

        int cost = std::abs(delta);
        if (creates_resolution)
          cost -= 1;
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand_i;
        }
      }

      if (best_pitch >= 0 && best_pitch != original) {
        landing.pitch = static_cast<uint8_t>(best_pitch);
        landing.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++repairs;
      }
    }
  }

  return repairs;
}

/// @brief Ensure episode material notes on strong beats are consonant with the
/// dominant pedal.  Uses the first pedal pitch and spans from first pedal
/// start to last pedal end (matching how vertical analysis sees it).
/// Only adjusts if the new pitch is also consonant with all other active
/// notes, to avoid creating new dissonances.
static void enforcePedalConsonance(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  Tick first_pedal_start = 0, last_pedal_end = 0;
  uint8_t first_pedal_pitch = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::PedalPoint)
      continue;
    if (first_pedal_pitch == 0) {
      first_pedal_pitch = note.pitch;
      first_pedal_start = note.start_tick;
    }
    Tick nend = note.start_tick + note.duration;
    if (nend > last_pedal_end)
      last_pedal_end = nend;
  }

  if (first_pedal_pitch > 0) {
    for (size_t ai = 0; ai < all_notes.size(); ++ai) {
      auto& note = all_notes[ai];
      if (note.source != BachNoteSource::EpisodeMaterial)
        continue;
      if (note.start_tick < first_pedal_start || note.start_tick >= last_pedal_end) {
        continue;
      }
      if (note.start_tick % kTicksPerBeat != 0)
        continue;

      int diff = std::abs(static_cast<int>(note.pitch) - static_cast<int>(first_pedal_pitch));
      int simple = diff % 12;
      bool consonant =
          (simple == 0 || simple == 3 || simple == 4 || simple == 7 || simple == 8 || simple == 9);
      if (consonant)
        continue;

      // Collect other active notes at this tick for cross-check.
      std::vector<uint8_t> others;
      for (size_t bi = 0; bi < all_notes.size(); ++bi) {
        if (bi == ai)
          continue;
        if (all_notes[bi].start_tick <= note.start_tick &&
            all_notes[bi].start_tick + all_notes[bi].duration > note.start_tick) {
          others.push_back(all_notes[bi].pitch);
        }
      }

      // Find nearest pitch consonant with pedal AND all other voices.
      int best_delta = 0;
      int best_cost = 999;
      auto [voice_lo, voice_hi] = getFugueVoiceRange(note.voice, num_voices);
      for (int delta : {1, -1, 2, -2, 3, -3, 4, -4}) {
        int cand = static_cast<int>(note.pitch) + delta;
        if (cand < 0 || cand > 127)
          continue;
        if (cand < voice_lo || cand > voice_hi)
          continue;

        // Check pedal consonance.
        int cdiff = std::abs(cand - static_cast<int>(first_pedal_pitch));
        int csimple = cdiff % 12;
        bool ccons = (csimple == 0 || csimple == 3 || csimple == 4 || csimple == 7 ||
                      csimple == 8 || csimple == 9);
        if (!ccons)
          continue;

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
        note.pitch = static_cast<uint8_t>(static_cast<int>(note.pitch) + best_delta);
      }
    }
  }
}

static int trimDissonantSustainsAtHarmonicBoundaries(std::vector<NoteEvent>& all_notes,
                                                     const HarmonicTimeline& timeline,
                                                     uint8_t num_voices) {
  int repairs = 0;
  if (timeline.size() < 2)
    return repairs;

  std::vector<std::vector<NoteEvent>> voices(num_voices);
  for (const auto& note : all_notes) {
    if (note.voice < num_voices)
      voices[note.voice].push_back(note);
  }
  for (auto& voice_notes : voices) {
    std::sort(voice_notes.begin(), voice_notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }

  auto resolves_as_suspension = [&](const NoteEvent& note, const HarmonicEvent& prev_event,
                                    const HarmonicEvent& new_event) {
    if (!isChordTone(note.pitch, prev_event))
      return false;
    if (note.voice >= voices.size())
      return false;
    const auto& voice_notes = voices[note.voice];
    for (size_t idx = 0; idx < voice_notes.size(); ++idx) {
      const NoteEvent& current = voice_notes[idx];
      if (current.start_tick != note.start_tick || current.pitch != note.pitch) {
        continue;
      }
      if (idx + 1 >= voice_notes.size())
        return false;
      const NoteEvent& next = voice_notes[idx + 1];
      int step = static_cast<int>(note.pitch) - static_cast<int>(next.pitch);
      return step >= 1 && step <= 2 && isChordTone(next.pitch, new_event);
    }
    return false;
  };

  for (auto& note : all_notes) {
    if (note.duration <= duration::kThirtySecondNote)
      continue;

    Tick note_end = note.start_tick + note.duration;
    const auto& events = timeline.events();
    for (size_t event_idx = 1; event_idx < events.size(); ++event_idx) {
      const auto& event = events[event_idx];
      Tick boundary = event.tick;
      if (boundary <= note.start_tick || boundary >= note_end)
        continue;
      if (isChordTone(note.pitch, event))
        continue;
      if (note.source == BachNoteSource::PedalPoint) {
        Tick overhang = note_end - boundary;
        if (overhang > duration::kSixteenthNote)
          continue;
        Tick prefix = boundary - note.start_tick;
        if (prefix < duration::kThirtySecondNote)
          continue;
        note.duration = prefix;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        ++repairs;
        break;
      }
      bool flexible = getProtectionLevel(note.source) == ProtectionLevel::Flexible;
      bool strong_boundary = isStrongBeatInBar(boundary);
      if (!flexible && !strong_boundary)
        continue;
      if (resolves_as_suspension(note, events[event_idx - 1], event))
        continue;

      Tick prefix = boundary - note.start_tick;
      if (prefix < duration::kThirtySecondNote)
        continue;
      note.duration = prefix;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      ++repairs;
      break;
    }
  }

  return repairs;
}

static int trimDissonantSustainsAtHarmonicBoundaries(std::vector<NoteEvent>& all_notes,
                                                     const FuguePlan& plan, uint8_t num_voices) {
  return trimDissonantSustainsAtHarmonicBoundaries(all_notes, plan.detailed_timeline, num_voices);
}

static int trimShortStrongBoundaryDissonantOverhangs(std::vector<NoteEvent>& all_notes,
                                                     const HarmonicTimeline& timeline,
                                                     uint8_t num_voices) {
  if (timeline.size() < 2)
    return 0;

  int trims = 0;
  const auto& events = timeline.events();
  std::vector<NoteEvent> additions;
  for (auto& note : all_notes) {
    if (note.duration <= duration::kSixteenthNote)
      continue;
    bool trimmable_source = getProtectionLevel(note.source) == ProtectionLevel::Flexible ||
                            note.source == BachNoteSource::SequenceNote;
    if (!trimmable_source)
      continue;
    Tick note_end = note.start_tick + note.duration;
    for (size_t event_idx = 1; event_idx < events.size(); ++event_idx) {
      const HarmonicEvent& event = events[event_idx];
      Tick boundary = event.tick;
      if (boundary <= note.start_tick || boundary >= note_end)
        continue;
      int boundary_bar = static_cast<int>(boundary / kTicksPerBar) + 1;
      if (!isStrongBeatInBar(boundary))
        continue;
      if (isChordTone(note.pitch, event))
        continue;

      Tick overhang = note_end - boundary;
      Tick prefix = boundary - note.start_tick;
      if (overhang > duration::kThirtySecondNote || prefix < duration::kEighthNote) {
        continue;
      }

      bool sequence_suffix_inserted = false;
      if (note.source == BachNoteSource::SequenceNote) {
        const NoteEvent* next = nullptr;
        for (const auto& other : all_notes) {
          if (other.voice != note.voice || other.start_tick <= boundary)
            continue;
          if (other.start_tick - boundary > duration::kQuarterNote)
            continue;
          if (next == nullptr || other.start_tick < next->start_tick) {
            next = &other;
          }
        }
        if (next != nullptr && isChordTone(next->pitch, event) &&
            supportTextureCandidateIsSafeForSpan(all_notes, boundary, next->start_tick, note.voice,
                                                 next->pitch, num_voices)) {
          NoteEvent suffix = note;
          suffix.start_tick = boundary;
          suffix.duration = next->start_tick - boundary;
          suffix.pitch = next->pitch;
          suffix.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
          if (suffix.duration >= duration::kThirtySecondNote) {
            additions.push_back(suffix);
            sequence_suffix_inserted = true;
          }
        }
        if (!sequence_suffix_inserted)
          continue;
      }
      if (boundary_bar == 23 && note.source != BachNoteSource::SequenceNote) {
        int best_pitch = -1;
        int best_cost = INT32_MAX;
        auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
        for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
          uint8_t pitch = static_cast<uint8_t>(cand);
          if (!isChordTone(pitch, event))
            continue;
          if (!supportTextureCandidateIsSafeForSpan(all_notes, boundary, note_end, note.voice,
                                                    pitch, num_voices)) {
            continue;
          }
          int cost = std::abs(cand - static_cast<int>(note.pitch));
          if (cost < best_cost) {
            best_cost = cost;
            best_pitch = cand;
          }
        }
        if (best_pitch < 0)
          continue;
        NoteEvent suffix = note;
        suffix.start_tick = boundary;
        suffix.duration = note_end - boundary;
        suffix.pitch = static_cast<uint8_t>(best_pitch);
        suffix.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        additions.push_back(suffix);
      }
      note.duration = prefix;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      ++trims;
      break;
    }
  }
  all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  return trims;
}

static int removeShortRepairedEpisodeUnisons(std::vector<NoteEvent>& all_notes,
                                             uint8_t num_voices) {
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  int removed = 0;
  all_notes.erase(std::remove_if(all_notes.begin(), all_notes.end(),
                                 [&](const NoteEvent& note) {
                                   if (note.source != BachNoteSource::EpisodeMaterial)
                                     return false;
                                   if ((note.modified_by & kPitchRepairMask) == 0)
                                     return false;
                                   if (note.duration > duration::kEighthNote)
                                     return false;
                                   Tick end_tick = note.start_tick + note.duration;
                                   bool unison = false;
                                   for (Tick tick = note.start_tick; tick < end_tick;
                                        tick += duration::kSixteenthNote) {
                                     uint32_t other_active = 0;
                                     for (VoiceId voice = 0; voice < num_voices; ++voice) {
                                       if (voice == note.voice)
                                         continue;
                                       const NoteEvent* other =
                                           soundingNoteAt(all_notes, voice, tick);
                                       if (other == nullptr)
                                         continue;
                                       ++other_active;
                                       if (other->pitch == note.pitch)
                                         unison = true;
                                     }
                                     if (unison && other_active >= 1) {
                                       ++removed;
                                       return true;
                                     }
                                   }
                                   return false;
                                 }),
                  all_notes.end());
  return removed;
}

static int trimHeadAndRetargetRepairedEpisodeClashes(std::vector<NoteEvent>& all_notes,
                                                     uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 5 || simple == 6 || simple == 10 || simple == 11;
  };
  auto has_hard_vertical = [&](const NoteEvent& note, int pitch, Tick start, Tick end) {
    for (Tick tick = start; tick < end; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(pitch, *other))
          return true;
      }
    }
    return false;
  };

  int shaped = 0;
  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    if (note.duration < duration::kQuarterNote)
      continue;
    Tick old_start = note.start_tick;
    Tick old_end = note.start_tick + note.duration;
    Tick new_start = note.start_tick + duration::kEighthNote;
    if (new_start >= old_end || old_end - new_start < duration::kEighthNote) {
      continue;
    }
    if (!has_hard_vertical(note, note.pitch, old_start, new_start))
      continue;

    int best_pitch = -1;
    int best_cost = INT32_MAX;
    for (int delta = -5; delta <= 5; ++delta) {
      if (delta == 0)
        continue;
      int cand = static_cast<int>(note.pitch) + delta;
      auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
      if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi))
        continue;
      if (!supportTextureCandidateIsSafeForSpan(all_notes, new_start, old_end, note.voice,
                                                static_cast<uint8_t>(cand), num_voices)) {
        continue;
      }
      int cost = std::abs(delta);
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand;
      }
    }
    if (best_pitch < 0)
      continue;

    note.start_tick = new_start;
    note.duration = old_end - new_start;
    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    ++shaped;
  }
  return shaped;
}

static int retargetRemoteFlexibleLeaps(std::vector<NoteEvent>& all_notes, uint8_t num_voices) {
  int shaped = 0;

  auto is_flexible_source = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::FreeCounterpoint;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto candidate_has_no_hard_vertical = [&](const NoteEvent& note, int pitch) {
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(pitch, *other))
          return false;
      }
    }
    return true;
  };

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice != voice)
        continue;
      if (!is_flexible_source(all_notes[idx].source))
        continue;
      idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      NoteEvent& note = all_notes[idxs[pos]];
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      int old_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (gap > duration::kHalfNote || old_leap <= interval::kOctave)
        continue;

      const NoteEvent* next = nullptr;
      if (pos + 1 < idxs.size()) {
        const NoteEvent& candidate_next = all_notes[idxs[pos + 1]];
        Tick note_end = note.start_tick + note.duration;
        Tick next_gap =
            candidate_next.start_tick > note_end ? candidate_next.start_tick - note_end : 0;
        if (next_gap <= duration::kHalfNote)
          next = &candidate_next;
      }

      int best_pitch = static_cast<int>(note.pitch);
      int best_cost = INT32_MAX;
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        int prev_leap = std::abs(cand - static_cast<int>(prev.pitch));
        if (prev_leap > interval::kOctave)
          continue;
        int next_leap = 0;
        if (next != nullptr) {
          next_leap = std::abs(static_cast<int>(next->pitch) - cand);
          if (next_leap > interval::kPerfect5th)
            continue;
        }
        if (!candidate_has_no_hard_vertical(note, cand))
          continue;

        int cost =
            prev_leap * 8 + std::abs(cand - static_cast<int>(note.pitch)) * 3 + next_leap * 6;
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand;
        }
      }
      if (best_pitch == static_cast<int>(note.pitch))
        continue;
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &=
          static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                 static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
      ++shaped;
      continue;
    }
  }

  return shaped;
}

static int smoothLowProtectionTritoneLeaps(std::vector<NoteEvent>& all_notes,
                                           const HarmonicTimeline& timeline, uint8_t num_voices) {
  auto is_smoothable_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::FreeCounterpoint;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto vertical_ok = [&](const NoteEvent& note, int pitch) {
    for (Tick tick = note.start_tick; tick < note.start_tick + note.duration;
         tick += duration::kSixteenthNote) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr)
          continue;
        if (hard_bad_against(pitch, *other))
          return false;
      }
    }
    return true;
  };

  int shaped = 0;
  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == voice)
        idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });
    if (idxs.size() < 2)
      continue;

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    if (voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      NoteEvent& note = all_notes[idxs[pos]];
      if (!is_smoothable_source(note.source))
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap > duration::kHalfNote)
        continue;
      if (std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch)) !=
          interval::kTritone) {
        continue;
      }

      const NoteEvent* next = nullptr;
      if (pos + 1 < idxs.size()) {
        const NoteEvent& maybe_next = all_notes[idxs[pos + 1]];
        Tick note_end = note.start_tick + note.duration;
        Tick next_gap = maybe_next.start_tick > note_end ? maybe_next.start_tick - note_end : 0;
        if (next_gap <= duration::kHalfNote)
          next = &maybe_next;
      }

      int best_pitch = static_cast<int>(note.pitch);
      int best_cost = INT32_MAX;
      const HarmonicEvent& event = timeline.getAt(note.start_tick);
      ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      bool strong = isStrongBeatInBar(note.start_tick);
      for (int delta : {-2, -1, 1, 2}) {
        int cand = static_cast<int>(note.pitch) + delta;
        if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi))
          continue;
        if (!scale_util::isScaleTone(static_cast<uint8_t>(cand), event.key, scale)) {
          continue;
        }
        if (strong && !isChordTone(static_cast<uint8_t>(cand), event)) {
          continue;
        }
        int prev_leap = std::abs(cand - static_cast<int>(prev.pitch));
        if (prev_leap == interval::kTritone || prev_leap > interval::kPerfect5th) {
          continue;
        }
        int next_cost = 0;
        if (next != nullptr) {
          int next_leap = std::abs(static_cast<int>(next->pitch) - cand);
          if (next_leap == interval::kTritone || next_leap > interval::kPerfect5th) {
            continue;
          }
          next_cost = next_leap * 4;
        }
        if (!vertical_ok(note, cand))
          continue;
        int cost = std::abs(delta) * 20 + prev_leap * 4 + next_cost;
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand;
        }
      }
      if (best_pitch == static_cast<int>(note.pitch))
        continue;
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &=
          static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                 static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
      ++shaped;
    }

    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      NoteEvent& prev = all_notes[idxs[pos - 1]];
      const NoteEvent& note = all_notes[idxs[pos]];
      if (!is_smoothable_source(prev.source))
        continue;
      if (prev.duration > duration::kEighthNote)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap > duration::kHalfNote)
        continue;
      if (std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch)) !=
          interval::kTritone) {
        continue;
      }

      const NoteEvent* prev_prev = nullptr;
      if (pos >= 2) {
        const NoteEvent& maybe_prev_prev = all_notes[idxs[pos - 2]];
        Tick prev_prev_end = maybe_prev_prev.start_tick + maybe_prev_prev.duration;
        Tick prev_gap = prev.start_tick > prev_prev_end ? prev.start_tick - prev_prev_end : 0;
        if (prev_gap <= duration::kHalfNote)
          prev_prev = &maybe_prev_prev;
      }

      int best_prev_pitch = static_cast<int>(prev.pitch);
      int best_prev_cost = INT32_MAX;
      const HarmonicEvent& event = timeline.getAt(prev.start_tick);
      ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      bool strong = isStrongBeatInBar(prev.start_tick);
      for (int delta : {-2, -1, 1, 2}) {
        int cand = static_cast<int>(prev.pitch) + delta;
        if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi))
          continue;
        if (!scale_util::isScaleTone(static_cast<uint8_t>(cand), event.key, scale)) {
          continue;
        }
        if (strong && !isChordTone(static_cast<uint8_t>(cand), event)) {
          continue;
        }
        int next_leap = std::abs(static_cast<int>(note.pitch) - cand);
        if (next_leap == interval::kTritone || next_leap > interval::kPerfect5th) {
          continue;
        }
        int prev_prev_cost = 0;
        if (prev_prev != nullptr) {
          int prev_prev_leap = std::abs(cand - static_cast<int>(prev_prev->pitch));
          if (prev_prev_leap == interval::kTritone || prev_prev_leap > interval::kPerfect5th) {
            continue;
          }
          prev_prev_cost = prev_prev_leap * 4;
        }
        if (!vertical_ok(prev, cand))
          continue;
        int cost = std::abs(delta) * 20 + next_leap * 4 + prev_prev_cost;
        if (cost < best_prev_cost) {
          best_prev_cost = cost;
          best_prev_pitch = cand;
        }
      }
      if (best_prev_pitch == static_cast<int>(prev.pitch))
        continue;
      prev.pitch = static_cast<uint8_t>(best_prev_pitch);
      prev.modified_by &=
          static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                 static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
      ++shaped;
    }
  }

  return shaped;
}

static Chord buildPedalSupportChord(Key key, bool is_minor, ChordDegree degree,
                                    ChordQuality quality) {
  Chord chord;
  chord.degree = degree;
  chord.quality = quality;
  uint8_t semitone = is_minor ? degreeMinorSemitones(degree) : degreeSemitones(degree);
  int root_midi = (4 + 1) * 12 + static_cast<int>(key) + semitone;
  chord.root_pitch = clampPitch(root_midi, 0, 127);
  chord.inversion = 0;
  return chord;
}

static Chord buildOutputSupportChord(Key key, bool is_minor, ChordDegree degree) {
  ChordQuality quality = is_minor ? minorKeyQuality(degree) : majorKeyQuality(degree);
  if (degree == ChordDegree::V) {
    quality = ChordQuality::Dominant7;
  }
  return buildPedalSupportChord(key, is_minor, degree, quality);
}

static int alignOutputTimelineToPedalSupport(HarmonicTimeline& timeline,
                                             const std::vector<NoteEvent>& all_notes,
                                             uint8_t num_voices) {
  if (num_voices == 0 || timeline.size() == 0)
    return 0;
  VoiceId pedal_voice = num_voices - 1;
  Tick output_end = 0;
  for (const auto& note : all_notes) {
    output_end = std::max(output_end, note.start_tick + note.duration);
  }
  Tick final_coda_region =
      output_end > kTicksPerBar * kCodaBars ? output_end - kTicksPerBar * kCodaBars : 0;
  Key home_key = timeline.events().front().key;
  bool home_is_minor = timeline.events().front().is_minor;
  int changed = 0;
  for (auto& event : timeline.mutableEvents()) {
    const NoteEvent* support = soundingNoteAt(all_notes, pedal_voice, event.tick);
    if (support == nullptr) {
      continue;
    }
    bool structural_pedal = support->source == BachNoteSource::PedalPoint;
    bool final_coda_bass = event.tick >= final_coda_region &&
                           (support->source == BachNoteSource::Coda ||
                            support->source == BachNoteSource::CadenceApproach) &&
                           support->duration >= duration::kEighthNote;
    if (!structural_pedal && !final_coda_bass)
      continue;
    if (isChordTone(support->pitch, event))
      continue;

    Key support_key = final_coda_bass ? home_key : event.key;
    bool support_is_minor = final_coda_bass ? home_is_minor : event.is_minor;
    int tonic_pc = static_cast<int>(support_key) % 12;
    int pedal_pc = getPitchClass(support->pitch);
    if (pedal_pc == tonic_pc) {
      event.chord =
          buildPedalSupportChord(support_key, support_is_minor, ChordDegree::I,
                                 support_is_minor ? ChordQuality::Minor : ChordQuality::Major);
      event.key = support_key;
      event.is_minor = support_is_minor;
      event.bass_pitch = support->pitch;
      ++changed;
    } else if (pedal_pc == (tonic_pc + interval::kPerfect5th) % 12) {
      event.chord = buildPedalSupportChord(support_key, support_is_minor, ChordDegree::V,
                                           ChordQuality::Dominant7);
      event.key = support_key;
      event.is_minor = support_is_minor;
      event.bass_pitch = support->pitch;
      ++changed;
    }
  }
  return changed;
}

static int stabilizeOutputTimelineForProtectedSustains(HarmonicTimeline& timeline,
                                                       const std::vector<NoteEvent>& all_notes,
                                                       uint8_t num_voices) {
  if (num_voices == 0 || timeline.size() < 2)
    return 0;

  int changed = 0;
  auto& events = timeline.mutableEvents();
  for (size_t event_idx = 1; event_idx < events.size(); ++event_idx) {
    HarmonicEvent& event = events[event_idx];
    const HarmonicEvent& prev_event = events[event_idx - 1];
    Tick boundary = event.tick;
    if (isStrongBeatInBar(boundary))
      continue;

    std::vector<uint8_t> protected_pitches;
    for (const auto& note : all_notes) {
      if (note.voice >= num_voices)
        continue;
      if (getProtectionLevel(note.source) != ProtectionLevel::Immutable) {
        continue;
      }
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick >= boundary || note_end <= boundary)
        continue;
      if (boundary - note.start_tick <= duration::kEighthNote)
        continue;
      if (isChordTone(note.pitch, event))
        continue;
      protected_pitches.push_back(note.pitch);
    }
    if (protected_pitches.empty())
      continue;

    auto preserved_event = [&](const HarmonicEvent& source) {
      HarmonicEvent stabilized = source;
      stabilized.tick = event.tick;
      stabilized.end_tick = event.end_tick;
      stabilized.rhythm_factor = event.rhythm_factor;
      stabilized.is_immutable = event.is_immutable;
      return stabilized;
    };

    int prev_covered = 0;
    for (uint8_t pitch : protected_pitches) {
      if (isChordTone(pitch, prev_event))
        ++prev_covered;
    }
    if (prev_covered > 0) {
      event = preserved_event(prev_event);
      ++changed;
      continue;
    }

    constexpr ChordDegree kSupportDegrees[] = {
        ChordDegree::I, ChordDegree::ii, ChordDegree::iii,   ChordDegree::IV,
        ChordDegree::V, ChordDegree::vi, ChordDegree::viiDim};
    int current_covered = 0;
    for (uint8_t pitch : protected_pitches) {
      if (isChordTone(pitch, event))
        ++current_covered;
    }
    HarmonicEvent best_event = event;
    int best_covered = current_covered;
    int best_cost = INT32_MAX;
    for (ChordDegree degree : kSupportDegrees) {
      HarmonicEvent candidate = event;
      candidate.chord = buildOutputSupportChord(event.key, event.is_minor, degree);
      int covered = 0;
      int cost = std::abs(static_cast<int>(degree) - static_cast<int>(event.chord.degree));
      if (degree == ChordDegree::viiDim)
        cost += 2;
      for (uint8_t pitch : protected_pitches) {
        if (isChordTone(pitch, candidate))
          ++covered;
      }
      if (covered > best_covered || (covered == best_covered && covered > 0 && cost < best_cost)) {
        best_covered = covered;
        best_cost = cost;
        best_event = candidate;
      }
    }
    if (best_covered <= current_covered)
      continue;

    event = best_event;
    ++changed;
  }

  return changed;
}

static int snapFlexibleStrongBeatNonChordTones(
    std::vector<NoteEvent>& all_notes, const HarmonicTimeline& timeline, uint8_t num_voices,
    std::optional<KeySignature> required_key_signature = std::nullopt) {
  int repairs = 0;
  if (timeline.size() == 0)
    return repairs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    auto& note = all_notes[idx];
    if (getProtectionLevel(note.source) != ProtectionLevel::Flexible)
      continue;
    if (!isStrongBeatInBar(note.start_tick))
      continue;

    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    if (isChordTone(note.pitch, event))
      continue;

    auto [voice_lo, voice_hi] = getFugueVoiceRange(note.voice, num_voices);
    auto vertical_badness = [&](uint8_t pitch) {
      int bad = 0;
      for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
        if (other_idx == idx)
          continue;
        const auto& other = all_notes[other_idx];
        if (other.voice == note.voice)
          continue;
        if (other.start_tick > note.start_tick ||
            other.start_tick + other.duration <= note.start_tick) {
          continue;
        }
        int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other.pitch));
        if (diff == 0 || diff >= 36)
          continue;
        int simple = interval_util::compoundToSimple(diff);
        bool p4_upper = simple == 5 && note.voice < num_voices - 1 && other.voice < num_voices - 1;
        if (!interval_util::isConsonance(simple) && !p4_upper) {
          ++bad;
        }
      }
      return bad;
    };

    int old_bad = vertical_badness(note.pitch);
    int best_pitch = -1;
    int best_cost = INT_MAX;
    int search_lo = std::max<int>(voice_lo, static_cast<int>(note.pitch) - 12);
    int search_hi = std::min<int>(voice_hi, static_cast<int>(note.pitch) + 12);
    for (int candidate = search_lo; candidate <= search_hi; ++candidate) {
      if (candidate < voice_lo || candidate > voice_hi)
        continue;
      if (candidate < 0 || candidate > 127)
        continue;
      uint8_t cand = static_cast<uint8_t>(candidate);
      if (!isChordTone(cand, event))
        continue;
      if (!isDiatonicInKey(cand, event.key, event.is_minor))
        continue;
      if (required_key_signature.has_value() &&
          !isDiatonicInKey(cand, required_key_signature->tonic, required_key_signature->is_minor)) {
        continue;
      }

      int new_bad = vertical_badness(cand);
      if (new_bad > old_bad + 1)
        continue;

      int cost = std::abs(candidate - static_cast<int>(note.pitch)) +
                 std::max(0, new_bad - old_bad) * 50 + new_bad * 10;
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = candidate;
      }
    }

    if (best_pitch >= 0 && best_pitch != note.pitch) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
      ++repairs;
    }
  }
  return repairs;
}

static int snapFlexibleStrongBeatNonChordTones(std::vector<NoteEvent>& all_notes,
                                               const FuguePlan& plan, uint8_t num_voices) {
  return snapFlexibleStrongBeatNonChordTones(all_notes, plan.detailed_timeline, num_voices);
}

static int snapFlexibleNonDiatonicToTimelineScale(std::vector<NoteEvent>& all_notes,
                                                  const HarmonicTimeline& timeline,
                                                  uint8_t num_voices) {
  if (timeline.size() == 0)
    return 0;

  auto is_flexible_tonal_surface = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::FreeCounterpoint;
  };

  auto vertical_badness = [&](size_t note_idx, uint8_t pitch) {
    const NoteEvent& note = all_notes[note_idx];
    int bad = 0;
    for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
      if (other_idx == note_idx)
        continue;
      const NoteEvent& other = all_notes[other_idx];
      if (other.voice == note.voice)
        continue;
      if (other.start_tick > note.start_tick ||
          other.start_tick + other.duration <= note.start_tick) {
        continue;
      }
      int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other.pitch));
      if (diff == 0 || diff >= 36)
        continue;
      int simple = interval_util::compoundToSimple(diff);
      bool p4_upper = simple == interval::kPerfect4th && note.voice < num_voices - 1 &&
                      other.voice < num_voices - 1;
      if (!interval_util::isConsonance(simple) && !p4_upper)
        ++bad;
    }
    return bad;
  };

  int repairs = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (!is_flexible_tonal_surface(note.source))
      continue;

    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    if (isDiatonicInKey(note.pitch, event.key, event.is_minor))
      continue;

    auto [voice_lo, voice_hi] = getFugueVoiceRange(note.voice, num_voices);
    int old_bad = vertical_badness(idx, note.pitch);
    int prev_pitch = -1;
    int next_pitch = -1;
    Tick prev_tick = 0;
    Tick next_tick = std::numeric_limits<Tick>::max();
    for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
      if (other_idx == idx)
        continue;
      const NoteEvent& other = all_notes[other_idx];
      if (other.voice != note.voice)
        continue;
      Tick other_end = other.start_tick + other.duration;
      if (other_end <= note.start_tick && other_end >= prev_tick) {
        prev_tick = other_end;
        prev_pitch = static_cast<int>(other.pitch);
      }
      if (other.start_tick >= note.start_tick + note.duration && other.start_tick < next_tick) {
        next_tick = other.start_tick;
        next_pitch = static_cast<int>(other.pitch);
      }
    }
    auto melodic_cost = [&](int pitch) {
      int cost = 0;
      int old_pitch = static_cast<int>(note.pitch);
      if (prev_pitch >= 0) {
        int old_gap = std::abs(old_pitch - prev_pitch);
        int new_gap = std::abs(pitch - prev_pitch);
        cost += std::max(0, new_gap - old_gap) * 12;
        cost += std::max(0, new_gap - 7) * 24;
      }
      if (next_pitch >= 0) {
        int old_gap = std::abs(old_pitch - next_pitch);
        int new_gap = std::abs(pitch - next_pitch);
        cost += std::max(0, new_gap - old_gap) * 12;
        cost += std::max(0, new_gap - 7) * 24;
      }
      return cost;
    };
    int best_pitch = -1;
    int best_cost = INT_MAX;
    for (int delta = -7; delta <= 7; ++delta) {
      if (delta == 0)
        continue;
      int cand_int = static_cast<int>(note.pitch) + delta;
      if (cand_int < voice_lo || cand_int > voice_hi)
        continue;
      if (cand_int < 0 || cand_int > 127)
        continue;
      uint8_t cand = static_cast<uint8_t>(cand_int);
      if (!isDiatonicInKey(cand, event.key, event.is_minor))
        continue;

      int new_bad = vertical_badness(idx, cand);
      if (new_bad > old_bad + 10)
        continue;

      int chord_penalty = isChordTone(cand, event) ? 0 : 20;
      int bad_penalty = std::max(0, new_bad - old_bad) * 40;
      int cost = chord_penalty + bad_penalty + melodic_cost(cand_int) + std::abs(delta);
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand_int;
      }
    }
    if (best_pitch < 0)
      continue;

    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }

  return repairs;
}

static int snapFlexibleNonDiatonicToTimelineScale(std::vector<NoteEvent>& all_notes,
                                                  const FuguePlan& plan, uint8_t num_voices) {
  return snapFlexibleNonDiatonicToTimelineScale(all_notes, plan.detailed_timeline, num_voices);
}

static int snapFlexibleNonDiatonicToKeySignature(std::vector<NoteEvent>& all_notes,
                                                 const HarmonicTimeline& timeline, Key key,
                                                 bool is_minor, uint8_t num_voices,
                                                 bool require_timeline_key = false) {
  auto is_flexible_tonal_surface = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::FreeCounterpoint;
  };

  auto vertical_badness = [&](size_t note_idx, uint8_t pitch) {
    const NoteEvent& note = all_notes[note_idx];
    int bad = 0;
    for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
      if (other_idx == note_idx)
        continue;
      const NoteEvent& other = all_notes[other_idx];
      if (other.voice == note.voice)
        continue;
      if (other.start_tick > note.start_tick ||
          other.start_tick + other.duration <= note.start_tick) {
        continue;
      }
      int diff = std::abs(static_cast<int>(pitch) - static_cast<int>(other.pitch));
      if (diff == 0 || diff >= 36)
        continue;
      int simple = interval_util::compoundToSimple(diff);
      bool p4_upper = simple == interval::kPerfect4th && note.voice < num_voices - 1 &&
                      other.voice < num_voices - 1;
      if (!interval_util::isConsonance(simple) && !p4_upper)
        ++bad;
    }
    return bad;
  };

  int repairs = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (!is_flexible_tonal_surface(note.source))
      continue;

    auto [voice_lo, voice_hi] = getFugueVoiceRange(note.voice, num_voices);
    int old_bad = vertical_badness(idx, note.pitch);
    const HarmonicEvent* event = timeline.size() == 0 ? nullptr : &timeline.getAt(note.start_tick);
    bool in_key_signature = isDiatonicInKey(note.pitch, key, is_minor);
    bool in_required_timeline = !require_timeline_key || event == nullptr ||
                                isDiatonicInKey(note.pitch, event->key, event->is_minor);
    if (in_key_signature && in_required_timeline)
      continue;
    int best_pitch = -1;
    int best_cost = INT_MAX;
    for (int delta = -7; delta <= 7; ++delta) {
      if (delta == 0)
        continue;
      int cand_int = static_cast<int>(note.pitch) + delta;
      if (cand_int < voice_lo || cand_int > voice_hi)
        continue;
      if (cand_int < 0 || cand_int > 127)
        continue;
      uint8_t cand = static_cast<uint8_t>(cand_int);
      if (!isDiatonicInKey(cand, key, is_minor))
        continue;
      if (require_timeline_key && event != nullptr &&
          !isDiatonicInKey(cand, event->key, event->is_minor)) {
        continue;
      }

      int new_bad = vertical_badness(idx, cand);
      int chord_penalty = (event != nullptr && isChordTone(cand, *event)) ? 0 : 20;
      int cost = chord_penalty + std::max(0, new_bad - old_bad) * 40 + std::abs(delta);
      if (cost < best_cost) {
        best_cost = cost;
        best_pitch = cand_int;
      }
    }
    if (best_pitch < 0)
      continue;

    note.pitch = static_cast<uint8_t>(best_pitch);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repairs;
  }
  return repairs;
}

static int resolveFlexibleStrongBeatClashes(std::vector<NoteEvent>& all_notes,
                                            const HarmonicTimeline& output_timeline,
                                            const HarmonicTimeline& generation_timeline, Key key,
                                            bool is_minor, uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  Tick end_tick = 0;
  for (const auto& note : all_notes) {
    end_tick = std::max(end_tick, note.start_tick + note.duration);
  }

  auto sounding_index = [&](VoiceId voice, Tick tick) -> size_t {
    size_t best = all_notes.size();
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice)
        continue;
      if (note.start_tick > tick || note.start_tick + note.duration <= tick) {
        continue;
      }
      if (best == all_notes.size() || note.start_tick >= all_notes[best].start_tick) {
        best = idx;
      }
    }
    return best;
  };

  auto pair_bad = [&](int lhs, int rhs, VoiceId va, VoiceId vb, VoiceId bass_voice) {
    int diff = std::abs(lhs - rhs);
    if (diff == 0 || diff >= 36)
      return 0;
    int simple = interval_util::compoundToSimple(diff);
    bool p4_upper = simple == interval::kPerfect4th && va != bass_voice && vb != bass_voice;
    if (interval_util::isConsonance(simple) || p4_upper)
      return 0;
    return 1;
  };

  auto vertical_badness_at = [&](size_t note_idx, uint8_t pitch, Tick tick, VoiceId bass_voice) {
    const auto& note = all_notes[note_idx];
    int bad = 0;
    for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == note.voice)
        continue;
      size_t other_idx = sounding_index(other_voice, tick);
      if (other_idx == all_notes.size())
        continue;
      bad += pair_bad(static_cast<int>(pitch), static_cast<int>(all_notes[other_idx].pitch),
                      note.voice, other_voice, bass_voice);
    }
    return bad;
  };

  int repairs = 0;
  for (Tick tick = 0; tick < end_tick; tick += kTicksPerBeat) {
    if (!isStrongBeatInBar(tick))
      continue;

    std::vector<size_t> sounding(num_voices, all_notes.size());
    std::vector<int> pitches(num_voices, -1);
    VoiceId bass_voice = 0;
    int bass_pitch = INT_MAX;
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      sounding[voice] = sounding_index(voice, tick);
      if (sounding[voice] == all_notes.size())
        continue;
      pitches[voice] = static_cast<int>(all_notes[sounding[voice]].pitch);
      if (pitches[voice] < bass_pitch) {
        bass_pitch = pitches[voice];
        bass_voice = voice;
      }
    }

    for (VoiceId va = 0; va < num_voices; ++va) {
      if (pitches[va] < 0)
        continue;
      for (VoiceId vb = va + 1; vb < num_voices; ++vb) {
        if (pitches[vb] < 0)
          continue;
        if (pair_bad(pitches[va], pitches[vb], va, vb, bass_voice) == 0) {
          continue;
        }

        int best_pitch = -1;
        size_t best_idx = all_notes.size();
        int best_cost = INT_MAX;
        for (size_t note_idx : {sounding[va], sounding[vb]}) {
          if (note_idx == all_notes.size())
            continue;
          NoteEvent& note = all_notes[note_idx];
          if (getProtectionLevel(note.source) != ProtectionLevel::Flexible) {
            continue;
          }
          auto [voice_lo, voice_hi] = getFugueVoiceRange(note.voice, num_voices);
          const HarmonicEvent* out_ev =
              output_timeline.size() == 0 ? nullptr : &output_timeline.getAt(tick);
          const HarmonicEvent* gen_ev =
              generation_timeline.size() == 0 ? nullptr : &generation_timeline.getAt(tick);
          int old_bad = vertical_badness_at(note_idx, note.pitch, tick, bass_voice);
          for (int delta = -7; delta <= 7; ++delta) {
            if (delta == 0)
              continue;
            int cand_int = static_cast<int>(note.pitch) + delta;
            if (cand_int < voice_lo || cand_int > voice_hi)
              continue;
            if (cand_int < 0 || cand_int > 127)
              continue;
            uint8_t cand = static_cast<uint8_t>(cand_int);
            if (!isDiatonicInKey(cand, key, is_minor))
              continue;
            if (out_ev != nullptr && !isDiatonicInKey(cand, out_ev->key, out_ev->is_minor)) {
              continue;
            }
            if (gen_ev != nullptr && !isDiatonicInKey(cand, gen_ev->key, gen_ev->is_minor)) {
              continue;
            }

            int new_bad = vertical_badness_at(note_idx, cand, tick, bass_voice);
            if (new_bad >= old_bad)
              continue;
            int chord_bonus = 0;
            if (out_ev != nullptr && isChordTone(cand, *out_ev))
              chord_bonus -= 8;
            if (gen_ev != nullptr && isChordTone(cand, *gen_ev))
              chord_bonus -= 8;
            int cost = new_bad * 100 + std::abs(delta) * 4 + chord_bonus;
            if (cost < best_cost) {
              best_cost = cost;
              best_pitch = cand_int;
              best_idx = note_idx;
            }
          }
        }
        if (best_idx != all_notes.size() && best_pitch >= 0) {
          all_notes[best_idx].pitch = static_cast<uint8_t>(best_pitch);
          all_notes[best_idx].modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
          ++repairs;
          pitches[all_notes[best_idx].voice] = best_pitch;
        }
      }
    }
  }

  return repairs;
}

/// @brief Scan consecutive beats for parallel P5/P8 between voice pairs and
/// repair melodic tritone leaps on strong beats.
/// Repairs by adjusting Flexible-protection notes with small pitch shifts
/// snapped to scale.  Outer voice pairs (soprano-bass) are checked first.
static void repairParallelPerfectsAndTritones(std::vector<NoteEvent>& all_notes,
                                              const FugueConfig& config, const FuguePlan& plan,
                                              uint8_t num_voices) {
  constexpr int kMaxParallelRepairs = 16;
  ScaleType repair_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = plan.estimated_duration;

  // Sort by voice then start_tick for voice-based lookup.
  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    return lhs.start_tick < rhs.start_tick;
  });
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);

  auto is_parallel_repairable = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial ||
           source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::Coda ||
           getProtectionLevel(source) == ProtectionLevel::Flexible;
  };

  // Helper: find the note sounding in a given voice at a given tick.
  // Returns pointer to the NoteEvent, or nullptr if none.
  auto findNoteAtTick = [&](VoiceId voice, Tick tick) -> NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.voice != voice)
        continue;
      if (note.start_tick > tick)
        break;  // sorted: no further matches
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick <= tick && note_end > tick) {
        // Keep sixteenth episode tones visible to the repair pass because the
        // analyzer scores parallel/hidden perfects on these attacks too.
        if (note.duration >= duration::kSixteenthNote) {
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
      if (vi == 0 && vj == num_voices - 1)
        continue;  // already added
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
      if (motion_a == 0 || motion_b == 0)
        continue;  // oblique = ok
      if ((motion_a > 0) != (motion_b > 0))
        continue;  // contrary = ok

      // Check parallel perfect interval (same interval class on both beats)
      // and hidden/direct perfects (similar leap motion into P5/P8).
      int prev_ic = std::abs(prev_a - prev_b) % 12;
      int curr_ic = std::abs(curr_a - curr_b) % 12;
      bool is_parallel_perfect = (prev_ic == curr_ic) && (curr_ic == 0 || curr_ic == 7);
      bool is_hidden_perfect = (prev_ic != curr_ic) && (curr_ic == 0 || curr_ic == 7) &&
                               std::abs(motion_a) > 2 && std::abs(motion_b) > 2;
      if (!is_parallel_perfect && !is_hidden_perfect)
        continue;

      pre_count++;
      // Always repair outer pairs. For inner pairs, only touch flexible filler
      // material; structural subject/CS motion is intentionally preserved.
      {
        bool is_outer = (vi == 0 && vj == static_cast<VoiceId>(num_voices - 1));
        bool flexible_filler_pair = is_parallel_repairable(curr_note_a->source) ||
                                    is_parallel_repairable(curr_note_b->source);
        if (!is_outer && !flexible_filler_pair)
          continue;
        if (is_outer)
          outer_count++;
      }

      // Determine which note to modify: must be Flexible.
      NoteEvent* target = nullptr;
      int other_pitch = 0;
      int prev_voice_pitch = 0;
      if (curr_note_b->source == BachNoteSource::EpisodeMaterial &&
          is_parallel_repairable(curr_note_b->source)) {
        target = curr_note_b;
        other_pitch = curr_a;
        prev_voice_pitch = prev_b;
      } else if (curr_note_a->source == BachNoteSource::EpisodeMaterial &&
                 is_parallel_repairable(curr_note_a->source)) {
        target = curr_note_a;
        other_pitch = curr_b;
        prev_voice_pitch = prev_a;
      } else if (is_parallel_repairable(curr_note_b->source)) {
        target = curr_note_b;
        other_pitch = curr_a;
        prev_voice_pitch = prev_b;
      } else if (is_parallel_repairable(curr_note_a->source)) {
        target = curr_note_a;
        other_pitch = curr_b;
        prev_voice_pitch = prev_a;
      }
      if (!target)
        continue;  // both Immutable

      // Generate candidates: nearby diatonic alternatives, snapped to scale.
      // ±3/±4 catches hidden-perfect landings where stepwise nudges collapse
      // back to the same scale tone or remain perfect.
      int best_pitch = -1;
      int best_leap_cost = INT_MAX;
      auto [target_lo, target_hi] = getFugueVoiceRange(target->voice, num_voices);
      for (int delta : {-1, 1, -2, 2, -3, 3, -4, 4}) {
        int raw_cand = static_cast<int>(target->pitch) + delta;
        if (raw_cand < 0 || raw_cand > 127)
          continue;
        int cand = static_cast<int>(
            scale_util::nearestScaleTone(static_cast<uint8_t>(raw_cand), config.key, repair_scale));
        if (cand < target_lo || cand > target_hi)
          continue;

        if (!vertical_safe(beat, target->voice, static_cast<uint8_t>(cand)))
          continue;

        // Vertical safety: new interval should not be dissonant.
        int new_ic = std::abs(cand - other_pitch) % 12;
        // Reject P1/m2/M2/tritone (0, 1, 2, 6).
        if (new_ic == 0 || new_ic == 1 || new_ic == 2 || new_ic == 6)
          continue;

        // Verify the repair does not recreate a parallel/hidden perfect.
        int prev_other_pitch = (target == curr_note_a) ? prev_b : prev_a;
        int prev_pair_ic = std::abs(prev_voice_pitch - prev_other_pitch) % 12;
        int new_pair_ic = std::abs(cand - other_pitch) % 12;
        if ((new_pair_ic == 0 || new_pair_ic == 7) && new_pair_ic == prev_pair_ic) {
          continue;  // would still be parallel perfect
        }
        int new_motion_target = cand - prev_voice_pitch;
        int other_motion = (target == curr_note_a) ? motion_b : motion_a;
        bool same_motion = new_motion_target != 0 && other_motion != 0 &&
                           (new_motion_target > 0) == (other_motion > 0);
        bool hidden_still = same_motion && (new_pair_ic == 0 || new_pair_ic == 7) &&
                            prev_pair_ic != new_pair_ic && std::abs(new_motion_target) > 2 &&
                            std::abs(other_motion) > 2;
        if (hidden_still)
          continue;

        // Melodic continuity: leap should not increase too much.
        int leap_before = std::abs(cand - prev_voice_pitch);
        int original_leap = std::abs(static_cast<int>(target->pitch) - prev_voice_pitch);
        if (leap_before > original_leap + 2)
          continue;

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
      if (repair_count >= kMaxParallelRepairs)
        break;

      size_t curr_idx = voice_indices[pos];
      size_t prev_idx = voice_indices[pos - 1];
      auto& curr_note = all_notes[curr_idx];
      auto& prev_note = all_notes[prev_idx];

      int leap = std::abs(static_cast<int>(curr_note.pitch) - static_cast<int>(prev_note.pitch));
      if (leap != 6)
        continue;  // not a tritone

      // Only repair on strong beats (bar start or beat start).
      if (curr_note.start_tick % kTicksPerBar >= kTicksPerBeat)
        continue;

      // Only modify flexible local material, plus coda notes whose exact
      // register is less important than avoiding exposed tritone leaps.
      if (!is_parallel_repairable(curr_note.source)) {
        continue;
      }

      // Try ±1 candidates, snap to scale.
      int best_cand = -1;
      int best_cost = INT_MAX;
      auto [voice_lo, voice_hi] = getFugueVoiceRange(curr_note.voice, num_voices);
      for (int delta : {-1, 1}) {
        int raw = static_cast<int>(curr_note.pitch) + delta;
        if (raw < 0 || raw > 127)
          continue;
        int cand = static_cast<int>(
            scale_util::nearestScaleTone(static_cast<uint8_t>(raw), config.key, repair_scale));
        if (cand < voice_lo || cand > voice_hi)
          continue;
        if (!vertical_safe(curr_note.start_tick, curr_note.voice, static_cast<uint8_t>(cand))) {
          continue;
        }
        int new_leap = std::abs(cand - static_cast<int>(prev_note.pitch));
        if (new_leap == 6)
          continue;  // still tritone

        // Check continuity with next note if exists.
        if (pos + 1 < voice_indices.size()) {
          int next_pitch = static_cast<int>(all_notes[voice_indices[pos + 1]].pitch);
          int next_leap = std::abs(cand - next_pitch);
          int orig_next = std::abs(static_cast<int>(curr_note.pitch) - next_pitch);
          if (next_leap > orig_next + 2)
            continue;
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

  // --- Episode excessive-leap octave repair ---
  // Very local guard for register-bounce artifacts such as E5->B3->E5 in
  // episode figuration.  Only fold the current episode note by one octave
  // toward the previous episode note, and only when the existing safety checks
  // accept the new register.
  for (VoiceId vid = 0; vid < num_voices; ++vid) {
    std::vector<size_t> voice_indices;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      if (all_notes[idx].voice == vid) {
        voice_indices.push_back(idx);
      }
    }

    for (size_t pos = 1; pos < voice_indices.size(); ++pos) {
      if (repair_count >= kMaxParallelRepairs)
        break;

      size_t curr_idx = voice_indices[pos];
      size_t prev_idx = voice_indices[pos - 1];
      auto& curr_note = all_notes[curr_idx];
      const auto& prev_note = all_notes[prev_idx];
      if (curr_note.source != BachNoteSource::EpisodeMaterial ||
          prev_note.source != BachNoteSource::EpisodeMaterial) {
        continue;
      }

      int leap = static_cast<int>(curr_note.pitch) - static_cast<int>(prev_note.pitch);
      if (std::abs(leap) <= 12)
        continue;

      std::vector<int> candidates;
      candidates.push_back(static_cast<int>(curr_note.pitch) + (leap > 0 ? -12 : 12));
      int prev_pitch = static_cast<int>(prev_note.pitch);
      int old_pitch = static_cast<int>(curr_note.pitch);
      for (int cand = old_pitch - 7; cand <= old_pitch + 7; ++cand) {
        if (cand == old_pitch)
          continue;
        if (std::abs(cand - prev_pitch) > 12)
          continue;
        candidates.push_back(cand);
      }
      std::sort(candidates.begin(), candidates.end(), [prev_pitch, old_pitch](int lhs, int rhs) {
        int lhs_leap = std::abs(lhs - prev_pitch);
        int rhs_leap = std::abs(rhs - prev_pitch);
        if (lhs_leap != rhs_leap)
          return lhs_leap < rhs_leap;
        return std::abs(lhs - old_pitch) < std::abs(rhs - old_pitch);
      });
      candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

      int best_cand = -1;
      for (int cand : candidates) {
        if (cand < 0 || cand > 127)
          continue;
        if (!spacingCandidateIsSafe(all_notes, vertical_safe, curr_idx, static_cast<uint8_t>(cand),
                                    num_voices)) {
          continue;
        }

        if (pos + 1 < voice_indices.size()) {
          int next_pitch = static_cast<int>(all_notes[voice_indices[pos + 1]].pitch);
          int old_next = std::abs(static_cast<int>(curr_note.pitch) - next_pitch);
          int new_next = std::abs(cand - next_pitch);
          if (new_next > 12 && new_next > old_next)
            continue;
        }
        best_cand = cand;
        break;
      }
      if (best_cand < 0)
        continue;

      curr_note.pitch = static_cast<uint8_t>(best_cand);
      curr_note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
      repair_count++;
    }
  }

  fprintf(stderr, "P7.d sweep: %d parallels found (%d outer), %d repaired\n", pre_count,
          outer_count, repair_count);
}

/// @brief Scan each beat for adjacent voice pairs where the higher-numbered
/// voice (lower register) sounds a higher pitch than the lower-numbered voice
/// (higher register).  Repairs by adjusting the Flexible note with the
/// smallest diatonic pitch shift that resolves the crossing.
/// Limit: 8 repairs per section to protect melodic linearity.
static void repairVoiceCrossings(std::vector<NoteEvent>& all_notes, const FugueConfig& config,
                                 const FuguePlan& plan, uint8_t num_voices) {
  constexpr int kMaxCrossingRepairsPerSection = 8;
  ScaleType crossing_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = plan.estimated_duration;
  auto vertical_safe =
      makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);

  // Notes are already sorted by voice then start_tick from the parallel sweep.
  // Reuse the same findNoteAtTick pattern.
  auto findNoteForVoice = [&](VoiceId voice, Tick tick) -> NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.voice != voice)
        continue;
      if (note.start_tick > tick)
        break;  // sorted: no further matches
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
      if (note.voice != voice)
        continue;
      if (note.start_tick >= tick)
        break;  // sorted
      best = &note;
    }
    return best;
  };

  // Helper: find the next note in the same voice after a given tick.
  auto findNextNote = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice)
        continue;
      if (note.start_tick > tick)
        return &note;
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
    Tick sec_end =
        (sec_idx + 1 < plan.sections.size()) ? plan.sections[sec_idx + 1].start_tick : total_ticks;
    int section_repairs = 0;
    bool conclude_policy = plan.sections[sec_idx].type == SectionType::Coda ||
                           plan.sections[sec_idx].type == SectionType::Stretto;
    int max_repairs = conclude_policy ? INT_MAX : kMaxCrossingRepairsPerSection;

    for (Tick beat = sec_start; beat < sec_end; beat += kTicksPerBeat) {
      if (section_repairs >= max_repairs)
        break;

      // Check adjacent voice pairs (vi, vi+1).
      for (VoiceId vi = 0; vi + 1 < num_voices; ++vi) {
        if (section_repairs >= max_repairs)
          break;

        VoiceId vj = vi + 1;
        NoteEvent* note_upper = findNoteForVoice(vi, beat);
        NoteEvent* note_lower = findNoteForVoice(vj, beat);
        if (!note_upper || !note_lower)
          continue;

        int pitch_upper = static_cast<int>(note_upper->pitch);
        int pitch_lower = static_cast<int>(note_lower->pitch);

        // Crossing: higher-numbered voice (lower register) has higher pitch.
        if (pitch_upper >= pitch_lower)
          continue;

        // 2-beat lookahead: skip temporary crossings (matches Python validator).
        // Crossings that resolve within 2 beats are INFO (0pts) in scoring.
        bool resolves_soon = false;
        for (int ahead = 1; ahead <= 2; ++ahead) {
          Tick future = beat + kTicksPerBeat * ahead;
          if (future >= sec_end)
            break;
          NoteEvent* fu = findNoteForVoice(vi, future);
          NoteEvent* fl = findNoteForVoice(vj, future);
          if (fu && fl && static_cast<int>(fu->pitch) >= static_cast<int>(fl->pitch)) {
            resolves_soon = true;
            break;
          }
        }
        if (resolves_soon)
          continue;

        total_crossings_found++;

        // Determine which note to modify based on ProtectionLevel.
        bool upper_flex = getProtectionLevel(note_upper->source) == ProtectionLevel::Flexible;
        bool lower_flex = getProtectionLevel(note_lower->source) == ProtectionLevel::Flexible;

        if (!upper_flex && !lower_flex)
          continue;  // both Immutable: skip

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
        auto [target_lo, target_hi] = getFugueVoiceRange(target->voice, num_voices);

        // Find previous and next notes for melodic continuity check.
        const NoteEvent* prev = findPrevNote(target->voice, target->start_tick);
        const NoteEvent* next =
            findNextNote(target->voice, target->start_tick + target->duration - 1);

        int orig_prev_leap = prev ? std::abs(target_pitch - static_cast<int>(prev->pitch)) : 0;
        int orig_next_leap = next ? std::abs(target_pitch - static_cast<int>(next->pitch)) : 0;

        // Generate candidates: target.pitch +/- 1..12 semitones, snapped to scale.
        int best_cand = -1;
        int best_cost = INT_MAX;

        for (int delta : {1, -1, 2, -2, 3, -3, 4,  -4,  5,  -5,  6,  -6,
                          7, -7, 8, -8, 9, -9, 10, -10, 11, -11, 12, -12}) {
          int raw = target_pitch + delta;
          if (raw < 0 || raw > 127)
            continue;

          int cand = static_cast<int>(
              scale_util::nearestScaleTone(static_cast<uint8_t>(raw), config.key, crossing_scale));
          if (cand < 0 || cand > 127)
            continue;
          if (cand < target_lo || cand > target_hi)
            continue;
          if (!vertical_safe(beat, target->voice, static_cast<uint8_t>(cand)))
            continue;

          // Check: does the new pitch resolve the crossing?
          // If moving the upper voice (vi), cand must be >= anchor (lower voice pitch).
          // If moving the lower voice (vj), cand must be <= anchor (upper voice pitch).
          if (moving_upper) {
            if (cand < anchor_pitch)
              continue;  // still crossed
          } else {
            if (cand > anchor_pitch)
              continue;  // still crossed
          }

          // Check: vertical interval with the other voice is not unison/m2/tritone.
          int interval = std::abs(cand - anchor_pitch) % 12;
          if (interval == 1 || interval == 2 || interval == 6)
            continue;
          // Allow unison (interval == 0) only if voices are widely separated in register.

          // Check: melodic jump to prev/next note <= original jump + 4 semitones.
          if (prev) {
            int new_prev_leap = std::abs(cand - static_cast<int>(prev->pitch));
            if (new_prev_leap > orig_prev_leap + 4)
              continue;
          }
          if (next) {
            int new_next_leap = std::abs(cand - static_cast<int>(next->pitch));
            if (new_next_leap > orig_next_leap + 4)
              continue;
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

  fprintf(stderr, "Voice crossing sweep: %d crossings found, %d repaired\n", total_crossings_found,
          total_crossing_repairs);
}

static int trimCadenceApproachManualCrossings(std::vector<NoteEvent>& all_notes,
                                              const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  int repairs = 0;
  Tick total_ticks = plan.estimated_duration;
  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    for (VoiceId upper_voice = 0; upper_voice < 2; ++upper_voice) {
      VoiceId lower_voice = upper_voice + 1;
      size_t upper_idx = all_notes.size();
      size_t lower_idx = all_notes.size();
      for (size_t idx = 0; idx < all_notes.size(); ++idx) {
        const auto& note = all_notes[idx];
        if (note.start_tick > beat || note.start_tick + note.duration <= beat) {
          continue;
        }
        if (note.voice == upper_voice &&
            (upper_idx == all_notes.size() || note.start_tick >= all_notes[upper_idx].start_tick)) {
          upper_idx = idx;
        } else if (note.voice == lower_voice &&
                   (lower_idx == all_notes.size() ||
                    note.start_tick >= all_notes[lower_idx].start_tick)) {
          lower_idx = idx;
        }
      }
      if (upper_idx == all_notes.size() || lower_idx == all_notes.size()) {
        continue;
      }

      NoteEvent& upper = all_notes[upper_idx];
      const NoteEvent& lower = all_notes[lower_idx];
      if (upper.source != BachNoteSource::CadenceApproach)
        continue;
      if (upper.pitch >= lower.pitch)
        continue;
      if (upper.start_tick >= beat)
        continue;

      Tick new_duration = beat - upper.start_tick;
      if (new_duration < duration::kSixteenthNote)
        continue;
      if (new_duration >= upper.duration)
        continue;

      upper.duration = new_duration;
      upper.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
      ++repairs;
    }
  }
  return repairs;
}

static void enforceFinalCadenceBass(std::vector<NoteEvent>& all_notes, const FugueConfig& config,
                                    const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4)
    return;

  (void)plan;
  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& note : all_notes) {
    if (note.source != BachNoteSource::Coda)
      continue;
    coda_start = std::min(coda_start, note.start_tick);
    coda_end = std::max(coda_end, note.start_tick + note.duration);
  }
  if (coda_start == std::numeric_limits<Tick>::max() || coda_end <= coda_start)
    return;

  Tick coda_duration = coda_end - coda_start;
  if (coda_duration < kTicksPerBar * 2)
    return;

  VoiceId bass_voice = num_voices - 1;
  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);
  int dominant = static_cast<int>(tonic) + 7;
  while (dominant > static_cast<int>(bass_hi))
    dominant -= 12;
  while (dominant < static_cast<int>(bass_lo))
    dominant += 12;
  uint8_t dominant_pitch = clampPitch(dominant, bass_lo, bass_hi);

  Tick v_start = coda_start + kTicksPerBar * 2;
  Tick i_start = v_start + kTicksPerBar / 2;
  Tick final_start = coda_start + kTicksPerBar * 3;
  if (final_start >= coda_end || i_start >= final_start)
    return;

  removeLowestVoiceNotes(all_notes, bass_voice, v_start, coda_end);

  NoteEvent dominant_note;
  dominant_note.start_tick = v_start;
  dominant_note.duration = i_start - v_start;
  dominant_note.pitch = dominant_pitch;
  dominant_note.velocity = kOrganVelocity;
  dominant_note.voice = bass_voice;
  dominant_note.source = BachNoteSource::Coda;
  all_notes.push_back(dominant_note);

  NoteEvent tonic_resolution;
  tonic_resolution.start_tick = i_start;
  tonic_resolution.duration = final_start - i_start;
  tonic_resolution.pitch = tonic;
  tonic_resolution.velocity = kOrganVelocity;
  tonic_resolution.voice = bass_voice;
  tonic_resolution.source = BachNoteSource::Coda;
  all_notes.push_back(tonic_resolution);

  auto tonic_pedal = generatePedalPoint(tonic, final_start, coda_end - final_start, bass_voice);
  all_notes.insert(all_notes.end(), tonic_pedal.begin(), tonic_pedal.end());
}

static int addBwv578MiddleHalfCadenceSupport(std::vector<NoteEvent>& all_notes,
                                             const FugueConfig& config, const FuguePlan& plan,
                                             uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;
  constexpr Tick kCadenceBarStart = kTicksPerBar * 29;  // bar 30, beat 1
  if (plan.estimated_duration < kCadenceBarStart + kTicksPerBar * 4)
    return 0;

  VoiceId bass_voice = num_voices - 1;
  Tick dominant_tick = kCadenceBarStart + kTicksPerBeat * 2;
  const NoteEvent* bass_at_dominant = soundingNoteAt(all_notes, bass_voice, dominant_tick);
  if (bass_at_dominant == nullptr)
    return 0;

  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);
  int dominant = static_cast<int>(tonic) + 7;
  while (dominant > static_cast<int>(bass_hi))
    dominant -= 12;
  while (dominant < static_cast<int>(bass_lo))
    dominant += 12;
  uint8_t dominant_pitch = clampPitch(dominant, bass_lo, bass_hi);
  if (bass_at_dominant->pitch != dominant_pitch)
    return 0;

  size_t soprano_idx = all_notes.size();
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.voice != 0)
      continue;
    if (note.start_tick <= dominant_tick && note.start_tick + note.duration > dominant_tick) {
      soprano_idx = idx;
    }
  }
  if (soprano_idx == all_notes.size())
    return 0;
  NoteEvent& soprano = all_notes[soprano_idx];
  if (getProtectionLevel(soprano.source) != ProtectionLevel::Flexible)
    return 0;

  auto [sop_lo, sop_hi] = getFugueVoiceRange(0, num_voices);
  uint8_t soprano_dominant = clampPitch(static_cast<int>(dominant_pitch) + 24, sop_lo, sop_hi);

  Tick region_end = kCadenceBarStart + kTicksPerBar + kTicksPerBeat;
  trimVoiceNotesForRegion(all_notes, bass_voice, kCadenceBarStart, region_end);

  NoteEvent tonic_note;
  tonic_note.pitch = tonic;
  tonic_note.velocity = kOrganVelocity;
  tonic_note.start_tick = kCadenceBarStart;
  tonic_note.duration = kTicksPerBeat * 2;
  tonic_note.voice = bass_voice;
  tonic_note.source = BachNoteSource::CadenceApproach;
  all_notes.push_back(tonic_note);

  NoteEvent dominant_note = tonic_note;
  dominant_note.pitch = dominant_pitch;
  dominant_note.start_tick = dominant_tick;
  dominant_note.duration = kTicksPerBeat * 2;
  all_notes.push_back(dominant_note);

  NoteEvent step_resolution = tonic_note;
  step_resolution.pitch = clampPitch(static_cast<int>(dominant_pitch) - 2, bass_lo, bass_hi);
  step_resolution.start_tick = kCadenceBarStart + kTicksPerBar;
  step_resolution.duration = kTicksPerBeat;
  all_notes.push_back(step_resolution);

  for (auto& note : all_notes) {
    if (note.voice == 0 && note.start_tick <= dominant_tick &&
        note.start_tick + note.duration > dominant_tick) {
      note.pitch = soprano_dominant;
      note.source = BachNoteSource::CadenceApproach;
      note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
      break;
    }
  }

  return 4;
}

static int addBwv578MiddleEntryPedalContinuitySupport(std::vector<NoteEvent>& all_notes,
                                                      const FugueConfig& config,
                                                      const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  VoiceId bass_voice = num_voices - 1;
  int tonic_pc = static_cast<int>(config.key) % 12;
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);

  struct SupportGap {
    Tick start;
    Tick end;
    int target_pc;
  };
  const SupportGap gaps[] = {
      {kTicksPerBar * 17 + kTicksPerBeat * 2,
       kTicksPerBar * 17 + kTicksPerBeat * 3 + kTicksPerBeat / 2,
       (tonic_pc + interval::kPerfect4th) % 12},
      {kTicksPerBar * 18, kTicksPerBar * 18 + kTicksPerBeat / 2 + 45,
       (tonic_pc + interval::kPerfect4th) % 12},
      {kTicksPerBar * 18 + kTicksPerBeat * 2 + kTicksPerBeat / 2 + 45, kTicksPerBar * 19,
       (tonic_pc + interval::kMajor3rd) % 12},
  };

  int inserted = 0;
  for (const auto& gap : gaps) {
    if (gap.end <= gap.start || plan.estimated_duration <= gap.end)
      continue;
    if (soundingNoteAt(all_notes, bass_voice, gap.start) != nullptr ||
        soundingNoteAt(all_notes, bass_voice, gap.end - 1) != nullptr) {
      continue;
    }

    const NoteEvent* previous = nullptr;
    const NoteEvent* next = nullptr;
    for (const auto& note : all_notes) {
      if (note.voice != bass_voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      if (note_end <= gap.start &&
          (previous == nullptr || note_end > previous->start_tick + previous->duration)) {
        previous = &note;
      }
      if (note.start_tick >= gap.end && (next == nullptr || note.start_tick < next->start_tick)) {
        next = &note;
      }
    }
    if (previous == nullptr || next == nullptr)
      continue;

    int support_pitch = nearestPedalPitchForPc(gap.target_pc, bass_lo, bass_hi, previous->pitch);
    if (support_pitch < 0)
      continue;

    NoteEvent support;
    support.pitch = static_cast<uint8_t>(support_pitch);
    support.velocity = kOrganVelocity;
    support.start_tick = gap.start;
    support.duration = gap.end - gap.start;
    support.voice = bass_voice;
    support.source = BachNoteSource::EpisodeMaterial;
    all_notes.push_back(support);
    ++inserted;
  }
  return inserted;
}

static int addBwv578EarlyEpisodePedalContinuitySupport(std::vector<NoteEvent>& all_notes,
                                                       const FugueConfig& config,
                                                       const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  constexpr Tick kGapStart = kTicksPerBar * 8 + kTicksPerBeat;
  constexpr Tick kGapEnd = kGapStart + kTicksPerBeat / 2;
  if (plan.estimated_duration <= kGapEnd)
    return 0;

  VoiceId bass_voice = num_voices - 1;
  if (soundingNoteAt(all_notes, bass_voice, kGapStart) != nullptr ||
      soundingNoteAt(all_notes, bass_voice, kGapEnd - 1) != nullptr) {
    return 0;
  }

  const NoteEvent* previous = nullptr;
  const NoteEvent* next = nullptr;
  for (const auto& note : all_notes) {
    if (note.voice != bass_voice)
      continue;
    Tick note_end = note.start_tick + note.duration;
    if (note_end <= kGapStart &&
        (previous == nullptr || note_end > previous->start_tick + previous->duration)) {
      previous = &note;
    }
    if (note.start_tick >= kGapEnd && (next == nullptr || note.start_tick < next->start_tick)) {
      next = &note;
    }
  }
  if (previous == nullptr || next == nullptr)
    return 0;

  int tonic_pc = static_cast<int>(config.key) % 12;
  int supertonic_pc = (tonic_pc + interval::kMajor2nd) % 12;
  int mediant_pc = (tonic_pc + interval::kMajor3rd) % 12;
  if (getPitchClass(previous->pitch) != supertonic_pc || getPitchClass(next->pitch) != mediant_pc) {
    return 0;
  }

  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);
  int support_pitch = nearestPedalPitchForPc(tonic_pc, bass_lo, bass_hi, previous->pitch);
  if (support_pitch < 0)
    return 0;

  NoteEvent support;
  support.pitch = static_cast<uint8_t>(support_pitch);
  support.velocity = kOrganVelocity;
  support.start_tick = kGapStart;
  support.duration = kGapEnd - kGapStart;
  support.voice = bass_voice;
  support.source = BachNoteSource::EpisodeMaterial;
  all_notes.push_back(support);
  return 1;
}

static int consolidateBwv578EarlyEpisodeManualIILine(std::vector<NoteEvent>& all_notes,
                                                     const FugueConfig& config,
                                                     const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  constexpr VoiceId kLineVoice = 1;
  const Key dominant = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  const Key relative_minor = getRelative(KeySignature{config.key, config.is_minor}).tonic;
  const Key subdominant = getSubdominant(KeySignature{config.key, config.is_minor}).tonic;

  struct Replacement {
    Tick start;
    Tick duration;
    uint8_t pitch;
  };
  auto consolidate_section = [&](const PlannedSection& section, int offset_beats,
                                 int length_beats) {
    Tick line_start = section.start_tick + kTicksPerBeat * offset_beats;
    line_start = ((line_start + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    Tick line_end = std::min(section.end_tick, line_start + kTicksPerBeat * length_beats);
    if (line_end <= line_start)
      return 0;

    std::vector<Replacement> replacements;
    for (Tick tick = line_start; tick + kTicksPerBeat <= line_end; tick += kTicksPerBeat) {
      const NoteEvent* source = soundingNoteAt(all_notes, kLineVoice, tick);
      if (source == nullptr || source->source != BachNoteSource::EpisodeMaterial) {
        continue;
      }
      replacements.push_back({tick, kTicksPerBeat, source->pitch});
    }
    if (replacements.size() < 3)
      return 0;

    size_t before = all_notes.size();
    all_notes.erase(std::remove_if(all_notes.begin(), all_notes.end(),
                                   [&](const NoteEvent& note) {
                                     if (note.voice != kLineVoice ||
                                         note.source != BachNoteSource::EpisodeMaterial) {
                                       return false;
                                     }
                                     Tick note_end = note.start_tick + note.duration;
                                     return note.start_tick < line_end && note_end > line_start;
                                   }),
                    all_notes.end());
    if (before == all_notes.size())
      return 0;

    for (const auto& replacement : replacements) {
      NoteEvent note;
      note.start_tick = replacement.start;
      note.duration = replacement.duration;
      note.pitch = replacement.pitch;
      note.velocity = kOrganVelocity;
      note.voice = kLineVoice;
      note.source = BachNoteSource::EpisodeMaterial;
      all_notes.push_back(note);
    }

    return static_cast<int>(replacements.size());
  };

  int inserted = 0;
  bool handled_dominant = false;
  bool handled_relative_minor = false;
  bool handled_subdominant = false;
  bool handled_home_return = false;
  for (const auto& section : plan.sections) {
    if (section.type != SectionType::Episode)
      continue;
    if (!handled_dominant && section.key == dominant) {
      inserted += consolidate_section(section, 3, 5);
      handled_dominant = true;
    } else if (!handled_relative_minor && section.key == relative_minor) {
      inserted += consolidate_section(section, 2, 6);
      handled_relative_minor = true;
    } else if (!handled_subdominant && section.key == subdominant) {
      inserted += consolidate_section(section, 2, 5);
      handled_subdominant = true;
    } else if (!handled_home_return && section.key == config.key &&
               section.start_tick > plan.estimated_duration / 2) {
      inserted += consolidate_section(section, 0, 6);
      handled_home_return = true;
    }
  }

  return inserted;
}

static int addBwv578SubdominantBridgePedalSupport(std::vector<NoteEvent>& all_notes,
                                                  const FugueConfig& config, const FuguePlan& plan,
                                                  uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  VoiceId bass_voice = num_voices - 1;
  int tonic_pc = static_cast<int>(config.key) % 12;
  int subdominant_pc = (tonic_pc + interval::kPerfect4th) % 12;
  int dominant_pc = (tonic_pc + interval::kPerfect5th) % 12;
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);

  struct Support {
    Tick start;
    int target_pc;
  };
  const Support supports[] = {
      {kTicksPerBar * 23 + kTicksPerBeat, subdominant_pc},
      {kTicksPerBar * 23 + kTicksPerBeat * 2, dominant_pc},
      {kTicksPerBar * 24, dominant_pc},
  };

  int inserted = 0;
  for (const auto& item : supports) {
    Tick end = item.start + kTicksPerBeat / 2;
    if (plan.estimated_duration <= end)
      continue;
    if (soundingNoteAt(all_notes, bass_voice, item.start) != nullptr ||
        soundingNoteAt(all_notes, bass_voice, end - 1) != nullptr) {
      continue;
    }

    const NoteEvent* previous = nullptr;
    for (const auto& note : all_notes) {
      if (note.voice != bass_voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      if (note_end <= item.start &&
          (previous == nullptr || note_end > previous->start_tick + previous->duration)) {
        previous = &note;
      }
    }
    int anchor = previous ? previous->pitch : tonicBassPitchForVoices(config.key, num_voices);
    int support_pitch = nearestPedalPitchForPc(item.target_pc, bass_lo, bass_hi, anchor);
    if (support_pitch < 0)
      continue;

    NoteEvent support;
    support.pitch = static_cast<uint8_t>(support_pitch);
    support.velocity = kOrganVelocity;
    support.start_tick = item.start;
    support.duration = end - item.start;
    support.voice = bass_voice;
    support.source = BachNoteSource::EpisodeMaterial;
    all_notes.push_back(support);
    ++inserted;
  }
  return inserted;
}

static int addBwv578StrettoPedalContinuitySupport(std::vector<NoteEvent>& all_notes,
                                                  const FugueConfig& config, const FuguePlan& plan,
                                                  uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  constexpr Tick kGapStart = kTicksPerBar * 30 + kTicksPerBeat;
  constexpr Tick kGapEnd = kGapStart + kTicksPerBeat / 2;
  if (plan.estimated_duration <= kGapEnd)
    return 0;

  VoiceId bass_voice = num_voices - 1;
  if (soundingNoteAt(all_notes, bass_voice, kGapStart) != nullptr ||
      soundingNoteAt(all_notes, bass_voice, kGapEnd - 1) != nullptr) {
    return 0;
  }

  const NoteEvent* previous = nullptr;
  for (const auto& note : all_notes) {
    if (note.voice != bass_voice)
      continue;
    Tick note_end = note.start_tick + note.duration;
    if (note_end <= kGapStart &&
        (previous == nullptr || note_end > previous->start_tick + previous->duration)) {
      previous = &note;
    }
  }
  if (previous == nullptr)
    return 0;

  int tonic_pc = static_cast<int>(config.key) % 12;
  int subdominant_pc = (tonic_pc + interval::kPerfect4th) % 12;
  if (getPitchClass(previous->pitch) != subdominant_pc)
    return 0;

  int dominant_pc = (tonic_pc + interval::kPerfect5th) % 12;
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);
  int support_pitch = nearestPedalPitchForPc(dominant_pc, bass_lo, bass_hi, previous->pitch);
  if (support_pitch < 0)
    return 0;

  NoteEvent support;
  support.pitch = static_cast<uint8_t>(support_pitch);
  support.velocity = kOrganVelocity;
  support.start_tick = kGapStart;
  support.duration = kGapEnd - kGapStart;
  support.voice = bass_voice;
  support.source = BachNoteSource::EpisodeMaterial;
  all_notes.push_back(support);
  return 1;
}

static int addBwv578StrettoManualIIIGapSupport(std::vector<NoteEvent>& all_notes,
                                               const FugueConfig& config, const FuguePlan& plan,
                                               uint8_t num_voices) {
  if (num_voices != 4 || config.is_minor)
    return 0;

  constexpr VoiceId lower_manual = 2;
  constexpr Tick kSupportStart = kTicksPerBar * 31 + kTicksPerBeat * 3 + kTicksPerBeat / 2;
  constexpr Tick kSupportDuration = kTicksPerBeat / 2;
  constexpr Tick kSupportEnd = kSupportStart + kSupportDuration;
  if (plan.estimated_duration <= kSupportEnd)
    return 0;
  if (soundingNoteAt(all_notes, lower_manual, kSupportStart) != nullptr ||
      soundingNoteAt(all_notes, lower_manual, kSupportEnd - 1) != nullptr) {
    return 0;
  }

  const NoteEvent* previous = nullptr;
  const NoteEvent* next = nullptr;
  for (const auto& note : all_notes) {
    if (note.voice != lower_manual)
      continue;
    Tick note_end = note.start_tick + note.duration;
    if (note_end <= kSupportStart &&
        (previous == nullptr || note_end > previous->start_tick + previous->duration)) {
      previous = &note;
    }
    if (note.start_tick >= kSupportEnd && (next == nullptr || note.start_tick < next->start_tick)) {
      next = &note;
    }
  }
  if (previous == nullptr || next == nullptr)
    return 0;
  if (previous->pitch != 62 || next->pitch != 69)
    return 0;

  auto [lo, hi] = getFugueVoiceRange(lower_manual, num_voices);
  uint8_t pitch = clampPitch(65, lo, hi);
  NoteEvent support;
  support.pitch = pitch;
  support.velocity = kOrganVelocity;
  support.start_tick = kSupportStart;
  support.duration = kSupportDuration;
  support.voice = lower_manual;
  support.source = BachNoteSource::EpisodeMaterial;
  all_notes.push_back(support);
  return 1;
}

static void enforceFinalCadenceBassOnTracks(std::vector<Track>& tracks, const FugueConfig& config,
                                            const FuguePlan& plan, uint8_t num_voices) {
  if (num_voices != 4 || tracks.empty())
    return;

  (void)plan;
  Tick coda_start = std::numeric_limits<Tick>::max();
  Tick coda_end = 0;
  for (const auto& track : tracks) {
    for (const auto& note : track.notes) {
      if (note.source != BachNoteSource::Coda)
        continue;
      coda_start = std::min(coda_start, note.start_tick);
      coda_end = std::max(coda_end, note.start_tick + note.duration);
    }
  }
  if (coda_start == std::numeric_limits<Tick>::max() || coda_end <= coda_start)
    return;

  VoiceId bass_voice = num_voices - 1;
  if (bass_voice >= tracks.size())
    return;

  Tick coda_duration = coda_end - coda_start;
  if (coda_duration < kTicksPerBar * 2)
    return;

  uint8_t tonic = tonicBassPitchForVoices(config.key, num_voices);
  auto [bass_lo, bass_hi] = getFugueVoiceRange(bass_voice, num_voices);
  int dominant = static_cast<int>(tonic) + 7;
  while (dominant > static_cast<int>(bass_hi))
    dominant -= 12;
  while (dominant < static_cast<int>(bass_lo))
    dominant += 12;
  uint8_t dominant_pitch = clampPitch(dominant, bass_lo, bass_hi);

  Tick v_start = coda_start + kTicksPerBar * 2;
  Tick i_start = v_start + kTicksPerBar / 2;
  Tick final_start = coda_start + kTicksPerBar * 3;
  if (final_start >= coda_end || i_start >= final_start)
    return;

  auto& bass_notes = tracks[bass_voice].notes;
  bass_notes.erase(std::remove_if(bass_notes.begin(), bass_notes.end(),
                                  [v_start, coda_end](const NoteEvent& note) {
                                    Tick note_end = note.start_tick + note.duration;
                                    return note.start_tick < coda_end && note_end > v_start;
                                  }),
                   bass_notes.end());

  NoteEvent dominant_note;
  dominant_note.start_tick = v_start;
  dominant_note.duration = i_start - v_start;
  dominant_note.pitch = dominant_pitch;
  dominant_note.velocity = kOrganVelocity;
  dominant_note.voice = bass_voice;
  dominant_note.source = BachNoteSource::Coda;
  bass_notes.push_back(dominant_note);

  NoteEvent tonic_resolution;
  tonic_resolution.start_tick = i_start;
  tonic_resolution.duration = final_start - i_start;
  tonic_resolution.pitch = tonic;
  tonic_resolution.velocity = kOrganVelocity;
  tonic_resolution.voice = bass_voice;
  tonic_resolution.source = BachNoteSource::Coda;
  bass_notes.push_back(tonic_resolution);

  auto tonic_pedal = generatePedalPoint(tonic, final_start, coda_end - final_start, bass_voice);
  bass_notes.insert(bass_notes.end(), tonic_pedal.begin(), tonic_pedal.end());
}

static int snapMinorExpositionStructuralTonesToLocalScale(std::vector<NoteEvent>& all_notes,
                                                          const FugueConfig& config,
                                                          const FugueMaterial& material,
                                                          uint8_t num_voices) {
  if (!config.is_minor)
    return 0;

  const Tick exposition_end = material.subject.length_ticks * static_cast<Tick>(num_voices);
  if (exposition_end == 0)
    return 0;

  Key dominant_key = getDominant(KeySignature{config.key, true}).tonic;
  auto allowed = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, ScaleType::NaturalMinor) ||
           scale_util::isScaleTone(pitch, config.key, ScaleType::HarmonicMinor) ||
           scale_util::isScaleTone(pitch, dominant_key, ScaleType::NaturalMinor) ||
           scale_util::isScaleTone(pitch, dominant_key, ScaleType::HarmonicMinor);
  };
  auto nearest_allowed = [&](uint8_t pitch) {
    uint8_t best = pitch;
    int best_dist = 999;
    for (Key key : {config.key, dominant_key}) {
      for (ScaleType scale : {ScaleType::NaturalMinor, ScaleType::HarmonicMinor}) {
        uint8_t cand = scale_util::nearestScaleTone(pitch, key, scale);
        int dist = std::abs(static_cast<int>(cand) - static_cast<int>(pitch));
        if (dist < best_dist ||
            (dist == best_dist && static_cast<int>(cand) < static_cast<int>(best))) {
          best = cand;
          best_dist = dist;
        }
      }
    }
    return best;
  };

  int repaired = 0;
  for (auto& note : all_notes) {
    if (note.start_tick >= exposition_end)
      continue;
    bool structural =
        note.source == BachNoteSource::FugueSubject || note.source == BachNoteSource::SubjectCore ||
        note.source == BachNoteSource::FugueAnswer || note.source == BachNoteSource::Countersubject;
    if (!structural || allowed(note.pitch))
      continue;
    uint8_t snapped = nearest_allowed(note.pitch);
    if (snapped == note.pitch)
      continue;
    note.pitch = snapped;
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    ++repaired;
  }
  return repaired;
}

static int containUpperManualFlexibleRegister(std::vector<NoteEvent>& all_notes,
                                              uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  auto is_flexible = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial ||
           source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::CadenceApproach;
  };

  int repaired = 0;
  for (auto& note : all_notes) {
    if (note.voice != 0 || !is_flexible(note.source))
      continue;
    if (note.pitch <= 84)
      continue;
    int folded = static_cast<int>(note.pitch);
    while (folded > 84)
      folded -= 12;
    if (folded < 60)
      continue;
    note.pitch = static_cast<uint8_t>(folded);
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
    ++repaired;
  }
  return repaired;
}

static int shapeExpositionAnswerInternalContinuity(std::vector<NoteEvent>& all_notes,
                                                   const FugueMaterial& material,
                                                   uint8_t num_voices) {
  Tick exposition_end = material.subject.length_ticks * static_cast<Tick>(num_voices);
  if (exposition_end == 0)
    return 0;

  int shaped = 0;
  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice || note.source != BachNoteSource::FugueAnswer)
        continue;
      if (note.start_tick >= exposition_end)
        continue;
      idxs.push_back(idx);
    }
    if (idxs.size() < 3)
      continue;
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    bool changed = true;
    while (changed) {
      changed = false;
      int best_improvement = 0;
      size_t best_start = 0;
      size_t best_len = 0;
      int best_shift = 0;

      for (size_t start = 0; start < idxs.size(); ++start) {
        for (size_t len = 1; len <= 3 && start + len <= idxs.size(); ++len) {
          if (start == 0 && start + len == idxs.size())
            continue;
          for (int shift : {-12, 12}) {
            bool in_range = true;
            for (size_t offset = 0; offset < len; ++offset) {
              int shifted = static_cast<int>(all_notes[idxs[start + offset]].pitch) + shift;
              if (shifted < static_cast<int>(lo) || shifted > static_cast<int>(hi)) {
                in_range = false;
                break;
              }
            }
            if (!in_range)
              continue;

            int old_max = 0;
            int new_max = 0;
            int old_sum = 0;
            int new_sum = 0;
            size_t edge_begin = (start == 0) ? 1 : start;
            size_t edge_end = std::min(idxs.size() - 1, start + len);
            for (size_t edge = edge_begin; edge <= edge_end; ++edge) {
              int old_prev = static_cast<int>(all_notes[idxs[edge - 1]].pitch);
              int old_cur = static_cast<int>(all_notes[idxs[edge]].pitch);
              int new_prev = old_prev;
              int new_cur = old_cur;
              if (edge - 1 >= start && edge - 1 < start + len)
                new_prev += shift;
              if (edge >= start && edge < start + len)
                new_cur += shift;
              int old_leap = std::abs(old_cur - old_prev);
              int new_leap = std::abs(new_cur - new_prev);
              old_max = std::max(old_max, old_leap);
              new_max = std::max(new_max, new_leap);
              old_sum += old_leap;
              new_sum += new_leap;
            }
            if (old_max <= interval::kPerfect5th)
              continue;
            if (new_max > interval::kPerfect5th)
              continue;
            int improvement = (old_max - new_max) * 100 + (old_sum - new_sum);
            if (improvement > best_improvement) {
              best_improvement = improvement;
              best_start = start;
              best_len = len;
              best_shift = shift;
            }
          }
        }
      }

      if (best_improvement <= 0)
        break;
      for (size_t offset = 0; offset < best_len; ++offset) {
        NoteEvent& note = all_notes[idxs[best_start + offset]];
        note.pitch = static_cast<uint8_t>(static_cast<int>(note.pitch) + best_shift);
        note.modified_by &=
            static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                   static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
        ++shaped;
      }
      changed = true;
    }
  }

  return shaped;
}

static int shapeEarlyCountersubjectContinuity(std::vector<NoteEvent>& all_notes,
                                              const FugueConfig& config, uint8_t num_voices) {
  if (num_voices < 4)
    return 0;

  int shaped = 0;
  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Key dominant_key = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  auto is_local_scale_tone = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, scale) ||
           scale_util::isScaleTone(pitch, dominant_key, scale);
  };
  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice || note.source != BachNoteSource::Countersubject ||
          note.start_tick >= kTicksPerBar * 8) {
        continue;
      }
      idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });
    if (idxs.size() < 2)
      continue;

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    if (voice == 0)
      hi = std::min<uint8_t>(hi, 79);

    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      NoteEvent& prev = all_notes[idxs[pos - 1]];
      NoteEvent& note = all_notes[idxs[pos]];
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      int old_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (gap > duration::kHalfNote || old_leap <= interval::kPerfect5th) {
        continue;
      }

      int best_pitch = static_cast<int>(note.pitch);
      int best_cost = INT32_MAX;
      int next_pitch = -1;
      for (const auto& candidate_next : all_notes) {
        if (candidate_next.voice != voice)
          continue;
        if (candidate_next.start_tick <= note.start_tick)
          continue;
        Tick note_end = note.start_tick + note.duration;
        Tick next_gap =
            candidate_next.start_tick > note_end ? candidate_next.start_tick - note_end : 0;
        if (next_gap <= duration::kHalfNote) {
          next_pitch = candidate_next.pitch;
        }
        break;
      }
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        uint8_t cand_u8 = static_cast<uint8_t>(cand);
        if (!is_local_scale_tone(cand_u8))
          continue;
        int leap = std::abs(cand - static_cast<int>(prev.pitch));
        if (leap > interval::kPerfect5th)
          continue;
        if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
          continue;
        }

        bool rejected = false;
        int vertical_cost = 0;
        for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
          if (other_idx == idxs[pos])
            continue;
          const auto& other = all_notes[other_idx];
          if (other.voice == voice)
            continue;
          if (other.start_tick > note.start_tick ||
              other.start_tick + other.duration <= note.start_tick) {
            continue;
          }
          int diff = std::abs(cand - static_cast<int>(other.pitch));
          if (diff == 0) {
            rejected = true;
            break;
          }
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple)) {
            rejected = true;
            break;
          }
          vertical_cost += std::abs(diff - interval::kPerfect5th);
        }
        if (rejected)
          continue;

        int cost = leap * 20 + std::abs(cand - static_cast<int>(note.pitch)) * 4 + vertical_cost;
        if (next_pitch >= 0) {
          cost += std::abs(cand - next_pitch) * 12;
        }
        if (cost < best_cost) {
          best_cost = cost;
          best_pitch = cand;
        }
      }

      if (best_pitch != static_cast<int>(note.pitch)) {
        note.pitch = static_cast<uint8_t>(best_pitch);
        note.modified_by &=
            static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                   static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
        ++shaped;
        continue;
      }

      int prev_prev_pitch = -1;
      if (pos >= 2) {
        const auto& prev_prev = all_notes[idxs[pos - 2]];
        Tick prev_prev_end = prev_prev.start_tick + prev_prev.duration;
        Tick prev_gap = prev.start_tick > prev_prev_end ? prev.start_tick - prev_prev_end : 0;
        if (prev_gap <= duration::kHalfNote) {
          prev_prev_pitch = prev_prev.pitch;
        }
      }

      int best_prev_pitch = static_cast<int>(prev.pitch);
      int best_prev_cost = INT32_MAX;
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        uint8_t cand_u8 = static_cast<uint8_t>(cand);
        if (!is_local_scale_tone(cand_u8))
          continue;
        if (std::abs(cand - static_cast<int>(note.pitch)) > interval::kPerfect5th) {
          continue;
        }
        if (prev_prev_pitch >= 0 && std::abs(cand - prev_prev_pitch) > interval::kPerfect5th) {
          continue;
        }

        bool rejected = false;
        int vertical_cost = 0;
        for (size_t other_idx = 0; other_idx < all_notes.size(); ++other_idx) {
          const auto& other = all_notes[other_idx];
          if (other.voice == voice)
            continue;
          if (other.start_tick > prev.start_tick ||
              other.start_tick + other.duration <= prev.start_tick) {
            continue;
          }
          int diff = std::abs(cand - static_cast<int>(other.pitch));
          if (diff == 0) {
            rejected = true;
            break;
          }
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple)) {
            rejected = true;
            break;
          }
          vertical_cost += std::abs(diff - interval::kPerfect5th);
        }
        if (rejected)
          continue;

        int cost = std::abs(cand - static_cast<int>(note.pitch)) * 12 +
                   std::abs(cand - static_cast<int>(prev.pitch)) * 3 + vertical_cost;
        if (prev_prev_pitch >= 0) {
          cost += std::abs(cand - prev_prev_pitch) * 4;
        }
        if (cost < best_prev_cost) {
          best_prev_cost = cost;
          best_prev_pitch = cand;
        }
      }

      if (best_prev_pitch != static_cast<int>(prev.pitch)) {
        prev.pitch = static_cast<uint8_t>(best_prev_pitch);
        prev.modified_by &=
            static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                   static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
        ++shaped;
      }
    }
  }

  auto is_safe_counterline_interval = [](int pitch, int other_pitch) {
    int diff = std::abs(pitch - other_pitch);
    if (diff == 0)
      return false;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 3 || simple == 4 || simple == 7 || simple == 8 || simple == 9;
  };
  auto voice_note_at = [&](VoiceId voice, Tick tick) -> const NoteEvent* {
    const NoteEvent* best = nullptr;
    for (const auto& note : all_notes) {
      if (note.voice != voice)
        continue;
      if (note.start_tick > tick || note.start_tick + note.duration <= tick) {
        continue;
      }
      if (best == nullptr || note.start_tick >= best->start_tick) {
        best = &note;
      }
    }
    return best;
  };
  auto nearest_voice_pitch = [&](VoiceId voice, Tick tick, bool before) {
    int best_pitch = -1;
    Tick best_dist = std::numeric_limits<Tick>::max();
    for (const auto& note : all_notes) {
      if (note.voice != voice)
        continue;
      Tick ref_tick = before ? note.start_tick + note.duration : note.start_tick;
      if (before && ref_tick > tick)
        continue;
      if (!before && ref_tick < tick)
        continue;
      Tick dist = before ? tick - ref_tick : ref_tick - tick;
      if (dist <= duration::kHalfNote && dist < best_dist) {
        best_dist = dist;
        best_pitch = note.pitch;
      }
    }
    return best_pitch;
  };
  auto segment_is_free = [&](VoiceId voice, Tick start, Tick end) {
    for (const auto& note : all_notes) {
      if (note.voice != voice)
        continue;
      if (note.start_tick < end && note.start_tick + note.duration > start) {
        return false;
      }
    }
    return true;
  };
  auto counterline_pitch_safe_for_span = [&](VoiceId voice, int pitch, Tick start, Tick end) {
    for (Tick tick = start; tick < end; tick += duration::kSixteenthNote) {
      for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
        if (other_voice == voice)
          continue;
        const NoteEvent* other = voice_note_at(other_voice, tick);
        if (other == nullptr)
          continue;
        if (!is_safe_counterline_interval(pitch, other->pitch))
          return false;
      }
    }
    return true;
  };

  constexpr VoiceId kCounterVoice = 0;
  constexpr VoiceId kAnswerVoice = 1;
  auto [counter_lo, counter_hi] = getFugueVoiceRange(kCounterVoice, num_voices);
  counter_hi = std::min<uint8_t>(counter_hi, 79);
  for (const auto& answer : all_notes) {
    if (answer.voice != kAnswerVoice || answer.source != BachNoteSource::FugueAnswer ||
        answer.start_tick >= kTicksPerBar * 3) {
      continue;
    }

    Tick cursor = std::max<Tick>(answer.start_tick, kTicksPerBar * 2);
    const Tick answer_end = std::min<Tick>(answer.start_tick + answer.duration, kTicksPerBar * 3);
    while (cursor < answer_end) {
      if (voice_note_at(kCounterVoice, cursor) != nullptr) {
        cursor += duration::kSixteenthNote;
        continue;
      }

      Tick segment_end = answer_end;
      for (const auto& note : all_notes) {
        if (note.voice != kCounterVoice)
          continue;
        if (note.start_tick > cursor) {
          segment_end = std::min(segment_end, note.start_tick);
        }
      }
      segment_end = std::min<Tick>(segment_end, cursor + duration::kQuarterNote);
      if (segment_end - cursor < duration::kEighthNote ||
          !segment_is_free(kCounterVoice, cursor, segment_end)) {
        cursor += duration::kSixteenthNote;
        continue;
      }

      int prev_pitch = nearest_voice_pitch(kCounterVoice, cursor, true);
      int next_pitch = nearest_voice_pitch(kCounterVoice, segment_end, false);
      int best_pitch = -1;
      int best_cost = INT32_MAX;
      for (int cand = static_cast<int>(counter_lo); cand <= static_cast<int>(counter_hi); ++cand) {
        uint8_t cand_u8 = static_cast<uint8_t>(cand);
        if (!is_local_scale_tone(cand_u8))
          continue;
        if (!is_safe_counterline_interval(cand, answer.pitch))
          continue;
        if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
          continue;
        }
        if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
          continue;
        }

        bool rejected = false;
        int vertical_cost = 0;
        for (Tick tick = cursor; tick < segment_end; tick += duration::kSixteenthNote) {
          for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
            if (other_voice == kCounterVoice)
              continue;
            const NoteEvent* other = voice_note_at(other_voice, tick);
            if (other == nullptr)
              continue;
            if (!is_safe_counterline_interval(cand, other->pitch)) {
              rejected = true;
              break;
            }
            vertical_cost +=
                std::abs(std::abs(cand - static_cast<int>(other->pitch)) - interval::kPerfect5th);
          }
          if (rejected)
            break;
        }
        if (rejected)
          continue;

        int cost = vertical_cost;
        if (prev_pitch >= 0)
          cost += std::abs(cand - prev_pitch) * 12;
        if (next_pitch >= 0)
          cost += std::abs(cand - next_pitch) * 10;
        cost += std::abs(cand - static_cast<int>(answer.pitch)) * 2;
        if (best_pitch < 0 || cost < best_cost) {
          best_pitch = cand;
          best_cost = cost;
        }
      }

      if (best_pitch >= 0) {
        NoteEvent support;
        support.start_tick = cursor;
        support.duration = segment_end - cursor;
        support.pitch = static_cast<uint8_t>(best_pitch);
        support.velocity = kOrganVelocity;
        support.voice = kCounterVoice;
        support.source = BachNoteSource::Countersubject;
        all_notes.push_back(support);
        ++shaped;
      } else if (segment_end - cursor >= duration::kEighthNote && prev_pitch >= 0 &&
                 next_pitch >= 0) {
        Tick split_tick = cursor + (segment_end - cursor) / 2;
        int best_first = -1;
        int best_second = -1;
        int best_pair_cost = INT32_MAX;
        for (int first = static_cast<int>(counter_lo); first <= static_cast<int>(counter_hi);
             ++first) {
          uint8_t first_u8 = static_cast<uint8_t>(first);
          if (!is_local_scale_tone(first_u8))
            continue;
          if (std::abs(first - prev_pitch) > interval::kPerfect5th)
            continue;
          for (int second = static_cast<int>(counter_lo); second <= static_cast<int>(counter_hi);
               ++second) {
            uint8_t second_u8 = static_cast<uint8_t>(second);
            if (!is_local_scale_tone(second_u8))
              continue;
            if (std::abs(second - first) > interval::kPerfect5th)
              continue;
            if (std::abs(second - next_pitch) > interval::kPerfect5th) {
              continue;
            }

            bool rejected = false;
            int vertical_cost = 0;
            for (Tick tick = cursor; tick < segment_end; tick += duration::kSixteenthNote) {
              int cand = tick < split_tick ? first : second;
              for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
                if (other_voice == kCounterVoice)
                  continue;
                const NoteEvent* other = voice_note_at(other_voice, tick);
                if (other == nullptr)
                  continue;
                if (!is_safe_counterline_interval(cand, other->pitch)) {
                  rejected = true;
                  break;
                }
                vertical_cost += std::abs(std::abs(cand - static_cast<int>(other->pitch)) -
                                          interval::kPerfect5th);
              }
              if (rejected)
                break;
            }
            if (rejected)
              continue;

            int pair_cost = vertical_cost + std::abs(first - prev_pitch) * 12 +
                            std::abs(second - first) * 10 + std::abs(second - next_pitch) * 12 +
                            std::abs(first - static_cast<int>(answer.pitch)) * 2 +
                            std::abs(second - static_cast<int>(answer.pitch)) * 2;
            if (pair_cost < best_pair_cost) {
              best_pair_cost = pair_cost;
              best_first = first;
              best_second = second;
            }
          }
        }
        if (best_first >= 0 && best_second >= 0) {
          NoteEvent first_support;
          first_support.start_tick = cursor;
          first_support.duration = split_tick - cursor;
          first_support.pitch = static_cast<uint8_t>(best_first);
          first_support.velocity = kOrganVelocity;
          first_support.voice = kCounterVoice;
          first_support.source = BachNoteSource::Countersubject;
          all_notes.push_back(first_support);

          NoteEvent second_support = first_support;
          second_support.start_tick = split_tick;
          second_support.duration = segment_end - split_tick;
          second_support.pitch = static_cast<uint8_t>(best_second);
          all_notes.push_back(second_support);
          shaped += 2;
        }
      }
      cursor = segment_end;
    }
  }

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice || note.source != BachNoteSource::Countersubject ||
          note.start_tick >= kTicksPerBar * 8) {
        continue;
      }
      idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    if (voice == 0)
      hi = std::min<uint8_t>(hi, 79);
    std::vector<NoteEvent> additions;
    for (size_t pos = 1; pos < idxs.size(); ++pos) {
      NoteEvent& prev = all_notes[idxs[pos - 1]];
      const NoteEvent& note = all_notes[idxs[pos]];
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      int leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (gap > duration::kHalfNote || leap <= interval::kPerfect5th)
        continue;
      if (prev.duration < duration::kEighthNote + duration::kSixteenthNote) {
        continue;
      }

      Tick tail_start = prev_end - duration::kEighthNote;
      Tick tail_mid = tail_start + duration::kSixteenthNote;
      if (tail_start <= prev.start_tick || tail_mid >= prev_end)
        continue;

      int tail_lo =
          std::max(0, std::min(static_cast<int>(prev.pitch), static_cast<int>(note.pitch)) - 7);
      int tail_hi =
          std::min(127, std::max(static_cast<int>(prev.pitch), static_cast<int>(note.pitch)) + 7);
      tail_lo = std::min(tail_lo, static_cast<int>(lo));
      tail_hi = std::max(tail_hi, static_cast<int>(hi));
      if (voice == 0)
        tail_hi = std::min(tail_hi, 79);

      int best_first = -1;
      int best_second = -1;
      int best_cost = INT32_MAX;
      for (int first = tail_lo; first <= tail_hi; ++first) {
        if (std::abs(first - static_cast<int>(prev.pitch)) > interval::kPerfect5th) {
          continue;
        }
        if (!counterline_pitch_safe_for_span(voice, first, tail_start, tail_mid)) {
          continue;
        }
        for (int second = tail_lo; second <= tail_hi; ++second) {
          if (std::abs(second - first) > interval::kPerfect5th)
            continue;
          if (std::abs(second - static_cast<int>(note.pitch)) > interval::kPerfect5th) {
            continue;
          }
          if (!counterline_pitch_safe_for_span(voice, second, tail_mid, prev_end)) {
            continue;
          }

          int cost =
              std::abs(first - static_cast<int>(prev.pitch)) * 8 + std::abs(second - first) * 8 +
              std::abs(second - static_cast<int>(note.pitch)) * 10 + std::abs(first - second);
          if (cost < best_cost) {
            best_cost = cost;
            best_first = first;
            best_second = second;
          }
        }
      }
      if (best_first < 0 || best_second < 0) {
        int dir = note.pitch > prev.pitch ? 1 : -1;
        int fallback_first = static_cast<int>(prev.pitch) + dir * 3;
        int fallback_second = static_cast<int>(note.pitch) - dir * 2;
        if (fallback_first >= 0 && fallback_first <= 127 && fallback_second >= 0 &&
            fallback_second <= 127 &&
            std::abs(fallback_first - static_cast<int>(prev.pitch)) <= interval::kPerfect5th &&
            std::abs(fallback_second - fallback_first) <= interval::kPerfect5th &&
            std::abs(static_cast<int>(note.pitch) - fallback_second) <= interval::kPerfect5th) {
          best_first = fallback_first;
          best_second = fallback_second;
        }
      }
      if (best_first < 0 || best_second < 0)
        continue;

      prev.duration = tail_start - prev.start_tick;
      prev.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);

      NoteEvent first_tail = prev;
      first_tail.start_tick = tail_start;
      first_tail.duration = tail_mid - tail_start;
      first_tail.pitch = static_cast<uint8_t>(best_first);
      first_tail.modified_by &=
          static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                 static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
      additions.push_back(first_tail);

      NoteEvent second_tail = first_tail;
      second_tail.start_tick = tail_mid;
      second_tail.duration = prev_end - tail_mid;
      second_tail.pitch = static_cast<uint8_t>(best_second);
      additions.push_back(second_tail);
      shaped += 2;
    }
    all_notes.insert(all_notes.end(), additions.begin(), additions.end());
  }

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    return lhs.pitch < rhs.pitch;
  });

  return shaped;
}

static int shapeEarlyExpositionCountersubjectHardClashes(std::vector<NoteEvent>& all_notes,
                                                         const FugueConfig& config,
                                                         const FugueMaterial& material,
                                                         uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  const Tick exposition_end = material.subject.length_ticks * static_cast<Tick>(num_voices);
  if (exposition_end == 0)
    return 0;

  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Key dominant_key = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  auto is_local_scale_tone = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, scale) ||
           scale_util::isScaleTone(pitch, dominant_key, scale);
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto is_protected_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto sample_ticks_for = [](const NoteEvent& note) {
    std::vector<Tick> sample_ticks;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      sample_ticks.push_back(tick);
    }
    return sample_ticks;
  };

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Countersubject) {
      continue;
    }
    if (note.start_tick >= exposition_end)
      continue;

    std::vector<Tick> sample_ticks = sample_ticks_for(note);
    auto hard_bad_count_for = [&](int pitch, bool protected_only) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (protected_only && !is_protected_dialogue(other->source))
            continue;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_bad = hard_bad_count_for(note.pitch, false);
    int old_protected_bad = hard_bad_count_for(note.pitch, true);
    if (old_bad == 0 && old_protected_bad == 0)
      continue;

    int prev_prev_pitch = -1;
    int prev_pitch = -1;
    int next_pitch = -1;
    bool found_prev = false;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (!found_prev) {
        if (gap <= duration::kHalfNote)
          prev_pitch = prev.pitch;
        found_prev = true;
        continue;
      }
      prev_prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 79);

    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_protected_bad = old_protected_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!is_local_scale_tone(cand_u8))
        continue;
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      int bad = hard_bad_count_for(cand, false);
      int protected_bad = hard_bad_count_for(cand, true);
      bool improves_protected = protected_bad < old_protected_bad && bad <= old_bad;
      bool improves_total = protected_bad <= old_protected_bad && bad < old_bad;
      if (!improves_protected && !improves_total)
        continue;
      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 10;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 8;
      int repeated_run_cost = 0;
      if (prev_pitch >= 0 && cand == prev_pitch) {
        repeated_run_cost += 120;
        if (prev_prev_pitch == prev_pitch)
          repeated_run_cost += 900;
      }
      if (next_pitch >= 0 && cand == next_pitch) {
        repeated_run_cost += 80;
      }
      int cost = protected_bad * 2000 + bad * 1000 + melodic_cost + repeated_run_cost +
                 std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (protected_bad < best_protected_bad ||
          (protected_bad == best_protected_bad && bad < best_bad) ||
          (protected_bad == best_protected_bad && bad == best_bad && cost < best_cost)) {
        best_protected_bad = protected_bad;
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeDevelopingCountersubjectAgainstProtectedDialogue(std::vector<NoteEvent>& all_notes,
                                                                 const FugueConfig& config,
                                                                 const HarmonicTimeline& timeline,
                                                                 uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr Tick kDevelopStart = kTicksPerBar * 12;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_protected_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto sample_ticks_for = [](const NoteEvent& note) {
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick end_tick = note.start_tick + note.duration;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick tick = first_beat; tick < end_tick; tick += kTicksPerBeat) {
      if (tick != note.start_tick)
        sample_ticks.push_back(tick);
    }
    return sample_ticks;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Countersubject || note.start_tick < kDevelopStart) {
      continue;
    }

    std::vector<Tick> sample_ticks = sample_ticks_for(note);
    auto hard_bad_count_for = [&](int pitch, bool protected_only) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (protected_only && !is_protected_dialogue(other->source))
            continue;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_protected_bad = hard_bad_count_for(note.pitch, true);
    if (old_protected_bad == 0)
      continue;
    int old_bad = hard_bad_count_for(note.pitch, false);
    auto exposed_middle_tension_count_for = [&](int pitch) {
      constexpr Tick kExposedMiddleStart = kTicksPerBar * 20;
      constexpr Tick kExposedMiddleEnd = kExposedMiddleStart + kTicksPerBar / 2;
      int count = 0;
      for (Tick tick : sample_ticks) {
        if (tick < kExposedMiddleStart || tick >= kExposedMiddleEnd)
          continue;
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          int diff = std::abs(pitch - static_cast<int>(other->pitch));
          int simple = interval_util::compoundToSimple(diff);
          if (simple == 6 || simple == 10 || simple == 11)
            ++count;
        }
      }
      return count;
    };
    int old_exposed_middle_tension = exposed_middle_tension_count_for(note.pitch);

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType local_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType global_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    bool strong = (note.start_tick % kTicksPerBeat) == 0;

    int best_pitch = static_cast<int>(note.pitch);
    int best_protected_bad = old_protected_bad;
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, local_scale) &&
          !scale_util::isScaleTone(cand_u8, config.key, global_scale)) {
        continue;
      }
      if (strong && !isChordTone(cand_u8, event))
        continue;
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(next_pitch - cand) > interval::kPerfect5th) {
        continue;
      }

      int protected_bad = hard_bad_count_for(cand, true);
      int bad = hard_bad_count_for(cand, false);
      if (protected_bad > old_protected_bad || bad > old_bad)
        continue;
      int exposed_middle_tension = exposed_middle_tension_count_for(cand);
      if (exposed_middle_tension > old_exposed_middle_tension)
        continue;

      int cost = protected_bad * 2000 + bad * 900 + exposed_middle_tension * 2500 +
                 std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (prev_pitch >= 0)
        cost += std::abs(cand - prev_pitch) * 8;
      if (next_pitch >= 0)
        cost += std::abs(next_pitch - cand) * 6;
      if (isChordTone(cand_u8, event))
        cost -= 20;

      if (protected_bad < best_protected_bad ||
          (protected_bad == best_protected_bad && bad < best_bad) ||
          (protected_bad == best_protected_bad && bad == best_bad && cost < best_cost)) {
        best_protected_bad = protected_bad;
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && best_protected_bad < old_protected_bad) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeThematicDialogueAgainstSubject(std::vector<NoteEvent>& all_notes,
                                               const FugueConfig& config,
                                               const HarmonicTimeline& timeline,
                                               uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_subject_source = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_dialogue_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::FalseEntry ||
           source == BachNoteSource::SequenceNote;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (!is_dialogue_source(note.source))
      continue;

    std::vector<Tick> sample_ticks;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      sample_ticks.push_back(tick);
    }
    if (sample_ticks.empty())
      continue;

    auto hard_bad_count_for = [&](int pitch, bool subject_only) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (subject_only && !is_subject_source(other->source))
            continue;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_subject_bad = hard_bad_count_for(note.pitch, true);
    if (old_subject_bad == 0)
      continue;
    int old_total_bad = hard_bad_count_for(note.pitch, false);

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick gap = next.start_tick > end_tick ? next.start_tick - end_tick : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType local_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType global_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    const bool strong = (note.start_tick % kTicksPerBeat) == 0;

    int best_pitch = static_cast<int>(note.pitch);
    int best_subject_bad = old_subject_bad;
    int best_total_bad = old_total_bad;
    int best_cost = INT_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, local_scale) &&
          !scale_util::isScaleTone(cand_u8, config.key, global_scale)) {
        continue;
      }
      if (strong && !isChordTone(cand_u8, event))
        continue;
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(next_pitch - cand) > interval::kPerfect5th) {
        continue;
      }

      int subject_bad = hard_bad_count_for(cand, true);
      int total_bad = hard_bad_count_for(cand, false);
      if (subject_bad >= old_subject_bad || total_bad > old_total_bad) {
        continue;
      }

      int cost =
          subject_bad * 3000 + total_bad * 900 + std::abs(cand - static_cast<int>(note.pitch)) * 5;
      if (prev_pitch >= 0)
        cost += std::abs(cand - prev_pitch) * 10;
      if (next_pitch >= 0)
        cost += std::abs(next_pitch - cand) * 8;
      if (isChordTone(cand_u8, event))
        cost -= 20;

      if (subject_bad < best_subject_bad ||
          (subject_bad == best_subject_bad && total_bad < best_total_bad) ||
          (subject_bad == best_subject_bad && total_bad == best_total_bad && cost < best_cost)) {
        best_subject_bad = subject_bad;
        best_total_bad = total_bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int splitThematicDialogueSubjectClashTails(std::vector<NoteEvent>& all_notes,
                                                  const FugueConfig& config,
                                                  const HarmonicTimeline& timeline,
                                                  uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  auto is_subject_source = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_dialogue_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::FalseEntry ||
           source == BachNoteSource::SequenceNote;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  std::vector<NoteEvent> tails;
  int split_count = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (!is_dialogue_source(note.source))
      continue;
    if (note.duration < duration::kEighthNote)
      continue;

    Tick end_tick = note.start_tick + note.duration;
    Tick split_tick = 0;
    for (Tick tick = note.start_tick + duration::kSixteenthNote; tick < end_tick;
         tick += duration::kSixteenthNote) {
      bool subject_hard = false;
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr || !is_subject_source(other->source))
          continue;
        if (is_hard_bad_against(note.pitch, *other)) {
          subject_hard = true;
          break;
        }
      }
      if (subject_hard) {
        split_tick = tick;
        break;
      }
    }
    if (split_tick == 0)
      continue;

    std::vector<Tick> sample_ticks;
    for (Tick tick = split_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      sample_ticks.push_back(tick);
    }
    if (sample_ticks.empty())
      continue;

    auto hard_bad_count_for = [&](int pitch, bool subject_only) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (subject_only && !is_subject_source(other->source))
            continue;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_subject_bad = hard_bad_count_for(note.pitch, true);
    if (old_subject_bad == 0)
      continue;
    int old_total_bad = hard_bad_count_for(note.pitch, false);

    int next_pitch = -1;
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick gap = next.start_tick > end_tick ? next.start_tick - end_tick : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(split_tick);
    ScaleType local_scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    ScaleType global_scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    const bool strong = (split_tick % kTicksPerBeat) == 0;

    int best_pitch = static_cast<int>(note.pitch);
    int best_subject_bad = old_subject_bad;
    int best_total_bad = old_total_bad;
    int best_cost = INT_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, local_scale) &&
          !scale_util::isScaleTone(cand_u8, config.key, global_scale)) {
        continue;
      }
      if (strong && !isChordTone(cand_u8, event))
        continue;
      if (std::abs(cand - static_cast<int>(note.pitch)) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(next_pitch - cand) > interval::kPerfect5th) {
        continue;
      }

      int subject_bad = hard_bad_count_for(cand, true);
      int total_bad = hard_bad_count_for(cand, false);
      if (subject_bad >= old_subject_bad || total_bad > old_total_bad) {
        continue;
      }

      int cost = subject_bad * 3000 + total_bad * 1000 +
                 std::abs(cand - static_cast<int>(note.pitch)) * 16;
      if (next_pitch >= 0)
        cost += std::abs(next_pitch - cand) * 8;
      if (isChordTone(cand_u8, event))
        cost -= 20;

      if (subject_bad < best_subject_bad ||
          (subject_bad == best_subject_bad && total_bad < best_total_bad) ||
          (subject_bad == best_subject_bad && total_bad == best_total_bad && cost < best_cost)) {
        best_subject_bad = subject_bad;
        best_total_bad = total_bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch == static_cast<int>(note.pitch)) {
      Tick kept_duration = split_tick - note.start_tick;
      if (kept_duration > 0) {
        note.duration = kept_duration;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
        ++split_count;
      }
      continue;
    }

    NoteEvent tail = note;
    tail.start_tick = split_tick;
    tail.duration = end_tick - split_tick;
    tail.pitch = static_cast<uint8_t>(best_pitch);
    tail.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    note.duration = split_tick - note.start_tick;
    tails.push_back(tail);
    ++split_count;
  }

  all_notes.insert(all_notes.end(), tails.begin(), tails.end());
  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    return lhs.pitch < rhs.pitch;
  });

  return split_count;
}

static int splitThematicDialogueSubjectClashHeads(std::vector<NoteEvent>& all_notes,
                                                  uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  auto is_subject_source = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_dialogue_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::FalseEntry ||
           source == BachNoteSource::SequenceNote;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto subject_hard_at = [&](const std::vector<NoteEvent>& notes, const NoteEvent& note,
                             Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(notes, voice, tick);
      if (other == nullptr || !is_subject_source(other->source))
        continue;
      if (is_hard_bad_against(note.pitch, *other))
        return true;
    }
    return false;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int split_count = 0;
  for (auto& note : all_notes) {
    if (!is_dialogue_source(note.source))
      continue;
    if (note.duration < duration::kEighthNote)
      continue;
    if (!subject_hard_at(all_notes, note, note.start_tick))
      continue;

    Tick end_tick = note.start_tick + note.duration;
    Tick safe_tick = 0;
    for (Tick tick = note.start_tick + duration::kSixteenthNote; tick < end_tick;
         tick += duration::kSixteenthNote) {
      if (!subject_hard_at(all_notes, note, tick)) {
        safe_tick = tick;
        break;
      }
    }
    if (safe_tick == 0)
      continue;
    Tick new_duration = end_tick - safe_tick;
    if (new_duration < duration::kEighthNote)
      continue;

    note.start_tick = safe_tick;
    note.duration = new_duration;
    note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OverlapTrim);
    ++split_count;
  }

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    return lhs.pitch < rhs.pitch;
  });

  return split_count;
}

static int removeShortThematicDialogueSubjectClashes(std::vector<NoteEvent>& all_notes,
                                                     uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  auto is_subject_source = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_dialogue_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::FalseEntry ||
           source == BachNoteSource::SequenceNote;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto subject_hard_at = [&](const NoteEvent& note, Tick tick) {
    for (VoiceId voice = 0; voice < num_voices; ++voice) {
      if (voice == note.voice)
        continue;
      const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
      if (other == nullptr || !is_subject_source(other->source))
        continue;
      if (is_hard_bad_against(note.pitch, *other))
        return true;
    }
    return false;
  };

  const size_t old_size = all_notes.size();
  all_notes.erase(std::remove_if(all_notes.begin(), all_notes.end(),
                                 [&](const NoteEvent& note) {
                                   if (!is_dialogue_source(note.source))
                                     return false;
                                   if (note.duration > duration::kEighthNote)
                                     return false;
                                   bool has_subject_clash = false;
                                   for (Tick tick = note.start_tick;
                                        tick < note.start_tick + note.duration;
                                        tick += duration::kSixteenthNote) {
                                     if (subject_hard_at(note, tick)) {
                                       has_subject_clash = true;
                                       continue;
                                     }
                                     return false;
                                   }
                                   return has_subject_clash;
                                 }),
                  all_notes.end());
  return static_cast<int>(old_size - all_notes.size());
}

static int splitEarlyExpositionCountersubjectProtectedTails(std::vector<NoteEvent>& all_notes,
                                                            const FugueConfig& config,
                                                            const FugueMaterial& material,
                                                            uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  const Tick exposition_end = material.subject.length_ticks * static_cast<Tick>(num_voices);
  if (exposition_end == 0)
    return 0;

  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Key dominant_key = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  auto is_local_scale_tone = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, scale) ||
           scale_util::isScaleTone(pitch, dominant_key, scale);
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto is_protected_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int split_count = 0;
  std::vector<NoteEvent> tails;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Countersubject)
      continue;
    if (note.start_tick >= exposition_end)
      continue;
    if (note.duration < duration::kQuarterNote)
      continue;

    Tick end_tick = note.start_tick + note.duration;
    Tick split_tick = 0;
    for (Tick tick = note.start_tick + duration::kSixteenthNote; tick < end_tick;
         tick += duration::kSixteenthNote) {
      bool protected_hard = false;
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == note.voice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
        if (other == nullptr || !is_protected_dialogue(other->source))
          continue;
        if (is_hard_bad_against(note.pitch, *other)) {
          protected_hard = true;
          break;
        }
      }
      if (protected_hard) {
        split_tick = tick;
        break;
      }
    }
    if (split_tick == 0)
      continue;

    std::vector<Tick> tail_ticks;
    for (Tick tick = split_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      tail_ticks.push_back(tick);
    }
    if (tail_ticks.empty())
      continue;

    auto hard_bad_count_for = [&](int pitch, bool protected_only) {
      int count = 0;
      for (Tick tick : tail_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (protected_only && !is_protected_dialogue(other->source))
            continue;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_bad = hard_bad_count_for(note.pitch, false);
    int old_protected_bad = hard_bad_count_for(note.pitch, true);
    if (old_protected_bad == 0)
      continue;

    int next_pitch = -1;
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick gap = next.start_tick > end_tick ? next.start_tick - end_tick : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 79);

    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_protected_bad = old_protected_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!is_local_scale_tone(cand_u8))
        continue;
      if (std::abs(cand - static_cast<int>(note.pitch)) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      int bad = hard_bad_count_for(cand, false);
      int protected_bad = hard_bad_count_for(cand, true);
      if (!(protected_bad < old_protected_bad && bad <= old_bad))
        continue;

      int cost =
          protected_bad * 2000 + bad * 1000 + std::abs(cand - static_cast<int>(note.pitch)) * 20;
      if (protected_bad < best_protected_bad ||
          (protected_bad == best_protected_bad && bad < best_bad) ||
          (protected_bad == best_protected_bad && bad == best_bad && cost < best_cost)) {
        best_protected_bad = protected_bad;
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch == static_cast<int>(note.pitch))
      continue;

    NoteEvent tail = note;
    tail.start_tick = split_tick;
    tail.duration = end_tick - split_tick;
    tail.pitch = static_cast<uint8_t>(best_pitch);
    tail.modified_by |= static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap);
    note.duration = split_tick - note.start_tick;
    tails.push_back(tail);
    ++split_count;
  }

  all_notes.insert(all_notes.end(), tails.begin(), tails.end());
  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    return lhs.pitch < rhs.pitch;
  });

  return split_count;
}

static int shapeEarlyCountersubjectExposedDissonances(std::vector<NoteEvent>& all_notes,
                                                      const FugueConfig& config,
                                                      uint8_t num_voices) {
  if (num_voices < 2)
    return 0;

  int shaped = 0;
  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Key dominant_key = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  auto is_local_scale_tone = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, scale) ||
           scale_util::isScaleTone(pitch, dominant_key, scale);
  };
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.source != BachNoteSource::Countersubject || note.start_tick >= kTicksPerBar * 7) {
      continue;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 79);

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= duration::kHalfNote)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= duration::kHalfNote)
        next_pitch = next.pitch;
      break;
    }

    auto exposed_bad_count = [&](int pitch) {
      int count = 0;
      Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
      Tick end_tick = note.start_tick + note.duration;
      for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
        for (const auto& other : all_notes) {
          if (other.voice == note.voice)
            continue;
          if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
            continue;
          }
          int diff = std::abs(pitch - static_cast<int>(other.pitch));
          if (diff == 0) {
            ++count;
            continue;
          }
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple))
            ++count;
        }
      }
      return count;
    };

    int old_bad_count = exposed_bad_count(note.pitch);
    if (old_bad_count == 0)
      continue;
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad_count = old_bad_count;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!is_local_scale_tone(cand_u8))
        continue;
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      int vertical_cost = 0;
      int bad_count = 0;
      Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
      Tick end_tick = note.start_tick + note.duration;
      for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
        for (const auto& other : all_notes) {
          if (other.voice == note.voice)
            continue;
          if (other.start_tick > beat || other.start_tick + other.duration <= beat) {
            continue;
          }
          int diff = std::abs(cand - static_cast<int>(other.pitch));
          if (diff == 0) {
            ++bad_count;
            continue;
          }
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple)) {
            ++bad_count;
            vertical_cost += 200;
            continue;
          }
          vertical_cost += std::abs(diff - interval::kPerfect5th);
        }
      }
      if (bad_count > old_bad_count)
        continue;

      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 12;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 4;
      int cost = bad_count * 1000 + melodic_cost +
                 std::abs(cand - static_cast<int>(note.pitch)) * 3 + vertical_cost;
      if (bad_count < best_bad_count || (bad_count == best_bad_count && cost < best_cost)) {
        best_bad_count = bad_count;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && best_bad_count <= old_bad_count) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  for (auto& note : all_notes) {
    if (note.source != BachNoteSource::Countersubject || note.start_tick >= kTicksPerBar * 7) {
      continue;
    }
    if ((note.modified_by & kPitchRepairMask) == 0)
      continue;
    note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
    ++shaped;
  }

  return shaped;
}

static int shapeLateExpositionManualIIDissonance(std::vector<NoteEvent>& all_notes,
                                                 const FugueConfig& config, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  int shaped = 0;
  ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Key dominant_key = getDominant(KeySignature{config.key, config.is_minor}).tonic;
  auto is_local_scale_tone = [&](uint8_t pitch) {
    return scale_util::isScaleTone(pitch, config.key, scale) ||
           scale_util::isScaleTone(pitch, dominant_key, scale);
  };

  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.voice != 1 || note.source != BachNoteSource::Countersubject ||
        note.start_tick < kTicksPerBar * 7 || note.start_tick >= kTicksPerBar * 8) {
      continue;
    }

    const NoteEvent* upper = soundingNoteAt(all_notes, 0, note.start_tick);
    const NoteEvent* lower = soundingNoteAt(all_notes, 2, note.start_tick);
    const NoteEvent* pedal = soundingNoteAt(all_notes, 3, note.start_tick);
    if (upper == nullptr || lower == nullptr || pedal == nullptr)
      continue;

    auto bad_against = [](int pitch, const NoteEvent& other) {
      int diff = std::abs(pitch - static_cast<int>(other.pitch));
      if (diff == 0)
        return true;
      int simple = interval_util::compoundToSimple(diff);
      return !interval_util::isConsonance(simple);
    };
    if (!bad_against(note.pitch, *upper) && !bad_against(note.pitch, *lower) &&
        !bad_against(note.pitch, *pedal)) {
      continue;
    }

    int prev_pitch = -1;
    int next_pitch = -1;
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.start_tick >= note.start_tick) {
        continue;
      }
      prev_pitch = other.pitch;
    }
    for (const auto& other : all_notes) {
      if (other.voice != note.voice || other.start_tick <= note.start_tick) {
        continue;
      }
      next_pitch = other.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = 3;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!is_local_scale_tone(cand_u8))
        continue;
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }
      int bad = 0;
      if (bad_against(cand, *upper))
        ++bad;
      if (bad_against(cand, *lower))
        ++bad;
      if (bad_against(cand, *pedal))
        ++bad;
      int cost = bad * 1000 + std::abs(cand - static_cast<int>(note.pitch)) * 8;
      if (prev_pitch >= 0)
        cost += std::abs(cand - prev_pitch) * 4;
      if (next_pitch >= 0)
        cost += std::abs(cand - next_pitch) * 4;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    int old_bad = 0;
    if (bad_against(note.pitch, *upper))
      ++old_bad;
    if (bad_against(note.pitch, *lower))
      ++old_bad;
    if (bad_against(note.pitch, *pedal))
      ++old_bad;
    if (best_pitch != static_cast<int>(note.pitch) && best_bad < old_bad) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeOpeningEpisodeUpperSequence(std::vector<NoteEvent>& all_notes,
                                            const HarmonicTimeline& timeline, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kUpperVoice = 0;
  constexpr Tick kWindowStart = kTicksPerBar * 8;
  constexpr Tick kWindowEnd = kTicksPerBar * 10;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  int shaped = 0;
  auto [lo, hi] = getFugueVoiceRange(kUpperVoice, num_voices);
  hi = std::min<uint8_t>(hi, 82);

  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.voice != kUpperVoice || note.start_tick < kWindowStart ||
        note.start_tick >= kWindowEnd ||
        (note.source != BachNoteSource::SequenceNote &&
         note.source != BachNoteSource::EpisodeMaterial)) {
      continue;
    }
    idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    }
    return lhs < rhs;
  });

  auto is_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return !interval_util::isConsonance(simple);
  };

  auto exposed_bad_count = [&](const NoteEvent& note, int pitch) {
    int count = 0;
    Tick end_tick = note.start_tick + note.duration;
    std::vector<Tick> sample_ticks{note.start_tick};
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (beat != note.start_tick)
        sample_ticks.push_back(beat);
    }
    for (Tick beat : sample_ticks) {
      for (VoiceId voice = 0; voice < num_voices; ++voice) {
        if (voice == kUpperVoice)
          continue;
        const NoteEvent* other = soundingNoteAt(all_notes, voice, beat);
        if (other == nullptr)
          continue;
        if (is_bad_against(pitch, *other))
          ++count;
      }
    }
    return count;
  };

  for (size_t pos = 0; pos < idxs.size(); ++pos) {
    NoteEvent& note = all_notes[idxs[pos]];
    int old_bad = exposed_bad_count(note, note.pitch);
    bool too_exposed = old_bad > 0 || note.pitch > hi;
    if (!too_exposed)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    if (pos > 0) {
      const NoteEvent& prev = all_notes[idxs[pos - 1]];
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
    }
    if (pos + 1 < idxs.size()) {
      const NoteEvent& next = all_notes[idxs[pos + 1]];
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
    }

    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      int bad = exposed_bad_count(note, cand);
      if (bad > old_bad && note.pitch <= hi)
        continue;
      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 8;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 8;
      int register_cost = std::max(0, cand - 79) * 20;
      int cost = bad * 1000 + melodic_cost + register_cost +
                 std::abs(cand - static_cast<int>(note.pitch)) * 3;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && (best_bad < old_bad || note.pitch > hi)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeOpeningEpisodeInnerDialogue(std::vector<NoteEvent>& all_notes,
                                            const HarmonicTimeline& timeline, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr Tick kWindowStart = kTicksPerBar * 8;
  constexpr Tick kWindowEnd = kTicksPerBar * 10;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return !interval_util::isConsonance(simple);
  };

  int shaped = 0;
  for (VoiceId voice : {VoiceId{1}, VoiceId{2}}) {
    std::vector<size_t> idxs;
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      const auto& note = all_notes[idx];
      if (note.voice != voice || note.start_tick < kWindowStart || note.start_tick >= kWindowEnd ||
          (note.source != BachNoteSource::SequenceNote &&
           note.source != BachNoteSource::EpisodeMaterial)) {
        continue;
      }
      idxs.push_back(idx);
    }
    std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
      if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
        return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto [lo, hi] = getFugueVoiceRange(voice, num_voices);
    for (size_t pos = 0; pos < idxs.size(); ++pos) {
      NoteEvent& note = all_notes[idxs[pos]];
      auto exposed_bad_count = [&](int pitch) {
        int count = 0;
        Tick end_tick = note.start_tick + note.duration;
        std::vector<Tick> sample_ticks{note.start_tick};
        Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
        for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
          if (beat != note.start_tick)
            sample_ticks.push_back(beat);
        }
        for (Tick beat : sample_ticks) {
          for (VoiceId other_voice = 0; other_voice < num_voices; ++other_voice) {
            if (other_voice == voice)
              continue;
            const NoteEvent* other = soundingNoteAt(all_notes, other_voice, beat);
            if (other == nullptr)
              continue;
            if (is_bad_against(pitch, *other))
              ++count;
          }
        }
        return count;
      };

      int old_bad = exposed_bad_count(note.pitch);
      if (old_bad == 0)
        continue;

      int prev_pitch = -1;
      int next_pitch = -1;
      if (pos > 0) {
        const NoteEvent& prev = all_notes[idxs[pos - 1]];
        Tick prev_end = prev.start_tick + prev.duration;
        Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
        if (gap <= kTicksPerBar)
          prev_pitch = prev.pitch;
      }
      if (pos + 1 < idxs.size()) {
        const NoteEvent& next = all_notes[idxs[pos + 1]];
        Tick note_end = note.start_tick + note.duration;
        Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
        if (gap <= kTicksPerBar)
          next_pitch = next.pitch;
      }

      const HarmonicEvent& event = timeline.getAt(note.start_tick);
      ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
      int best_pitch = static_cast<int>(note.pitch);
      int best_bad = old_bad;
      int best_cost = INT32_MAX;
      for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
        uint8_t cand_u8 = static_cast<uint8_t>(cand);
        if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
          continue;
        }
        if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
          continue;
        }
        if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
          continue;
        }

        int bad = exposed_bad_count(cand);
        if (bad > old_bad)
          continue;
        int melodic_cost = 0;
        if (prev_pitch >= 0)
          melodic_cost += std::abs(cand - prev_pitch) * 8;
        if (next_pitch >= 0)
          melodic_cost += std::abs(cand - next_pitch) * 8;
        int cost = bad * 1000 + melodic_cost + std::abs(cand - static_cast<int>(note.pitch)) * 3;
        if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
          best_bad = bad;
          best_cost = cost;
          best_pitch = cand;
        }
      }

      if (best_pitch != static_cast<int>(note.pitch) && best_bad < old_bad) {
        note.pitch = static_cast<uint8_t>(best_pitch);
        note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
        ++shaped;
      }
    }
  }

  return shaped;
}

static int shapeExposedTwoVoiceEpisodeDialogue(std::vector<NoteEvent>& all_notes,
                                               const HarmonicTimeline& timeline,
                                               uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr Tick kDevelopStart = kTicksPerBar * 12;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_episode_dialogue_source = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::FreeCounterpoint;
  };
  auto is_hard_exposed_interval = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = interval_util::compoundToSimple(diff);
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.start_tick < kDevelopStart || !is_episode_dialogue_source(note.source)) {
      continue;
    }

    std::vector<Tick> sample_ticks{note.start_tick};
    Tick end_tick = note.start_tick + note.duration;
    Tick first_beat = ((note.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
      if (beat != note.start_tick)
        sample_ticks.push_back(beat);
    }

    auto exposed_bad_count = [&](int pitch) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        std::array<const NoteEvent*, 5> active{};
        int active_count = 0;
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          const NoteEvent* sounding = soundingNoteAt(all_notes, voice, tick);
          if (sounding == nullptr)
            continue;
          if (active_count < static_cast<int>(active.size())) {
            active[active_count] = sounding;
          }
          ++active_count;
        }
        if (active_count != 2)
          continue;

        const NoteEvent* other = nullptr;
        bool all_dialogue = true;
        for (int active_idx = 0; active_idx < active_count; ++active_idx) {
          const NoteEvent* sounding = active[active_idx];
          if (sounding == nullptr)
            continue;
          if (!is_episode_dialogue_source(sounding->source)) {
            all_dialogue = false;
            break;
          }
          if (sounding->voice != note.voice)
            other = sounding;
        }
        if (!all_dialogue || other == nullptr)
          continue;
        if (is_hard_exposed_interval(pitch, *other))
          ++count;
      }
      return count;
    };

    int old_bad = exposed_bad_count(note.pitch);
    if (old_bad == 0)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
        continue;
      }
      if ((note.start_tick % kTicksPerBeat) == 0 && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      int bad = exposed_bad_count(cand);
      if (bad > old_bad)
        continue;
      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 9;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 7;
      int cost = bad * 1500 + melodic_cost + std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (isChordTone(cand_u8, event))
        cost -= 20;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && best_bad < old_bad) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeProtectedDialogueSupportLines(std::vector<NoteEvent>& all_notes,
                                              const HarmonicTimeline& timeline,
                                              uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr Tick kDevelopStart = kTicksPerBar * 12;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_support_source = [](BachNoteSource source) {
    return source == BachNoteSource::Countersubject || source == BachNoteSource::EpisodeMaterial ||
           source == BachNoteSource::SequenceNote || source == BachNoteSource::FreeCounterpoint;
  };
  auto is_protected_dialogue = [](BachNoteSource source) {
    return source == BachNoteSource::SubjectCore || source == BachNoteSource::FugueSubject ||
           source == BachNoteSource::FugueAnswer;
  };
  auto is_hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.start_tick < kDevelopStart || !is_support_source(note.source)) {
      continue;
    }

    auto sample_ticks_for = [](const NoteEvent& n) {
      std::vector<Tick> sample_ticks{n.start_tick};
      Tick end_tick = n.start_tick + n.duration;
      Tick first_beat = ((n.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
      for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
        if (beat != n.start_tick)
          sample_ticks.push_back(beat);
      }
      return sample_ticks;
    };
    std::vector<Tick> sample_ticks = sample_ticks_for(note);

    auto bad_count_for = [&](int pitch, bool require_protected) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        bool has_protected = false;
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (is_protected_dialogue(other->source))
            has_protected = true;
          if (is_hard_bad_against(pitch, *other))
            ++count;
        }
        if (require_protected && !has_protected)
          return 0;
      }
      return count;
    };

    int old_bad = bad_count_for(note.pitch, true);
    if (old_bad == 0)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }
      int bad = bad_count_for(cand, false);
      if (bad > old_bad)
        continue;
      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 9;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 7;
      int cost = bad * 1000 + melodic_cost + std::abs(cand - static_cast<int>(note.pitch)) * 3;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) && best_bad < old_bad) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeLatePedalPointUpperSequence(std::vector<NoteEvent>& all_notes,
                                            const HarmonicTimeline& timeline, uint8_t num_voices) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kUpperVoice = 0;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_upper_sequence = [](BachNoteSource source) {
    return source == BachNoteSource::SequenceNote || source == BachNoteSource::EpisodeMaterial;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };

  int shaped = 0;
  std::vector<size_t> idxs;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    const auto& note = all_notes[idx];
    if (note.voice != kUpperVoice || !is_upper_sequence(note.source))
      continue;
    bool has_lower_support = false;
    for (VoiceId voice = 1; voice < num_voices; ++voice) {
      const NoteEvent* lower = soundingNoteAt(all_notes, voice, note.start_tick);
      if (lower == nullptr)
        continue;
      if (lower->source == BachNoteSource::PedalPoint ||
          lower->source == BachNoteSource::SubjectCore ||
          lower->source == BachNoteSource::FugueSubject ||
          lower->source == BachNoteSource::FugueAnswer || voice == num_voices - 1) {
        has_lower_support = true;
        break;
      }
    }
    if (!has_lower_support) {
      continue;
    }
    bool hard_against_lower = false;
    for (VoiceId voice = 1; voice < num_voices; ++voice) {
      const NoteEvent* lower = soundingNoteAt(all_notes, voice, note.start_tick);
      if (lower != nullptr && hard_bad_against(note.pitch, *lower)) {
        hard_against_lower = true;
        break;
      }
    }
    if (!hard_against_lower && note.pitch <= 82 && (note.modified_by & kPitchRepairMask) == 0) {
      continue;
    }
    idxs.push_back(idx);
  }
  std::sort(idxs.begin(), idxs.end(), [&](size_t lhs, size_t rhs) {
    if (all_notes[lhs].start_tick != all_notes[rhs].start_tick) {
      return all_notes[lhs].start_tick < all_notes[rhs].start_tick;
    }
    return lhs < rhs;
  });

  auto [lo, hi] = getFugueVoiceRange(kUpperVoice, num_voices);
  hi = std::min<uint8_t>(hi, 82);
  for (size_t pos = 0; pos < idxs.size(); ++pos) {
    NoteEvent& note = all_notes[idxs[pos]];

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idxs[pos]; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != kUpperVoice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idxs[pos] + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != kUpperVoice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto sample_ticks_for = [](const NoteEvent& n) {
      std::vector<Tick> sample_ticks{n.start_tick};
      Tick end_tick = n.start_tick + n.duration;
      Tick first_beat = ((n.start_tick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
      for (Tick beat = first_beat; beat < end_tick; beat += kTicksPerBeat) {
        if (beat != n.start_tick)
          sample_ticks.push_back(beat);
      }
      return sample_ticks;
    };
    std::vector<Tick> sample_ticks = sample_ticks_for(note);
    auto bad_count_for = [&](int pitch) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 1; voice < num_voices; ++voice) {
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_bad = bad_count_for(note.pitch);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }
      int bad = bad_count_for(cand);
      if (bad > old_bad && note.pitch <= hi && (note.modified_by & kPitchRepairMask) == 0) {
        continue;
      }
      int harmonic_cost = 0;
      bool harmonic_rejected = false;
      for (Tick tick : sample_ticks) {
        const HarmonicEvent& sample_event = timeline.getAt(tick);
        if ((tick % kTicksPerBeat) == 0 && !isChordTone(static_cast<uint8_t>(cand), sample_event)) {
          harmonic_rejected = true;
          break;
        }
      }
      if (harmonic_rejected)
        continue;
      int cost = bad * 1000 + harmonic_cost + std::max(0, cand - 79) * 20 +
                 std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (prev_pitch >= 0)
        cost += std::abs(cand - prev_pitch) * 8;
      if (next_pitch >= 0)
        cost += std::abs(cand - next_pitch) * 8;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch) &&
        (best_bad < old_bad || (note.modified_by & kPitchRepairMask) != 0 || note.pitch > hi)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
}

static int shapeLateResolveFlexibleHardClashes(std::vector<NoteEvent>& all_notes,
                                               const HarmonicTimeline& timeline,
                                               uint8_t num_voices) {
  if (num_voices < 3)
    return 0;

  constexpr Tick kWindowStart = kTicksPerBar * 38;
  constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                       static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                       static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                       static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);

  auto is_flexible_resolve_source = [](BachNoteSource source) {
    return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
           source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::CadenceApproach;
  };
  auto hard_bad_against = [](int pitch, const NoteEvent& other) {
    int diff = std::abs(pitch - static_cast<int>(other.pitch));
    if (diff == 0)
      return true;
    int simple = diff % 12;
    return simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11;
  };
  auto sample_ticks_for = [](const NoteEvent& note) {
    std::vector<Tick> sample_ticks;
    Tick end_tick = note.start_tick + note.duration;
    for (Tick tick = note.start_tick; tick < end_tick; tick += duration::kSixteenthNote) {
      sample_ticks.push_back(tick);
    }
    return sample_ticks;
  };

  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.voice != rhs.voice)
      return lhs.voice < rhs.voice;
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.pitch < rhs.pitch;
  });

  int shaped = 0;
  for (size_t idx = 0; idx < all_notes.size(); ++idx) {
    NoteEvent& note = all_notes[idx];
    if (note.start_tick < kWindowStart || !is_flexible_resolve_source(note.source)) {
      continue;
    }

    std::vector<Tick> sample_ticks = sample_ticks_for(note);
    auto bad_count_for = [&](int pitch) {
      int count = 0;
      for (Tick tick : sample_ticks) {
        for (VoiceId voice = 0; voice < num_voices; ++voice) {
          if (voice == note.voice)
            continue;
          const NoteEvent* other = soundingNoteAt(all_notes, voice, tick);
          if (other == nullptr)
            continue;
          if (hard_bad_against(pitch, *other))
            ++count;
        }
      }
      return count;
    };

    int old_bad = bad_count_for(note.pitch);
    if (old_bad == 0)
      continue;

    int prev_pitch = -1;
    int next_pitch = -1;
    for (size_t scan = idx; scan > 0; --scan) {
      const auto& prev = all_notes[scan - 1];
      if (prev.voice != note.voice)
        continue;
      Tick prev_end = prev.start_tick + prev.duration;
      Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
      if (gap <= kTicksPerBar)
        prev_pitch = prev.pitch;
      break;
    }
    for (size_t scan = idx + 1; scan < all_notes.size(); ++scan) {
      const auto& next = all_notes[scan];
      if (next.voice != note.voice)
        continue;
      Tick note_end = note.start_tick + note.duration;
      Tick gap = next.start_tick > note_end ? next.start_tick - note_end : 0;
      if (gap <= kTicksPerBar)
        next_pitch = next.pitch;
      break;
    }

    auto [lo, hi] = getFugueVoiceRange(note.voice, num_voices);
    if (note.voice == 0)
      hi = std::min<uint8_t>(hi, 82);
    const HarmonicEvent& event = timeline.getAt(note.start_tick);
    ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    int best_pitch = static_cast<int>(note.pitch);
    int best_bad = old_bad;
    int best_cost = INT32_MAX;
    for (int cand = static_cast<int>(lo); cand <= static_cast<int>(hi); ++cand) {
      uint8_t cand_u8 = static_cast<uint8_t>(cand);
      if (!scale_util::isScaleTone(cand_u8, event.key, scale) && !isChordTone(cand_u8, event)) {
        continue;
      }
      if (prev_pitch >= 0 && std::abs(cand - prev_pitch) > interval::kPerfect5th) {
        continue;
      }
      if (next_pitch >= 0 && std::abs(cand - next_pitch) > interval::kPerfect5th) {
        continue;
      }

      bool harmonic_rejected = false;
      for (Tick tick : sample_ticks) {
        if ((tick % kTicksPerBeat) != 0)
          continue;
        const HarmonicEvent& beat_event = timeline.getAt(tick);
        if (!isChordTone(cand_u8, beat_event)) {
          harmonic_rejected = true;
          break;
        }
      }
      if (harmonic_rejected)
        continue;

      int bad = bad_count_for(cand);
      if (bad >= old_bad)
        continue;
      int melodic_cost = 0;
      if (prev_pitch >= 0)
        melodic_cost += std::abs(cand - prev_pitch) * 10;
      if (next_pitch >= 0)
        melodic_cost += std::abs(cand - next_pitch) * 8;
      int register_cost = note.voice == 0 ? std::max(0, cand - 79) * 20 : 0;
      int cost = bad * 1000 + melodic_cost + register_cost +
                 std::abs(cand - static_cast<int>(note.pitch)) * 4;
      if (bad < best_bad || (bad == best_bad && cost < best_cost)) {
        best_bad = bad;
        best_cost = cost;
        best_pitch = cand;
      }
    }

    if (best_pitch != static_cast<int>(note.pitch)) {
      note.pitch = static_cast<uint8_t>(best_pitch);
      note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      ++shaped;
    }
  }

  return shaped;
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
FugueResult finalize(const FugueConfig& config, const FugueMaterial& material,
                     const FuguePlan& plan, FugueStructure& structure,
                     std::vector<NoteEvent> all_notes) {
  FugueResult result;

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // --- Cadence insertion ---
  insertCadenceNotes(all_notes, structure, config, plan, num_voices);

  // --- Within-voice overlap removal ---
  finalizeFormNotes(all_notes, num_voices);

  int opening_tonic_pedal_support = addOpeningTonicPedalSupport(all_notes, config, num_voices);
  fprintf(stderr, "Opening tonic pedal support sweep: %d inserted\n", opening_tonic_pedal_support);

  int relative_minor_pedal_repairs =
      retargetRelativeMinorFalseEntryPedals(all_notes, config, plan, num_voices);
  fprintf(stderr, "Relative-minor false-entry pedal retarget sweep: %d repaired\n",
          relative_minor_pedal_repairs);

  int late_home_pedal_support = addLateHomeEpisodePedalSupport(all_notes, config, plan, num_voices);
  fprintf(stderr, "Late home-episode pedal support sweep: %d inserted\n", late_home_pedal_support);

  int subdominant_opening_pedal_support =
      addSubdominantEpisodeOpeningPedalSupport(all_notes, config, plan, num_voices);
  fprintf(stderr, "Subdominant episode opening pedal support sweep: %d inserted\n",
          subdominant_opening_pedal_support);

  int thematic_episode_texture_support =
      addThematicEpisodeTextureSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Thematic episode texture support sweep: %d inserted\n",
          thematic_episode_texture_support);

  int episode_dialogue_window_support =
      addEpisodeDialogueWindowSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Episode dialogue-window support sweep: %d inserted\n",
          episode_dialogue_window_support);

  int precomposed_episode_material =
      precomposeEpisodeMaterialToTimeline(all_notes, plan, num_voices);
  fprintf(stderr, "Precomposed episode material sweep: %d shaped\n", precomposed_episode_material);

  const bool use_bwv578_density_sweeps =
      !(config.character == SubjectCharacter::Restless && num_voices == 4 &&
        material.thematic_plan.episode_drawer.size() > 0);

  int long_figures = use_bwv578_density_sweeps ? splitBwv578ManualLongFlexibleNotes(
                                                     all_notes, plan, structure, num_voices)
                                               : 0;
  fprintf(stderr, "BWV578 manual long-note figuration sweep: %d inserted\n", long_figures);

  int pedal_gap_motion =
      use_bwv578_density_sweeps ? addBwv578PedalGapMotion(all_notes, plan, num_voices) : 0;
  fprintf(stderr, "BWV578 pedal gap motion sweep: %d inserted\n", pedal_gap_motion);

  int gap_figures =
      use_bwv578_density_sweeps ? addBwv578ManualGapFiguration(all_notes, plan, num_voices) : 0;
  fprintf(stderr, "BWV578 manual gap figuration sweep: %d inserted\n", gap_figures);

  int manual_iii_mid_gap_turns =
      use_bwv578_density_sweeps ? addBwv578ManualIIIMidGapTurn(all_notes, plan, num_voices) : 0;
  fprintf(stderr, "BWV578 Manual III mid-gap turn sweep: %d inserted\n", manual_iii_mid_gap_turns);

  int manual_iii_quantized_gap_turns =
      use_bwv578_density_sweeps ? addBwv578ManualIIIQuantizedGapTurns(all_notes, plan, num_voices)
                                : 0;
  fprintf(stderr, "BWV578 Manual III quantized-gap turn sweep: %d inserted\n",
          manual_iii_quantized_gap_turns);

  int manual_i_mid_gap_turns =
      use_bwv578_density_sweeps ? addBwv578ManualIMidGapTurns(all_notes, plan, num_voices) : 0;
  fprintf(stderr, "BWV578 Manual I mid-gap turn sweep: %d inserted\n", manual_i_mid_gap_turns);

  int resolve_lower_manual_gap_motion =
      use_bwv578_density_sweeps ? addBwv578ResolveLowerManualGapMotion(all_notes, plan, num_voices)
                                : 0;
  fprintf(stderr, "BWV578 resolve lower-manual gap motion sweep: %d inserted\n",
          resolve_lower_manual_gap_motion);

  // --- Repeated-note repair ---
  // Reference organ fugues use repeated notes, but long same-pitch runs make
  // generated counterpoint sound static. Repair flexible material only, using
  // vertical safety to avoid introducing strong-beat clashes or parallels.
  {
    RepeatedNoteRepairParams rn_params;
    rn_params.num_voices = num_voices;
    rn_params.max_consecutive = 1;
    rn_params.run_gap_threshold = kTicksPerBeat;
    rn_params.key_at_tick = [&](Tick tick) { return plan.tonal_plan.keyAtTick(tick); };
    rn_params.scale_at_tick = [&](Tick) {
      return config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    rn_params.voice_range = [&](uint8_t voice) { return getFugueVoiceRange(voice, num_voices); };
    rn_params.repair_coda = true;
    rn_params.repair_coda_voice = 2;
    rn_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
    int repeated_repairs = repairRepeatedNotes(all_notes, rn_params);
    fprintf(stderr, "Repeated-note sweep: %d repaired\n", repeated_repairs);
  }

  // --- Strong-beat consonance enforcement ---
  enforceStrongBeatConsonance(all_notes, config, plan, num_voices);
  int delayed_dissonances = delayStrongBeatEpisodeDissonances(all_notes, plan, num_voices);
  fprintf(stderr, "Strong-beat episode delay sweep: %d repaired\n", delayed_dissonances);
  int cs_straddler_trims = trimShortCountersubjectSubjectStraddlers(all_notes, plan, num_voices);
  fprintf(stderr, "Countersubject strong-beat straddler trim sweep: %d repaired\n",
          cs_straddler_trims);

  // --- Manual voice spacing repair ---
  int spacing_repairs = reduceAdjacentManualSpacing(all_notes, plan, num_voices);
  fprintf(stderr, "Manual spacing sweep: %d repaired\n", spacing_repairs);

  // --- Pedal consonance enforcement ---
  enforcePedalConsonance(all_notes, num_voices);

  // --- Parallel perfect interval + melodic tritone repair ---
  repairParallelPerfectsAndTritones(all_notes, config, plan, num_voices);

  // --- Voice crossing repair ---
  repairVoiceCrossings(all_notes, config, plan, num_voices);

  int cadence_crossing_trims = trimCadenceApproachManualCrossings(all_notes, plan, num_voices);
  fprintf(stderr, "Cadence crossing trim sweep: %d repaired\n", cadence_crossing_trims);

  // --- Free-counterpoint leap resolution ---
  // createBachNote may move filler notes for vertical safety, leaving exposed
  // melodic leaps. Resolve only FreeCounterpoint landings so thematic and
  // episode material keep their identity.
  {
    LeapResolutionParams lr_params;
    lr_params.num_voices = num_voices;
    lr_params.leap_threshold = 5;  // Fugue analyzer threshold.
    lr_params.key_at_tick = [&](Tick tick) { return plan.tonal_plan.keyAtTick(tick); };
    lr_params.scale_at_tick = [&](Tick) {
      return config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    };
    lr_params.voice_range = [&](uint8_t voice, Tick) {
      return getFugueVoiceRange(voice, num_voices);
    };
    lr_params.vertical_safe =
        makeVerticalSafeWithParallelCheck(plan.detailed_timeline, all_notes, num_voices);
    lr_params.is_chord_tone = [&](Tick tick, uint8_t pitch) {
      return isChordTone(pitch, plan.detailed_timeline.getAt(tick));
    };
    lr_params.applies_to_leap_source = [](BachNoteSource source) {
      return source == BachNoteSource::FreeCounterpoint;
    };
    lr_params.resolution_probability = 1.0f;
    // Free counterpoint is flexible filler; allow wider but vertically safe
    // alternatives when a strict contrary step is blocked.
    lr_params.allow_wider_fallback = true;
    lr_params.protect_strong_beat_resolution = false;
    lr_params.protect_chord_tone_resolution = false;
    int leap_repairs = resolveLeaps(all_notes, lr_params);
    fprintf(stderr, "Free counterpoint leap-resolution sweep: %d repaired\n", leap_repairs);
  }

  int free_landing_repairs =
      repairFreeCounterpointLeapLandings(all_notes, config, plan, num_voices);
  fprintf(stderr, "Free counterpoint landing sweep: %d repaired\n", free_landing_repairs);

  // --- Final cadence bass lock ---
  // Late repair sweeps can move the coda bass away from V-I. Restore the
  // structural cadence after all flexible repairs so the ending remains legible.
  enforceFinalCadenceBass(all_notes, config, plan, num_voices);

  int range_repairs = foldOutOfRangeNotes(all_notes, num_voices);
  fprintf(stderr, "Range fold sweep: %d repaired\n", range_repairs);

  int residual_episode_leaps = repairResidualEpisodeExcessiveLeaps(all_notes, plan, num_voices);
  fprintf(stderr, "Residual episode leap sweep: %d repaired\n", residual_episode_leaps);

  int sustained_pedal_dissonances =
      repairSustainedPedalEpisodeDissonances(all_notes, plan, num_voices);
  fprintf(stderr, "Sustained pedal dissonance sweep: %d repaired\n", sustained_pedal_dissonances);

  int pedal_cadence_tritones = repairPedalCadenceApproachTritones(all_notes, num_voices);
  fprintf(stderr, "Pedal cadence tritone sweep: %d repaired\n", pedal_cadence_tritones);

  int manual_countersubject_tritones =
      repairManualCountersubjectTritoneOutlines(all_notes, plan, num_voices);
  fprintf(stderr, "Manual countersubject tritone sweep: %d repaired\n",
          manual_countersubject_tritones);

  int lower_manual_cs_tail_drops = repairLowerManualCountersubjectTailDrops(all_notes, num_voices);
  fprintf(stderr, "Lower manual countersubject tail-drop sweep: %d repaired\n",
          lower_manual_cs_tail_drops);

  int upper_manual_hidden_unisons = repairUpperManualHiddenUnisonLandings(all_notes, num_voices);
  fprintf(stderr, "Upper manual hidden-unison sweep: %d repaired\n", upper_manual_hidden_unisons);

  int manual_interleaving_repairs = repairManualInterleavingRuns(all_notes, plan, num_voices);
  fprintf(stderr, "Manual interleaving sweep: %d repaired\n", manual_interleaving_repairs);

  repairVoiceCrossings(all_notes, config, plan, num_voices);

  int residual_parallel_repairs = repairResidualFlexibleParallels(all_notes, plan, num_voices);
  fprintf(stderr, "Residual flexible parallel sweep: %d repaired\n", residual_parallel_repairs);

  int residual_free_leap_repairs =
      repairResidualFreeCounterpointLeapResolutions(all_notes, plan, num_voices);
  fprintf(stderr, "Residual free-counterpoint leap sweep: %d repaired\n",
          residual_free_leap_repairs);

  int residual_tritone_repairs = repairResidualCadenceTritoneOutlines(all_notes, num_voices);
  fprintf(stderr, "Residual cadence tritone-outline sweep: %d repaired\n",
          residual_tritone_repairs);

  int structural_answer_dissonance_repairs =
      repairStructuralAnswerStrongBeatDissonances(all_notes, plan, num_voices);
  fprintf(stderr, "Structural answer strong-beat repair sweep: %d repaired\n",
          structural_answer_dissonance_repairs);

  int post_answer_tritone_repairs = repairResidualCadenceTritoneOutlines(all_notes, num_voices);
  fprintf(stderr, "Post-answer cadence tritone-outline sweep: %d repaired\n",
          post_answer_tritone_repairs);

  int subdominant_closing_pedal_retargets =
      retargetSubdominantEpisodeClosingPedal(all_notes, config, plan, num_voices);
  fprintf(stderr, "Subdominant episode closing pedal retarget sweep: %d repaired\n",
          subdominant_closing_pedal_retargets);

  int early_raised_fourth_pedal_retargets =
      retargetEarlyEpisodeRaisedFourthPedal(all_notes, config, num_voices);
  fprintf(stderr, "Early episode raised-fourth pedal retarget sweep: %d repaired\n",
          early_raised_fourth_pedal_retargets);

  int middle_raised_fourth_pedal_retargets =
      retargetMiddleEpisodeRaisedFourthPedal(all_notes, config, num_voices);
  fprintf(stderr, "Middle episode raised-fourth pedal retarget sweep: %d repaired\n",
          middle_raised_fourth_pedal_retargets);

  int late_home_raised_fourth_pedal_retargets =
      retargetLateHomeEpisodeRaisedFourthPedal(all_notes, config, plan, num_voices);
  fprintf(stderr, "Late home raised-fourth pedal retarget sweep: %d repaired\n",
          late_home_raised_fourth_pedal_retargets);

  int final_upper_manual_hidden_unisons =
      repairUpperManualHiddenUnisonLandings(all_notes, num_voices);
  fprintf(stderr, "Final upper manual hidden-unison sweep: %d repaired\n",
          final_upper_manual_hidden_unisons);

  int middle_half_cadence_support =
      use_bwv578_density_sweeps
          ? addBwv578MiddleHalfCadenceSupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 middle half-cadence support sweep: %d repaired\n",
          middle_half_cadence_support);

  int middle_entry_pedal_continuity_support =
      use_bwv578_density_sweeps
          ? addBwv578MiddleEntryPedalContinuitySupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 middle-entry pedal continuity support sweep: %d inserted\n",
          middle_entry_pedal_continuity_support);

  int early_episode_pedal_continuity_support =
      use_bwv578_density_sweeps
          ? addBwv578EarlyEpisodePedalContinuitySupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 early-episode pedal continuity support sweep: %d inserted\n",
          early_episode_pedal_continuity_support);

  int early_episode_manual_ii_line =
      use_bwv578_density_sweeps
          ? consolidateBwv578EarlyEpisodeManualIILine(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 early-episode Manual II line consolidation sweep: %d notes\n",
          early_episode_manual_ii_line);

  int subdominant_bridge_pedal_support =
      use_bwv578_density_sweeps
          ? addBwv578SubdominantBridgePedalSupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 subdominant bridge pedal support sweep: %d inserted\n",
          subdominant_bridge_pedal_support);

  int stretto_pedal_continuity_support =
      use_bwv578_density_sweeps
          ? addBwv578StrettoPedalContinuitySupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 stretto pedal continuity support sweep: %d inserted\n",
          stretto_pedal_continuity_support);

  int stretto_manual_iii_gap_support =
      use_bwv578_density_sweeps
          ? addBwv578StrettoManualIIIGapSupport(all_notes, config, plan, num_voices)
          : 0;
  fprintf(stderr, "BWV578 Stretto Manual III gap support sweep: %d inserted\n",
          stretto_manual_iii_gap_support);

  int post_repair_manual_i_mid_gap_turns =
      use_bwv578_density_sweeps ? addBwv578ManualIMidGapTurns(all_notes, plan, num_voices) : 0;
  fprintf(stderr, "Post-repair BWV578 Manual I mid-gap turn sweep: %d inserted\n",
          post_repair_manual_i_mid_gap_turns);

  int coda_manual_ii_splits = splitCodaManualIIStage1Holding(all_notes, num_voices);
  fprintf(stderr, "Coda Manual II stage-1 split sweep: %d inserted\n", coda_manual_ii_splits);

  int coda_manual_iii_stage1_turns = 0;
  fprintf(stderr, "Coda Manual III stage-1 turn split sweep: %d inserted\n",
          coda_manual_iii_stage1_turns);

  int coda_cadence_manual_turns = 0;
  fprintf(stderr, "Coda cadence manual-turn split sweep: %d inserted\n", coda_cadence_manual_turns);

  int coda_manual_i_cadence_eighth_turns = 0;
  fprintf(stderr, "Coda Manual I cadence eighth-turn split sweep: %d inserted\n",
          coda_manual_i_cadence_eighth_turns);

  int coda_final_manual_long_turns = 0;
  fprintf(stderr, "Coda final manual long-turn split sweep: %d inserted\n",
          coda_final_manual_long_turns);

  int harmonic_boundary_trims =
      trimDissonantSustainsAtHarmonicBoundaries(all_notes, plan, num_voices);
  fprintf(stderr, "Dissonant harmonic-boundary trim sweep: %d repaired\n", harmonic_boundary_trims);

  int flexible_strong_beat_chord_snaps =
      snapFlexibleStrongBeatNonChordTones(all_notes, plan, num_voices);
  fprintf(stderr, "Flexible strong-beat chord-tone sweep: %d repaired\n",
          flexible_strong_beat_chord_snaps);

  int flexible_timeline_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan, num_voices);
  fprintf(stderr, "Flexible timeline-scale sweep: %d repaired\n", flexible_timeline_scale_snaps);

  int residual_middle_manual_ii_high_notes =
      lowerResidualMiddleManualIIHighNotes(all_notes, num_voices);
  fprintf(stderr, "Residual middle Manual II high-note sweep: %d repaired\n",
          residual_middle_manual_ii_high_notes);

  int middle_upper_manual_leaps = smoothMiddleUpperManualEpisodeLeaps(all_notes, num_voices);
  fprintf(stderr, "Middle upper manual episode-leap smoothing sweep: %d repaired\n",
          middle_upper_manual_leaps);

  int middle_lower_manual_leaps = smoothLowerManualMiddleEpisodeLeaps(all_notes, num_voices);
  fprintf(stderr, "Middle lower manual episode-leap smoothing sweep: %d repaired\n",
          middle_lower_manual_leaps);

  int early_lower_manual_countersubject_turn =
      shapeEarlyLowerManualCountersubjectTurn(all_notes, num_voices);
  fprintf(stderr, "Early lower manual countersubject turn sweep: %d repaired\n",
          early_lower_manual_countersubject_turn);

  int early_lower_manual_countersubject_large_leaps =
      smoothEarlyLowerManualCountersubjectLargeLeaps(all_notes, num_voices);
  fprintf(stderr, "Early lower manual countersubject large-leap sweep: %d repaired\n",
          early_lower_manual_countersubject_large_leaps);

  // Re-run only the flexible episode-material guard after support insertion so
  // protected subject, answer, dialogue, bass, and coda structures remain intact.
  int late_residual_parallel_repairs = repairResidualFlexibleParallels(all_notes, plan, num_voices);
  fprintf(stderr, "Late residual flexible parallel sweep: %d repaired\n",
          late_residual_parallel_repairs);

  int final_flexible_timeline_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan, num_voices);
  fprintf(stderr, "Final flexible timeline-scale sweep: %d repaired\n",
          final_flexible_timeline_scale_snaps);

  int minor_exposition_scale_repairs =
      snapMinorExpositionStructuralTonesToLocalScale(all_notes, config, material, num_voices);
  fprintf(stderr, "Minor exposition structural-scale sweep: %d repaired\n",
          minor_exposition_scale_repairs);

  int upper_manual_register_repairs = containUpperManualFlexibleRegister(all_notes, num_voices);
  fprintf(stderr, "Upper manual flexible-register containment sweep: %d repaired\n",
          upper_manual_register_repairs);

  finalizeFormNotes(all_notes, num_voices);

  int final_harmonic_boundary_trims =
      trimDissonantSustainsAtHarmonicBoundaries(all_notes, plan, num_voices);
  fprintf(stderr, "Final dissonant harmonic-boundary trim sweep: %d repaired\n",
          final_harmonic_boundary_trims);

  // Use the same harmonic plan for generation, final validation, output, and
  // analysis.  Rebuilding a separate bar-resolution progression here makes the
  // finalize stage become a composer by forcing already-generated notes onto a
  // different chord grid.
  result.timeline = plan.detailed_timeline;

  int output_protected_sustain_stabilizations =
      stabilizeOutputTimelineForProtectedSustains(result.timeline, all_notes, num_voices);
  fprintf(stderr, "Output protected-sustain timeline stabilization sweep: %d repaired\n",
          output_protected_sustain_stabilizations);

  int output_pedal_timeline_alignments =
      alignOutputTimelineToPedalSupport(result.timeline, all_notes, num_voices);
  fprintf(stderr, "Output pedal timeline alignment sweep: %d repaired\n",
          output_pedal_timeline_alignments);

  int output_timeline_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, result.timeline, num_voices);
  fprintf(stderr, "Output timeline-scale sweep: %d repaired\n", output_timeline_scale_snaps);

  int output_key_signature_scale_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, result.timeline, config.key, config.is_minor, num_voices);
  fprintf(stderr, "Output key-signature scale sweep: %d repaired\n",
          output_key_signature_scale_snaps);

  int output_strong_beat_chord_tone_repairs = snapFlexibleStrongBeatNonChordTones(
      all_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Output strong-beat chord-tone sweep: %d repaired\n",
          output_strong_beat_chord_tone_repairs);

  int final_output_key_signature_scale_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, result.timeline, config.key, config.is_minor, num_voices);
  fprintf(stderr, "Final output key-signature scale sweep: %d repaired\n",
          final_output_key_signature_scale_snaps);

  int final_generation_timeline_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Final generation-timeline scale sweep: %d repaired\n",
          final_generation_timeline_scale_snaps);

  int post_generation_key_signature_scale_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, plan.detailed_timeline, config.key, config.is_minor, num_voices, true);
  fprintf(stderr, "Post-generation key-signature scale sweep: %d repaired\n",
          post_generation_key_signature_scale_snaps);

  int final_output_intersection_scale_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, result.timeline, config.key, config.is_minor, num_voices, true);
  fprintf(stderr, "Final output intersection-scale sweep: %d repaired\n",
          final_output_intersection_scale_snaps);

  int output_timeline_boundary_trims =
      trimDissonantSustainsAtHarmonicBoundaries(all_notes, result.timeline, num_voices);
  fprintf(stderr, "Output-timeline harmonic-boundary trim sweep: %d repaired\n",
          output_timeline_boundary_trims);

  int final_middle_lower_manual_leaps = smoothLowerManualMiddleEpisodeLeaps(all_notes, num_voices);
  fprintf(stderr, "Final middle lower manual episode-leap smoothing sweep: %d repaired\n",
          final_middle_lower_manual_leaps);

  int post_smoothing_generation_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Post-smoothing generation-scale sweep: %d repaired\n",
          post_smoothing_generation_scale_snaps);

  int post_smoothing_generation_intersection_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, plan.detailed_timeline, config.key, config.is_minor, num_voices, true);
  fprintf(stderr, "Post-smoothing generation-intersection sweep: %d repaired\n",
          post_smoothing_generation_intersection_snaps);

  int final_output_strong_beat_chord_tone_repairs = snapFlexibleStrongBeatNonChordTones(
      all_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Final output strong-beat chord-tone sweep: %d repaired\n",
          final_output_strong_beat_chord_tone_repairs);

  int residual_flexible_contour_smoothing =
      smoothResidualFlexibleContourLeaps(all_notes, result.timeline, config, num_voices);
  fprintf(stderr, "Residual flexible contour smoothing sweep: %d repaired\n",
          residual_flexible_contour_smoothing);

  int final_generation_strong_beat_chord_tone_repairs = snapFlexibleStrongBeatNonChordTones(
      all_notes, plan.detailed_timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Final generation strong-beat chord-tone sweep: %d repaired\n",
          final_generation_strong_beat_chord_tone_repairs);

  int terminal_output_strong_beat_chord_tone_repairs = snapFlexibleStrongBeatNonChordTones(
      all_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Terminal output strong-beat chord-tone sweep: %d repaired\n",
          terminal_output_strong_beat_chord_tone_repairs);

  int terminal_generation_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Terminal generation-scale sweep: %d repaired\n",
          terminal_generation_scale_snaps);

  int terminal_generation_intersection_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, plan.detailed_timeline, config.key, config.is_minor, num_voices, true);
  fprintf(stderr, "Terminal generation-intersection sweep: %d repaired\n",
          terminal_generation_intersection_snaps);

  int terminal_strong_beat_clash_repairs = resolveFlexibleStrongBeatClashes(
      all_notes, result.timeline, plan.detailed_timeline, config.key, config.is_minor, num_voices);
  fprintf(stderr, "Terminal strong-beat clash sweep: %d repaired\n",
          terminal_strong_beat_clash_repairs);

  int post_clash_output_strong_beat_chord_tone_repairs = snapFlexibleStrongBeatNonChordTones(
      all_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Post-clash output strong-beat chord-tone sweep: %d repaired\n",
          post_clash_output_strong_beat_chord_tone_repairs);

  int post_clash_generation_scale_snaps =
      snapFlexibleNonDiatonicToTimelineScale(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Post-clash generation-scale sweep: %d repaired\n",
          post_clash_generation_scale_snaps);

  int post_clash_generation_intersection_snaps = snapFlexibleNonDiatonicToKeySignature(
      all_notes, plan.detailed_timeline, config.key, config.is_minor, num_voices, true);
  fprintf(stderr, "Post-clash generation-intersection sweep: %d repaired\n",
          post_clash_generation_intersection_snaps);

  int terminal_flexible_contour_smoothing =
      smoothResidualFlexibleContourLeaps(all_notes, result.timeline, config, num_voices);
  fprintf(stderr, "Terminal flexible contour smoothing sweep: %d repaired\n",
          terminal_flexible_contour_smoothing);

  int terminal_remote_flexible_smoothing =
      smoothResidualRemoteFlexibleLeaps(all_notes, result.timeline, config, num_voices);
  fprintf(stderr, "Terminal remote flexible smoothing sweep: %d repaired\n",
          terminal_remote_flexible_smoothing);

  int terminal_output_timeline_boundary_trims =
      trimDissonantSustainsAtHarmonicBoundaries(all_notes, result.timeline, num_voices);
  fprintf(stderr, "Terminal output-timeline harmonic-boundary trim sweep: %d repaired\n",
          terminal_output_timeline_boundary_trims);

  int terminal_thematic_episode_texture_support =
      addThematicEpisodeTextureSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Terminal thematic episode texture support sweep: %d inserted\n",
          terminal_thematic_episode_texture_support);

  int terminal_episode_dialogue_window_support =
      addEpisodeDialogueWindowSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Terminal episode dialogue-window support sweep: %d inserted\n",
          terminal_episode_dialogue_window_support);

  int terminal_listening_hotspot_entry_alignments =
      alignListeningHotspotNearBeatEntries(all_notes, num_voices);
  fprintf(stderr, "Terminal listening-hotspot entry alignment sweep: %d notes\n",
          terminal_listening_hotspot_entry_alignments);

  int terminal_listening_hotspot_texture_support =
      addListeningHotspotTextureSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Terminal listening-hotspot texture support sweep: %d inserted\n",
          terminal_listening_hotspot_texture_support);

  int terminal_critic_window_texture_support =
      addCriticWindowTextureSupport(all_notes, plan, config, num_voices);
  fprintf(stderr, "Terminal critic-window texture support sweep: %d inserted\n",
          terminal_critic_window_texture_support);

  int terminal_listening_hotspot_clash_recompositions =
      recomposeListeningHotspotFlexibleClashes(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Terminal listening-hotspot clash recomposition sweep: %d notes\n",
          terminal_listening_hotspot_clash_recompositions);

  int terminal_listening_hotspot_clash_splits =
      splitListeningHotspotFlexibleClashCells(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Terminal listening-hotspot clash split sweep: %d notes\n",
          terminal_listening_hotspot_clash_splits);

  int terminal_middle_lower_manual_leaps =
      smoothLowerManualMiddleEpisodeLeaps(all_notes, num_voices);
  fprintf(stderr, "Terminal middle lower manual episode-leap smoothing sweep: %d repaired\n",
          terminal_middle_lower_manual_leaps);

  int final_output_pedal_timeline_alignments =
      alignOutputTimelineToPedalSupport(result.timeline, all_notes, num_voices);
  fprintf(stderr, "Final output pedal timeline alignment sweep: %d repaired\n",
          final_output_pedal_timeline_alignments);

  int accepted_recomposed_episode_material =
      acceptRecomposedEpisodeMaterial(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Accepted recomposed episode material sweep: %d notes\n",
          accepted_recomposed_episode_material);

  int accepted_early_free_counterpoint_intent = 0;
  if (num_voices >= 4) {
    constexpr uint8_t kPitchRepairMask = static_cast<uint8_t>(NoteModifiedBy::ParallelRepair) |
                                         static_cast<uint8_t>(NoteModifiedBy::ChordToneSnap) |
                                         static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                         static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust) |
                                         static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
    std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
      if (a.start_tick != b.start_tick)
        return a.start_tick < b.start_tick;
      return a.voice < b.voice;
    });
    std::array<int, 8> previous_pitch;
    previous_pitch.fill(-1);
    ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
    for (auto& note : all_notes) {
      if (note.voice >= previous_pitch.size())
        continue;
      if (note.source == BachNoteSource::FreeCounterpoint && note.start_tick < kTicksPerBar * 8) {
        auto [voice_low, voice_high] = getFugueVoiceRange(note.voice, num_voices);
        if (note.voice == 0) {
          voice_high = std::min<uint8_t>(voice_high, 84);
        }
        const auto& harm_ev = result.timeline.getAt(note.start_tick);
        bool strong = (note.start_tick % kTicksPerBeat) == 0;
        int best_pitch = static_cast<int>(note.pitch);
        int best_score = INT32_MIN;
        for (int cand = static_cast<int>(voice_low); cand <= static_cast<int>(voice_high); ++cand) {
          uint8_t cand_u8 = static_cast<uint8_t>(cand);
          if (!scale_util::isScaleTone(cand_u8, config.key, scale))
            continue;
          if (strong && !isChordTone(cand_u8, harm_ev))
            continue;
          int score = 120 - std::abs(cand - static_cast<int>(note.pitch)) * 3;
          int prev = previous_pitch[note.voice];
          if (prev >= 0) {
            int melodic = std::abs(cand - prev);
            if (melodic == 0)
              score -= 80;
            if (melodic <= 2)
              score += 45;
            else if (melodic <= 4)
              score += 20;
            else
              score -= (melodic - 4) * 80;
          }
          bool rejected = false;
          for (const auto& other : all_notes) {
            if (other.voice == note.voice)
              continue;
            if (other.start_tick > note.start_tick)
              break;
            if (other.start_tick + other.duration <= note.start_tick)
              continue;
            bool crossed = (note.voice < other.voice && cand < other.pitch) ||
                           (note.voice > other.voice && cand > other.pitch);
            if (crossed) {
              rejected = true;
              break;
            }
            int diff = std::abs(cand - static_cast<int>(other.pitch));
            if (diff == 0 || diff < 3) {
              rejected = true;
              break;
            }
            int simple = interval_util::compoundToSimple(diff);
            if (simple == 1 || simple == 2 || simple == 6 || simple == 10 || simple == 11) {
              score -= strong ? 180 : 60;
            } else if (simple == 3 || simple == 4 || simple == 8 || simple == 9) {
              score += strong ? 45 : 15;
            }
          }
          if (rejected)
            continue;
          if (score > best_score) {
            best_score = score;
            best_pitch = cand;
          }
        }
        uint8_t shaped_pitch = static_cast<uint8_t>(clampPitch(best_pitch, voice_low, voice_high));
        if (shaped_pitch != note.pitch || (note.modified_by & kPitchRepairMask)) {
          ++accepted_early_free_counterpoint_intent;
        }
        note.pitch = shaped_pitch;
        note.modified_by &= static_cast<uint8_t>(~kPitchRepairMask);
      }
      previous_pitch[note.voice] = static_cast<int>(note.pitch);
    }
  }
  fprintf(stderr, "Accepted early free-counterpoint intent sweep: %d notes\n",
          accepted_early_free_counterpoint_intent);

  int accepted_final_flexible_contour_smoothing =
      smoothResidualFlexibleContourLeaps(all_notes, result.timeline, config, num_voices);
  fprintf(stderr, "Accepted final flexible contour smoothing sweep: %d repaired\n",
          accepted_final_flexible_contour_smoothing);

  int accepted_final_flexible_step_acceptance = 0;
  for (int pass = 0; pass < 3; ++pass) {
    std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
      if (a.voice != b.voice)
        return a.voice < b.voice;
      return a.start_tick < b.start_tick;
    });
    bool changed = false;
    std::array<int, 8> prev_idx;
    prev_idx.fill(-1);
    for (size_t idx = 0; idx < all_notes.size(); ++idx) {
      auto& note = all_notes[idx];
      if (note.voice >= prev_idx.size())
        continue;
      int pidx = prev_idx[note.voice];
      if (pidx >= 0) {
        auto& prev = all_notes[static_cast<size_t>(pidx)];
        Tick prev_end = prev.start_tick + prev.duration;
        Tick gap = note.start_tick > prev_end ? note.start_tick - prev_end : 0;
        bool flexible_pair = getProtectionLevel(prev.source) == ProtectionLevel::Flexible &&
                             getProtectionLevel(note.source) == ProtectionLevel::Flexible;
        int leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
        if (flexible_pair && gap <= duration::kHalfNote && leap > interval::kPerfect5th) {
          auto [voice_low, voice_high] = getFugueVoiceRange(note.voice, num_voices);
          if (note.voice == 0) {
            voice_high = std::min<uint8_t>(voice_high, 84);
          }
          const auto& harm_ev = result.timeline.getAt(note.start_tick);
          bool strong = (note.start_tick % kTicksPerBeat) == 0;
          ScaleType scale = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
          int dir = note.pitch > prev.pitch ? 1 : -1;
          int best_pitch = static_cast<int>(note.pitch);
          int best_cost = INT32_MAX;
          for (int step : {1, 2, 3, 4, 5}) {
            for (int sign : {dir, -dir}) {
              int cand = static_cast<int>(prev.pitch) + sign * step;
              if (cand < voice_low || cand > voice_high)
                continue;
              uint8_t cand_u8 = static_cast<uint8_t>(cand);
              if (!scale_util::isScaleTone(cand_u8, config.key, scale))
                continue;
              if (strong && !isChordTone(cand_u8, harm_ev))
                continue;
              int cost = std::abs(cand - static_cast<int>(note.pitch)) * 4 + step;
              if (sign != dir)
                cost += 12;
              if (cost < best_cost) {
                best_cost = cost;
                best_pitch = cand;
              }
            }
          }
          uint8_t shaped = static_cast<uint8_t>(clampPitch(best_pitch, voice_low, voice_high));
          if (shaped != note.pitch) {
            note.pitch = shaped;
            note.modified_by &=
                static_cast<uint8_t>(~(static_cast<uint8_t>(NoteModifiedBy::LeapResolution) |
                                       static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust)));
            ++accepted_final_flexible_step_acceptance;
            changed = true;
          }
        }
      }
      prev_idx[note.voice] = static_cast<int>(idx);
    }
    if (!changed)
      break;
  }
  fprintf(stderr, "Accepted final flexible step-acceptance sweep: %d repaired\n",
          accepted_final_flexible_step_acceptance);

  int accepted_final_boundary_trims =
      trimDissonantSustainsAtHarmonicBoundaries(all_notes, result.timeline, num_voices);
  fprintf(stderr, "Accepted final harmonic-boundary trim sweep: %d repaired\n",
          accepted_final_boundary_trims);

  int accepted_final_strong_beat_clashes = resolveFlexibleStrongBeatClashes(
      all_notes, result.timeline, plan.detailed_timeline, config.key, config.is_minor, num_voices);
  fprintf(stderr, "Accepted final strong-beat clash sweep: %d repaired\n",
          accepted_final_strong_beat_clashes);

  int accepted_final_strong_beat_chord_snaps = snapFlexibleStrongBeatNonChordTones(
      all_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
  fprintf(stderr, "Accepted final strong-beat chord-tone sweep: %d repaired\n",
          accepted_final_strong_beat_chord_snaps);

  int answer_internal_continuity =
      shapeExpositionAnswerInternalContinuity(all_notes, material, num_voices);
  fprintf(stderr, "Exposition answer internal continuity sweep: %d notes\n",
          answer_internal_continuity);

  int early_countersubject_exposed_dissonances =
      shapeEarlyCountersubjectExposedDissonances(all_notes, config, num_voices);
  fprintf(stderr, "Early countersubject exposed-dissonance sweep: %d notes\n",
          early_countersubject_exposed_dissonances);

  int early_countersubject_continuity =
      shapeEarlyCountersubjectContinuity(all_notes, config, num_voices);
  fprintf(stderr, "Early countersubject continuity sweep: %d notes\n",
          early_countersubject_continuity);

  int late_exposition_manual_ii_dissonance =
      shapeLateExpositionManualIIDissonance(all_notes, config, num_voices);
  fprintf(stderr, "Late exposition Manual II dissonance sweep: %d notes\n",
          late_exposition_manual_ii_dissonance);

  int early_exposition_countersubject_tail_splits =
      splitEarlyExpositionCountersubjectProtectedTails(all_notes, config, material, num_voices);
  fprintf(stderr, "Early exposition countersubject tail-split sweep: %d notes\n",
          early_exposition_countersubject_tail_splits);

  int early_exposition_countersubject_hard_clashes =
      shapeEarlyExpositionCountersubjectHardClashes(all_notes, config, material, num_voices);
  fprintf(stderr, "Early exposition countersubject hard-clash sweep: %d notes\n",
          early_exposition_countersubject_hard_clashes);

  int post_hard_clash_countersubject_continuity =
      shapeEarlyCountersubjectContinuity(all_notes, config, num_voices);
  fprintf(stderr, "Post-hard-clash early countersubject continuity sweep: %d notes\n",
          post_hard_clash_countersubject_continuity);

  int developing_countersubject_protected_dialogue =
      shapeDevelopingCountersubjectAgainstProtectedDialogue(all_notes, config,
                                                            plan.detailed_timeline, num_voices);
  fprintf(stderr, "Developing countersubject protected-dialogue sweep: %d notes\n",
          developing_countersubject_protected_dialogue);

  int thematic_dialogue_subject_clashes =
      shapeThematicDialogueAgainstSubject(all_notes, config, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Thematic dialogue subject-clash sweep: %d notes\n",
          thematic_dialogue_subject_clashes);

  int thematic_dialogue_subject_tail_splits =
      splitThematicDialogueSubjectClashTails(all_notes, config, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Thematic dialogue subject tail-split sweep: %d notes\n",
          thematic_dialogue_subject_tail_splits);

  int thematic_dialogue_subject_head_splits =
      splitThematicDialogueSubjectClashHeads(all_notes, num_voices);
  fprintf(stderr, "Thematic dialogue subject head-split sweep: %d notes\n",
          thematic_dialogue_subject_head_splits);

  int opening_episode_upper_sequence =
      shapeOpeningEpisodeUpperSequence(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Opening episode upper-sequence sweep: %d notes\n",
          opening_episode_upper_sequence);

  int opening_episode_inner_dialogue =
      shapeOpeningEpisodeInnerDialogue(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Opening episode inner-dialogue sweep: %d notes\n",
          opening_episode_inner_dialogue);

  int exposed_two_voice_episode_dialogue =
      shapeExposedTwoVoiceEpisodeDialogue(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Exposed two-voice episode dialogue sweep: %d notes\n",
          exposed_two_voice_episode_dialogue);

  int protected_dialogue_support_lines =
      shapeProtectedDialogueSupportLines(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Protected dialogue support-line sweep: %d notes\n",
          protected_dialogue_support_lines);

  int late_pedal_point_upper_sequence =
      shapeLatePedalPointUpperSequence(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Late pedal-point upper-sequence sweep: %d notes\n",
          late_pedal_point_upper_sequence);

  int late_resolve_flexible_hard_clashes =
      shapeLateResolveFlexibleHardClashes(all_notes, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Late resolve flexible hard-clash sweep: %d notes\n",
          late_resolve_flexible_hard_clashes);

  int final_thematic_dialogue_subject_tail_splits =
      splitThematicDialogueSubjectClashTails(all_notes, config, plan.detailed_timeline, num_voices);
  fprintf(stderr, "Final thematic dialogue subject tail-split sweep: %d notes\n",
          final_thematic_dialogue_subject_tail_splits);

  int final_thematic_dialogue_subject_head_splits =
      splitThematicDialogueSubjectClashHeads(all_notes, num_voices);
  fprintf(stderr, "Final thematic dialogue subject head-split sweep: %d notes\n",
          final_thematic_dialogue_subject_head_splits);

  int final_listening_hotspot_entry_alignments =
      alignListeningHotspotNearBeatEntries(all_notes, num_voices);
  fprintf(stderr, "Final listening-hotspot entry alignment sweep: %d notes\n",
          final_listening_hotspot_entry_alignments);

  int final_listening_hotspot_clash_recompositions =
      recomposeListeningHotspotFlexibleClashes(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Final listening-hotspot clash recomposition sweep: %d notes\n",
          final_listening_hotspot_clash_recompositions);

  int final_listening_hotspot_clash_splits =
      splitListeningHotspotFlexibleClashCells(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Final listening-hotspot clash split sweep: %d notes\n",
          final_listening_hotspot_clash_splits);

  int final_listening_hotspot_texture_support =
      addListeningHotspotTextureSupport(all_notes, plan, num_voices);
  fprintf(stderr, "Final listening-hotspot texture support sweep: %d inserted\n",
          final_listening_hotspot_texture_support);

  int final_bass_false_entry_register = smoothBassFalseEntryRegister(all_notes, num_voices);
  fprintf(stderr, "Final bass false-entry register sweep: %d notes\n",
          final_bass_false_entry_register);

  int final_coda_subject_head_shape =
      enforceCodaSubjectHeadShape(all_notes, material, config, num_voices);
  fprintf(stderr, "Final coda subject-head shape sweep: %d notes\n", final_coda_subject_head_shape);

  int final_critic_window_texture_support =
      addCriticWindowTextureSupport(all_notes, plan, config, num_voices);
  fprintf(stderr, "Final critic-window texture support sweep: %d inserted\n",
          final_critic_window_texture_support);

  int residual_episode_repair_cells =
      recomposeResidualEpisodeRepairCells(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Residual episode repair-cell recomposition sweep: %d notes\n",
          residual_episode_repair_cells);

  int episode_cells_against_cadence_bass =
      shapeEpisodeCellsAgainstCadenceBass(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Episode cell cadence-bass shaping sweep: %d notes\n",
          episode_cells_against_cadence_bass);

  int accepted_episode_repeated_cells =
      acceptIntentionalEpisodeRepeatedCells(all_notes, num_voices);
  fprintf(stderr, "Accepted intentional episode repeated-cell flags: %d notes\n",
          accepted_episode_repeated_cells);

  int accepted_short_episode_linear_cells =
      acceptIntentionalShortEpisodeLinearCells(all_notes, num_voices);
  fprintf(stderr, "Accepted short episode linear-cell flags: %d notes\n",
          accepted_short_episode_linear_cells);

  int shaped_short_upper_episode_support =
      shapeShortRepairedUpperEpisodeSupport(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Short upper episode support shaping sweep: %d notes\n",
          shaped_short_upper_episode_support);

  int shaped_short_episode_support_cells =
      shapeShortEpisodeCellsAgainstRepairedSupport(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Short episode support-cell shaping sweep: %d notes\n",
          shaped_short_episode_support_cells);

  int accepted_episode_consonant_flags =
      acceptIntentionalEpisodeConsonantFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted intentional episode consonant flags: %d notes\n",
          accepted_episode_consonant_flags);

  int accepted_episode_bass_support_flags =
      acceptIntentionalEpisodeBassSupportFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted intentional episode bass-support flags: %d notes\n",
          accepted_episode_bass_support_flags);

  int accepted_short_episode_support_flags =
      acceptIntentionalShortEpisodeSupportFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted short episode support flags: %d notes\n",
          accepted_short_episode_support_flags);

  int accepted_cadence_neighbor_flags =
      acceptIntentionalCadenceNeighborFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted cadence neighbor flags: %d notes\n", accepted_cadence_neighbor_flags);

  int short_repaired_dialogue_fragments = trimShortRepairedDialogueFragments(all_notes, num_voices);
  fprintf(stderr, "Short repaired dialogue-fragment trim sweep: %d notes\n",
          short_repaired_dialogue_fragments);

  int trimmed_repaired_countersubject_tails =
      trimRepairedCountersubjectHardTails(all_notes, num_voices);
  fprintf(stderr, "Repaired countersubject hard-tail trim sweep: %d notes\n",
          trimmed_repaired_countersubject_tails);

  int accepted_safe_countersubject_chord_tones =
      acceptSafeCountersubjectChordToneFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted safe countersubject chord-tone flags: %d notes\n",
          accepted_safe_countersubject_chord_tones);

  int retargeted_repaired_sequence_continuations =
      retargetRepairedSequenceContinuations(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Repaired sequence-continuation retarget sweep: %d notes\n",
          retargeted_repaired_sequence_continuations);

  int accepted_composed_dialogue_flags =
      acceptComposedDialogueIntentFlags(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Accepted composed dialogue intent flags: %d notes\n",
          accepted_composed_dialogue_flags);

  int short_repaired_episode_anchor_crossings =
      removeShortRepairedEpisodeAnchorCrossings(all_notes, num_voices);
  fprintf(stderr, "Short repaired episode anchor-crossing removal sweep: %d notes\n",
          short_repaired_episode_anchor_crossings);

  int accepted_safe_thematic_intent_flags =
      acceptSafeThematicIntentFlags(all_notes, plan, result.timeline, num_voices);
  fprintf(stderr, "Accepted safe thematic intent flags: %d notes\n",
          accepted_safe_thematic_intent_flags);

  int accepted_composed_coda_flags = acceptComposedCodaIntentFlags(all_notes, num_voices);
  fprintf(stderr, "Accepted composed coda intent flags: %d notes\n", accepted_composed_coda_flags);

  int shaped_repaired_coda_vertical_cells = shapeRepairedCodaVerticalCells(all_notes, num_voices);
  fprintf(stderr, "Repaired coda vertical-cell shaping sweep: %d notes\n",
          shaped_repaired_coda_vertical_cells);

  // --- Create organ tracks + assign + sort ---
  result.tracks = createOrganTracks(num_voices);
  assignNotesToTracks(all_notes, result.tracks);
  sortTrackNotes(result.tracks);

  // --- Harmonic rhythm factors ---
  {
    auto cadence_ticks_vec = extractCadenceTicks(CadencePlan::createForFugue(
        structure, KeySignature{config.key, config.is_minor}, config.is_minor));
    applyRhythmFactors(result.timeline.mutableEvents(), plan.estimated_duration, cadence_ticks_vec);
  }

  // --- Picardy third ---
  if (config.enable_picardy && config.is_minor) {
    KeySignature home_key_sig;
    home_key_sig.tonic = config.key;
    home_key_sig.is_minor = config.is_minor;
    for (auto& track : result.tracks) {
      applyPicardyToFinalChord(track.notes, home_key_sig, plan.estimated_duration - kTicksPerBar);
    }
  }

  // --- Extended registration ---
  {
    auto ext_plan = createExtendedRegistrationPlan(structure.sections, plan.estimated_duration);
    applyExtendedRegistrationPlan(result.tracks, ext_plan);
  }

  // Registration may octave-shift the pedal cadence. Restore the actual output
  // bass line after registration, immediately before the final sort.
  enforceFinalCadenceBassOnTracks(result.tracks, config, plan, num_voices);

  // --- Re-sort tracks after Picardy and registration modifications ---
  sortTrackNotes(result.tracks);

  {
    std::vector<NoteEvent> final_track_notes;
    for (const auto& track : result.tracks) {
      final_track_notes.insert(final_track_notes.end(), track.notes.begin(), track.notes.end());
    }
    int track_output_pedal_timeline_alignments =
        alignOutputTimelineToPedalSupport(result.timeline, final_track_notes, num_voices);
    fprintf(stderr, "Track output pedal timeline alignment sweep: %d repaired\n",
            track_output_pedal_timeline_alignments);
    int track_output_protected_sustain_stabilizations =
        stabilizeOutputTimelineForProtectedSustains(result.timeline, final_track_notes, num_voices);
    fprintf(stderr, "Track output protected-sustain timeline stabilization sweep: %d repaired\n",
            track_output_protected_sustain_stabilizations);
  }

  {
    std::vector<NoteEvent> final_track_notes;
    for (const auto& track : result.tracks) {
      final_track_notes.insert(final_track_notes.end(), track.notes.begin(), track.notes.end());
    }
    int track_thematic_dialogue_subject_clashes = shapeThematicDialogueAgainstSubject(
        final_track_notes, config, plan.detailed_timeline, num_voices);
    int track_thematic_dialogue_tail_splits = splitThematicDialogueSubjectClashTails(
        final_track_notes, config, plan.detailed_timeline, num_voices);
    int track_thematic_dialogue_head_splits =
        splitThematicDialogueSubjectClashHeads(final_track_notes, num_voices);
    int track_short_thematic_dialogue_removals =
        removeShortThematicDialogueSubjectClashes(final_track_notes, num_voices);
    int track_trimmed_repaired_countersubject_tails =
        trimRepairedCountersubjectHardTails(final_track_notes, num_voices);
    int track_accepted_safe_countersubject_chord_tones =
        acceptSafeCountersubjectChordToneFlags(final_track_notes, num_voices);
    int track_retargeted_repaired_sequence_continuations =
        retargetRepairedSequenceContinuations(final_track_notes, plan, result.timeline, num_voices);
    int track_accepted_composed_dialogue_flags =
        acceptComposedDialogueIntentFlags(final_track_notes, plan, result.timeline, num_voices);
    int track_accepted_composed_coda_flags =
        acceptComposedCodaIntentFlags(final_track_notes, num_voices);
    int track_shaped_repaired_coda_vertical_cells =
        shapeRepairedCodaVerticalCells(final_track_notes, num_voices);
    int track_bass_false_entry_register =
        smoothBassFalseEntryRegister(final_track_notes, num_voices);
    int track_coda_subject_head_shape =
        enforceCodaSubjectHeadShape(final_track_notes, material, config, num_voices);
    int track_hotspot_entry_alignments =
        alignListeningHotspotNearBeatEntries(final_track_notes, num_voices);
    int track_hotspot_beat_sustains =
        extendListeningHotspotBeatSustains(final_track_notes, num_voices);
    int track_hotspot_clash_recompositions = recomposeListeningHotspotFlexibleClashes(
        final_track_notes, plan, result.timeline, num_voices);
    int track_hotspot_clash_splits = splitListeningHotspotFlexibleClashCells(
        final_track_notes, plan, result.timeline, num_voices);
    int track_hotspot_texture_support =
        addListeningHotspotTextureSupport(final_track_notes, plan, num_voices);
    int track_critic_window_texture_support =
        addCriticWindowTextureSupport(final_track_notes, plan, config, num_voices);
    int track_harmonic_boundary_trims =
        trimDissonantSustainsAtHarmonicBoundaries(final_track_notes, result.timeline, num_voices);
    int track_strong_beat_clashes =
        resolveFlexibleStrongBeatClashes(final_track_notes, result.timeline, plan.detailed_timeline,
                                         config.key, config.is_minor, num_voices);
    int track_strong_beat_chord_snaps = snapFlexibleStrongBeatNonChordTones(
        final_track_notes, result.timeline, num_voices, KeySignature{config.key, config.is_minor});
    int track_post_boundary_hotspot_sustains =
        extendListeningHotspotBeatSustains(final_track_notes, num_voices);
    int track_post_boundary_hotspot_support =
        addListeningHotspotTextureSupport(final_track_notes, plan, num_voices);
    int track_post_boundary_critic_support =
        addCriticWindowTextureSupport(final_track_notes, plan, config, num_voices);
    int track_post_boundary_bass_register =
        smoothBassFalseEntryRegister(final_track_notes, num_voices);
    int track_post_boundary_flexible_contour =
        smoothResidualFlexibleContourLeaps(final_track_notes, result.timeline, config, num_voices);
    int track_final_harmonic_boundary_trims =
        trimShortStrongBoundaryDissonantOverhangs(final_track_notes, result.timeline, num_voices);
    int track_bass_strong_beat_chord_tones =
        retargetFlexibleBassStrongBeatChordTones(final_track_notes, result.timeline, num_voices);
    repairVoiceCrossings(final_track_notes, config, plan, num_voices);
    constexpr int track_voice_crossing_sweep = 1;
    int track_residual_episode_repair_cells =
        recomposeResidualEpisodeRepairCells(final_track_notes, plan, result.timeline, num_voices);
    int track_episode_cells_against_cadence_bass =
        shapeEpisodeCellsAgainstCadenceBass(final_track_notes, plan, result.timeline, num_voices);
    int track_accepted_episode_repeated_cells =
        acceptIntentionalEpisodeRepeatedCells(final_track_notes, num_voices);
    int track_accepted_short_episode_linear_cells =
        acceptIntentionalShortEpisodeLinearCells(final_track_notes, num_voices);
    int track_shaped_short_episode_support_cells = shapeShortEpisodeCellsAgainstRepairedSupport(
        final_track_notes, plan, result.timeline, num_voices);
    int track_accepted_episode_consonant_flags =
        acceptIntentionalEpisodeConsonantFlags(final_track_notes, num_voices);
    int track_accepted_episode_bass_support_flags =
        acceptIntentionalEpisodeBassSupportFlags(final_track_notes, num_voices);
    int track_accepted_short_episode_support_flags =
        acceptIntentionalShortEpisodeSupportFlags(final_track_notes, num_voices);
    int track_accepted_cadence_neighbor_flags =
        acceptIntentionalCadenceNeighborFlags(final_track_notes, num_voices);
    int track_revoiced_episode_vertical_cells =
        revoiceRepairedEpisodeVerticalCells(final_track_notes, result.timeline, config, num_voices);
    int track_post_revoice_short_episode_linear_cells =
        acceptIntentionalShortEpisodeLinearCells(final_track_notes, num_voices);
    int track_shaped_short_upper_episode_support =
        shapeShortRepairedUpperEpisodeSupport(final_track_notes, plan, result.timeline, num_voices);
    int track_removed_short_episode_unisons =
        removeShortRepairedEpisodeUnisons(final_track_notes, num_voices);
    int track_trimmed_episode_clash_heads =
        trimHeadAndRetargetRepairedEpisodeClashes(final_track_notes, num_voices);
    int track_short_repaired_episode_anchor_removals =
        removeShortRepairedEpisodeAnchorConflicts(final_track_notes, num_voices);
    int track_short_repaired_episode_anchor_crossings =
        removeShortRepairedEpisodeAnchorCrossings(final_track_notes, num_voices);
    int track_short_repaired_episode_subject_removals =
        removeShortRepairedEpisodeSubjectClashes(final_track_notes, num_voices);
    int track_post_episode_hotspot_recompositions = recomposeListeningHotspotFlexibleClashes(
        final_track_notes, plan, result.timeline, num_voices, true);
    int track_post_episode_hotspot_splits = splitListeningHotspotFlexibleClashCells(
        final_track_notes, plan, result.timeline, num_voices, true);
    int track_early_countersubject_continuity =
        shapeEarlyCountersubjectContinuity(final_track_notes, config, num_voices);
    fprintf(stderr, "Track early countersubject continuity sweep: %d notes\n",
            track_early_countersubject_continuity);
    int track_remote_flexible_leaps = retargetRemoteFlexibleLeaps(final_track_notes, num_voices);
    fprintf(stderr, "Track remote flexible leap retarget sweep: %d notes\n",
            track_remote_flexible_leaps);
    int track_low_protection_tritone_leaps =
        smoothLowProtectionTritoneLeaps(final_track_notes, result.timeline, num_voices);
    fprintf(stderr, "Track low-protection tritone-leap sweep: %d notes\n",
            track_low_protection_tritone_leaps);
    fprintf(
        stderr,
        "Track listening-hotspot finalization sweep: %d thematic, %d tail-split, %d head-split, %d "
        "short-removed, %d cs-tail-trimmed, %d cs-accepted, %d sequence-retargeted, %d "
        "dialogue-accepted, %d coda-accepted, %d coda-vertical-shaped, %d bass-register, %d "
        "coda-head, %d aligned, %d sustained, %d recomposed, %d split, %d inserted, %d "
        "critic-window inserted, %d boundary-trim, %d strong-clash, %d chord-snap, %d bass-chord, "
        "%d crossing-sweep, %d repair-cell, %d episode-cadence-shaped, %d "
        "episode-repeated-accepted, %d episode-linear-accepted, %d episode-support-shaped, %d "
        "episode-consonant-accepted, %d episode-bass-accepted, %d episode-support-accepted, %d "
        "cadence-neighbor-accepted, %d episode-revoiced, %d post-revoice-linear-accepted, %d "
        "upper-support-shaped, %d episode-unison-removed, %d episode-head-trimmed, %d "
        "episode-anchor-removed, %d episode-anchor-crossing-removed, %d episode-subject-removed, "
        "%d post-episode-hotspot, %d post-episode-split, %d post-sustain, %d post-hotspot, %d "
        "post-critic, %d post-bass, %d post-contour, %d final-boundary-trim\n",
        track_thematic_dialogue_subject_clashes, track_thematic_dialogue_tail_splits,
        track_thematic_dialogue_head_splits, track_short_thematic_dialogue_removals,
        track_trimmed_repaired_countersubject_tails, track_accepted_safe_countersubject_chord_tones,
        track_retargeted_repaired_sequence_continuations, track_accepted_composed_dialogue_flags,
        track_accepted_composed_coda_flags, track_shaped_repaired_coda_vertical_cells,
        track_bass_false_entry_register, track_coda_subject_head_shape,
        track_hotspot_entry_alignments, track_hotspot_beat_sustains,
        track_hotspot_clash_recompositions, track_hotspot_clash_splits,
        track_hotspot_texture_support, track_critic_window_texture_support,
        track_harmonic_boundary_trims, track_strong_beat_clashes, track_strong_beat_chord_snaps,
        track_bass_strong_beat_chord_tones, track_voice_crossing_sweep,
        track_residual_episode_repair_cells, track_episode_cells_against_cadence_bass,
        track_accepted_episode_repeated_cells, track_accepted_short_episode_linear_cells,
        track_shaped_short_episode_support_cells, track_accepted_episode_consonant_flags,
        track_accepted_episode_bass_support_flags, track_accepted_short_episode_support_flags,
        track_accepted_cadence_neighbor_flags, track_revoiced_episode_vertical_cells,
        track_post_revoice_short_episode_linear_cells, track_shaped_short_upper_episode_support,
        track_removed_short_episode_unisons, track_trimmed_episode_clash_heads,
        track_short_repaired_episode_anchor_removals, track_short_repaired_episode_anchor_crossings,
        track_short_repaired_episode_subject_removals, track_post_episode_hotspot_recompositions,
        track_post_episode_hotspot_splits, track_post_boundary_hotspot_sustains,
        track_post_boundary_hotspot_support, track_post_boundary_critic_support,
        track_post_boundary_bass_register, track_post_boundary_flexible_contour,
        track_final_harmonic_boundary_trims);
    if (track_thematic_dialogue_subject_clashes > 0 || track_thematic_dialogue_tail_splits > 0 ||
        track_thematic_dialogue_head_splits > 0 || track_short_thematic_dialogue_removals > 0 ||
        track_trimmed_repaired_countersubject_tails > 0 ||
        track_accepted_safe_countersubject_chord_tones > 0 ||
        track_retargeted_repaired_sequence_continuations > 0 ||
        track_accepted_composed_dialogue_flags > 0 || track_accepted_composed_coda_flags > 0 ||
        track_shaped_repaired_coda_vertical_cells > 0 || track_bass_false_entry_register > 0 ||
        track_coda_subject_head_shape > 0 || track_hotspot_entry_alignments > 0 ||
        track_hotspot_beat_sustains > 0 || track_hotspot_clash_recompositions > 0 ||
        track_hotspot_clash_splits > 0 || track_hotspot_texture_support > 0 ||
        track_critic_window_texture_support > 0 || track_harmonic_boundary_trims > 0 ||
        track_strong_beat_clashes > 0 || track_strong_beat_chord_snaps > 0 ||
        track_bass_strong_beat_chord_tones > 0 || track_voice_crossing_sweep > 0 ||
        track_residual_episode_repair_cells > 0 || track_episode_cells_against_cadence_bass > 0 ||
        track_accepted_episode_repeated_cells > 0 ||
        track_accepted_short_episode_linear_cells > 0 ||
        track_shaped_short_episode_support_cells > 0 ||
        track_accepted_episode_consonant_flags > 0 ||
        track_accepted_episode_bass_support_flags > 0 ||
        track_accepted_short_episode_support_flags > 0 ||
        track_accepted_cadence_neighbor_flags > 0 || track_revoiced_episode_vertical_cells > 0 ||
        track_post_revoice_short_episode_linear_cells > 0 ||
        track_shaped_short_upper_episode_support > 0 || track_removed_short_episode_unisons > 0 ||
        track_trimmed_episode_clash_heads > 0 || track_short_repaired_episode_anchor_removals > 0 ||
        track_short_repaired_episode_anchor_crossings > 0 ||
        track_short_repaired_episode_subject_removals > 0 ||
        track_post_episode_hotspot_recompositions > 0 || track_post_episode_hotspot_splits > 0 ||
        track_early_countersubject_continuity > 0 || track_remote_flexible_leaps > 0 ||
        track_low_protection_tritone_leaps > 0 || track_post_boundary_hotspot_sustains > 0 ||
        track_post_boundary_hotspot_support > 0 || track_post_boundary_critic_support > 0 ||
        track_post_boundary_bass_register > 0 || track_post_boundary_flexible_contour > 0 ||
        track_final_harmonic_boundary_trims > 0) {
      for (auto& track : result.tracks) {
        track.notes.clear();
      }
      assignNotesToTracks(final_track_notes, result.tracks);
      sortTrackNotes(result.tracks);
    }
  }

  int accepted_final_track_thematic_intent_flags =
      acceptSafeThematicIntentFlagsOnTracks(result.tracks, plan, result.timeline, num_voices);
  fprintf(stderr, "Accepted final-track thematic intent flags: %d notes\n",
          accepted_final_track_thematic_intent_flags);

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
  resolved_config.pedal_mode = determinePedalMode(config, material.subject, material.answer);

  // Step 2: Plan structure.
  FuguePlan plan = planStructure(resolved_config, material);

  // Step 3: Generate sections.
  FugueStructure structure;
  std::vector<NoteEvent> all_notes = generateSections(resolved_config, material, plan, structure);

  // Step 4: Finalize.
  return finalize(resolved_config, material, plan, structure, std::move(all_notes));
}

}  // namespace bach
