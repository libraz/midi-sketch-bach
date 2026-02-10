// Shared utility functions for analyzers.

#ifndef BACH_ANALYSIS_ANALYSIS_UTILS_H
#define BACH_ANALYSIS_ANALYSIS_UTILS_H

#include <vector>

#include "core/basic_types.h"

namespace bach {
namespace analysis_util {

/// @brief Collect all NoteEvents from all tracks into a single sorted vector.
/// @param tracks Input tracks.
/// @return All notes sorted by start_tick.
std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks);

/// @brief Get notes within a tick range [start, end).
/// @param all_notes Sorted notes.
/// @param start Start tick (inclusive).
/// @param end End tick (exclusive).
/// @return Notes whose start_tick falls within the range.
std::vector<NoteEvent> notesInRange(const std::vector<NoteEvent>& all_notes,
                                    Tick start, Tick end);

/// @brief Get the average pitch of a set of notes.
/// @param notes Notes to analyze.
/// @return Average MIDI pitch, or 0.0 if empty.
float averagePitch(const std::vector<NoteEvent>& notes);

}  // namespace analysis_util
}  // namespace bach

#endif  // BACH_ANALYSIS_ANALYSIS_UTILS_H
