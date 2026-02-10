// Nachschlag (ornamental ending) ornament generation.

#ifndef BACH_ORNAMENT_NACHSCHLAG_H
#define BACH_ORNAMENT_NACHSCHLAG_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a Nachschlag (after-stroke) ornament.
///
/// Adds 2 quick ornamental notes at the end of the main note: a resolution
/// pitch followed by a return to the main pitch. Commonly used at the end
/// of trills for a clean resolution.
///
/// Each ending sub-note has duration kTicksPerBeat / 8 = 60 ticks.
/// The main note is shortened by 2 * 60 = 120 ticks to make room.
/// If the original note is too short (< kTicksPerBeat / 2 = 240 ticks),
/// the note is returned unchanged.
///
/// @param note The original note to ornament.
/// @param resolution_pitch MIDI pitch of the resolution note (typically
///                         the stepwise lower neighbor, note.pitch - 2).
/// @return Vector of 3 notes: {shortened_main, resolution_note, return_note}.
///         Returns {original_note} unchanged if duration is too short.
std::vector<NoteEvent> generateNachschlag(const NoteEvent& note, uint8_t resolution_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_NACHSCHLAG_H
