/// @file
/// @brief Fugue subject generation with character-driven note and rhythm selection.

#include "fugue/subject.h"

#include <algorithm>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "fugue/motif_template.h"

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

std::vector<NoteEvent> Subject::extractKopfmotiv(size_t max_notes) const {
  std::vector<NoteEvent> result;
  size_t count = std::min(max_notes, notes.size());
  result.reserve(count);
  for (size_t idx = 0; idx < count; ++idx) {
    result.push_back(notes[idx]);
  }
  return result;
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
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kStepIntervals[] = {-2, -1, 1, 2};
/// @brief Number of entries in kStepIntervals.
[[maybe_unused]] constexpr int kStepCount = 4;
/// @brief Scale degree intervals for leaps (thirds, fourths, fifths).
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kLeapIntervals[] = {-5, -4, -3, 3, 4, 5};
/// @brief Number of entries in kLeapIntervals.
[[maybe_unused]] constexpr int kLeapCount = 6;

/// @brief Step intervals biased downward for Noble character.
/// Favors descending motion (-2, -1) over ascending (+1).
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kNobleStepIntervals[] = {-2, -2, -1, -1, 1};
/// @brief Number of entries in kNobleStepIntervals.
[[maybe_unused]] constexpr int kNobleStepCount = 5;

/// @brief Leap intervals biased downward for Noble character.
/// Favors descending leaps.
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kNobleLeapIntervals[] = {-5, -4, -3, -3, 3, 4};
/// @brief Number of entries in kNobleLeapIntervals.
[[maybe_unused]] constexpr int kNobleLeapCount = 6;

/// @brief Chromatic semitone intervals for Restless character.
/// Encourages semitone steps (minor 2nds), including chromatic motion.
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kRestlessChromaticSteps[] = {-1, 1, -1, 1, -2, 2};
/// @brief Number of entries in kRestlessChromaticSteps.
[[maybe_unused]] constexpr int kRestlessChromaticCount = 6;

/// @brief Leap intervals for Restless character.
/// Includes tritone (6 semitones = augmented 4th equivalent in degrees)
/// and wide leaps for instability.
/// Retained for future interval-based generation variants.
[[maybe_unused]] constexpr int kRestlessLeapIntervals[] = {-5, -4, -3, 3, 4, 5, -6, 6};
/// @brief Number of entries in kRestlessLeapIntervals.
[[maybe_unused]] constexpr int kRestlessLeapCount = 8;

/// @brief Apply random rhythm variation to a template duration.
/// @param base_dur Base duration from template.
/// @param gen RNG.
/// @return Varied duration, clamped to [kTicksPerBeat/4, kTicksPerBeat*3].
Tick varyDuration(Tick base_dur, std::mt19937& gen) {
  if (!rng::rollProbability(gen, 0.30f)) return base_dur;
  // Duration step table.
  constexpr Tick kDurSteps[] = {
      kTicksPerBeat / 4, kTicksPerBeat / 2, kTicksPerBeat,
      kTicksPerBeat * 3 / 2, kTicksPerBeat * 2, kTicksPerBeat * 3};
  constexpr int kNumSteps = 6;
  // Find current step index.
  int cur_idx = 2;  // Default to quarter note.
  for (int step = 0; step < kNumSteps; ++step) {
    if (kDurSteps[step] >= base_dur) {
      cur_idx = step;
      break;
    }
  }
  // Shift +/-1 step.
  int shift = rng::rollProbability(gen, 0.5f) ? 1 : -1;
  int new_idx = std::max(0, std::min(kNumSteps - 1, cur_idx + shift));
  return kDurSteps[new_idx];
}

}  // namespace

// ---------------------------------------------------------------------------
// SubjectGenerator
// ---------------------------------------------------------------------------

