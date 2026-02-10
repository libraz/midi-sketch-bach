// Motif transformations for fugue episode development.

#ifndef BACH_TRANSFORM_MOTIF_TRANSFORM_H
#define BACH_TRANSFORM_MOTIF_TRANSFORM_H

#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Invert a melodic fragment around a pivot pitch.
/// Each note's pitch becomes: pivot + (pivot - original_pitch).
/// @param notes Input note sequence.
/// @param pivot MIDI pitch to invert around.
/// @return Inverted note sequence (same rhythm, inverted pitches).
std::vector<NoteEvent> invertMelody(const std::vector<NoteEvent>& notes, uint8_t pivot);

/// @brief Reverse the order of notes in a melodic fragment (retrograde).
/// Pitch order is reversed; tick positions are reassigned to maintain the
/// original rhythmic spacing (durations and inter-note gaps are preserved
/// in reverse order).
/// @param notes Input note sequence.
/// @param start_tick Starting tick for the retrograded fragment.
/// @return Retrograde note sequence.
std::vector<NoteEvent> retrogradeMelody(const std::vector<NoteEvent>& notes, Tick start_tick);

/// @brief Augment a melodic fragment (multiply all durations and gaps).
/// @param notes Input note sequence.
/// @param start_tick Starting tick for the augmented fragment.
/// @param factor Augmentation factor (default 2).
/// @return Augmented note sequence.
std::vector<NoteEvent> augmentMelody(const std::vector<NoteEvent>& notes, Tick start_tick,
                                     int factor = 2);

/// @brief Diminish a melodic fragment (divide all durations and gaps).
/// Durations are guaranteed to be at least 1 tick.
/// @param notes Input note sequence.
/// @param start_tick Starting tick for the diminished fragment.
/// @param factor Diminution divisor (default 2).
/// @return Diminished note sequence.
std::vector<NoteEvent> diminishMelody(const std::vector<NoteEvent>& notes, Tick start_tick,
                                      int factor = 2);

/// @brief Transpose a melodic fragment by a given interval.
/// Pitches are clamped to the valid MIDI range [0, 127].
/// @param notes Input note sequence.
/// @param semitones Interval in semitones (positive = up, negative = down).
/// @return Transposed note sequence (same timing).
std::vector<NoteEvent> transposeMelody(const std::vector<NoteEvent>& notes, int semitones);

}  // namespace bach

#endif  // BACH_TRANSFORM_MOTIF_TRANSFORM_H
