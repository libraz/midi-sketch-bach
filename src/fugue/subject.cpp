/// @file
/// @brief Fugue subject generation with character-driven note and rhythm selection.

#include "fugue/subject.h"

#include <algorithm>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"

namespace bach {

// ---------------------------------------------------------------------------
// Subject member functions
// ---------------------------------------------------------------------------

uint8_t Subject::lowestPitch() const {
  if (notes.empty()) return 127;
  uint8_t lowest = 127;
  for (const auto& note : notes) {
    if (note.pitch < lowest) lowest = note.pitch;
  }
  return lowest;
}

uint8_t Subject::highestPitch() const {
  if (notes.empty()) return 0;
  uint8_t highest = 0;
  for (const auto& note : notes) {
    if (note.pitch > highest) highest = note.pitch;
  }
  return highest;
}

int Subject::range() const {
  if (notes.empty()) return 0;
  return static_cast<int>(highestPitch()) - static_cast<int>(lowestPitch());
}

size_t Subject::noteCount() const {
  return notes.size();
}

// ---------------------------------------------------------------------------
// Duration tables per character
// ---------------------------------------------------------------------------

namespace {

/// @brief Half note duration in ticks.
constexpr Tick kHalfNote = kTicksPerBeat * 2;      // 960
/// @brief Quarter note duration in ticks.
constexpr Tick kQuarterNote = kTicksPerBeat;        // 480
/// @brief Eighth note duration in ticks.
constexpr Tick kEighthNote = kTicksPerBeat / 2;     // 240
/// @brief Dotted quarter note duration in ticks.
constexpr Tick kDottedQuarter = kQuarterNote + kEighthNote;  // 720
/// @brief Dotted eighth note duration in ticks.
constexpr Tick kDottedEighth = kEighthNote + kEighthNote / 2;  // 360

/// @brief Durations for Severe character: even, no dots.
constexpr Tick kSevereDurations[] = {kHalfNote, kQuarterNote, kEighthNote};
constexpr int kSevereDurCount = 3;

/// @brief Durations for Playful character: includes dotted values.
constexpr Tick kPlayfulDurations[] = {
    kQuarterNote, kEighthNote, kDottedQuarter, kDottedEighth};
constexpr int kPlayfulDurCount = 4;

/// @brief Dotted half note duration in ticks.
constexpr Tick kDottedHalf = kHalfNote + kQuarterNote;  // 1440
/// @brief Sixteenth note duration in ticks.
constexpr Tick kSixteenthNote = kTicksPerBeat / 4;  // 120

/// @brief Durations for Noble character: stately, longer values preferred.
/// Half notes and dotted quarters dominate; no syncopation.
constexpr Tick kNobleDurations[] = {kDottedHalf, kHalfNote, kDottedQuarter, kQuarterNote};
constexpr int kNobleDurCount = 4;

/// @brief Durations for Restless character: short, syncopated, nervous.
/// Emphasizes eighth and sixteenth notes with occasional dotted values for
/// off-beat syncopation.
constexpr Tick kRestlessDurations[] = {
    kEighthNote, kSixteenthNote, kDottedEighth, kQuarterNote, kDottedQuarter};
constexpr int kRestlessDurCount = 5;

/// @brief Parameters that shape subject generation for a given character type.
struct CharacterParams {
  float leap_prob;        // Probability of a leap (vs step)
  int max_range;          // Maximum pitch range in semitones
  const Tick* durations;  // Available duration values
  int dur_count;          // Number of duration values
};

/// @brief Return generation parameters for the given subject character.
/// @param character The SubjectCharacter to look up.
/// @return CharacterParams with leap probability, range, and duration table.
CharacterParams getCharacterParams(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {0.15f, 8, kSevereDurations, kSevereDurCount};
    case SubjectCharacter::Playful:
      return {0.45f, 12, kPlayfulDurations, kPlayfulDurCount};
    case SubjectCharacter::Noble:
      // Noble: long note values, narrow range, few leaps (<= 30%).
      // Downward tendency is handled in generateNotes().
      return {0.25f, 10, kNobleDurations, kNobleDurCount};
    case SubjectCharacter::Restless:
      // Restless: shorter values, wider range, more leaps (>= 30%).
      // Chromatic motion and syncopation handled in generateNotes().
      return {0.40f, 12, kRestlessDurations, kRestlessDurCount};
  }
  return {0.20f, 10, kSevereDurations, kSevereDurCount};
}

/// @brief Scale degree intervals for stepwise motion (seconds).
constexpr int kStepIntervals[] = {-2, -1, 1, 2};
/// @brief Number of entries in kStepIntervals.
constexpr int kStepCount = 4;
/// @brief Scale degree intervals for leaps (thirds, fourths, fifths).
constexpr int kLeapIntervals[] = {-5, -4, -3, 3, 4, 5};
/// @brief Number of entries in kLeapIntervals.
constexpr int kLeapCount = 6;

/// @brief Step intervals biased downward for Noble character.
/// Favors descending motion (-2, -1) over ascending (+1).
constexpr int kNobleStepIntervals[] = {-2, -2, -1, -1, 1};
/// @brief Number of entries in kNobleStepIntervals.
constexpr int kNobleStepCount = 5;

/// @brief Leap intervals biased downward for Noble character.
/// Favors descending leaps.
constexpr int kNobleLeapIntervals[] = {-5, -4, -3, -3, 3, 4};
/// @brief Number of entries in kNobleLeapIntervals.
constexpr int kNobleLeapCount = 6;

/// @brief Chromatic semitone intervals for Restless character.
/// Encourages semitone steps (minor 2nds), including chromatic motion.
constexpr int kRestlessChromaticSteps[] = {-1, 1, -1, 1, -2, 2};
/// @brief Number of entries in kRestlessChromaticSteps.
constexpr int kRestlessChromaticCount = 6;

/// @brief Leap intervals for Restless character.
/// Includes tritone (6 semitones = augmented 4th equivalent in degrees)
/// and wide leaps for instability.
constexpr int kRestlessLeapIntervals[] = {-5, -4, -3, 3, 4, 5, -6, 6};
/// @brief Number of entries in kRestlessLeapIntervals.
constexpr int kRestlessLeapCount = 8;

}  // namespace

