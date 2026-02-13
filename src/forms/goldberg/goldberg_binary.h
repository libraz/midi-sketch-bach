// Binary form repeat structure for Goldberg Variations.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_BINARY_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_BINARY_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Apply binary form repeats: ||: A(16 bars) :||: B(16 bars) :||
///
/// Duplicates notes to create the standard Baroque binary repeat structure.
/// Each section is played twice. Result is 4x section_ticks total duration.
///
/// @param unique_notes Original 32-bar note sequence.
/// @param section_ticks Duration of one 16-bar section in ticks.
/// @param ornament_variation If true, slightly vary ornaments on repeats (future).
/// @return Notes with repeats applied (A-A-B-B ordering).
std::vector<NoteEvent> applyBinaryRepeats(
    const std::vector<NoteEvent>& unique_notes,
    Tick section_ticks,
    bool ornament_variation = false);

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_BINARY_H
