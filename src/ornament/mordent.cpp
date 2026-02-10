// Mordent ornament generation implementation.

#include "ornament/mordent.h"

namespace bach {

std::vector<NoteEvent> generateMordent(const NoteEvent& note, uint8_t lower_pitch) {
  std::vector<NoteEvent> result;

  // Need at least 4 ticks to split into 3 meaningful sub-notes.
  if (note.duration < 4) {
    result.push_back(note);
    return result;
  }

  result.reserve(3);

  // Match pralltriller pattern: short ornamental notes + long sustain.
  const Tick short_dur = note.duration / 12;
  const Tick last_dur = note.duration - 2 * short_dur;  // Absorb rounding remainder.

  // Note 1: main pitch, short ornamental duration.
  NoteEvent first;
  first.start_tick = note.start_tick;
  first.duration = short_dur;
  first.pitch = note.pitch;
  first.velocity = note.velocity;
  first.voice = note.voice;
  first.source = BachNoteSource::Ornament;
  result.push_back(first);

  // Note 2: lower neighbor, short ornamental duration.
  NoteEvent second;
  second.start_tick = note.start_tick + short_dur;
  second.duration = short_dur;
  second.pitch = lower_pitch;
  second.velocity = note.velocity;
  second.voice = note.voice;
  second.source = BachNoteSource::Ornament;
  result.push_back(second);

  // Note 3: main pitch, remaining duration (sustain).
  NoteEvent third;
  third.start_tick = note.start_tick + 2 * short_dur;
  third.duration = last_dur;
  third.pitch = note.pitch;
  third.velocity = note.velocity;
  third.voice = note.voice;
  third.source = BachNoteSource::Ornament;
  result.push_back(third);

  return result;
}

}  // namespace bach
