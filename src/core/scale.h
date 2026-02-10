// Scale degree utilities -- membership testing and pitch snapping.

#ifndef BACH_CORE_SCALE_H
#define BACH_CORE_SCALE_H

#include <cstdint>

#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach {
namespace scale_util {

/// Number of degrees in a diatonic scale.
constexpr int kScaleDegreeCount = 7;

/// @brief Get the number of degrees in a diatonic scale.
/// @return Always 7.
inline constexpr int scaleDegreeCount() { return kScaleDegreeCount; }

/// @brief Check if a MIDI pitch belongs to a given scale in a given key.
/// @param pitch MIDI note number (0-127).
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type (Major, NaturalMinor, etc.).
/// @return True if the pitch class matches one of the 7 scale degrees.
bool isScaleTone(uint8_t pitch, Key key, ScaleType scale);

/// @brief Snap a MIDI pitch to the nearest scale tone.
/// @param pitch MIDI note number (0-127).
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type.
/// @return The nearest MIDI pitch that belongs to the scale. If equidistant
///         between two scale tones, the lower pitch is returned.
///         Result is clamped to [0, 127].
uint8_t nearestScaleTone(uint8_t pitch, Key key, ScaleType scale);

/// @brief Get the scale degree (0-6) of a pitch within a key/scale.
/// @param pitch MIDI note number.
/// @param key Musical key.
/// @param scale Scale type.
/// @param out_degree Output: scale degree (0 = root, 6 = 7th) if found.
/// @return True if the pitch is a scale tone, false otherwise.
bool pitchToScaleDegree(uint8_t pitch, Key key, ScaleType scale,
                        int& out_degree);

}  // namespace scale_util
}  // namespace bach

#endif  // BACH_CORE_SCALE_H
