// Articulation and phrasing for Bach MIDI generation.

#ifndef BACH_EXPRESSION_ARTICULATION_H
#define BACH_EXPRESSION_ARTICULATION_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

class HarmonicTimeline;

/// Articulation types ordered by increasing detachment.
enum class ArticulationType : uint8_t {
  Legato,     ///< Connected, smooth playing
  NonLegato,  ///< Slight separation between notes
  Staccato,   ///< Short, detached notes
  Marcato     ///< Accented, emphasized notes
};

/// @brief A rule defining how articulation modifies note properties.
///
/// The gate_ratio controls the proportion of the notated duration that is
/// actually sounded (note-on to note-off). The velocity_offset adjusts
/// dynamic level for non-organ instruments.
struct ArticulationRule {
  ArticulationType type = ArticulationType::NonLegato;
  float gate_ratio = 0.85f;       ///< Note-on to note-off ratio (0.0-1.0)
  int8_t velocity_offset = 0;     ///< Velocity adjustment (-127 to 127)
};

/// @brief Get the default articulation rule for a given voice role.
///
/// Each voice role has a characteristic articulation:
/// - Ground: Legato (0.95 gate) -- sustained pedal bass
/// - Assert: NonLegato (0.85 gate) -- clear subject presentation
/// - Respond: NonLegato (0.87 gate) -- slightly more connected answer
/// - Propel: NonLegato (0.85 gate) -- driving counterpoint
///
/// @param role The voice role to query.
/// @return ArticulationRule with type and gate_ratio appropriate for the role.
ArticulationRule getDefaultArticulation(VoiceRole role);

/// @brief Apply articulation to a vector of note events in place.
///
/// This function modifies note durations according to the articulation rule
/// for the given voice role. Additionally:
/// - For non-organ instruments, applies beat-position velocity accents
///   (beat 0: +8, beat 2: +4).
/// - For organ instruments (velocity == 80 fixed), velocity is not modified.
/// - At cadence points detected via the harmonic timeline (V->I progressions
///   or key changes), the note preceding the cadence has its duration reduced
///   by 20% to create a breathing space.
///
/// @param notes The note events to modify (modified in place).
/// @param role The voice role governing articulation choice.
/// @param timeline Optional harmonic timeline for cadence detection. May be nullptr.
/// @param is_organ If true, velocity is not modified (pipe organs lack velocity sensitivity).
void applyArticulation(std::vector<NoteEvent>& notes, VoiceRole role,
                       const HarmonicTimeline* timeline, bool is_organ = true);

}  // namespace bach

#endif  // BACH_EXPRESSION_ARTICULATION_H
