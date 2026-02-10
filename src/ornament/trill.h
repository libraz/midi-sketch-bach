// Trill ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_TRILL_H
#define BACH_ORNAMENT_TRILL_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a trill ornament from a single note.
///
/// Alternates between the main note pitch and the upper neighbor pitch.
/// The trill always ends on the main note. Each alternation sub-note has
/// equal duration derived from the original note's duration and the speed.
///
/// @param note The original note to ornament.
/// @param upper_pitch MIDI pitch of the upper neighbor (typically main + 2).
/// @param speed Number of full alternation cycles per beat (default 4).
///              Each cycle = 2 sub-notes (main + upper), so total sub-notes
///              are proportional to speed and the note's duration.
/// @return Vector of sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short for a trill.
std::vector<NoteEvent> generateTrill(const NoteEvent& note, uint8_t upper_pitch,
                                     uint8_t speed = 4);

}  // namespace bach

#endif  // BACH_ORNAMENT_TRILL_H
