// Bow direction assignment for bowed string instruments.

#ifndef BACH_INSTRUMENT_BOWED_BOW_DIRECTION_H
#define BACH_INSTRUMENT_BOWED_BOW_DIRECTION_H

#include <vector>

#include "core/basic_types.h"
#include "instrument/bowed/bowed_string_instrument.h"

namespace bach {

/// @brief Assign bow directions to a sequence of notes.
///
/// Rules applied in order:
///   - Bar start (beat 0) = Down bow
///   - Subsequent notes alternate Up/Down
///   - String crossing >= 3 strings = Natural (let performer decide)
///   - Slurred groups (consecutive stepwise motion) maintain direction
///
/// Results are written to each note's bow_direction field as a uint8_t
/// cast of BowDirection.
///
/// @param notes Notes to assign bow directions to (modified in-place).
/// @param open_strings Open string pitches for the instrument (e.g., {55,62,69,76} for violin).
void assignBowDirections(std::vector<NoteEvent>& notes,
                         const std::vector<uint8_t>& open_strings);

/// @brief Check if a pitch change requires crossing 3+ strings.
///
/// Estimates which string each pitch belongs to by finding the nearest open
/// string that is at or below the pitch, then counts the string index distance.
///
/// @param from_pitch Starting MIDI pitch.
/// @param to_pitch Ending MIDI pitch.
/// @param open_strings Open string pitches for the instrument.
/// @return True if the interval spans 3 or more strings.
bool isLargeStringCrossing(uint8_t from_pitch, uint8_t to_pitch,
                           const std::vector<uint8_t>& open_strings);

/// @brief Get open string pitches for a given instrument type.
/// @param instrument The instrument type (Violin, Cello, Guitar).
/// @return Vector of open string MIDI pitches, low to high.
std::vector<uint8_t> getOpenStrings(InstrumentType instrument);

}  // namespace bach

#endif  // BACH_INSTRUMENT_BOWED_BOW_DIRECTION_H
