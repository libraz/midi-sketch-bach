// Chord tone snapping utilities for harmonic validation.

#ifndef BACH_HARMONY_CHORD_TONE_UTILS_H
#define BACH_HARMONY_CHORD_TONE_UTILS_H

#include <cstdint>
#include <vector>

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

/// @brief Find the nearest pitch from a set of chord tones.
///
/// @param target Target MIDI pitch to snap.
/// @param chord_pitches Available chord tone pitches.
/// @return Nearest chord tone, or target if chord_pitches is empty.
uint8_t nearestChordTone(uint8_t target, const std::vector<uint8_t>& chord_pitches);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_TONE_UTILS_H
