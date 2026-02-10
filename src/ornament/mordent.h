// Mordent ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_MORDENT_H
#define BACH_ORNAMENT_MORDENT_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a mordent ornament from a single note.
///
/// Produces 3 notes: main -> lower neighbor -> main.
/// Duration split: first = 50%, second = 25%, third = 25% of original.
/// Voice and velocity are preserved from the original note.
///
/// @param note The original note to ornament.
/// @param lower_pitch MIDI pitch of the lower neighbor (typically main - 2).
/// @return Vector of 3 sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short to subdivide.
std::vector<NoteEvent> generateMordent(const NoteEvent& note, uint8_t lower_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_MORDENT_H
