// Shared cross-relation detection utility for counterpoint validation.

#ifndef BACH_COUNTERPOINT_CROSS_RELATION_H
#define BACH_COUNTERPOINT_CROSS_RELATION_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Check if placing pitch in voice at tick creates a same-tick
///        cross-relation with any other sounding voice.
///
/// Cross-relation occurs when two voices have conflicting chromatic alterations
/// of the same pitch class at the same time (e.g. C-natural in voice 0, C# in
/// voice 1).
///
/// @note Same-tick only. Does NOT check preceding/following ticks.
///       Excludes natural half-steps (E/F, B/C).
///
/// @param notes The full note list (all voices).
/// @param num_voices Number of active voices.
/// @param voice Target voice index.
/// @param pitch Candidate MIDI pitch.
/// @param tick Note start tick.
/// @return true if cross-relation detected.
bool hasCrossRelation(const std::vector<NoteEvent>& notes,
                      uint8_t num_voices, uint8_t voice,
                      uint8_t pitch, Tick tick);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_CROSS_RELATION_H
