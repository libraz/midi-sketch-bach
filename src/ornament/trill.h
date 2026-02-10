// Trill ornament generation for Bach ornamentation system.

#ifndef BACH_ORNAMENT_TRILL_H
#define BACH_ORNAMENT_TRILL_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Generate a trill ornament from a single note.
///
/// Alternates between the main note pitch and the upper neighbor pitch.
/// The trill always ends on the main note. Each alternation sub-note has
/// equal duration derived from the original note's duration and the speed.
///
/// When start_from_upper is true (C.P.E. Bach convention), the pattern is:
///   upper, main, upper, main, ..., main
/// When false (legacy behavior):
///   main, upper, main, upper, ..., main
///
/// For notes with duration >= kTicksPerBeat (1 beat) and at least 5 sub-notes,
/// a Nachschlag (termination) is automatically appended: the last 2 sub-notes
/// are replaced with lower chromatic neighbor -> main note. This follows
/// C.P.E. Bach's standard trill termination practice.
///
/// @param note The original note to ornament.
/// @param upper_pitch MIDI pitch of the upper neighbor (typically main + 1 or +2).
/// @param speed Number of full alternation cycles per beat (default 4).
///              Each cycle = 2 sub-notes (main + upper), so total sub-notes
///              are proportional to speed and the note's duration.
/// @param start_from_upper If true, the trill begins on the upper neighbor
///                         (C.P.E. Bach default). If false, begins on main note.
/// @return Vector of sub-notes replacing the original note. Returns the
///         original note unchanged if the duration is too short for a trill.
std::vector<NoteEvent> generateTrill(const NoteEvent& note, uint8_t upper_pitch,
                                     uint8_t speed = 4, bool start_from_upper = true);

/// @brief Compute recommended trill speed based on tempo.
///
/// Maps BPM to a trill speed (alternation cycles per beat) that sounds
/// natural at the given tempo. Faster tempi use fewer cycles per beat
/// to avoid excessively rapid alternation.
///
/// @param bpm Beats per minute (40-200 typical range).
/// @return Trill speed clamped to [2, 8].
uint8_t computeTrillSpeed(uint16_t bpm);

}  // namespace bach

#endif  // BACH_ORNAMENT_TRILL_H
