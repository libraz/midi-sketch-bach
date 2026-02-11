/// @file
/// @brief Fugue subject generation with character-driven note and rhythm selection.

#include "fugue/subject.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
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
  float leap_prob;            // Probability of a leap (vs step)
  int max_range_degrees;      // Maximum pitch range in scale degrees
  const Tick* durations;      // Available duration values
  int dur_count;              // Number of duration values
};

/// @brief Return generation parameters for the given subject character.
/// @param character The SubjectCharacter to look up.
/// @return CharacterParams with leap probability, range (degrees), and duration table.
CharacterParams getCharacterParams(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {0.15f, 5, kSevereDurations, kSevereDurCount};    // 6th (~9 semitones)
    case SubjectCharacter::Playful:
      return {0.45f, 7, kPlayfulDurations, kPlayfulDurCount};  // octave (12 semitones)
    case SubjectCharacter::Noble:
      return {0.25f, 6, kNobleDurations, kNobleDurCount};      // 7th (~11 semitones)
    case SubjectCharacter::Restless:
      return {0.40f, 8, kRestlessDurations, kRestlessDurCount}; // 9th (~14 semitones)
  }
  return {0.20f, 5, kSevereDurations, kSevereDurCount};
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

/// @brief Metrically neutral pair substitution for rhythm variation.
///
/// Replaces adjacent note pairs with alternatives that preserve the combined
/// duration, preventing beat displacement. Character-specific constraints
/// control which substitutions are allowed and their probability.
///
/// @param dur_a Duration of the first note in the pair.
/// @param dur_b Duration of the second note in the pair.
/// @param character Subject character type (controls allowed substitutions).
/// @param gen RNG.
/// @param[out] out_a Output: new duration for the first note.
/// @param[out] out_b Output: new duration for the second note.
void varyDurationPair(Tick dur_a, Tick dur_b, SubjectCharacter character,
                      std::mt19937& gen, Tick& out_a, Tick& out_b) {
  out_a = dur_a;
  out_b = dur_b;

  // Determine probability and allowed substitutions by character.
  float prob = 0.0f;
  switch (character) {
    case SubjectCharacter::Severe:
      prob = 0.15f;
      break;
    case SubjectCharacter::Playful:
      prob = 0.30f;
      break;
    case SubjectCharacter::Noble:
      prob = 0.15f;
      break;
    case SubjectCharacter::Restless:
      prob = 0.30f;
      break;
  }

  if (!rng::rollProbability(gen, prob)) return;

  Tick sum = dur_a + dur_b;

  switch (character) {
    case SubjectCharacter::Severe:
      // Only Q → 8+8 division.
      if (dur_a == kQuarterNote && dur_b == kQuarterNote) {
        // Q+Q (960) → 8+8+8+8 is not pair-preserving.
        // Instead: Q+Q → Q+8+8 would change note count.
        // Pair substitution: just swap long/short within pair.
        if (rng::rollProbability(gen, 0.5f)) {
          // Keep durations but swap order if they differ.
          out_a = dur_b;
          out_b = dur_a;
        }
      }
      break;

    case SubjectCharacter::Noble:
      // Only H → DQ+8 allowed. Never subdivide below quarter note.
      if (sum >= kHalfNote + kQuarterNote) {
        // DH+Q or similar: allow DH+Q ↔ H+H if sum matches.
        if (sum == kDottedHalf + kQuarterNote) {
          out_a = kHalfNote;
          out_b = kHalfNote;
        } else if (sum == kHalfNote + kHalfNote) {
          out_a = kDottedHalf;
          out_b = kQuarterNote;
        }
      } else if (sum == kHalfNote) {
        // H → DQ+8 is allowed.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        }
      }
      break;

    case SubjectCharacter::Playful:
    case SubjectCharacter::Restless:
      // All substitutions allowed.
      if (sum == kQuarterNote + kQuarterNote) {
        // Q+Q (960) → DQ+8 or 8+DQ.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        } else {
          out_a = kEighthNote;
          out_b = kDottedQuarter;
        }
      } else if (sum == kEighthNote + kEighthNote) {
        // 8+8 (480) → D8+16 or 16+D8.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedEighth;
          out_b = kSixteenthNote;
        } else {
          out_a = kSixteenthNote;
          out_b = kDottedEighth;
        }
      } else if (sum == kHalfNote) {
        // H (960) → DQ+8 or 8+DQ.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        } else {
          out_a = kEighthNote;
          out_b = kDottedQuarter;
        }
      }
      break;
  }
}

