// Turn ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_TURN_H
#define BACH_ORNAMENT_TURN_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a turn ornament from a single note.
///
/// Produces 4 notes: upper -> main -> lower -> main.
/// Each note receives 25% of the original duration.
/// Voice and velocity are preserved from the original note.
///
/// @param note The original note to ornament.
/// @param upper_pitch MIDI pitch of the upper neighbor (typically main + 2).
/// @param lower_pitch MIDI pitch of the lower neighbor (typically main - 2).
/// @return Vector of 4 sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short to subdivide.
std::vector<NoteEvent> generateTurn(const NoteEvent& note, uint8_t upper_pitch,
                                    uint8_t lower_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_TURN_H
