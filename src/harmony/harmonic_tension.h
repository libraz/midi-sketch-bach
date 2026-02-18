// Harmonic tension computation -- shared utility for chord-based tension levels.

#ifndef BACH_HARMONY_HARMONIC_TENSION_H
#define BACH_HARMONY_HARMONIC_TENSION_H

#include "harmony/chord_types.h"

namespace bach {

struct HarmonicEvent;

/// @brief Compute harmonic tension level from chord degree and quality.
///
/// Maps chord degree and quality to a tension value in [0.0, 1.0].
/// Tonic/stable chords return low tension; dominant and diminished chords
/// return high tension. Inversion affects tension for dominant 7th chords.
///
/// @param degree The chord degree (I, ii, V, etc.).
/// @param quality The chord quality (Major, Dominant7, etc.).
/// @param inversion Chord inversion (0=root, 1=first, 2=second, 3=third).
/// @return Tension level in [0.0, 1.0].
float computeHarmonicTension(ChordDegree degree, ChordQuality quality, int inversion = 0);

/// @brief Compute harmonic tension level from a harmonic event.
///
/// Convenience overload that extracts degree, quality, and inversion from
/// the event's chord. Delegates to the degree-based overload.
///
/// @param harm The harmonic event to evaluate.
/// @return Tension level in [0.0, 1.0].
float computeHarmonicTension(const HarmonicEvent& harm);

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_TENSION_H
