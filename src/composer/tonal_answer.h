#ifndef BACH_COMPOSER_TONAL_ANSWER_H
#define BACH_COMPOSER_TONAL_ANSWER_H

#include <cstdint>
#include <vector>

#include "composer/material.h"
#include "core/basic_types.h"

namespace bach::composer::tonal_answer {

// Derive a tonal answer from a subject. The result is the subject
// transposed by -P4 (real-answer base), with one extra mutation step
// applied to head pitches that lie on the tonic or dominant scale
// degree:
//
//   subject pc == tonic_pc      → answer pc = dominant_pc (head only)
//   subject pc == dominant_pc   → answer pc = tonic_pc    (head only)
//
// Only the first head_length notes (default = 4, i.e. the first beat
// or two depending on rhythm) are subject to the mutation. The
// remaining notes follow the standard real-answer transposition
// (subject - 5 semitones).
//
// `head_length == 0` is shorthand for "mutate only the first note".
// `head_length >= subject.size()` mutates the whole answer.
//
// `target_start_tick` re-anchors the answer's first note so the result
// can be dropped into a harness fixture at an arbitrary bar boundary.
std::vector<MaterialNote> deriveTonalAnswer(const std::vector<MaterialNote>& subject,
                                            std::uint8_t tonic_pc, Tick target_start_tick,
                                            std::size_t head_length = 4);

}  // namespace bach::composer::tonal_answer

#endif  // BACH_COMPOSER_TONAL_ANSWER_H
