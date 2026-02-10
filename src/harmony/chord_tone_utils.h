// Chord tone snapping utilities for harmonic validation.

#ifndef BACH_HARMONY_CHORD_TONE_UTILS_H
#define BACH_HARMONY_CHORD_TONE_UTILS_H

#include <cstdint>

#include "harmony/harmonic_event.h"

namespace bach {

/// @brief Find the nearest chord tone to a given pitch.
///
/// Iterates chord pitch classes (root, 3rd, 5th) and finds the closest
/// MIDI pitch in the same or adjacent octave. Prefers the closest match;
/// ties are broken toward the original pitch direction.
///
/// @param pitch Source MIDI pitch to snap.
/// @param event Harmonic event containing the target chord.
/// @return MIDI pitch of the nearest chord tone (clamped to [0, 127]).
uint8_t nearestChordTone(uint8_t pitch, const HarmonicEvent& event);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_TONE_UTILS_H
