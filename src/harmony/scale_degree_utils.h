// Shared scale degree utilities for pitch calculation.

#ifndef BACH_HARMONY_SCALE_DEGREE_UTILS_H
#define BACH_HARMONY_SCALE_DEGREE_UTILS_H

#include <cstdint>
#include <vector>

#include "harmony/chord_types.h"

namespace bach {

/// @brief Get the scale intervals array for a key mode.
/// @param is_minor True for natural minor, false for major.
/// @return Pointer to a 7-element array of semitone offsets.
const int* scaleIntervalsForMode(bool is_minor);

/// @brief Convert a scale degree (0-based, with octave wrap) to semitone offset.
///
/// Handles octave wrapping: degree 7 = root+octave, degree 9 = 3rd+octave, etc.
/// Supports negative degrees for downward wrapping.
///
/// @param degree Scale degree (0=root, 1=2nd, ..., 6=7th; supports >= 7 for octaves).
/// @param is_minor True for natural minor intervals.
/// @return Semitone offset from tonic (may exceed 12 for higher degrees).
int degreeToPitchOffset(int degree, bool is_minor);

/// @brief Get standard chord tone degrees for a chord quality.
///
/// Returns pattern degrees (0-based from chord root) for the chord type:
///   - Major/Minor/Diminished/Augmented triad: root(0), 3rd(2), 5th(4)
///   - Seventh chords: root(0), 3rd(2), 5th(4), 7th(6)
///
/// @param quality The chord quality.
/// @return Vector of scale degrees for arpeggio generation.
std::vector<int> getChordDegrees(ChordQuality quality);

}  // namespace bach

#endif  // BACH_HARMONY_SCALE_DEGREE_UTILS_H
