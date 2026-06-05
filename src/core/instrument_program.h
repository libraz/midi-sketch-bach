// Instrument-to-GM-program mapping and track application helpers.

#ifndef BACH_CORE_INSTRUMENT_PROGRAM_H
#define BACH_CORE_INSTRUMENT_PROGRAM_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Map an instrument type to its General MIDI program number.
///
/// Returns the legacy-consistent GM program used across the existing organ and
/// solo-string output paths, so MIDI output matches prior releases:
/// Organ=Church Organ (19), Harpsichord (6), Piano=Acoustic Grand (0),
/// Violin (40), Cello (42), Guitar=Nylon String Guitar (24).
///
/// @param instrument The instrument type.
/// @return GM program number (0-indexed per the MIDI specification).
std::uint8_t gmProgramFor(InstrumentType instrument);

/// @brief Apply an instrument to a set of tracks.
///
/// Sets each track's GM program to gmProgramFor(instrument). For any track whose
/// name is empty, assigns a default "Voice N" name where N is the track index,
/// preserving any name already set by the generation path.
///
/// @param tracks Tracks to update in place.
/// @param instrument The instrument to apply.
void applyInstrument(std::vector<Track>& tracks, InstrumentType instrument);

}  // namespace bach

#endif  // BACH_CORE_INSTRUMENT_PROGRAM_H
