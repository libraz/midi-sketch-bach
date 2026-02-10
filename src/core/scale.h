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

/// @brief Convert a MIDI pitch to an absolute scale degree spanning octaves.
///
/// The absolute degree encodes both the octave and the within-octave degree:
/// `abs_degree = octave * 7 + degree_in_octave`, where octave = pitch / 12.
/// Non-scale pitches are snapped to the nearest scale tone before conversion.
///
/// @param pitch MIDI note number (0-127).
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type.
/// @return Absolute scale degree (always non-negative for valid MIDI pitches).
int pitchToAbsoluteDegree(uint8_t pitch, Key key, ScaleType scale);

/// @brief Convert an absolute scale degree back to a MIDI pitch.
///
/// Inverse of pitchToAbsoluteDegree. Computes the octave from `abs_degree / 7`
/// and the within-octave degree from `abs_degree % 7`, then maps to the
/// corresponding MIDI pitch via the scale interval table.
///
/// @param abs_degree Absolute scale degree (may be negative for very low pitches).
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type.
/// @return MIDI pitch clamped to [0, 127].
uint8_t absoluteDegreeToPitch(int abs_degree, Key key, ScaleType scale);

}  // namespace scale_util
}  // namespace bach

#endif  // BACH_CORE_SCALE_H
