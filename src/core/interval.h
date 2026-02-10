// Additional interval utilities -- human-readable names, compound interval
// reduction, and interval category queries.

#ifndef BACH_CORE_INTERVAL_H
#define BACH_CORE_INTERVAL_H

#include "core/pitch_utils.h"

namespace bach {
namespace interval_util {

/// @brief Get a human-readable name for an interval in semitones.
/// @param semitones Interval size (may be negative or compound).
/// @return Null-terminated string such as "Perfect 5th", "Minor 3rd".
///         Returns "Unknown" for out-of-range values after reduction.
const char* intervalName(int semitones);

/// @brief Reduce a compound interval to its simple equivalent (0-11).
/// @param semitones Interval size in semitones (may be negative or compound).
/// @return Simple interval in range [0, 11].
///
/// Examples: 19 (compound 5th) -> 7, -3 -> 3, 24 (double octave) -> 0.
int compoundToSimple(int semitones);

/// @brief Check whether a simple interval is a perfect interval.
/// @param semitones Interval size in semitones (compound intervals are reduced).
/// @return True for unison (0), perfect 4th (5), perfect 5th (7), and octave (12).
///
/// The perfect 4th is included here as a "perfect" interval in the traditional
/// music-theory sense, regardless of its counterpoint classification.
bool isPerfectInterval(int semitones);

/// @brief Check whether a simple interval is a consonance (perfect or imperfect).
/// @param semitones Interval size in semitones (compound intervals are reduced).
/// @return True for unison, m3, M3, P5, m6, M6, and octave.
///
/// Note: Perfect 4th returns false here (dissonant in two-voice counterpoint).
/// Use classifyInterval() from pitch_utils.h for full counterpoint classification.
bool isConsonance(int semitones);

/// @brief Calculate the inversion of a simple interval.
/// @param semitones Simple interval (0-12). Compound intervals are reduced first.
/// @return Inverted interval: e.g. M3 (4) -> m6 (8), P5 (7) -> P4 (5).
int invertInterval(int semitones);

}  // namespace interval_util
}  // namespace bach

#endif  // BACH_CORE_INTERVAL_H