Subject SubjectGenerator::generate(const FugueConfig& config,
                                   uint32_t seed) const {
  Subject subject;
  subject.key = config.key;
  subject.is_minor = config.is_minor;
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

  subject.notes = generateNotes(config.character, config.key, config.is_minor,
                                bars, seed);
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
    SubjectCharacter character, Key key, bool is_minor, uint8_t bars,
    uint32_t seed) const {
  std::mt19937 gen(seed);
  int key_offset = static_cast<int>(key);
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  Tick total_ticks = static_cast<Tick>(bars) * kTicksPerBar;

  // Get design values (Principle 4: fixed per character).
  GoalTone goal = goalToneForCharacter(character);

  // Select template pair from 4 options (Principle 3: fixed set of choices).
  uint32_t template_idx = gen() % 4;
  auto [motif_a, motif_b] = motifTemplatesForCharacter(character, template_idx);

  // Start on tonic (degree 0) or dominant (degree 4).
  constexpr int kBaseNote = 60;
  int start_degree = rng::rollProbability(gen, 0.6f) ? 0 : 4;

  CharacterParams params = getCharacterParams(character);

  // Compute goal tone position and pitch.
  Tick climax_tick = static_cast<Tick>(static_cast<float>(total_ticks) * goal.position_ratio);
  int start_pitch = degreeToPitch(start_degree, kBaseNote, key_offset, scale);
  int tonic_base = degreeToPitch(0, kBaseNote, key_offset, scale);

  // Establish pitch bounds so the overall range stays within max_range.
  int pitch_floor = std::min(start_pitch, tonic_base);
  int pitch_ceil = pitch_floor + params.max_range;

  // Clamp to valid MIDI range.
  pitch_floor = std::max(36, pitch_floor);
  pitch_ceil = std::min(96, pitch_ceil);

  int climax_pitch =
      pitch_floor +
      static_cast<int>(static_cast<float>(pitch_ceil - pitch_floor) * goal.pitch_ratio);
  climax_pitch = std::max(pitch_floor, std::min(pitch_ceil, climax_pitch));

  // Phase 1: Generate notes using Motif A (ascending toward climax).
  std::vector<NoteEvent> result;
  Tick current_tick = 0;
  int current_degree = start_degree;
  int current_pitch = start_pitch;

  // Place Motif A notes, scaling degrees toward the climax.
  for (size_t idx = 0;
       idx < motif_a.degree_offsets.size() && current_tick < climax_tick; ++idx) {
    int target_degree = start_degree + motif_a.degree_offsets[idx];

    // Bias the degree toward the climax pitch direction.
    int target_pitch = degreeToPitch(target_degree, kBaseNote, key_offset, scale);

    // Scale toward climax: interpolate pitch between start and climax.
    float progress =
        (climax_tick > 0)
            ? static_cast<float>(current_tick) / static_cast<float>(climax_tick)
            : 0.0f;
    int interp_pitch =
        start_pitch +
        static_cast<int>(
            static_cast<float>(climax_pitch - start_pitch) * progress);

    // Use the motif template's degree offset but shift to follow the
    // interpolated path.
    int degree_shift = 0;
    if (target_pitch < interp_pitch) {
      degree_shift = (interp_pitch - target_pitch + 1) / 2;  // Rough degree approx
    }
    int adjusted_degree = target_degree + degree_shift;

    // Interval fluctuation: +/-1 degree with 40% probability.
    if (rng::rollProbability(gen, 0.40f)) {
      adjusted_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
    }

    int pitch = degreeToPitch(adjusted_degree, kBaseNote, key_offset, scale);
    pitch = std::max(pitch_floor, std::min(pitch_ceil, pitch));

    Tick duration = (idx < motif_a.durations.size())
                        ? varyDuration(motif_a.durations[idx], gen)
                        : kTicksPerBeat;
    if (current_tick + duration > climax_tick) {
      duration = climax_tick - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = duration;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = 0;
    result.push_back(note);

    current_tick += duration;
    current_degree = adjusted_degree;
    current_pitch = pitch;
  }

  // Climax note: placed at the goal tone position with the climax pitch.
  if (current_tick < total_ticks) {
    Tick climax_dur = kTicksPerBeat;
    if (current_tick + climax_dur > total_ticks) {
      climax_dur = total_ticks - current_tick;
    }

    NoteEvent climax_note;
    climax_note.start_tick = current_tick;
    climax_note.duration = climax_dur;
    climax_note.pitch = static_cast<uint8_t>(climax_pitch);
    climax_note.velocity = 80;
    climax_note.voice = 0;
    result.push_back(climax_note);

    current_tick += climax_dur;
    current_degree =
        start_degree +
        static_cast<int>(goal.pitch_ratio * static_cast<float>(params.max_range) / 2.0f);
    current_pitch = climax_pitch;
  }

  // Phase 2: Generate notes using Motif B (descending from climax toward tonic).
  int tonic_pitch = degreeToPitch(0, kBaseNote, key_offset, scale);
  tonic_pitch = std::max(pitch_floor, std::min(pitch_ceil, tonic_pitch));

  for (size_t idx = 0;
       idx < motif_b.degree_offsets.size() && current_tick < total_ticks; ++idx) {
    int offset = motif_b.degree_offsets[idx];

    // Scale the descent: interpolate from climax toward tonic.
    float remaining_ratio =
        (total_ticks > climax_tick)
            ? static_cast<float>(current_tick - climax_tick) /
                  static_cast<float>(total_ticks - climax_tick)
            : 1.0f;
    int interp_pitch =
        climax_pitch +
        static_cast<int>(
            static_cast<float>(tonic_pitch - climax_pitch) * remaining_ratio);

    // Apply the motif offset relative to current position.
    int target_degree = current_degree + offset;
    int target_pitch = degreeToPitch(target_degree, kBaseNote, key_offset, scale);

    // Nudge toward interpolated path.
    int degree_shift = 0;
    if (target_pitch > interp_pitch + 2) {
      degree_shift = -1;
    } else if (target_pitch < interp_pitch - 2) {
      degree_shift = 1;
    }
    int adjusted_degree = target_degree + degree_shift;

    // Interval fluctuation: +/-1 degree with 40% probability.
    if (rng::rollProbability(gen, 0.40f)) {
      adjusted_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
    }

    int pitch = degreeToPitch(adjusted_degree, kBaseNote, key_offset, scale);
    pitch = std::max(pitch_floor, std::min(pitch_ceil, pitch));

    Tick duration = (idx < motif_b.durations.size())
                        ? varyDuration(motif_b.durations[idx], gen)
                        : kTicksPerBeat;
    if (current_tick + duration > total_ticks) {
      duration = total_ticks - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = duration;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = 0;
    result.push_back(note);

    current_tick += duration;
    current_degree = adjusted_degree;
    current_pitch = pitch;
  }

  // Fill remaining time by repeating Motif A rhythm pattern (rhythm reuse).
  if (current_tick < total_ticks) {
    size_t rhythm_idx = 0;
    while (current_tick < total_ticks) {
      Tick dur = motif_a.durations[rhythm_idx % motif_a.durations.size()];
      if (current_tick + dur > total_ticks) {
        dur = total_ticks - current_tick;
        if (dur < kTicksPerBeat / 4) break;
      }

      // Continue stepping toward tonic.
      if (current_pitch > tonic_pitch + 2) {
        current_degree -= 1;
      } else if (current_pitch < tonic_pitch - 2) {
        current_degree += 1;
      }
      int pitch = degreeToPitch(current_degree, kBaseNote, key_offset, scale);
      pitch = std::max(pitch_floor, std::min(pitch_ceil, pitch));
      current_pitch = pitch;

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = static_cast<uint8_t>(pitch);
      note.velocity = 80;
      note.voice = 0;
      result.push_back(note);

      current_tick += dur;
      ++rhythm_idx;
    }
  }

  // Ending: 50% tonic, 50% dominant (degree 4) for variety.
  if (!result.empty()) {
    if (rng::rollProbability(gen, 0.50f)) {
      result.back().pitch = static_cast<uint8_t>(tonic_pitch);
    } else {
      int dominant_pitch = degreeToPitch(4, kBaseNote, key_offset, scale);
      dominant_pitch = std::max(pitch_floor, std::min(pitch_ceil, dominant_pitch));
      result.back().pitch = static_cast<uint8_t>(dominant_pitch);
    }
  }

  return result;
}

}  // namespace bach
