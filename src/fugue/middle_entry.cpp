// Implementation of middle entry generation for fugue development.

#include "fugue/middle_entry.h"

#include "transform/motif_transform.h"

namespace bach {

MiddleEntry generateMiddleEntry(const Subject& subject, Key target_key, Tick start_tick,
                                VoiceId voice_id) {
  MiddleEntry entry;
  entry.key = target_key;
  entry.start_tick = start_tick;
  entry.voice_id = voice_id;
  entry.end_tick = start_tick + subject.length_ticks;

  if (subject.notes.empty()) {
    return entry;
  }

  // Calculate transposition interval from subject key to target key.
  int semitones = static_cast<int>(target_key) - static_cast<int>(subject.key);

  // Use transposeMelody from the transform module.
  entry.notes = transposeMelody(subject.notes, semitones);

  // Offset tick positions so the entry starts at start_tick.
  Tick original_start = subject.notes[0].start_tick;
  for (auto& note : entry.notes) {
    note.start_tick = note.start_tick - original_start + start_tick;
    note.voice = voice_id;
  }

  return entry;
}

}  // namespace bach
