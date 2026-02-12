// Implementation of bow direction assignment for bowed string instruments.

#include "instrument/bowed/bow_direction.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/pitch_utils.h"

namespace bach {

namespace {

/// @brief Estimate which string index a pitch belongs to.
///
/// Finds the highest open string that is at or below the given pitch.
/// If the pitch is below all open strings, returns 0 (lowest string).
///
/// @param pitch MIDI pitch number.
/// @param open_strings Open string pitches, sorted low to high.
/// @return String index (0-based from lowest).
int estimateStringIndex(uint8_t pitch, const std::vector<uint8_t>& open_strings) {
  if (open_strings.empty()) return 0;

  int best_idx = 0;
  for (size_t idx = 0; idx < open_strings.size(); ++idx) {
    if (open_strings[idx] <= pitch) {
      best_idx = static_cast<int>(idx);
    }
  }
  return best_idx;
}

/// @brief Check if two consecutive notes form stepwise motion (1-2 semitones).
/// @param pitch_a First MIDI pitch.
/// @param pitch_b Second MIDI pitch.
/// @return True if the interval is a half or whole step.
bool isStepwiseMotion(uint8_t pitch_a, uint8_t pitch_b) {
  int interval = absoluteInterval(pitch_a, pitch_b);
  return interval >= 1 && interval <= 2;
}

}  // namespace

std::vector<uint8_t> getOpenStrings(InstrumentType instrument) {
  switch (instrument) {
    case InstrumentType::Violin:
      return {55, 62, 69, 76};  // G3, D4, A4, E5
    case InstrumentType::Cello:
      return {36, 43, 50, 57};  // C2, G2, D3, A3
    case InstrumentType::Guitar:
      return {40, 45, 50, 55, 59, 64};  // E2, A2, D3, G3, B3, E4
    default:
      return {};
  }
}

bool isLargeStringCrossing(uint8_t from_pitch, uint8_t to_pitch,
                           const std::vector<uint8_t>& open_strings) {
  if (open_strings.empty()) return false;

  int from_string = estimateStringIndex(from_pitch, open_strings);
  int to_string = estimateStringIndex(to_pitch, open_strings);
  int distance = std::abs(from_string - to_string);
  return distance >= 3;
}

void assignBowDirections(std::vector<NoteEvent>& notes,
                         const std::vector<uint8_t>& open_strings) {
  if (notes.empty()) return;

  BowDirection current_dir = BowDirection::Down;

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    auto& note = notes[idx];

    // Rule 1: Bar start (beat 0) resets to Down bow.
    if (beatInBar(note.start_tick) == 0) {
      current_dir = BowDirection::Down;
    }

    // Rule 2: Check for large string crossing -> Natural.
    if (idx > 0 && isLargeStringCrossing(notes[idx - 1].pitch, note.pitch, open_strings)) {
      note.bow_direction = static_cast<uint8_t>(BowDirection::Natural);
      // Do not update current_dir; next note resumes alternation from previous direction.
      continue;
    }

    // Rule 3: Slurred groups (stepwise motion) maintain direction.
    if (idx > 0 && isStepwiseMotion(notes[idx - 1].pitch, note.pitch) &&
        beatInBar(note.start_tick) != 0) {
      note.bow_direction = static_cast<uint8_t>(current_dir);
      continue;
    }

    // Rule 4: Default assignment with alternation.
    note.bow_direction = static_cast<uint8_t>(current_dir);
    current_dir = (current_dir == BowDirection::Down) ? BowDirection::Up : BowDirection::Down;
  }
}

}  // namespace bach