/// @brief Snap a pitch to the nearest scale tone within bounds.
/// @param pitch Raw pitch value.
/// @param key Musical key.
/// @param scale Scale type.
/// @param floor_pitch Minimum allowed pitch.
/// @param ceil_pitch Maximum allowed pitch.
/// @return Scale-snapped, clamped MIDI pitch.
int snapToScale(int pitch, Key key, ScaleType scale, int floor_pitch,
                int ceil_pitch) {
  pitch = std::max(floor_pitch, std::min(ceil_pitch, pitch));
  int snapped = static_cast<int>(scale_util::nearestScaleTone(
      static_cast<uint8_t>(std::max(0, std::min(127, pitch))), key, scale));
  // Ensure snapped result respects ceiling (nearestScaleTone may snap up).
  if (snapped > ceil_pitch) {
    // Find the scale tone below the ceiling.
    int abs_deg = scale_util::pitchToAbsoluteDegree(
        static_cast<uint8_t>(std::max(0, std::min(127, ceil_pitch))),
        key, scale);
    int candidate = static_cast<int>(
        scale_util::absoluteDegreeToPitch(abs_deg, key, scale));
    if (candidate > ceil_pitch && abs_deg > 0) {
      candidate = static_cast<int>(
          scale_util::absoluteDegreeToPitch(abs_deg - 1, key, scale));
    }
    snapped = std::max(floor_pitch, candidate);
  }
  return snapped;
}

/// @brief Quantize a tick position to the nearest strong beat (beat 1 or 3).
/// @param raw_tick Raw tick position.
/// @param character Subject character type (Noble always uses beat 1).
/// @param total_ticks Total subject length for bounds checking.
/// @return Quantized tick on beat 1 or beat 3.
Tick quantizeToStrongBeat(Tick raw_tick, SubjectCharacter character,
                          Tick total_ticks) {
  Tick bar = raw_tick / kTicksPerBar;
  Tick beat1 = bar * kTicksPerBar;
  Tick beat3 = bar * kTicksPerBar + kTicksPerBeat * 2;

  Tick result = beat1;

  if (character == SubjectCharacter::Noble) {
    // Noble: always beat 1 for gravitas.
    result = beat1;
  } else {
    // Snap to nearest strong beat.
    int dist_beat1 = std::abs(static_cast<int>(raw_tick) - static_cast<int>(beat1));
    int dist_beat3 = std::abs(static_cast<int>(raw_tick) - static_cast<int>(beat3));
    result = (dist_beat1 <= dist_beat3) ? beat1 : beat3;
  }

  // Ensure the climax is not at the very beginning — leave at least 1 bar
  // for Motif A to develop.
  if (result < kTicksPerBar && total_ticks > kTicksPerBar) {
    result = kTicksPerBar;  // Beat 1 of bar 1.
  }

  return result;
}

/// @brief Cadential degree formula per character (degrees relative to tonic).
struct CadentialFormula {
  const int* degrees;     // Scale degrees from tonic (0 = tonic).
  const Tick* durations;  // Duration for each note.
  int count;              // Number of notes.
};

constexpr int kSevereCadDegrees[] = {2, 1, 0};
constexpr Tick kSevereCadDurations[] = {kQuarterNote, kQuarterNote, kHalfNote};

constexpr int kPlayfulCadDegrees[] = {4, 3, 2, 1, 0};
constexpr Tick kPlayfulCadDurations[] = {
    kEighthNote, kEighthNote, kEighthNote, kEighthNote, kQuarterNote};

constexpr int kNobleCadDegrees[] = {1, 0};
constexpr Tick kNobleCadDurations[] = {kHalfNote, kHalfNote};

constexpr int kRestlessCadDegrees[] = {3, 2, 1, 0};
constexpr Tick kRestlessCadDurations[] = {
    kEighthNote, kEighthNote, kEighthNote, kQuarterNote};

