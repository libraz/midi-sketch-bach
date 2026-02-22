/// @file
/// @brief Countersubject generation with contrary motion, complementary
/// rhythm, and consonant interval selection.

#include "fugue/countersubject.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <random>

#include "core/interval.h"
#include "core/markov_tables.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/archetype_policy.h"
#include "fugue/fugue_config.h"

namespace bach {

// ---------------------------------------------------------------------------
// Countersubject member functions
// ---------------------------------------------------------------------------

size_t Countersubject::noteCount() const { return notes.size(); }

uint8_t Countersubject::lowestPitch() const {
  if (notes.empty()) return 127;
  uint8_t lowest = 127;
  for (const auto& note : notes) {
    if (note.pitch < lowest) lowest = note.pitch;
  }
  return lowest;
}

uint8_t Countersubject::highestPitch() const {
  if (notes.empty()) return 0;
  uint8_t highest = 0;
  for (const auto& note : notes) {
    if (note.pitch > highest) highest = note.pitch;
  }
  return highest;
}

int Countersubject::range() const {
  if (notes.empty()) return 0;
  return static_cast<int>(highestPitch()) - static_cast<int>(lowestPitch());
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// @brief Standard note duration constants.
constexpr Tick kHalfNote = kTicksPerBeat * 2;
constexpr Tick kQuarterNote = kTicksPerBeat;
constexpr Tick kEighthNote = kTicksPerBeat / 2;

/// @brief Parameters that shape countersubject generation per character.
struct CSCharacterParams {
  float leap_prob;       // Probability of a leap (vs step) in motion
  int max_range;         // Maximum pitch range in semitones
  float long_split_prob; // Probability of splitting long notes into shorter
};

/// @brief Return generation parameters based on the subject character.
///
/// Applies small RNG-driven variation to each parameter so that different
/// seeds produce subtly different countersubject behaviour even for the
/// same SubjectCharacter.
///
/// @param character The SubjectCharacter of the accompanying subject.
/// @param rng Mersenne Twister for per-seed variation.
/// @return CSCharacterParams with leap probability, range, and split rate.
CSCharacterParams getCSParams(SubjectCharacter character, std::mt19937& rng) {
  CSCharacterParams params;
  switch (character) {
    case SubjectCharacter::Severe:
      params = {0.10f, 10, 0.6f};
      break;
    case SubjectCharacter::Playful:
      params = {0.40f, 14, 0.7f};
      break;
    case SubjectCharacter::Noble:
      params = {0.20f, 12, 0.5f};
      break;
    case SubjectCharacter::Restless:
      params = {0.35f, 14, 0.75f};
      break;
    default:
      params = {0.15f, 10, 0.6f};
      break;
  }

  // Apply small per-seed variation (Principle 3: fewer choices, subtle variety).
  params.leap_prob = std::clamp(params.leap_prob + rng::rollFloat(rng, -0.05f, 0.05f),
                                0.0f, 1.0f);
  params.max_range = std::max(4, params.max_range + rng::rollRange(rng, -1, 1));
  params.long_split_prob = std::clamp(
      params.long_split_prob + rng::rollFloat(rng, -0.05f, 0.05f), 0.0f, 1.0f);

  return params;
}

/// @brief Apply archetype-specific constraints to countersubject params.
void applyArchetypeToCSParams(CSCharacterParams& params,
                               const ArchetypePolicy& policy) {
  // Cantabile: smoother melodic flow — reduce leaps, constrain range.
  if (policy.min_step_ratio > 0.5f) {
    params.leap_prob = std::min(params.leap_prob, 1.0f - policy.min_step_ratio);
  }
  if (policy.max_sixteenth_density < 0.3f) {
    params.long_split_prob = std::min(params.long_split_prob, 0.5f);
  }

  // Compact: more fragmentable motifs — encourage shorter note values.
  if (policy.require_fragmentable) {
    params.long_split_prob = std::max(params.long_split_prob, 0.65f);
  }

  // Invertible: keep range moderate for double counterpoint at the octave.
  if (policy.require_invertible) {
    params.max_range = std::min(params.max_range, 12);
  }

  // Chromatic: stepwise CS for harmonic grounding against chromatic subject.
  if (policy.require_functional_resolution) {
    params.leap_prob = std::min(params.leap_prob, 0.15f);
  }
}

/// @brief Minimum pitch for countersubject voices (C3).
constexpr uint8_t kCSPitchLow = 48;
/// @brief Maximum pitch for countersubject voices (C6).
constexpr uint8_t kCSPitchHigh = 96;

/// @brief Find a consonant pitch relative to the subject pitch.
///
/// Selects from imperfect consonances (3rds, 6ths) preferentially, placing
/// the countersubject note in the given direction (above or below) the
/// subject pitch. The result is snapped to the nearest scale tone and
/// clamped to a valid range.
///
/// @param subject_pitch The current subject MIDI pitch.
/// @param direction +1 for above, -1 for below the subject.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @param key Musical key for diatonic enforcement.
/// @param scale Scale type for diatonic enforcement.
/// @param gen Random number generator.
/// @return A consonant, diatonic MIDI pitch, or snapped fallback.
uint8_t findConsonantPitch(uint8_t subject_pitch, int direction,
                           uint8_t low_range, uint8_t high_range,
                           Key key, ScaleType scale,
                           std::mt19937& gen) {
  // Shuffle through imperfect consonances in the preferred direction.
  constexpr auto kImperfectCount =
      static_cast<int>(std::size(interval_util::kImperfectConsonantIntervals));
  int offsets[kImperfectCount];
  for (int idx = 0; idx < kImperfectCount; ++idx) {
    offsets[idx] = interval_util::kImperfectConsonantIntervals[idx] * direction;
  }

  // Try each offset; pick the first one that stays in range and is diatonic.
  // Shuffle order for variety.
  for (int idx = kImperfectCount - 1; idx > 0; --idx) {
    int jdx = rng::rollRange(gen, 0, idx);
    std::swap(offsets[idx], offsets[jdx]);
  }

  for (int idx = 0; idx < kImperfectCount; ++idx) {
    int candidate = static_cast<int>(subject_pitch) + offsets[idx];
    uint8_t snapped = scale_util::nearestScaleTone(
        clampPitch(candidate, 0, 127), key, scale);
    if (snapped >= low_range && snapped <= high_range) {
      return snapped;
    }
  }

  // Fallback: try all consonances with diatonic snap.
  for (int consonant_interval : interval_util::kConsonantIntervals) {
    int candidate = static_cast<int>(subject_pitch) +
                    consonant_interval * direction;
    uint8_t snapped = scale_util::nearestScaleTone(
        clampPitch(candidate, 0, 127), key, scale);
    if (snapped >= low_range && snapped <= high_range) {
      return snapped;
    }
  }

  // Last resort: minor 3rd in the given direction, snapped and clamped.
  int fallback = static_cast<int>(subject_pitch) + 3 * direction;
  uint8_t snapped = scale_util::nearestScaleTone(
      clampPitch(fallback, 0, 127), key, scale);
  return clampPitch(static_cast<int>(snapped), low_range, high_range);
}

/// @brief Generate complementary duration for a countersubject note.
///
/// Long subject notes get shorter countersubject notes (more motion), and
/// short subject notes get longer countersubject notes (sustained contrast).
///
/// @param subject_duration Duration of the corresponding subject note.
/// @param split_prob Probability of splitting long notes (character-based).
/// @param gen Random number generator.
/// @return Complementary duration in ticks.
[[maybe_unused]] Tick complementaryDuration(Tick subject_duration, float split_prob,
                                            std::mt19937& gen) {
  if (subject_duration >= kHalfNote) {
    // Long subject note -> shorter CS notes for rhythmic contrast.
    if (rng::rollProbability(gen, split_prob)) {
      return kEighthNote;
    }
    return kQuarterNote;
  }
  if (subject_duration <= kEighthNote) {
    // Short subject note -> longer CS note for contrast.
    if (rng::rollProbability(gen, split_prob)) {
      return kHalfNote;
    }
    return kQuarterNote;
  }
  // Quarter note in subject -> vary the CS duration.
  if (rng::rollProbability(gen, 0.5f)) {
    return kEighthNote;
  }
  return kHalfNote;
}

/// Select countersubject duration with style-aware constraints.
/// 16th notes restricted to Playful/Restless characters only.
Tick selectCSDuration(Tick subject_duration, float split_prob,
                      Tick tick, std::mt19937& gen,
                      const MarkovModel* markov = nullptr,
                      DurCategory prev_dur_cat = DurCategory::Qtr,
                      DirIntervalClass dir_ivl = DirIntervalClass::StepUp) {
  // Map split_prob [0.5, 0.75] -> energy [0.4, 0.7].
  // Severe (0.6) -> 0.52, Noble (0.5) -> 0.40
  // Playful (0.7) -> 0.64, Restless (0.75) -> 0.70
  float energy = 0.4f + (split_prob - 0.5f) * 1.2f;
  if (energy < 0.3f) energy = 0.3f;
  if (energy > 0.75f) energy = 0.75f;

  // Use Countersubject voice profile (default: voice 1 in 4-voice texture).
  VoiceProfile cs_profile = getVoiceProfile(TextureFunction::Countersubject, 1, 4);
  Tick dur = FugueEnergyCurve::selectDuration(energy, tick, gen, subject_duration,
                                               cs_profile,
                                               false, 1.0f, false,
                                               markov, prev_dur_cat, dir_ivl);

  // 16th note restriction: forbidden when energy < 0.6 (Severe, Noble).
  // Baroque practice: CS sixteenths only in Playful/Restless contexts.
  if (dur < kTicksPerBeat / 2 && energy < 0.6f) {
    dur = kTicksPerBeat / 2;  // Floor at eighth note.
  }

  return dur;
}

/// @brief Determine the motion direction for the countersubject.
///
/// Primary goal: contrary motion. When the subject ascends, the
/// countersubject descends, and vice versa. When the subject holds
/// (oblique), we introduce gentle motion.
///
/// @param subject_direction Direction of the subject motion (+, -, or 0).
/// @param gen Random number generator for oblique case.
/// @return +1 (ascending) or -1 (descending).
int contraryDirection(int subject_direction, std::mt19937& gen) {
  if (subject_direction > 0) return -1;  // Subject up -> CS down
  if (subject_direction < 0) return 1;   // Subject down -> CS up
  // Oblique: subject holds, CS moves freely.
  return rng::rollProbability(gen, 0.5f) ? 1 : -1;
}

/// @brief Step-based motion: move current pitch by a diatonic step in the
/// given direction, staying within the scale and range.
///
/// @param current_pitch Current countersubject pitch.
/// @param direction +1 for ascending, -1 for descending.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @param key Musical key for diatonic enforcement.
/// @param scale Scale type for diatonic enforcement.
/// @return New pitch after step motion.
uint8_t stepMotion(uint8_t current_pitch, int direction,
                   uint8_t low_range, uint8_t high_range,
                   Key key, ScaleType scale) {
  // Try a major 2nd (2 semitones), check if it's diatonic in the key.
  int candidate = static_cast<int>(current_pitch) + 2 * direction;
  if (candidate >= static_cast<int>(low_range) &&
      candidate <= static_cast<int>(high_range) &&
      scale_util::isScaleTone(static_cast<uint8_t>(candidate), key, scale)) {
    return static_cast<uint8_t>(candidate);
  }

  // Try a minor 2nd (1 semitone), but only if it's diatonic.
  candidate = static_cast<int>(current_pitch) + 1 * direction;
  if (candidate >= static_cast<int>(low_range) &&
      candidate <= static_cast<int>(high_range) &&
      scale_util::isScaleTone(static_cast<uint8_t>(candidate), key, scale)) {
    return static_cast<uint8_t>(candidate);
  }

  // Snap fallback: reverse direction and snap to nearest scale tone.
  candidate = static_cast<int>(current_pitch) + 2 * (-direction);
  uint8_t snapped = scale_util::nearestScaleTone(
      clampPitch(candidate, 0, 127), key, scale);
  return clampPitch(static_cast<int>(snapped), low_range, high_range);
}

/// @brief Leap motion: move by a diatonic 3rd or 4th in the given direction.
///
/// @param current_pitch Current countersubject pitch.
/// @param direction +1 for ascending, -1 for descending.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @param key Musical key for diatonic enforcement.
/// @param scale Scale type for diatonic enforcement.
/// @param gen Random number generator.
/// @return New pitch after leap, snapped to the nearest scale tone.
uint8_t leapMotion(uint8_t current_pitch, int direction,
                   uint8_t low_range, uint8_t high_range,
                   Key key, ScaleType scale,
                   std::mt19937& gen) {
  // Choose between minor 3rd, major 3rd, perfect 4th.
  constexpr int kLeapSizes[] = {3, 4, 5};
  constexpr int kLeapCount = 3;

  int leap_idx = rng::rollRange(gen, 0, kLeapCount - 1);
  int leap = kLeapSizes[leap_idx] * direction;
  int candidate = static_cast<int>(current_pitch) + leap;

  // Snap to nearest scale tone for diatonic output.
  uint8_t snapped = scale_util::nearestScaleTone(
      clampPitch(candidate, 0, 127), key, scale);
  if (snapped >= low_range && snapped <= high_range) {
    return snapped;
  }

  // Reverse direction on range violation, snap again.
  candidate = static_cast<int>(current_pitch) - leap;
  snapped = scale_util::nearestScaleTone(
      clampPitch(candidate, 0, 127), key, scale);
  return clampPitch(static_cast<int>(snapped), low_range, high_range);
}

/// @brief Check whether two pitches form a consonant interval.
/// @param pitch_a First MIDI pitch.
/// @param pitch_b Second MIDI pitch.
/// @return True if the interval is perfect or imperfect consonance.
bool isConsonant(uint8_t pitch_a, uint8_t pitch_b) {
  return interval_util::isConsonance(
      interval_util::compoundToSimple(absoluteInterval(pitch_a, pitch_b)));
}

/// @brief Check if moving from prev_interval to curr_interval creates
/// parallel 5ths or parallel octaves.
/// @param prev_cs Previous countersubject pitch.
/// @param prev_subj Previous subject pitch.
/// @param curr_cs Current countersubject pitch.
/// @param curr_subj Current subject pitch.
/// @return True if parallel 5ths or octaves detected.
bool hasParallelFifthsOrOctaves(uint8_t prev_cs, uint8_t prev_subj,
                                uint8_t curr_cs, uint8_t curr_subj) {
  int prev_interval = absoluteInterval(prev_cs, prev_subj);
  int curr_interval = absoluteInterval(curr_cs, curr_subj);

  // Check parallel motion direction (both voices moving same direction).
  int cs_motion = static_cast<int>(curr_cs) - static_cast<int>(prev_cs);
  int subj_motion = static_cast<int>(curr_subj) - static_cast<int>(prev_subj);

  bool same_direction = (cs_motion > 0 && subj_motion > 0) ||
                        (cs_motion < 0 && subj_motion < 0);

  if (!same_direction) return false;

  return isParallelFifths(prev_interval, curr_interval) ||
         isParallelOctaves(prev_interval, curr_interval);
}

/// @brief Validate countersubject quality: count consonant strong beats.
///
/// Checks each countersubject note against the subject note sounding at
/// the same time. Returns the consonance rate on strong beats (beats 1, 3).
///
/// @param cs_notes Countersubject notes.
/// @param subject Subject to validate against.
/// @return Fraction of strong-beat consonances (0.0 to 1.0).
/// @brief Compute consonance rate between a countersubject and subject when
/// the countersubject is inverted at the octave.
///
/// Transposes CS notes by 12 semitones (octave up) and checks consonance
/// against the subject on strong beats. Bach frequently uses invertible
/// counterpoint at the octave.
///
/// @param cs_notes The countersubject notes.
/// @param subject The fugue subject.
/// @return Consonance rate (0.0-1.0) for the inverted pairing.
float validateInvertedConsonanceRate(const std::vector<NoteEvent>& cs_notes,
                                     const Subject& subject) {
  if (cs_notes.empty() || subject.notes.empty()) return 0.0f;

  int strong_beat_checks = 0;
  int consonant_count = 0;

  for (const auto& cs_note : cs_notes) {
    uint8_t beat = beatInBar(cs_note.start_tick);
    if (beat != 0 && beat != 2) continue;

    strong_beat_checks++;

    // Invert at the octave: transpose CS up by 12 semitones.
    int inverted_pitch = clampPitch(static_cast<int>(cs_note.pitch) + 12, 0, 127);

    // Find the subject note sounding at this tick.
    for (const auto& subj_note : subject.notes) {
      Tick subj_end = subj_note.start_tick + subj_note.duration;
      if (subj_note.start_tick <= cs_note.start_tick &&
          subj_end > cs_note.start_tick) {
        if (isConsonant(static_cast<uint8_t>(inverted_pitch), subj_note.pitch)) {
          consonant_count++;
        }
        break;
      }
    }
  }

  if (strong_beat_checks == 0) return 1.0f;
  return static_cast<float>(consonant_count) /
         static_cast<float>(strong_beat_checks);
}

float validateConsonanceRate(const std::vector<NoteEvent>& cs_notes,
                             const Subject& subject) {
  if (cs_notes.empty() || subject.notes.empty()) return 0.0f;

  int strong_beat_checks = 0;
  int consonant_count = 0;

  for (const auto& cs_note : cs_notes) {
    // Check if this note starts on a strong beat (beat 0 or 2 in the bar).
    uint8_t beat = beatInBar(cs_note.start_tick);
    if (beat != 0 && beat != 2) continue;

    strong_beat_checks++;

    // Find the subject note sounding at this tick.
    for (const auto& subj_note : subject.notes) {
      Tick subj_end = subj_note.start_tick + subj_note.duration;
      if (subj_note.start_tick <= cs_note.start_tick &&
          subj_end > cs_note.start_tick) {
        if (isConsonant(cs_note.pitch, subj_note.pitch)) {
          consonant_count++;
        }
        break;
      }
    }
  }

  if (strong_beat_checks == 0) return 1.0f;  // No strong beats to check.
  return static_cast<float>(consonant_count) /
         static_cast<float>(strong_beat_checks);
}

/// @brief Detect if the subject is in a chromatic run at the given index.
/// A chromatic run = 3+ consecutive notes where each interval is 1 semitone
/// AND at least one pitch is non-diatonic (NaturalMinor/Major).
bool inChromaticRun(const Subject& subject, size_t idx,
                    Key key, ScaleType base_scale) {
  if (idx + 2 >= subject.notes.size()) return false;
  for (size_t i = idx; i < idx + 2; ++i) {
    int iv = std::abs(directedInterval(
        subject.notes[i].pitch, subject.notes[i + 1].pitch));
    if (iv != 1) return false;
  }
  // At least one non-diatonic pitch in the run.
  for (size_t i = idx; i <= idx + 2; ++i) {
    if (!scale_util::isScaleTone(subject.notes[i].pitch, key, base_scale))
      return true;
  }
  return false;  // All diatonic (e.g. E-F-E) → not a chromatic run.
}

/// @brief Generate a single attempt at a countersubject.
///
/// Walks through the subject notes, generating countersubject notes with
/// contrary motion and complementary rhythm. Uses imperfect consonances
/// preferentially, avoids parallel 5ths/octaves, and enforces diatonic
/// pitch selection in the given key.
///
/// @param subject The fugue subject.
/// @param params Character-based generation parameters.
/// @param key Musical key for diatonic enforcement.
/// @param scale Scale type for diatonic enforcement.
/// @param gen Seeded random number generator.
/// @param enforce_chromatic_contrary If true, force contrary stepwise motion
///        during chromatic runs in the subject.
/// @return Vector of generated countersubject notes.
std::vector<NoteEvent> generateCSAttempt(const Subject& subject,
                                         const CSCharacterParams& params,
                                         Key key, ScaleType scale,
                                         std::mt19937& gen,
                                         bool enforce_chromatic_contrary = false) {
  if (subject.notes.empty()) return {};

  std::vector<NoteEvent> result;

  // Determine starting pitch: a consonant interval above or below the
  // subject's first note (prefer a 3rd or 6th above).
  int start_direction = rng::rollProbability(gen, 0.6f) ? 1 : -1;
  uint8_t current_pitch = findConsonantPitch(
      subject.notes[0].pitch, start_direction,
      kCSPitchLow, kCSPitchHigh, key, scale, gen);

  Tick current_tick = 0;
  Tick total_ticks = subject.length_ticks;
  size_t subj_idx = 0;

  while (current_tick < total_ticks) {
    // Find the subject note at the current tick position.
    while (subj_idx + 1 < subject.notes.size() &&
           subject.notes[subj_idx + 1].start_tick <= current_tick) {
      subj_idx++;
    }

    const NoteEvent& subj_note = subject.notes[subj_idx];

    // Determine subject motion direction.
    int subj_direction = 0;
    if (subj_idx + 1 < subject.notes.size()) {
      subj_direction = directedInterval(
          subject.notes[subj_idx].pitch,
          subject.notes[subj_idx + 1].pitch);
    }

    // Generate complementary duration with Markov context.
    const MarkovModel* markov = nullptr;
    DurCategory prev_dur_cat = DurCategory::Qtr;
    DirIntervalClass dir_ivl = DirIntervalClass::StepUp;
    if (!result.empty()) {
      prev_dur_cat = ticksToDurCategory(result.back().duration);
      DegreeStep step = computeDegreeStep(result.back().pitch, current_pitch,
                                          key, scale);
      dir_ivl = toDirIvlClass(step);
      markov = &kFugueUpperMarkov;
    }
    Tick duration = selectCSDuration(subj_note.duration,
                                     params.long_split_prob, current_tick, gen,
                                     markov, prev_dur_cat, dir_ivl);
    if (current_tick + duration > total_ticks) {
      duration = total_ticks - current_tick;
      if (duration < kEighthNote) break;
    }

    // Determine motion direction (contrary to subject).
    // During chromatic runs: deterministic contrary + stepwise forced.
    ScaleType base_scale = subject.is_minor ? ScaleType::NaturalMinor
                                            : ScaleType::Major;
    bool in_chromatic = false;
    if (enforce_chromatic_contrary) {
      in_chromatic = inChromaticRun(subject, subj_idx, key, base_scale);
    }

    int cs_direction;
    uint8_t next_pitch;
    if (in_chromatic && subj_direction != 0) {
      cs_direction = (subj_direction > 0) ? -1 : 1;
      next_pitch = stepMotion(current_pitch, cs_direction,
                              kCSPitchLow, kCSPitchHigh, key, scale);
    } else {
      cs_direction = contraryDirection(subj_direction, gen);
      if (rng::rollProbability(gen, params.leap_prob)) {
        next_pitch = leapMotion(current_pitch, cs_direction,
                                kCSPitchLow, kCSPitchHigh, key, scale, gen);
      } else {
        next_pitch = stepMotion(current_pitch, cs_direction,
                                kCSPitchLow, kCSPitchHigh, key, scale);
      }
    }

    // Check for parallel 5ths/octaves against previous note pair.
    if (!result.empty()) {
      uint8_t prev_cs_pitch = result.back().pitch;
      // Find the subject note that was sounding at the previous CS note.
      uint8_t prev_subj_pitch = subj_note.pitch;
      for (const auto& sn : subject.notes) {
        Tick sn_end = sn.start_tick + sn.duration;
        if (sn.start_tick <= result.back().start_tick &&
            sn_end > result.back().start_tick) {
          prev_subj_pitch = sn.pitch;
          break;
        }
      }

      if (hasParallelFifthsOrOctaves(prev_cs_pitch, prev_subj_pitch,
                                     next_pitch, subj_note.pitch)) {
        // Adjust by a step to avoid parallels.
        next_pitch = stepMotion(next_pitch, cs_direction,
                                kCSPitchLow, kCSPitchHigh, key, scale);
      }
    }

    // Prefer consonant intervals on strong beats. If the note falls on
    // a strong beat and is dissonant, try to adjust.
    uint8_t beat = beatInBar(current_tick);
    if ((beat == 0 || beat == 2) && !isConsonant(next_pitch, subj_note.pitch)) {
      // Try finding a consonant pitch near the desired direction.
      uint8_t consonant = findConsonantPitch(
          subj_note.pitch, cs_direction, kCSPitchLow, kCSPitchHigh,
          key, scale, gen);
      // Only use it if reasonably close to the intended pitch (within a 5th).
      if (absoluteInterval(consonant, current_pitch) <=
          interval::kPerfect5th) {
        next_pitch = consonant;
      }
    }

    NoteEvent cs_note;
    cs_note.start_tick = current_tick;
    cs_note.duration = duration;
    cs_note.pitch = next_pitch;
    cs_note.velocity = 80;
    cs_note.voice = 1;  // Countersubject is typically voice 1.
    cs_note.source = BachNoteSource::Countersubject;

    result.push_back(cs_note);
    current_pitch = next_pitch;
    current_tick += duration;
  }

  // Ensure last note ends on a consonant interval with the subject's last
  // note.
  if (!result.empty() && !subject.notes.empty()) {
    uint8_t last_subj_pitch = subject.notes.back().pitch;
    if (!isConsonant(result.back().pitch, last_subj_pitch)) {
      // Find a consonant ending pitch.
      int end_dir = (static_cast<int>(result.back().pitch) >=
                     static_cast<int>(last_subj_pitch))
                        ? 1
                        : -1;
      result.back().pitch = findConsonantPitch(
          last_subj_pitch, end_dir, kCSPitchLow, kCSPitchHigh,
          key, scale, gen);
    }
  }

  return result;
}

/// @brief Find the subject pitch sounding at a given tick.
uint8_t findSubjectPitchAt(const Subject& subject, Tick tick) {
  for (const auto& note : subject.notes) {
    Tick end = note.start_tick + note.duration;
    if (note.start_tick <= tick && end > tick) return note.pitch;
  }
  return subject.notes.empty() ? 60 : subject.notes.back().pitch;
}

/// @brief Insert chromatic passing tones on weak beats between whole-tone
///        diatonic steps in the countersubject.
void insertCSChromaticPassingTones(std::vector<NoteEvent>& cs_notes,
                                   Key key, ScaleType scale,
                                   int max_insertions) {
  if (cs_notes.size() < 2 || max_insertions <= 0) return;

  int insertions = 0;
  // Reverse iterate so insertions don't invalidate earlier indices.
  for (int idx = static_cast<int>(cs_notes.size()) - 2; idx >= 0; --idx) {
    if (insertions >= max_insertions) break;

    auto& curr = cs_notes[static_cast<size_t>(idx)];
    const auto& next = cs_notes[static_cast<size_t>(idx) + 1];

    // Condition 1: both notes diatonic.
    if (!scale_util::isScaleTone(curr.pitch, key, scale)) continue;
    if (!scale_util::isScaleTone(next.pitch, key, scale)) continue;

    // Condition 2: interval = exactly whole tone (2 semitones).
    int interval = directedInterval(curr.pitch, next.pitch);
    if (std::abs(interval) != 2) continue;

    // Condition 3: intermediate pitch is non-diatonic.
    uint8_t mid_pitch = static_cast<uint8_t>(
        static_cast<int>(curr.pitch) + (interval > 0 ? 1 : -1));
    if (scale_util::isScaleTone(mid_pitch, key, scale)) continue;

    // Condition 4: duration >= eighth note.
    if (curr.duration < kEighthNote) continue;

    // Condition 5: contiguous (no gap or overlap).
    if (curr.start_tick + curr.duration != next.start_tick) continue;

    // Condition 6: passing tone position must land on a weak beat.
    Tick half = curr.duration / 2;
    uint8_t passing_beat = beatInBar(curr.start_tick + half);
    if (passing_beat != 1 && passing_beat != 3) continue;

    NoteEvent passing;
    passing.start_tick = curr.start_tick + half;
    passing.duration = half;
    passing.pitch = mid_pitch;
    passing.velocity = curr.velocity;
    passing.voice = curr.voice;
    passing.source = BachNoteSource::ChromaticPassing;

    curr.duration = half;

    cs_notes.insert(cs_notes.begin() + idx + 1, passing);
    insertions++;
  }
}

/// @brief Snap strong-beat notes back to consonance with the subject.
///        Skips ChromaticPassing notes (which are on weak beats).
void snapStrongBeatConsonance(std::vector<NoteEvent>& cs_notes,
                              const Subject& subject,
                              Key key, ScaleType scale,
                              std::mt19937& gen) {
  for (size_t i = 0; i < cs_notes.size(); ++i) {
    auto& note = cs_notes[i];
    if (note.source == BachNoteSource::ChromaticPassing) continue;
    // Skip if the next note is a passing tone (preserve stepwise approach).
    if (i + 1 < cs_notes.size() &&
        cs_notes[i + 1].source == BachNoteSource::ChromaticPassing) continue;
    uint8_t beat = beatInBar(note.start_tick);
    if (beat != 0 && beat != 2) continue;
    uint8_t subj_pitch = findSubjectPitchAt(subject, note.start_tick);
    if (isConsonant(note.pitch, subj_pitch)) continue;
    int dir = (static_cast<int>(note.pitch) >= static_cast<int>(subj_pitch))
                  ? 1 : -1;
    uint8_t fixed = findConsonantPitch(
        subj_pitch, dir, kCSPitchLow, kCSPitchHigh, key, scale, gen);
    if (absoluteInterval(fixed, note.pitch) <= interval::kPerfect5th) {
      note.pitch = fixed;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Countersubject generateCountersubject(const Subject& subject,
                                       uint32_t seed,
                                       int max_retries,
                                       FugueArchetype archetype) {
  if (subject.notes.empty()) {
    Countersubject empty;
    empty.key = subject.key;
    empty.length_ticks = subject.length_ticks;
    return empty;
  }

  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  Countersubject best;
  best.key = subject.key;
  best.length_ticks = subject.length_ticks;
  float best_score = -1.0f;

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    std::mt19937 gen(seed + static_cast<uint32_t>(attempt) * 1000003u);

    // Per-attempt RNG variation on character params.
    CSCharacterParams params = getCSParams(subject.character, gen);
    const ArchetypePolicy& policy = getArchetypePolicy(archetype);
    applyArchetypeToCSParams(params, policy);

    bool enforce_chromatic_contrary = policy.require_functional_resolution;
    std::vector<NoteEvent> cs_notes =
        generateCSAttempt(subject, params, subject.key, scale, gen,
                          enforce_chromatic_contrary);

    if (policy.require_functional_resolution) {
      int max_chromatic = policy.max_consecutive_chromatic / 2;
      insertCSChromaticPassingTones(cs_notes, subject.key, scale,
                                    max_chromatic);
      snapStrongBeatConsonance(cs_notes, subject, subject.key, scale, gen);
    }

    float score = validateConsonanceRate(cs_notes, subject);

    // Check invertibility at the octave: inverted consonance must be >= 60%.
    float inverted_score = validateInvertedConsonanceRate(cs_notes, subject);
    // Invertible archetype: weight inverted quality more heavily.
    float inv_weight = policy.require_invertible ? 0.45f : 0.30f;
    float composite = score * (1.0f - inv_weight) + inverted_score * inv_weight;

    if (composite > best_score) {
      best_score = composite;
      best.notes = std::move(cs_notes);
    }

    // Accept if original consonance >= 70% AND inverted consonance >= 60%.
    if (score >= 0.70f && inverted_score >= 0.60f) break;
  }

  return best;
}

Countersubject generateSecondCountersubject(const Subject& subject,
                                             const Countersubject& first_cs,
                                             uint32_t seed,
                                             int max_retries,
                                             FugueArchetype archetype) {
  if (subject.notes.empty()) {
    Countersubject empty;
    empty.key = subject.key;
    empty.length_ticks = subject.length_ticks;
    return empty;
  }

  // Determine register for CS2: if CS1 is above subject, place CS2 below,
  // and vice versa. This ensures registral separation between all three lines.
  bool cs1_above_subject = true;
  if (!first_cs.notes.empty() && !subject.notes.empty()) {
    int cs1_avg = 0;
    for (const auto& note : first_cs.notes) {
      cs1_avg += static_cast<int>(note.pitch);
    }
    cs1_avg /= static_cast<int>(first_cs.notes.size());

    int subj_avg = 0;
    for (const auto& note : subject.notes) {
      subj_avg += static_cast<int>(note.pitch);
    }
    subj_avg /= static_cast<int>(subject.notes.size());

    cs1_above_subject = cs1_avg >= subj_avg;
  }

  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  Countersubject best;
  best.key = subject.key;
  best.length_ticks = subject.length_ticks;
  float best_score = -1.0f;

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    std::mt19937 gen(seed + static_cast<uint32_t>(attempt) * 1000003u);

    // Use modified character params with per-attempt RNG variation:
    // slightly more leaps and wider range to differentiate from CS1.
    CSCharacterParams params = getCSParams(subject.character, gen);
    params.leap_prob = std::min(params.leap_prob + 0.10f, 0.60f);
    params.max_range = std::min(params.max_range + 2, 16);
    const ArchetypePolicy& policy2 = getArchetypePolicy(archetype);
    applyArchetypeToCSParams(params, policy2);

    // Generate a CS attempt against the subject.
    bool enforce_chromatic_contrary2 = policy2.require_functional_resolution;
    std::vector<NoteEvent> cs2_notes =
        generateCSAttempt(subject, params, subject.key, scale, gen,
                          enforce_chromatic_contrary2);

    if (policy2.require_functional_resolution) {
      int max_chromatic2 = policy2.max_consecutive_chromatic / 2;
      insertCSChromaticPassingTones(cs2_notes, subject.key, scale,
                                    max_chromatic2);
      snapStrongBeatConsonance(cs2_notes, subject, subject.key, scale, gen);
    }

    // Shift register: if CS1 is above, push CS2 down by an octave offset;
    // otherwise push CS2 up. Snap to scale to preserve diatonic membership.
    int register_shift = cs1_above_subject ? -7 : 7;
    for (auto& note : cs2_notes) {
      int shifted = static_cast<int>(note.pitch) + register_shift;
      uint8_t clamped = clampPitch(shifted, kCSPitchLow, kCSPitchHigh);
      note.pitch = scale_util::nearestScaleTone(clamped, subject.key, scale);
      note.voice = 2;  // CS2 is typically voice 2.
    }

    // Score against subject and CS1 consonance.
    float subj_score = validateConsonanceRate(cs2_notes, subject);

    // Also check consonance with CS1 (build a temporary "Subject" from CS1 notes).
    Subject cs1_as_subject;
    cs1_as_subject.notes = first_cs.notes;
    cs1_as_subject.length_ticks = first_cs.length_ticks;
    cs1_as_subject.key = first_cs.key;
    cs1_as_subject.is_minor = subject.is_minor;
    float cs1_score = validateConsonanceRate(cs2_notes, cs1_as_subject);

    // Combined score: both lines must be consonant.
    float combined = subj_score * 0.6f + cs1_score * 0.4f;

    if (combined > best_score) {
      best_score = combined;
      best.notes = std::move(cs2_notes);
    }

    // Accept if combined consonance rate is at least 60%.
    if (best_score >= 0.60f) break;
  }

  return best;
}

// ---------------------------------------------------------------------------
// adaptCSToKey
// ---------------------------------------------------------------------------

std::vector<NoteEvent> adaptCSToKey(const std::vector<NoteEvent>& cs_notes,
                                     Key to_key, ScaleType scale) {
  auto adapted = cs_notes;
  for (auto& note : adapted) {
    if (!scale_util::isScaleTone(note.pitch, to_key, scale)) {
      note.pitch = scale_util::nearestScaleTone(note.pitch, to_key, scale);
    }
  }
  return adapted;
}

}  // namespace bach
