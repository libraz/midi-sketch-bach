// Appoggiatura ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_APPOGGIATURA_H
#define BACH_ORNAMENT_APPOGGIATURA_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate an appoggiatura ornament from a single note.
///
/// Produces 2 notes: upper neighbor (grace note on beat) -> main note.
/// Duration split: first = 25% of original, second = 75% of original.
/// Voice and velocity are preserved from the original note.
///
/// @param note The original note to ornament.
/// @param upper_pitch MIDI pitch of the upper neighbor (typically main + 2).
/// @return Vector of 2 sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short to subdivide.
std::vector<NoteEvent> generateAppoggiatura(const NoteEvent& note, uint8_t upper_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_APPOGGIATURA_H
