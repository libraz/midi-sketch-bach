// Natural harmonics detection and marking for bowed string instruments.

#ifndef BACH_INSTRUMENT_BOWED_HARMONICS_H
#define BACH_INSTRUMENT_BOWED_HARMONICS_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Check if a pitch can be played as a natural harmonic.
///
/// Natural harmonics occur at integer divisions of open strings:
///   - Octave (1/2 string length): open + 12 semitones
///   - Octave + fifth (1/3 string length): open + 19 semitones
///
/// @param pitch MIDI pitch to check.
/// @param instrument The instrument type.
/// @return True if the pitch is a natural harmonic on any string.
bool isNaturalHarmonic(uint8_t pitch, InstrumentType instrument);

/// @brief Mark eligible notes as harmonics in a note sequence.
///
/// Conditions for harmonic marking:
///   - Note is a natural harmonic pitch for the given instrument
///   - Duration >= kTicksPerBeat (sustained enough for the harmonic to sound)
///   - ArcPhase is Peak (harmonics reserved for climactic moments)
///
/// Sets note.is_harmonic = 1 for eligible notes.
///
/// @param notes Notes to check and mark (modified in-place).
/// @param instrument The instrument type.
/// @param arc_phase Current arc phase (only Peak enables harmonics).
void markHarmonics(std::vector<NoteEvent>& notes, InstrumentType instrument,
                   ArcPhase arc_phase);

/// @brief Get all natural harmonic pitches for an instrument.
///
/// For each open string, returns the octave harmonic (open + 12) and
/// the fifth harmonic (open + 19).
///
/// @param instrument The instrument type.
/// @return Vector of MIDI pitches that are natural harmonics, sorted ascending.
std::vector<uint8_t> getNaturalHarmonicPitches(InstrumentType instrument);

}  // namespace bach

#endif  // BACH_INSTRUMENT_BOWED_HARMONICS_H
