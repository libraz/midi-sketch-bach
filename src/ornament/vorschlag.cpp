// Vorschlag (pre-stroke grace note) ornament implementation.

#include "ornament/vorschlag.h"

#include <algorithm>

namespace bach {

std::vector<NoteEvent> generateVorschlag(const NoteEvent& note, uint8_t grace_pitch) {
  std::vector<NoteEvent> result;

  // Minimum duration to apply a Vorschlag: a sixteenth note (kTicksPerBeat / 4 = 120).
  constexpr Tick kMinVorschlagDuration = kTicksPerBeat / 4;  // 120 ticks
  if (note.duration < kMinVorschlagDuration) {
    result.push_back(note);
    return result;
  }

  result.reserve(2);

  // Grace note duration: min(25% of main, kTicksPerBeat / 4 = 120 ticks).
  const Tick quarter_duration = note.duration / 4;
  const Tick grace_duration = std::min(quarter_duration, kMinVorschlagDuration);

  // Grace note: placed at the original start, stealing time from the main note.
  NoteEvent grace_note;
  grace_note.start_tick = note.start_tick;
  grace_note.duration = grace_duration;
  grace_note.pitch = grace_pitch;
  grace_note.velocity = note.velocity;
  grace_note.voice = note.voice;
  grace_note.source = BachNoteSource::Ornament;
  result.push_back(grace_note);

  // Main note: offset forward by grace duration, shortened accordingly.
  NoteEvent main_note = note;
  main_note.start_tick = note.start_tick + grace_duration;
  main_note.duration = note.duration - grace_duration;
  result.push_back(main_note);

  return result;
}

}  // namespace bach
