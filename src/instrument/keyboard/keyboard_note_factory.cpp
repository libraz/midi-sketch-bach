// Factory for creating keyboard-constrained NoteEvents.

#include "instrument/keyboard/keyboard_note_factory.h"

#include <algorithm>

#include "instrument/keyboard/keyboard_instrument.h"

namespace bach {

KeyboardNoteFactory::KeyboardNoteFactory(const IKeyboardInstrument& instrument)
    : instrument_(instrument) {}

NoteEvent KeyboardNoteFactory::createNote(uint8_t pitch, Tick start,
                                          Tick duration,
                                          uint8_t velocity) const {
  NoteEvent note;
  note.pitch = std::clamp(pitch, instrument_.getLowestPitch(),
                          instrument_.getHighestPitch());
  note.start_tick = start;
  note.duration = duration;
  note.velocity = std::min(velocity, static_cast<uint8_t>(127));
  note.voice = 0;
  return note;
}

}  // namespace bach
