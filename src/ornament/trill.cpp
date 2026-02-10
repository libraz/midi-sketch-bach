// Trill ornament generation implementation.

#include "ornament/trill.h"

namespace bach {

std::vector<NoteEvent> generateTrill(const NoteEvent& note, uint8_t upper_pitch,
                                     uint8_t speed) {
  std::vector<NoteEvent> result;

  // Calculate number of sub-notes based on speed and duration.
  // Speed = alternation cycles per beat. Each cycle = 2 sub-notes.
  // Total sub-notes = speed * 2 * (duration / kTicksPerBeat).
  // Minimum: 4 sub-notes (2 cycles) for a meaningful trill.
  const Tick beats_in_note = note.duration / kTicksPerBeat;
  const Tick remainder_ticks = note.duration % kTicksPerBeat;

  // For sub-beat notes, calculate based on proportional duration.
  uint32_t total_subnotes = static_cast<uint32_t>(speed) * 2;
  if (beats_in_note > 0) {
    total_subnotes = static_cast<uint32_t>(speed) * 2 * static_cast<uint32_t>(beats_in_note);
  }

  // Account for fractional beat remainder by adding proportional sub-notes.
  if (remainder_ticks > 0 && beats_in_note > 0) {
    uint32_t extra = static_cast<uint32_t>(speed) * 2 * remainder_ticks / kTicksPerBeat;
    total_subnotes += extra;
  }

  // Minimum 3 sub-notes for a trill to make sense (main-upper-main).
  if (total_subnotes < 3) {
    total_subnotes = 3;
  }

  // Ensure odd count so the last index (total-1) is even = main note.
  // Pattern: idx 0=main, 1=upper, 2=main, 3=upper, ... last(even)=main.
  if (total_subnotes % 2 == 0) {
    total_subnotes += 1;
  }

  // If original note is too short to subdivide meaningfully, return as-is.
  const Tick subnote_duration = note.duration / total_subnotes;
  if (subnote_duration == 0) {
    result.push_back(note);
    return result;
  }

  result.reserve(total_subnotes);

  Tick current_tick = note.start_tick;
  for (uint32_t idx = 0; idx < total_subnotes; ++idx) {
    NoteEvent sub_note;
    sub_note.start_tick = current_tick;
    sub_note.velocity = note.velocity;
    sub_note.voice = note.voice;

    // Alternate: even indices = main note, odd indices = upper neighbor.
    sub_note.pitch = (idx % 2 == 0) ? note.pitch : upper_pitch;

    // Last sub-note gets any remaining ticks to cover the full duration.
    if (idx == total_subnotes - 1) {
      sub_note.duration = (note.start_tick + note.duration) - current_tick;
    } else {
      sub_note.duration = subnote_duration;
    }

    result.push_back(sub_note);
    current_tick += subnote_duration;
  }

  return result;
}

}  // namespace bach