// ---------------------------------------------------------------------------
// SubjectGenerator
// ---------------------------------------------------------------------------

Subject SubjectGenerator::generate(const FugueConfig& config,
                                   uint32_t seed) const {
  Subject subject;
  subject.key = config.key;
  subject.character = config.character;

  uint8_t bars = config.subject_bars;
  if (bars < 2) bars = 2;
  if (bars > 4) bars = 4;

  // Determine anacrusis based on character type.
  std::mt19937 anacrusis_gen(seed ^ 0x41756674u);  // "Auft" in ASCII
  float anacrusis_prob = 0.0f;
  switch (config.character) {
    case SubjectCharacter::Severe:  anacrusis_prob = 0.30f; break;
    case SubjectCharacter::Playful: anacrusis_prob = 0.70f; break;
    case SubjectCharacter::Noble:   anacrusis_prob = 0.40f; break;
    case SubjectCharacter::Restless: anacrusis_prob = 0.60f; break;
  }
  bool has_anacrusis = rng::rollProbability(anacrusis_gen, anacrusis_prob);
  Tick anacrusis_ticks = 0;
  if (has_anacrusis) {
    // Anacrusis length: 1 beat (Severe/Noble) or 1-2 beats (Playful/Restless).
    if (config.character == SubjectCharacter::Playful ||
        config.character == SubjectCharacter::Restless) {
      anacrusis_ticks = rng::rollProbability(anacrusis_gen, 0.5f)
                            ? kTicksPerBeat
                            : kTicksPerBeat * 2;
    } else {
      anacrusis_ticks = kTicksPerBeat;
    }
  }

  subject.notes = generateNotes(config.character, config.key, bars, seed);
  subject.length_ticks = static_cast<Tick>(bars) * kTicksPerBar;
  subject.anacrusis_ticks = anacrusis_ticks;

  // If anacrusis is present, shift first note(s) to fill the anacrusis region.
  // The anacrusis notes precede bar 0: they occupy [-anacrusis_ticks, 0).
  // In practice, we keep notes at positive ticks but mark the anacrusis length
  // so exposition can adjust entry_tick offsets.
  if (anacrusis_ticks > 0 && !subject.notes.empty()) {
    // Assign the anacrusis duration to the first note by splitting it.
    Tick first_dur = subject.notes[0].duration;
    if (first_dur > anacrusis_ticks) {
      // Split first note: anacrusis part + remainder.
      NoteEvent anacrusis_note = subject.notes[0];
      anacrusis_note.duration = anacrusis_ticks;
      subject.notes[0].start_tick = anacrusis_ticks;
      subject.notes[0].duration = first_dur - anacrusis_ticks;
      subject.notes.insert(subject.notes.begin(), anacrusis_note);
    }
    // Total length includes anacrusis.
    subject.length_ticks += anacrusis_ticks;
  }

  return subject;
}

