#pragma once

#include <cstddef>

namespace test_helpers {

/// @brief Count total notes across all tracks in a generation result.
/// @tparam Result Any result type with a `tracks` member where each track has a `notes` member.
/// @param result The generation result to count.
/// @return Total number of NoteEvents across all tracks.
template <typename Result>
size_t totalNoteCount(const Result& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) count += track.notes.size();
  return count;
}

}  // namespace test_helpers
