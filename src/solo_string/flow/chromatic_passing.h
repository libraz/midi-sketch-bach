// Chromatic passing tone insertion for solo string flow generation.

#ifndef BACH_SOLO_STRING_FLOW_CHROMATIC_PASSING_H
#define BACH_SOLO_STRING_FLOW_CHROMATIC_PASSING_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief Insert chromatic passing tones between chord tones in a melody.
///
/// Conditions for insertion:
///   - Adjacent chord tones are 2+ semitones apart (whole tone or more)
///   - Insertion point falls on a weak beat (beat 1 or 3 in 4/4)
///   - Density controlled by energy_level * 0.3 (conservative)
///
/// The original note is shortened by half its duration, and the chromatic
/// passing tone fills the remaining time. The passing tone moves chromatically
/// toward the next note (ascending or descending by one semitone from the
/// first note's pitch).
///
/// @param notes Input note sequence.
/// @param timeline Harmonic timeline for chord tone identification.
/// @param energy_level Current energy level [0.0, 1.0].
/// @param seed Deterministic RNG seed.
/// @return New note sequence with chromatic passing tones inserted.
std::vector<NoteEvent> insertChromaticPassingTones(
    const std::vector<NoteEvent>& notes,
    const HarmonicTimeline& timeline,
    float energy_level,
    uint32_t seed);

/// @brief Check if a chromatic passing tone can be inserted between two notes.
///
/// Requires both notes to be chord tones of their respective harmonic events,
/// and the pitch interval between them to be at least 2 semitones.
///
/// @param note1 First note.
/// @param note2 Second note.
/// @param timeline Harmonic timeline for chord tone lookup.
/// @return True if the interval allows a chromatic passing tone.
bool canInsertChromaticPassing(const NoteEvent& note1, const NoteEvent& note2,
                               const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_SOLO_STRING_FLOW_CHROMATIC_PASSING_H