std::vector<NoteEvent> SubjectGenerator::generateNotes(
    SubjectCharacter character, Key key, uint8_t bars,
    uint32_t seed) const {
  std::mt19937 gen(seed);
  CharacterParams params = getCharacterParams(character);
  int key_offset = static_cast<int>(key);

  // Total duration to fill.
  Tick total_ticks = static_cast<Tick>(bars) * kTicksPerBar;

  // Start on tonic (degree 0) or dominant (degree 4).
  // Base note: C4 = MIDI 60.
  constexpr int kBaseNote = 60;
  int start_degree = rng::rollProbability(gen, 0.6f) ? 0 : 4;
  int current_degree = start_degree;

  std::vector<NoteEvent> result;
  Tick current_tick = 0;

  // Track the range bounds relative to the starting pitch.
  int start_pitch = degreeToPitch(current_degree, kBaseNote, key_offset);
  int min_pitch = start_pitch;
  int max_pitch = start_pitch;

  while (current_tick < total_ticks) {
    // Select duration.
    int dur_idx = rng::rollRange(gen, 0, params.dur_count - 1);
    Tick duration = params.durations[dur_idx];

    // Ensure we do not exceed total length.
    if (current_tick + duration > total_ticks) {
      duration = total_ticks - current_tick;
      // Snap to nearest valid duration (at least an eighth note).
      if (duration < kEighthNote) break;
    }

    // Compute pitch from current degree.
    int pitch = degreeToPitch(current_degree, kBaseNote, key_offset);

    // Clamp to valid MIDI range.
    pitch = std::max(36, std::min(96, pitch));

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = duration;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = 0;

    result.push_back(note);
    current_tick += duration;

    // Track range.
    if (pitch < min_pitch) min_pitch = pitch;
    if (pitch > max_pitch) max_pitch = pitch;

    // Choose next degree: step or leap.
    // Interval selection is character-dependent:
    //   Noble: biased downward, avoids syncopation
    //   Restless: chromatic steps, unstable leaps
    //   Default: standard diatonic intervals
    bool use_leap = rng::rollProbability(gen, params.leap_prob);

    int interval = 0;
    if (character == SubjectCharacter::Noble) {
      if (use_leap) {
        interval = kNobleLeapIntervals[rng::rollRange(gen, 0, kNobleLeapCount - 1)];
      } else {
        interval = kNobleStepIntervals[rng::rollRange(gen, 0, kNobleStepCount - 1)];
      }
    } else if (character == SubjectCharacter::Restless) {
      if (use_leap) {
        interval = kRestlessLeapIntervals[rng::rollRange(gen, 0, kRestlessLeapCount - 1)];
      } else {
        interval = kRestlessChromaticSteps[rng::rollRange(gen, 0, kRestlessChromaticCount - 1)];
      }
    } else {
      if (use_leap) {
        interval = kLeapIntervals[rng::rollRange(gen, 0, kLeapCount - 1)];
      } else {
        interval = kStepIntervals[rng::rollRange(gen, 0, kStepCount - 1)];
      }
    }

    int next_degree = current_degree + interval;
    int next_pitch = degreeToPitch(next_degree, kBaseNote, key_offset);

    // Enforce range constraint: if the next note would exceed the maximum
    // allowed range, reverse the direction.
    int tentative_min = std::min(min_pitch, next_pitch);
    int tentative_max = std::max(max_pitch, next_pitch);
    if (tentative_max - tentative_min > params.max_range) {
      // Reverse direction.
      interval = -interval;
      next_degree = current_degree + interval;
      next_pitch = degreeToPitch(next_degree, kBaseNote, key_offset);

      // If still out of range, just step back toward the start.
      tentative_min = std::min(min_pitch, next_pitch);
      tentative_max = std::max(max_pitch, next_pitch);
      if (tentative_max - tentative_min > params.max_range) {
        // Move toward starting degree.
        if (current_degree > start_degree) {
          next_degree = current_degree - 1;
        } else {
          next_degree = current_degree + 1;
        }
      }
    }

    current_degree = next_degree;
  }

  // Ensure the last note ends on the tonic.
  if (!result.empty()) {
    int tonic_pitch = degreeToPitch(0, kBaseNote, key_offset);
    tonic_pitch = std::max(36, std::min(96, tonic_pitch));
    result.back().pitch = static_cast<uint8_t>(tonic_pitch);
  }

  return result;
}

}  // namespace bach
