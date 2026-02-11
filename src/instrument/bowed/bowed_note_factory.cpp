// Factory for creating bowed-instrument-constrained NoteEvents.

#include "instrument/bowed/bowed_note_factory.h"

#include <algorithm>

#include "instrument/bowed/bowed_string_instrument.h"

namespace bach {

BowedNoteFactory::BowedNoteFactory(const IBowedStringInstrument& instrument)
    : instrument_(instrument) {}

NoteEvent BowedNoteFactory::createNote(const BowedNoteOptions& options) const {
  NoteEvent note;
  note.pitch = std::clamp(options.pitch, instrument_.getLowestPitch(),
                          instrument_.getHighestPitch());
  note.start_tick = options.tick;
  note.duration = options.duration;
  note.velocity = std::min(options.velocity, static_cast<uint8_t>(127));
  note.voice = 0;

  // Open string resonance: slight velocity boost for natural ring.
  if (options.prefer_open_string && instrument_.isOpenString(note.pitch)) {
    constexpr uint8_t kOpenStringBoost = 5;
    note.velocity = std::min(static_cast<uint8_t>(note.velocity + kOpenStringBoost),
                             static_cast<uint8_t>(127));
  }

  note.source = options.source;
  return note;
}

NoteEvent BowedNoteFactory::createNote(uint8_t pitch, Tick start,
                                       Tick duration, uint8_t velocity,
                                       BachNoteSource source) const {
  BowedNoteOptions options;
  options.pitch = pitch;
  options.tick = start;
  options.duration = duration;
  options.velocity = velocity;
  options.prefer_open_string = true;
  options.source = source;
  return createNote(options);
}

}  // namespace bach
