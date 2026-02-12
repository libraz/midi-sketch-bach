// Shared cross-relation detection: scans all sounding notes to find
// chromatic pitch-class conflicts between voices at the same tick.

#include "counterpoint/cross_relation.h"

#include <cmath>

#include "core/pitch_utils.h"

namespace bach {

bool hasCrossRelation(const std::vector<NoteEvent>& notes,
                      uint8_t num_voices, uint8_t voice,
                      uint8_t pitch, Tick tick) {
  int pitch_class = getPitchClass(pitch);
  // Track which voices we've already checked (one sounding note per voice).
  bool checked[5] = {false, false, false, false, false};
  checked[voice] = true;

  for (const auto& other_note : notes) {
    if (other_note.voice == voice) continue;
    if (other_note.voice >= num_voices || other_note.voice >= 5) continue;
    if (checked[other_note.voice]) continue;
    // Check if this note is sounding at the given tick.
    if (other_note.start_tick <= tick &&
        other_note.start_tick + other_note.duration > tick) {
      checked[other_note.voice] = true;
      int other_pc = getPitchClass(other_note.pitch);
      int diff = std::abs(pitch_class - other_pc);
      if (diff == 1 || diff == 11) {
        // Exclude natural half-steps E/F and B/C.
        int low_pc = std::min(pitch_class, other_pc);
        int high_pc = std::max(pitch_class, other_pc);
        if (low_pc == 4 && high_pc == 5) continue;   // E/F
        if (low_pc == 0 && high_pc == 11) continue;   // B/C (wrapped)
        if (low_pc == 0 && high_pc == 1) continue;    // B#/C enharmonic
        return true;
      }
    }
  }
  return false;
}

}  // namespace bach
