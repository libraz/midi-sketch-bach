// Trill ornament generation implementation.

#include "ornament/trill.h"

#include <algorithm>

namespace bach {

std::vector<NoteEvent> generateTrill(const NoteEvent& note, uint8_t upper_pitch,
                                     uint8_t speed, bool start_from_upper) {
  std::vector<NoteEvent> result;

  // Calculate number of sub-notes based on speed and duration.
  // Speed = alternation cycles per beat. Each cycle = 2 sub-notes.
  // Total sub-notes = speed * 2 * (duration / kTicksPerBeat).
  // Minimum: 3 sub-notes for a meaningful trill.
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

  // Minimum 3 sub-notes for a trill to make sense.
  if (total_subnotes < 3) {
    total_subnotes = 3;
  }

  // Ensure odd count so the trill ends on the main note.
  // For start_from_upper: upper(0), main(1), upper(2), ..., main(last=even) -- odd count.
  // For legacy: main(0), upper(1), main(2), ..., main(last=even) -- odd count.
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

    if (start_from_upper) {
      // C.P.E. Bach pattern: even indices = upper, odd indices = main.
      // Last sub-note (even index at total-1) must be main to resolve.
      if (idx == total_subnotes - 1) {
        sub_note.pitch = note.pitch;  // Always end on main note.
      } else {
        sub_note.pitch = (idx % 2 == 0) ? upper_pitch : note.pitch;
      }
    } else {
      // Legacy pattern: even indices = main note, odd indices = upper neighbor.
      sub_note.pitch = (idx % 2 == 0) ? note.pitch : upper_pitch;
    }

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

uint8_t computeTrillSpeed(uint16_t bpm) {
  // Map BPM to trill speed: bpm / 30, clamped to [2, 8].
  // 60 bpm -> 2, 120 bpm -> 4, 240 bpm -> 8.
  uint8_t raw = static_cast<uint8_t>(bpm / 30);
  return std::max(static_cast<uint8_t>(2), std::min(static_cast<uint8_t>(8), raw));
}

}  // namespace bach
