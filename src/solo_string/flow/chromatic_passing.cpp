// Implementation of chromatic passing tone insertion.

#include "solo_string/flow/chromatic_passing.h"

#include <cmath>
#include <cstdlib>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

bool canInsertChromaticPassing(const NoteEvent& note1, const NoteEvent& note2,
                               const HarmonicTimeline& timeline) {
  // Pitch interval must be at least 2 semitones (whole tone or more).
  int pitch_diff = absoluteInterval(note1.pitch, note2.pitch);
  if (pitch_diff < 2) return false;

  // Both notes must be chord tones of their respective harmonic contexts.
  const auto& event1 = timeline.getAt(note1.start_tick);
  const auto& event2 = timeline.getAt(note2.start_tick);

  if (!isChordTone(note1.pitch, event1)) return false;
  if (!isChordTone(note2.pitch, event2)) return false;

  return true;
}

std::vector<NoteEvent> insertChromaticPassingTones(
    const std::vector<NoteEvent>& notes,
    const HarmonicTimeline& timeline,
    float energy_level,
    uint32_t seed) {
  if (notes.empty()) return notes;

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  std::mt19937 rng_engine(seed);
  float insertion_probability = energy_level * 0.3f;

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& current = notes[idx];

    // Last note: no successor to pass toward, just copy.
    if (idx + 1 >= notes.size()) {
      result.push_back(current);
      continue;
    }

    const auto& next_note = notes[idx + 1];

    // Check if this beat position is weak (beat 1 or 3 in 4/4).
    uint8_t beat = beatInBar(current.start_tick);
    bool is_weak_beat = (beat == 1 || beat == 3);

    // Must have enough duration to split (at least 2 ticks for both halves).
    bool has_enough_duration = current.duration >= 2;

    if (is_weak_beat && has_enough_duration &&
        canInsertChromaticPassing(current, next_note, timeline) &&
        rng::rollProbability(rng_engine, insertion_probability)) {
      // Shorten the current note by half.
      Tick half_duration = current.duration / 2;
      NoteEvent shortened = current;
      shortened.duration = half_duration;
      result.push_back(shortened);

      // Create the chromatic passing tone.
      NoteEvent passing;
      passing.start_tick = current.start_tick + half_duration;
      passing.duration = current.duration - half_duration;
      passing.velocity = current.velocity;
      passing.voice = current.voice;
      passing.source = BachNoteSource::ChromaticPassing;

      // Move chromatically toward the next note.
      if (next_note.pitch > current.pitch) {
        passing.pitch = current.pitch + 1;  // Ascending chromatic step
      } else {
        passing.pitch = current.pitch - 1;  // Descending chromatic step
      }

      result.push_back(passing);
    } else {
      result.push_back(current);
    }
  }

  return result;
}

}  // namespace bach
