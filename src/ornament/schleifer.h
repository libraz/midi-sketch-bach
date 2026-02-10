// Schleifer ornament: ascending grace note group (slide).

#ifndef BACH_ORNAMENT_SCHLEIFER_H
#define BACH_ORNAMENT_SCHLEIFER_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Generate a Schleifer (slide) ornament.
///
/// The Schleifer is an ascending group of 2-3 short grace notes leading
/// into the main note. It uses diatonic pitches from the current key,
/// starting a 3rd below the main note.
///
/// Example: for main note G in C major, generates E->F->G.
///
/// @param note The target note to ornament.
/// @param key The key context for diatonic pitch selection.
/// @param is_minor True for minor key context.
/// @return Vector of NoteEvents: grace notes + main note (shortened).
///         Empty if the note is too short for ornamentation.
std::vector<NoteEvent> generateSchleifer(const NoteEvent& note, Key key, bool is_minor);

/// @brief Get the diatonic neighbor pitch in a given key.
///
/// Returns the nearest pitch on the diatonic scale above or below
/// the given pitch, respecting the key and mode.
///
/// @param pitch The reference MIDI pitch.
/// @param upper True for upper neighbor, false for lower.
/// @param key The key context.
/// @param is_minor True for minor key (harmonic minor scale).
/// @return The diatonic neighbor pitch.
uint8_t getDiatonicNeighbor(uint8_t pitch, bool upper, Key key, bool is_minor);

}  // namespace bach

#endif  // BACH_ORNAMENT_SCHLEIFER_H
