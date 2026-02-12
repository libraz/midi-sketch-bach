// Factory for creating fretted-instrument-constrained NoteEvents.

#include "instrument/fretted/fretted_note_factory.h"

#include <algorithm>

#include "core/pitch_utils.h"
#include "instrument/fretted/fretted_instrument.h"

namespace bach {

FrettedNoteFactory::FrettedNoteFactory(const IFrettedInstrument& instrument)
    : instrument_(instrument) {}

NoteEvent FrettedNoteFactory::createNote(uint8_t pitch, Tick start,
                                         Tick duration, uint8_t velocity,
                                         BachNoteSource source) const {
  NoteEvent note;
  note.pitch = clampPitch(pitch, instrument_.getLowestPitch(),
                          instrument_.getHighestPitch());
  note.start_tick = start;
  note.duration = duration;
  note.velocity = std::min(velocity, static_cast<uint8_t>(127));
  note.voice = 0;
  note.source = source;
  return note;
}

}  // namespace bach
