/// @file
/// @brief Countersubject generation with contrary motion, complementary
/// rhythm, and consonant interval selection.

#include "fugue/countersubject.h"

#include <algorithm>
#include <cstdlib>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"

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

/// @brief Imperfect consonance intervals (semitones) -- preferred for
/// countersubject because they avoid parallel 5ths/8ths while sounding good.
constexpr int kImperfectConsonances[] = {3, 4, 8, 9};
constexpr int kImperfectConsonanceCount = 4;

/// @brief All consonance intervals (semitones) -- including perfect, used
/// as fallback when imperfect consonance placement fails range constraints.
constexpr int kAllConsonances[] = {3, 4, 7, 8, 9, 12};
constexpr int kAllConsonanceCount = 6;

/// @brief Parameters that shape countersubject generation per character.
struct CSCharacterParams {
  float leap_prob;       // Probability of a leap (vs step) in motion
  int max_range;         // Maximum pitch range in semitones
  float long_split_prob; // Probability of splitting long notes into shorter
};

/// @brief Return generation parameters based on the subject character.
/// @param character The SubjectCharacter of the accompanying subject.
/// @return CSCharacterParams with leap probability, range, and split rate.
CSCharacterParams getCSParams(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {0.10f, 10, 0.6f};
    case SubjectCharacter::Playful:
      return {0.40f, 14, 0.7f};
    case SubjectCharacter::Noble:
      return {0.20f, 12, 0.5f};
    case SubjectCharacter::Restless:
      return {0.35f, 14, 0.75f};
  }
  return {0.15f, 10, 0.6f};
}

/// @brief Minimum pitch for countersubject voices (C3).
constexpr uint8_t kCSPitchLow = 48;
/// @brief Maximum pitch for countersubject voices (C6).
constexpr uint8_t kCSPitchHigh = 96;

