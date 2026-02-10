// Appoggiatura ornament generation implementation.

#include "ornament/appoggiatura.h"

namespace bach {

std::vector<NoteEvent> generateAppoggiatura(const NoteEvent& note, uint8_t upper_pitch) {
  std::vector<NoteEvent> result;

  // Need at least 2 ticks to split into 2 meaningful sub-notes (25/75).
  if (note.duration < 2) {
    result.push_back(note);
    return result;
  }

  result.reserve(2);

  const Tick grace_dur = note.duration / 4;
  const Tick main_dur = note.duration - grace_dur;  // Absorb rounding remainder.

  // Note 1: upper neighbor (appoggiatura grace note, on the beat).
  NoteEvent grace;
  grace.start_tick = note.start_tick;
  grace.duration = grace_dur;
  grace.pitch = upper_pitch;
  grace.velocity = note.velocity;
  grace.voice = note.voice;
  result.push_back(grace);

  // Note 2: main pitch (resolution), remaining 75% duration.
  NoteEvent main_note;
  main_note.start_tick = note.start_tick + grace_dur;
  main_note.duration = main_dur;
  main_note.pitch = note.pitch;
  main_note.velocity = note.velocity;
  main_note.voice = note.voice;
  result.push_back(main_note);

  return result;
}

}  // namespace bach
