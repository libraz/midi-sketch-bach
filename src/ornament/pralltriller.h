// Pralltriller (short trill) ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_PRALLTRILLER_H
#define BACH_ORNAMENT_PRALLTRILLER_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a pralltriller (short trill) ornament from a single note.
///
/// Produces 4 notes: upper -> main -> upper -> main.
/// Duration split: first 3 notes each get duration/12, last note gets remainder.
/// Voice and velocity are preserved from the original note.
///
/// @param note The original note to ornament.
/// @param upper_pitch MIDI pitch of the upper neighbor (typically main + 2).
/// @return Vector of 4 sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short to subdivide.
std::vector<NoteEvent> generatePralltriller(const NoteEvent& note, uint8_t upper_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_PRALLTRILLER_H
