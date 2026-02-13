// Implementation of binary form repeats.

#include "forms/goldberg/goldberg_binary.h"

#include <algorithm>

namespace bach {

std::vector<NoteEvent> applyBinaryRepeats(
    const std::vector<NoteEvent>& unique_notes,
    Tick section_ticks,
    bool /*ornament_variation*/) {
  // Split notes into A section (0 to section_ticks) and B section.
  std::vector<NoteEvent> a_notes;
  std::vector<NoteEvent> b_notes;

  for (const auto& note : unique_notes) {
    if (note.start_tick < section_ticks) {
      a_notes.push_back(note);
    } else {
      b_notes.push_back(note);
    }
  }

  std::vector<NoteEvent> result;
  result.reserve(unique_notes.size() * 2);

  // A section (first time)
  for (const auto& note : a_notes) {
    result.push_back(note);
  }

  // A section repeat (offset by section_ticks)
  for (auto note : a_notes) {
    note.start_tick += section_ticks;
    result.push_back(note);
  }

  // B section (offset by section_ticks for the A repeat)
  Tick b_offset = section_ticks;  // A repeat shifts everything
  for (auto note : b_notes) {
    note.start_tick += b_offset;
    result.push_back(note);
  }

  // B section repeat (offset by another section_ticks)
  for (auto note : b_notes) {
    note.start_tick += b_offset + section_ticks;
    result.push_back(note);
  }

  // Sort by start_tick for clean output
  std::sort(result.begin(), result.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  return result;
}

}  // namespace bach