/// @brief Get the cadential formula for a given character.
CadentialFormula getCadentialFormula(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {kSevereCadDegrees, kSevereCadDurations, 3};
    case SubjectCharacter::Playful:
      return {kPlayfulCadDegrees, kPlayfulCadDurations, 5};
    case SubjectCharacter::Noble:
      return {kNobleCadDegrees, kNobleCadDurations, 2};
    case SubjectCharacter::Restless:
      return {kRestlessCadDegrees, kRestlessCadDurations, 4};
  }
  return {kSevereCadDegrees, kSevereCadDurations, 3};
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

  // [Fix 2] Compute pitch bounds via scale degrees using absoluteDegreeToPitch.
  int start_pitch = degreeToPitch(start_degree, kBaseNote, key_offset, scale);
  int tonic_base = degreeToPitch(0, kBaseNote, key_offset, scale);

  int tonic_abs = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(tonic_base), key, scale);
  int start_abs = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(start_pitch), key, scale);
  int floor_abs = std::min(start_abs, tonic_abs);
  int ceil_abs = floor_abs + params.max_range_degrees;

  int pitch_floor = static_cast<int>(
      scale_util::absoluteDegreeToPitch(floor_abs, key, scale));
  int pitch_ceil = static_cast<int>(
      scale_util::absoluteDegreeToPitch(ceil_abs, key, scale));

  // Clamp to valid MIDI range.
  pitch_floor = std::max(36, pitch_floor);
  pitch_ceil = std::min(96, pitch_ceil);

  // [Fix 2] Compute climax pitch via scale degrees.
  // Ensure climax is at least 1 degree above start to guarantee a true peak.
  int climax_abs = floor_abs +
      static_cast<int>(static_cast<float>(ceil_abs - floor_abs) *
                        goal.pitch_ratio);
  if (climax_abs <= start_abs) {
    climax_abs = std::min(start_abs + 1, ceil_abs);
  }
  int climax_pitch = static_cast<int>(
      scale_util::absoluteDegreeToPitch(climax_abs, key, scale));
  climax_pitch = std::max(pitch_floor, std::min(pitch_ceil, climax_pitch));

  // [Fix 4] Quantize climax to strong beat (beat 1 or beat 3 in 4/4).
  Tick raw_climax_tick =
      static_cast<Tick>(static_cast<float>(total_ticks) * goal.position_ratio);
  Tick climax_tick = quantizeToStrongBeat(raw_climax_tick, character, total_ticks);
  // Ensure climax_tick is within valid range.
  if (climax_tick >= total_ticks) {
    climax_tick = (total_ticks > kTicksPerBar) ? total_ticks - kTicksPerBar : 0;
  }

  // Phase 1: Generate notes using Motif A (ascending toward climax).
  std::vector<NoteEvent> result;
  Tick current_tick = 0;

  // Place Motif A notes, scaling degrees toward the climax.
  // [Fix 1] Use varyDurationPair for metrically neutral rhythm variation.
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
      degree_shift = (interp_pitch - target_pitch + 1) / 2;
    }
    int adjusted_degree = target_degree + degree_shift;

    // [Fix 5] Interval fluctuation: 20%, skip first note (opening interval).
    if (idx > 0 && rng::rollProbability(gen, 0.20f)) {
      adjusted_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
    }

    // [Fix 2] Snap pitch to scale after all adjustments.
    // Cap below climax so the designed climax is the true peak.
    int pitch = snapToScale(
        degreeToPitch(adjusted_degree, kBaseNote, key_offset, scale),
        key, scale, pitch_floor, climax_pitch - 1);

    // [Fix 1] Apply pair substitution for durations.
    Tick duration = (idx < motif_a.durations.size())
                        ? motif_a.durations[idx]
                        : kTicksPerBeat;
    // Apply pair substitution if we have a next note.
    if (idx + 1 < motif_a.durations.size() && idx + 1 < motif_a.degree_offsets.size()) {
      Tick next_dur = motif_a.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, character, gen, new_a, new_b);
      duration = new_a;
      // Store the modified next duration back for when idx+1 is processed.
      // Since motif_a.durations is const, we apply pair variation only to
      // even-indexed pairs to avoid double-processing.
      if (idx % 2 == 0) {
        // We'll use the pair result. Skip pair variation on next iteration.
        // For simplicity, only vary even-indexed pairs.
        duration = new_a;
      }
    }

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
  }

  // Climax note: placed at the quantized climax_tick position.
  // [Fix 3] Store climax absolute degree for Motif B reference.
  // [Fix 4] Bridge the gap between Motif A end and climax_tick by extending
  // the last Motif A note's duration.
  int climax_abs_degree = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(std::max(0, std::min(127, climax_pitch))),
      key, scale);

  if (current_tick < climax_tick && !result.empty()) {
    // Extend last Motif A note to reach the climax tick.
    result.back().duration += (climax_tick - current_tick);
    current_tick = climax_tick;
  }

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
  }

  // Phase 2: Generate notes using Motif B (descending from climax toward tonic).
  int tonic_pitch = degreeToPitch(0, kBaseNote, key_offset, scale);
  tonic_pitch = std::max(pitch_floor, std::min(pitch_ceil, tonic_pitch));

  bool last_fluctuated = false;  // [Fix 5] Track consecutive fluctuation.

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

    // [Fix 3] Apply the motif offset relative to climax degree, not cumulative.
    int target_degree_abs = climax_abs_degree + offset;
    int target_pitch = static_cast<int>(
        scale_util::absoluteDegreeToPitch(target_degree_abs, key, scale));

    // Nudge toward interpolated path.
    int degree_shift = 0;
    if (target_pitch > interp_pitch + 2) {
      degree_shift = -1;
    } else if (target_pitch < interp_pitch - 2) {
      degree_shift = 1;
    }
    int adjusted_abs_degree = target_degree_abs + degree_shift;

    // [Fix 5] Interval fluctuation: 20%, never two consecutive, skip first note.
    bool fluctuated = false;
    if (idx > 0 && !last_fluctuated && rng::rollProbability(gen, 0.20f)) {
      adjusted_abs_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
      fluctuated = true;
    }
    last_fluctuated = fluctuated;

    // [Fix 2] Snap to scale. Cap below climax (descent phase).
    int pitch = snapToScale(
        static_cast<int>(scale_util::absoluteDegreeToPitch(
            adjusted_abs_degree, key, scale)),
        key, scale, pitch_floor, climax_pitch - 1);

    // [Fix 1] Apply pair substitution for durations.
    Tick duration = (idx < motif_b.durations.size())
                        ? motif_b.durations[idx]
                        : kTicksPerBeat;
    if (idx % 2 == 0 && idx + 1 < motif_b.durations.size()) {
      Tick next_dur = motif_b.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, character, gen, new_a, new_b);
      duration = new_a;
    }

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
  }

  // [Fix 6] Replace fill loop with character-specific cadential formula.
  if (current_tick < total_ticks) {
    CadentialFormula cadence = getCadentialFormula(character);
    int tonic_abs_degree = scale_util::pitchToAbsoluteDegree(
        static_cast<uint8_t>(std::max(0, std::min(127, tonic_pitch))),
        key, scale);

    // Calculate total cadence duration.
    Tick cadence_total = 0;
    for (int ci = 0; ci < cadence.count; ++ci) {
      cadence_total += cadence.durations[ci];
    }

    Tick remaining = total_ticks - current_tick;

    // Determine how many formula notes fit. Truncate from the beginning
    // if remaining time < formula duration.
    int start_idx = 0;
    if (remaining < cadence_total) {
      // Truncate from beginning: skip notes until we fit.
      Tick accumulated = 0;
      for (int ci = 0; ci < cadence.count; ++ci) {
        if (cadence_total - accumulated <= remaining) {
          start_idx = ci;
          break;
        }
        accumulated += cadence.durations[ci];
      }
    }

    for (int ci = start_idx; ci < cadence.count && current_tick < total_ticks;
         ++ci) {
      Tick dur = cadence.durations[ci];
      if (current_tick + dur > total_ticks) {
        dur = total_ticks - current_tick;
        if (dur < kTicksPerBeat / 4) break;
      }

      int degree_abs = tonic_abs_degree + cadence.degrees[ci];
      int pitch = snapToScale(
          static_cast<int>(scale_util::absoluteDegreeToPitch(
              degree_abs, key, scale)),
          key, scale, pitch_floor, climax_pitch - 1);

      NoteEvent note;
      note.start_tick = current_tick;
      note.duration = dur;
      note.pitch = static_cast<uint8_t>(pitch);
      note.velocity = 80;
      note.voice = 0;
      result.push_back(note);

      current_tick += dur;
    }
  }

  // [Fix 7] Ending: 70% dominant, 30% tonic.
  // Bach subjects predominantly end on the dominant to facilitate the answer.
  // Ending pitch is not capped below climax — dominant/tonic are structural.
  if (!result.empty()) {
    if (rng::rollProbability(gen, 0.70f)) {
      int dominant_pitch = degreeToPitch(4, kBaseNote, key_offset, scale);
      dominant_pitch = snapToScale(dominant_pitch, key, scale,
                                   pitch_floor, pitch_ceil);
      result.back().pitch = static_cast<uint8_t>(dominant_pitch);
    } else {
      result.back().pitch = static_cast<uint8_t>(tonic_pitch);
    }
  }

  return result;
}

}  // namespace bach
