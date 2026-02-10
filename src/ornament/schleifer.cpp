// Implementation of Schleifer (slide) ornament generation.

#include "ornament/schleifer.h"

#include "core/pitch_utils.h"

namespace bach {

namespace {

/// @brief Minimum note duration for Schleifer eligibility (quarter note).
constexpr Tick kMinSchleiferDuration = kTicksPerBeat;

/// @brief Duration of each grace note in the Schleifer (1/8 of a beat).
constexpr Tick kGraceNoteDuration = kTicksPerBeat / 8;

}  // namespace

uint8_t getDiatonicNeighbor(uint8_t pitch, bool upper, Key key, bool is_minor) {
  int tonic_pc = static_cast<int>(key);
  const int* scale = is_minor ? kScaleHarmonicMinor : kScaleMajor;

  int pc = getPitchClass(pitch);
  int relative_pc = ((pc - tonic_pc) + 12) % 12;

  // Find the current scale degree.
  int current_degree = -1;
  for (int d = 0; d < 7; ++d) {
    if (scale[d] == relative_pc) {
      current_degree = d;
      break;
    }
  }

  if (current_degree < 0) {
    // Pitch is not diatonic; return chromatic neighbor.
    return upper ? pitch + 1 : pitch - 1;
  }

  if (upper) {
    int next_degree = (current_degree + 1) % 7;
    int next_pc = (tonic_pc + scale[next_degree]) % 12;
    int result = static_cast<int>(pitch);
    // Find the next occurrence of next_pc above pitch.
    while (getPitchClass(static_cast<uint8_t>(result)) != next_pc || result <= pitch) {
      ++result;
      if (result > 127) return pitch + 1;
    }
    return static_cast<uint8_t>(result);
  } else {
    int prev_degree = (current_degree + 6) % 7;  // -1 mod 7
    int prev_pc = (tonic_pc + scale[prev_degree]) % 12;
    int result = static_cast<int>(pitch);
    // Find the next occurrence of prev_pc below pitch.
    while (getPitchClass(static_cast<uint8_t>(result)) != prev_pc || result >= pitch) {
      --result;
      if (result < 0) return pitch - 1;
    }
    return static_cast<uint8_t>(result);
  }
}

std::vector<NoteEvent> generateSchleifer(const NoteEvent& note, Key key, bool is_minor) {
  std::vector<NoteEvent> result;

  // Check minimum duration.
  if (note.duration < kMinSchleiferDuration) return result;

  // Start from a diatonic 3rd below the main note.
  uint8_t step1 = getDiatonicNeighbor(note.pitch, false, key, is_minor);
  step1 = getDiatonicNeighbor(step1, false, key, is_minor);  // Two steps below = 3rd
  uint8_t step2 = getDiatonicNeighbor(step1, true, key, is_minor);
  uint8_t step3 = getDiatonicNeighbor(step2, true, key, is_minor);

  // Verify the grace notes ascend to the main note.
  // If step3 != main pitch, adjust to 2-note Schleifer.
  bool three_note = (step3 == note.pitch);

  Tick grace_total = three_note ? kGraceNoteDuration * 3 : kGraceNoteDuration * 2;
  Tick main_start = note.start_tick + grace_total;
  Tick main_duration = note.duration - grace_total;

  if (main_duration < kTicksPerBeat / 4) {
    // Not enough room; return empty (no ornament).
    return result;
  }

  // Grace note 1.
  NoteEvent g1;
  g1.pitch = step1;
  g1.start_tick = note.start_tick;
  g1.duration = kGraceNoteDuration;
  g1.velocity = note.velocity;
  g1.voice = note.voice;
  g1.source = BachNoteSource::Ornament;
  result.push_back(g1);

  // Grace note 2.
  NoteEvent g2;
  g2.pitch = step2;
  g2.start_tick = note.start_tick + kGraceNoteDuration;
  g2.duration = kGraceNoteDuration;
  g2.velocity = note.velocity;
  g2.voice = note.voice;
  g2.source = BachNoteSource::Ornament;
  result.push_back(g2);

  if (three_note) {
    // Grace note 3 (step3 should equal main pitch, so it leads directly).
    NoteEvent g3;
    g3.pitch = step3;
    g3.start_tick = note.start_tick + kGraceNoteDuration * 2;
    g3.duration = kGraceNoteDuration;
    g3.velocity = note.velocity;
    g3.voice = note.voice;
    g3.source = BachNoteSource::Ornament;
    result.push_back(g3);
  }

  // Main note (shortened).
  NoteEvent main_note = note;
  main_note.start_tick = main_start;
  main_note.duration = main_duration;
  result.push_back(main_note);

  return result;
}

}  // namespace bach
