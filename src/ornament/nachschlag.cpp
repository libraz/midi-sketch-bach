// Nachschlag (ornamental ending) ornament implementation.

#include "ornament/nachschlag.h"

namespace bach {

std::vector<NoteEvent> generateNachschlag(const NoteEvent& note, uint8_t resolution_pitch) {
  std::vector<NoteEvent> result;

  // Each ending sub-note is 1/8 of a beat (60 ticks). Two ending notes = 120 ticks.
  constexpr Tick kSubNoteDuration = kTicksPerBeat / 8;  // 60 ticks
  constexpr Tick kEndingDuration = kSubNoteDuration * 2;  // 120 ticks

  // Minimum note duration: half beat (240 ticks) to leave meaningful main note.
  constexpr Tick kMinNachschlagDuration = kTicksPerBeat / 2;  // 240 ticks
  if (note.duration < kMinNachschlagDuration) {
    result.push_back(note);
    return result;
  }

  result.reserve(3);

  // Main note: shortened to make room for the 2 ending notes.
  NoteEvent main_note = note;
  main_note.duration = note.duration - kEndingDuration;
  result.push_back(main_note);

  const Tick ending_start = note.start_tick + main_note.duration;

  // Resolution note: stepwise lower neighbor.
  NoteEvent resolution_note;
  resolution_note.start_tick = ending_start;
  resolution_note.duration = kSubNoteDuration;
  resolution_note.pitch = resolution_pitch;
  resolution_note.velocity = note.velocity;
  resolution_note.voice = note.voice;
  resolution_note.source = BachNoteSource::Ornament;
  result.push_back(resolution_note);

  // Return note: back to the main pitch.
  NoteEvent return_note;
  return_note.start_tick = ending_start + kSubNoteDuration;
  return_note.duration = kSubNoteDuration;
  return_note.pitch = note.pitch;
  return_note.velocity = note.velocity;
  return_note.voice = note.voice;
  return_note.source = BachNoteSource::Ornament;
  result.push_back(return_note);

  return result;
}

}  // namespace bach
