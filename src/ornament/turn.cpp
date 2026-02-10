// Turn ornament generation implementation.

#include "ornament/turn.h"

namespace bach {

std::vector<NoteEvent> generateTurn(const NoteEvent& note, uint8_t upper_pitch,
                                    uint8_t lower_pitch) {
  std::vector<NoteEvent> result;

  // Need at least 4 ticks to split into 4 meaningful sub-notes.
  if (note.duration < 4) {
    result.push_back(note);
    return result;
  }

  result.reserve(4);

  const Tick quarter_dur = note.duration / 4;
  const Tick last_dur = note.duration - (quarter_dur * 3);  // Absorb rounding remainder.

  // The 4 pitches in order: upper, main, lower, main.
  const uint8_t pitches[4] = {upper_pitch, note.pitch, lower_pitch, note.pitch};

  Tick current_tick = note.start_tick;
  for (uint8_t idx = 0; idx < 4; ++idx) {
    NoteEvent sub_note;
    sub_note.start_tick = current_tick;
    sub_note.pitch = pitches[idx];
    sub_note.velocity = note.velocity;
    sub_note.voice = note.voice;
    sub_note.source = BachNoteSource::Ornament;

    // Last note absorbs any rounding remainder.
    if (idx == 3) {
      sub_note.duration = last_dur;
    } else {
      sub_note.duration = quarter_dur;
    }

    result.push_back(sub_note);
    current_tick += quarter_dur;
  }

  return result;
}

}  // namespace bach
