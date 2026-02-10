// Compound ornament generation: combinations of basic ornaments.

#ifndef BACH_ORNAMENT_COMPOUND_ORNAMENT_H
#define BACH_ORNAMENT_COMPOUND_ORNAMENT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// Types of compound ornaments (combinations of basic ornaments).
enum class CompoundOrnamentType : uint8_t {
  TrillWithNachschlag,  // Trill + resolution ending
  TurnThenTrill         // Turn followed by trill
};

/// @brief Generate a compound ornament from a single note.
///
/// Compound ornaments combine two basic ornaments within a single note's
/// duration. The note must be at least kTicksPerBeat (one quarter note)
/// long to be eligible.
///
/// - TrillWithNachschlag: 85% trill + 15% nachschlag resolution.
/// - TurnThenTrill: 25% turn + 75% trill.
///
/// The original note's voice, velocity, and source are preserved across
/// all generated sub-notes.
///
/// @param note The original note to ornament.
/// @param type The compound ornament type to apply.
/// @param upper_pitch MIDI pitch of the upper neighbor.
/// @param lower_pitch MIDI pitch of the lower neighbor.
/// @param speed Trill speed (alternation cycles per beat, default 4).
/// @return Vector of sub-notes replacing the original note. Returns
///         {original_note} unchanged if duration is too short.
std::vector<NoteEvent> generateCompoundOrnament(const NoteEvent& note,
                                                CompoundOrnamentType type,
                                                uint8_t upper_pitch, uint8_t lower_pitch,
                                                uint8_t speed = 4);

}  // namespace bach

#endif  // BACH_ORNAMENT_COMPOUND_ORNAMENT_H
