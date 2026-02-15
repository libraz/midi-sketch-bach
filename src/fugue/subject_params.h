/// @file
/// @brief Subject generation parameters and helper functions.
/// Extracted from subject.cpp for testability.

#ifndef BACH_FUGUE_SUBJECT_PARAMS_H
#define BACH_FUGUE_SUBJECT_PARAMS_H

#include <cstdint>
#include <random>

#include "core/basic_types.h"
#include "fugue/motif_template.h"

namespace bach {

// ---------------------------------------------------------------------------
// Duration constants
// ---------------------------------------------------------------------------

/// @brief Half note duration in ticks.
constexpr Tick kHalfNote = kTicksPerBeat * 2;  // 960
/// @brief Quarter note duration in ticks.
constexpr Tick kQuarterNote = kTicksPerBeat;  // 480
/// @brief Eighth note duration in ticks.
constexpr Tick kEighthNote = kTicksPerBeat / 2;  // 240
/// @brief Dotted quarter note duration in ticks.
constexpr Tick kDottedQuarter = kQuarterNote + kEighthNote;  // 720
/// @brief Dotted eighth note duration in ticks.
constexpr Tick kDottedEighth = kEighthNote + kEighthNote / 2;  // 360
/// @brief Dotted half note duration in ticks.
constexpr Tick kDottedHalf = kHalfNote + kQuarterNote;  // 1440
/// @brief Sixteenth note duration in ticks.
constexpr Tick kSixteenthNote = kTicksPerBeat / 4;  // 120

// ---------------------------------------------------------------------------
// Character-specific duration tables
// ---------------------------------------------------------------------------

/// @brief Durations for Severe character: even, no dots.
constexpr Tick kSevereDurations[] = {kHalfNote, kQuarterNote, kEighthNote};
constexpr int kSevereDurCount = 3;

/// @brief Durations for Playful character: includes dotted values.
constexpr Tick kPlayfulDurations[] = {
    kQuarterNote, kEighthNote, kDottedQuarter, kDottedEighth};
constexpr int kPlayfulDurCount = 4;

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

// ---------------------------------------------------------------------------
// Step/leap interval tables (retained for future use)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Parameter structs
// ---------------------------------------------------------------------------

/// @brief Parameters that shape subject generation for a given character type.
struct CharacterParams {
  float leap_prob;        ///< Probability of a leap (vs step).
  int max_range_degrees;  ///< Maximum pitch range in scale degrees.
  const Tick* durations;  ///< Available duration values.
  int dur_count;          ///< Number of duration values.
};

/// @brief Cadential degree formula per character (degrees relative to tonic).
struct CadentialFormula {
  const int* degrees;     ///< Scale degrees from tonic (0 = tonic).
  const Tick* durations;  ///< Duration for each note.
  int count;              ///< Number of notes.
};

// ---------------------------------------------------------------------------
// Cadential formula data
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

/// @brief Return generation parameters for the given subject character.
/// @param character The SubjectCharacter to look up.
/// @param rng_eng RNG engine for sampling leap probability from character-specific ranges.
/// @return CharacterParams with leap probability, range (degrees), and duration table.
CharacterParams getCharacterParams(SubjectCharacter character, std::mt19937& rng_eng);

/// @brief Clamp a leap that exceeds the character's maximum interval.
///
/// When the interval between two consecutive pitches exceeds the allowed
/// maximum, the new pitch is pulled toward the previous pitch while
/// maintaining melodic direction. For Playful/Restless characters,
/// 8-9 semitone intervals are allowed 20% of the time.
///
/// @param pitch Candidate pitch.
/// @param prev_pitch Previous note's pitch.
/// @param character Subject character type.
/// @param key Musical key for scale snapping.
/// @param scale Scale type.
/// @param pitch_floor Minimum allowed pitch.
/// @param pitch_ceil Maximum allowed pitch.
/// @param gen RNG engine (for probabilistic 6th allowance).
/// @param large_leap_count Optional pointer to large leap counter.
/// @return Adjusted pitch within the leap limit.
int clampLeap(int pitch, int prev_pitch, SubjectCharacter character,
              Key key, ScaleType scale, int pitch_floor, int pitch_ceil,
              std::mt19937& gen, int* large_leap_count = nullptr);

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
                      std::mt19937& gen, Tick& out_a, Tick& out_b);

/// @brief Avoid consecutive same-pitch notes by shifting to adjacent scale degree.
///
/// When the candidate pitch matches the previous pitch, tries moving up or down
/// by the nearest scale tone within bounds. This eliminates monotonous same-pitch
/// repetition that degrades melodic quality in fugue subjects.
///
/// @param pitch Candidate pitch.
/// @param prev_pitch Previous note's pitch (-1 if no previous note).
/// @param key Musical key.
/// @param scale Scale type.
/// @param floor_pitch Minimum allowed pitch.
/// @param ceil_pitch Maximum allowed pitch.
/// @return Adjusted pitch avoiding unison with prev_pitch, or original if unavoidable.
int avoidUnison(int pitch, int prev_pitch, Key key, ScaleType scale,
                int floor_pitch, int ceil_pitch);

/// @brief Snap a pitch to the nearest scale tone within bounds.
/// @param pitch Raw pitch value.
/// @param key Musical key.
/// @param scale Scale type.
/// @param floor_pitch Minimum allowed pitch.
/// @param ceil_pitch Maximum allowed pitch.
/// @return Scale-snapped, clamped MIDI pitch.
int snapToScale(int pitch, Key key, ScaleType scale, int floor_pitch,
                int ceil_pitch);

/// @brief Quantize a tick position to the nearest strong beat (beat 1 or 3).
/// @param raw_tick Raw tick position.
/// @param character Subject character type (Noble always uses beat 1).
/// @param total_ticks Total subject length for bounds checking.
/// @return Quantized tick on beat 1 or beat 3.
Tick quantizeToStrongBeat(Tick raw_tick, SubjectCharacter character,
                          Tick total_ticks);

/// @brief Get the cadential formula for a given character.
/// @param character Subject character type.
/// @return CadentialFormula with degrees, durations, and count.
CadentialFormula getCadentialFormula(SubjectCharacter character);

struct ArchetypePolicy;  // Forward declaration.

/// @brief Apply archetype constraints on top of character parameters.
///
/// Tightens max_range_degrees to the intersection of the character and
/// archetype ranges. Called after getCharacterParams() to narrow the
/// generation window according to the selected archetype strategy.
///
/// @param params Character parameters to constrain (modified in place).
/// @param policy Archetype policy providing range bounds.
void applyArchetypeConstraints(CharacterParams& params,
                                const ArchetypePolicy& policy);

/// @brief Get acceleration profile for a character x archetype combination.
///
/// Controls how note durations decrease from subject head to tail.
/// 20-30% chance of None (equal rhythm), remaining split 70/30 EaseIn/Linear.
///
/// @param character Subject character type.
/// @param archetype Fugue archetype.
/// @param rng RNG for stochastic selection.
/// @return AccelProfile with curve_type and min_dur.
AccelProfile getAccelProfile(SubjectCharacter character, FugueArchetype archetype,
                             std::mt19937& rng);

/// Default number of pitch path candidates for N-candidate selection.
constexpr int kDefaultPathCandidates = 8;

}  // namespace bach

#endif  // BACH_FUGUE_SUBJECT_PARAMS_H
