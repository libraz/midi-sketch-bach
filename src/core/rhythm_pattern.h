// Rhythm pattern system for beat subdivision based on SubjectCharacter.

#ifndef BACH_CORE_RHYTHM_PATTERN_H
#define BACH_CORE_RHYTHM_PATTERN_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Rhythmic pattern types for note subdivision within a beat.
///
/// Each pattern defines how a beat (kTicksPerBeat = 480 ticks) is divided.
/// The beat grid itself (kTicksPerBeat boundaries) is never changed.
/// DottedQuarter is the exception: it spans 2 beats (960 ticks).
enum class RhythmPattern : uint8_t {
  Straight,       ///< [480] equal quarter notes
  DottedEighth,   ///< [360, 120] dotted eighth + sixteenth
  DottedQuarter,  ///< [720, 240] dotted quarter + eighth (spans 2 beats)
  Syncopated,     ///< [240, 240] equal eighths with off-beat accent
  LombardReverse, ///< [120, 360] sixteenth + dotted eighth (Lombard rhythm)
  Triplet         ///< [160, 160, 160] triplet subdivision (3 per beat)
};

/// @brief Convert RhythmPattern to human-readable string.
/// @param pattern The rhythm pattern enum value.
/// @return Null-terminated string representation.
const char* rhythmPatternToString(RhythmPattern pattern);

/// @brief Get the duration values for one cycle of a rhythm pattern.
///
/// Returns a vector of tick durations for a single pattern cycle.
/// Most patterns sum to kTicksPerBeat (480 ticks).
/// DottedQuarter sums to 2 * kTicksPerBeat (960 ticks) because it spans two beats.
///
/// @param pattern The rhythm pattern.
/// @return Vector of durations in ticks.
std::vector<Tick> getPatternDurations(RhythmPattern pattern);

/// @brief Get the number of notes produced per beat for a pattern.
///
/// For DottedQuarter (which spans 2 beats), this returns 1 -- meaning
/// one note onset per beat on average (2 notes across 2 beats).
///
/// @param pattern The rhythm pattern.
/// @return Number of notes per beat.
uint8_t notesPerBeat(RhythmPattern pattern);

/// @brief Get allowed rhythm patterns for a SubjectCharacter.
///
/// Per design (Principle 3: Reduce Generation), each character maps to
/// a fixed, curated set of rhythmic patterns:
///   - Severe:   {Straight, DottedQuarter}
///   - Playful:  {Straight, DottedEighth, Triplet, LombardReverse}
///   - Noble:    {Straight, DottedQuarter}
///   - Restless: {DottedEighth, Syncopated, LombardReverse, Triplet}
///
/// @param character The subject character.
/// @return Vector of allowed patterns (never empty).
std::vector<RhythmPattern> getAllowedPatterns(SubjectCharacter character);

/// @brief Select a rhythm pattern from the allowed set using a deterministic choice.
///
/// Uses modular indexing (seed_value % allowed.size()) to select from the
/// character's allowed patterns. Same inputs always produce same output.
///
/// @param character The subject character (determines allowed patterns).
/// @param seed_value A value used to deterministically select a pattern.
/// @return Selected pattern from the allowed set.
RhythmPattern selectPattern(SubjectCharacter character, uint32_t seed_value);

/// @brief Apply a rhythm pattern to generate note durations for a given span.
///
/// Fills the span with repeated pattern cycles. The last cycle may be
/// truncated to fit exactly within total_ticks. All returned durations
/// are guaranteed to be positive, and their sum equals total_ticks.
///
/// @param pattern The rhythm pattern to apply.
/// @param total_ticks Total duration to fill with the pattern. Must be > 0.
/// @return Vector of individual note durations that sum to total_ticks.
std::vector<Tick> applyPatternToSpan(RhythmPattern pattern, Tick total_ticks);

}  // namespace bach

#endif  // BACH_CORE_RHYTHM_PATTERN_H
