// Vorschlag (grace note before main note) ornament generation.

#ifndef BACH_ORNAMENT_VORSCHLAG_H
#define BACH_ORNAMENT_VORSCHLAG_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a Vorschlag (pre-stroke grace note) ornament.
///
/// Creates a short grace note before the main note. The grace note steals
/// time from the beginning of the main note. The grace pitch is typically
/// the upper neighbor (whole step above).
///
/// Grace note duration = min(25% of main note duration, kTicksPerBeat / 4).
/// If the original note is too short (< kTicksPerBeat / 4 = 120 ticks),
/// the note is returned unchanged.
///
/// @param note The original note to ornament.
/// @param grace_pitch MIDI pitch of the grace note (typically note.pitch + 2).
/// @return Vector of 2 notes: {grace_note, shortened_main_note}.
///         Returns {original_note} unchanged if duration is too short.
std::vector<NoteEvent> generateVorschlag(const NoteEvent& note, uint8_t grace_pitch);

}  // namespace bach

#endif  // BACH_ORNAMENT_VORSCHLAG_H
