// Sequential transposition (Sequence/Zeugma) for fugue episodes.

#ifndef BACH_TRANSFORM_SEQUENCE_H
#define BACH_TRANSFORM_SEQUENCE_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Calculate the total duration of a motif (first note start to last note end).
/// @param notes The note sequence.
/// @return Duration in ticks, or 0 if empty.
Tick motifDuration(const std::vector<NoteEvent>& notes);

/// @brief Generate a sequence (repeated transposition of a motif).
/// The motif is repeated N times, each transposed by interval_step semitones
/// relative to the previous repetition. This is the Baroque "Zeugma" technique
/// commonly used in fugue episodes.
/// @param motif The source melodic fragment.
/// @param repetitions Number of repetitions (excluding the original).
/// @param interval_step Semitone interval for each transposition step.
/// @param start_tick Starting tick for the first repetition.
/// @return All repetitions concatenated (does not include the original motif).
std::vector<NoteEvent> generateSequence(const std::vector<NoteEvent>& motif, int repetitions,
                                        int interval_step, Tick start_tick);

/// @brief Generate a diatonic sequence (repeated transposition by scale degrees).
///
/// Like generateSequence but transposes by diatonic degree steps instead of
/// semitones. This is the Baroque "Zeugma" technique as actually practiced by
/// Bach: each repetition moves by a fixed number of scale degrees, so interval
/// quality (major/minor) adjusts naturally to fit the key.
///
/// @param motif The source melodic fragment.
/// @param repetitions Number of repetitions (excluding the original).
/// @param degree_step Scale-degree interval for each step (e.g. -1 = descending by step).
/// @param start_tick Starting tick for the first repetition.
/// @param key Musical key for diatonic context.
/// @param scale Scale type.
/// @return All repetitions concatenated (does not include the original motif).
std::vector<NoteEvent> generateDiatonicSequence(const std::vector<NoteEvent>& motif,
                                                int repetitions, int degree_step,
                                                Tick start_tick, Key key, ScaleType scale);

}  // namespace bach

#endif  // BACH_TRANSFORM_SEQUENCE_H
