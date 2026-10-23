/// @file
/// @brief Phrase-aware velocity curves for non-organ instruments.

#include "midi/velocity_curve.h"

#include <algorithm>

namespace bach {
namespace {

TimeSignature normalizedTimeSignature(TimeSignature time_signature) {
  if (time_signature.numerator == 0 || time_signature.denominator == 0) {
    return {};
  }
  return time_signature;
}

}  // namespace

uint8_t computeVelocity(Tick tick, const std::vector<Tick>& cadence_ticks, Tick phrase_start_tick,
                        TimeSignature time_signature, MeterProfile meter_profile) {
  constexpr int kBaseVelocity = 70;
  int velocity = kBaseVelocity;

  // Beat emphasis follows the form's meter rather than the global 4/4 grid.
  const TimeSignature meter = normalizedTimeSignature(time_signature);
  const Tick beat_ticks = kTicksPerBeat * 4 / meter.denominator;
  const Tick ticks_per_bar = meter.ticksPerBar();
  const Tick beat = (tick % ticks_per_bar) / beat_ticks;
  if (beat == 0) {
    velocity += 10;
  } else if ((meter.numerator == 4 && meter.denominator == 4 && beat == 2) ||
             (meter.numerator == 3 && meter.denominator == 4 &&
              meter_profile == MeterProfile::SarabandeTriple && beat == 1)) {
    velocity += 5;
  } else {
    velocity -= 5;
  }

  // Phrase start emphasis (first bar of phrase).
  if (tick >= phrase_start_tick && tick - phrase_start_tick < ticks_per_bar) {
    velocity += 8;
  }

  // Pre-cadence diminuendo: 2 beats before any cadence tick.
  const Tick cadence_window = beat_ticks * 2;
  for (Tick cad_tick : cadence_ticks) {
    if (cad_tick > cadence_window && tick >= cad_tick - cadence_window && tick < cad_tick) {
      velocity -= 3;
      break;
    }
  }

  // Clamp to valid range.
  return static_cast<uint8_t>(std::clamp(velocity, 50, 110));
}

void applyVelocityCurve(std::vector<NoteEvent>& notes, InstrumentType instrument,
                        const std::vector<Tick>& cadence_ticks, TimeSignature time_signature,
                        MeterProfile meter_profile) {
  // Organ and harpsichord use fixed velocity -- do not modify.
  if (instrument == InstrumentType::Organ || instrument == InstrumentType::Harpsichord) {
    return;
  }

  // A phrase starts at the piece start and at every cadence tick.
  const TimeSignature meter = normalizedTimeSignature(time_signature);
  for (auto& note : notes) {
    const auto next_cadence =
        std::upper_bound(cadence_ticks.begin(), cadence_ticks.end(), note.start_tick);
    const Tick phrase_start = next_cadence == cadence_ticks.begin() ? 0 : *(next_cadence - 1);
    note.velocity =
        computeVelocity(note.start_tick, cadence_ticks, phrase_start, meter, meter_profile);
  }
}

}  // namespace bach
