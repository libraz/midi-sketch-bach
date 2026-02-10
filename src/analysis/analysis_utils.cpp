// Shared utility functions for analyzers.

#include "analysis/analysis_utils.h"

#include <algorithm>

namespace bach {
namespace analysis_util {

std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks) {
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return all_notes;
}

std::vector<NoteEvent> notesInRange(const std::vector<NoteEvent>& all_notes,
                                    Tick start, Tick end) {
  std::vector<NoteEvent> result;
  for (const auto& note : all_notes) {
    if (note.start_tick >= end) break;
    if (note.start_tick >= start) {
      result.push_back(note);
    }
  }
  return result;
}

float averagePitch(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;
  float sum = 0.0f;
  for (const auto& note : notes) {
    sum += static_cast<float>(note.pitch);
  }
  return sum / static_cast<float>(notes.size());
}

}  // namespace analysis_util
}  // namespace bach
