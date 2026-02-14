/// @file
/// @brief Phrase-aware velocity curves for non-organ instruments.

#include "midi/velocity_curve.h"

#include <algorithm>

namespace bach {

uint8_t computeVelocity(Tick tick, const std::vector<Tick>& cadence_ticks,
                         Tick phrase_start_tick) {
  constexpr int kBaseVelocity = 70;
  int velocity = kBaseVelocity;

  // Beat emphasis.
  uint8_t beat = beatInBar(tick);
  switch (beat) {
    case 0: velocity += 10; break;  // Downbeat
    case 2: velocity += 5;  break;  // Secondary strong beat
    case 1:
    case 3: velocity -= 5;  break;  // Weak beats
    default: break;
  }

  // Phrase start emphasis (first bar of phrase).
  if (tick >= phrase_start_tick && tick < phrase_start_tick + kTicksPerBar) {
    velocity += 8;
  }

  // Pre-cadence diminuendo: 2 beats before any cadence tick.
  for (Tick cad_tick : cadence_ticks) {
    if (cad_tick > kTicksPerBeat * 2 &&
        tick >= cad_tick - kTicksPerBeat * 2 &&
        tick < cad_tick) {
      velocity -= 3;
      break;
    }
  }

  // Clamp to valid range.
  return static_cast<uint8_t>(std::clamp(velocity, 50, 110));
}

void applyVelocityCurve(std::vector<NoteEvent>& notes,
                        InstrumentType instrument,
                        const std::vector<Tick>& cadence_ticks) {
  // Organ and harpsichord use fixed velocity -- do not modify.
  if (instrument == InstrumentType::Organ ||
      instrument == InstrumentType::Harpsichord) {
    return;
  }

  // Estimate phrase boundaries: each bar is a rough phrase unit.
  for (auto& note : notes) {
    Tick phrase_start = (note.start_tick / kTicksPerBar) * kTicksPerBar;
    note.velocity = computeVelocity(note.start_tick, cadence_ticks, phrase_start);
  }
}

}  // namespace bach