/// @brief Find a consonant pitch relative to the subject pitch.
///
/// Selects from imperfect consonances (3rds, 6ths) preferentially, placing
/// the countersubject note in the given direction (above or below) the
/// subject pitch. The result is clamped to a valid range.
///
/// @param subject_pitch The current subject MIDI pitch.
/// @param direction +1 for above, -1 for below the subject.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @param gen Random number generator.
/// @return A consonant MIDI pitch, or subject_pitch +/- 3 as fallback.
uint8_t findConsonantPitch(uint8_t subject_pitch, int direction,
                           uint8_t low_range, uint8_t high_range,
                           std::mt19937& gen) {
  // Shuffle through imperfect consonances in the preferred direction.
  int offsets[kImperfectConsonanceCount];
  for (int idx = 0; idx < kImperfectConsonanceCount; ++idx) {
    offsets[idx] = kImperfectConsonances[idx] * direction;
  }

  // Try each offset; pick the first one that stays in range.
  // Shuffle order for variety.
  for (int idx = kImperfectConsonanceCount - 1; idx > 0; --idx) {
    int jdx = rng::rollRange(gen, 0, idx);
    std::swap(offsets[idx], offsets[jdx]);
  }

  for (int idx = 0; idx < kImperfectConsonanceCount; ++idx) {
    int candidate = static_cast<int>(subject_pitch) + offsets[idx];
    if (candidate >= static_cast<int>(low_range) &&
        candidate <= static_cast<int>(high_range)) {
      return static_cast<uint8_t>(candidate);
    }
  }

  // Fallback: try all consonances.
  for (int idx = 0; idx < kAllConsonanceCount; ++idx) {
    int candidate = static_cast<int>(subject_pitch) +
                    kAllConsonances[idx] * direction;
    if (candidate >= static_cast<int>(low_range) &&
        candidate <= static_cast<int>(high_range)) {
      return static_cast<uint8_t>(candidate);
    }
  }

  // Last resort: minor 3rd in the given direction, clamped.
  int fallback = static_cast<int>(subject_pitch) + 3 * direction;
  return clampPitch(fallback, low_range, high_range);
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
Tick complementaryDuration(Tick subject_duration, float split_prob,
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

/// @brief Step-based motion: move current pitch by a step (2nd) in the
/// given direction, staying diatonic and within range.
///
/// @param current_pitch Current countersubject pitch.
/// @param direction +1 for ascending, -1 for descending.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @return New pitch after step motion.
uint8_t stepMotion(uint8_t current_pitch, int direction,
                   uint8_t low_range, uint8_t high_range) {
  // Try a major 2nd (2 semitones), fall back to minor 2nd (1).
  int candidate = static_cast<int>(current_pitch) + 2 * direction;
  if (candidate >= static_cast<int>(low_range) &&
      candidate <= static_cast<int>(high_range) &&
      isDiatonic(candidate)) {
    return static_cast<uint8_t>(candidate);
  }

  candidate = static_cast<int>(current_pitch) + 1 * direction;
  if (candidate >= static_cast<int>(low_range) &&
      candidate <= static_cast<int>(high_range)) {
    return static_cast<uint8_t>(candidate);
  }

  // Reverse direction if out of range.
  candidate = static_cast<int>(current_pitch) + 2 * (-direction);
  return clampPitch(candidate, low_range, high_range);
}

/// @brief Leap motion: move by a 3rd or 4th in the given direction.
///
/// @param current_pitch Current countersubject pitch.
/// @param direction +1 for ascending, -1 for descending.
/// @param low_range Lower pitch bound.
/// @param high_range Upper pitch bound.
/// @param gen Random number generator.
/// @return New pitch after leap.
uint8_t leapMotion(uint8_t current_pitch, int direction,
                   uint8_t low_range, uint8_t high_range,
                   std::mt19937& gen) {
  // Choose between minor 3rd, major 3rd, perfect 4th.
  constexpr int kLeapSizes[] = {3, 4, 5};
  constexpr int kLeapCount = 3;

  int leap_idx = rng::rollRange(gen, 0, kLeapCount - 1);
  int leap = kLeapSizes[leap_idx] * direction;
  int candidate = static_cast<int>(current_pitch) + leap;

  if (candidate >= static_cast<int>(low_range) &&
      candidate <= static_cast<int>(high_range)) {
    return static_cast<uint8_t>(candidate);
  }

  // Reverse direction on range violation.
  candidate = static_cast<int>(current_pitch) - leap;
  return clampPitch(candidate, low_range, high_range);
}

/// @brief Check whether two pitches form a consonant interval.
/// @param pitch_a First MIDI pitch.
/// @param pitch_b Second MIDI pitch.
/// @return True if the interval is perfect or imperfect consonance.
bool isConsonant(uint8_t pitch_a, uint8_t pitch_b) {
  int abs_interval = absoluteInterval(pitch_a, pitch_b);
  IntervalQuality quality = classifyInterval(abs_interval);
  return quality == IntervalQuality::PerfectConsonance ||
         quality == IntervalQuality::ImperfectConsonance;
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

/// @brief Generate a single attempt at a countersubject.
///
/// Walks through the subject notes, generating countersubject notes with
/// contrary motion and complementary rhythm. Uses imperfect consonances
/// preferentially and avoids parallel 5ths/octaves.
///
/// @param subject The fugue subject.
/// @param params Character-based generation parameters.
/// @param gen Seeded random number generator.
/// @return Vector of generated countersubject notes.
std::vector<NoteEvent> generateCSAttempt(const Subject& subject,
                                         const CSCharacterParams& params,
                                         std::mt19937& gen) {
  if (subject.notes.empty()) return {};

  std::vector<NoteEvent> result;

  // Determine starting pitch: a consonant interval above or below the
  // subject's first note (prefer a 3rd or 6th above).
  int start_direction = rng::rollProbability(gen, 0.6f) ? 1 : -1;
  uint8_t current_pitch = findConsonantPitch(
      subject.notes[0].pitch, start_direction,
      kCSPitchLow, kCSPitchHigh, gen);

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

    // Generate complementary duration.
    Tick duration = complementaryDuration(subj_note.duration,
                                          params.long_split_prob, gen);
    if (current_tick + duration > total_ticks) {
      duration = total_ticks - current_tick;
      if (duration < kEighthNote) break;
    }

    // Determine motion direction (contrary to subject).
    int cs_direction = contraryDirection(subj_direction, gen);

    // Move the countersubject pitch.
    uint8_t next_pitch;
    if (rng::rollProbability(gen, params.leap_prob)) {
      next_pitch = leapMotion(current_pitch, cs_direction,
                              kCSPitchLow, kCSPitchHigh, gen);
    } else {
      next_pitch = stepMotion(current_pitch, cs_direction,
                              kCSPitchLow, kCSPitchHigh);
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
                                kCSPitchLow, kCSPitchHigh);
      }
    }

    // Prefer consonant intervals on strong beats. If the note falls on
    // a strong beat and is dissonant, try to adjust.
    uint8_t beat = beatInBar(current_tick);
    if ((beat == 0 || beat == 2) && !isConsonant(next_pitch, subj_note.pitch)) {
      // Try finding a consonant pitch near the desired direction.
      uint8_t consonant = findConsonantPitch(
          subj_note.pitch, cs_direction, kCSPitchLow, kCSPitchHigh, gen);
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
          last_subj_pitch, end_dir, kCSPitchLow, kCSPitchHigh, gen);
    }
  }

  return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Countersubject generateCountersubject(const Subject& subject,
                                       uint32_t seed,
                                       int max_retries) {
  if (subject.notes.empty()) {
    Countersubject empty;
    empty.key = subject.key;
    empty.length_ticks = subject.length_ticks;
    return empty;
  }

  CSCharacterParams params = getCSParams(subject.character);

  Countersubject best;
  best.key = subject.key;
  best.length_ticks = subject.length_ticks;
  float best_score = -1.0f;

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    std::mt19937 gen(seed + static_cast<uint32_t>(attempt) * 1000003u);

    std::vector<NoteEvent> cs_notes = generateCSAttempt(subject, params, gen);
    float score = validateConsonanceRate(cs_notes, subject);

    if (score > best_score) {
      best_score = score;
      best.notes = std::move(cs_notes);
    }

    // Accept if consonance rate is at least 70%.
    if (best_score >= 0.70f) break;
  }

  return best;
}

}  // namespace bach
