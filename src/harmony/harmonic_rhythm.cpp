// Implementation of harmonic rhythm dynamic control.

#include "harmony/harmonic_rhythm.h"

namespace bach {

float computeRhythmFactor(Tick tick, Tick total_duration,
                          const std::vector<Tick>& cadence_ticks) {
  // Edge case: zero duration returns default factor.
  if (total_duration == 0) {
    return 1.0f;
  }

  // Check pre-cadence acceleration first (takes priority over phase).
  for (const auto& cadence_tick : cadence_ticks) {
    // The acceleration window is [cadence_tick - kPreCadenceWindow, cadence_tick).
    if (cadence_tick >= kPreCadenceWindow) {
      Tick window_start = cadence_tick - kPreCadenceWindow;
      if (tick >= window_start && tick < cadence_tick) {
        return kPreCadenceAcceleration;
      }
    } else {
      // Cadence is within the first 2 beats; window starts at tick 0.
      if (tick < cadence_tick) {
        return kPreCadenceAcceleration;
      }
    }
  }

  // Determine phase from normalized position.
  // Design values (fixed, not configurable per Principle 3):
  //   [0.0, 1/3): Establish
  //   [1/3, 2/3): Develop
  //   [2/3, 1.0]: Resolve
  float position = static_cast<float>(tick) / static_cast<float>(total_duration);
  if (position < 0.0f) position = 0.0f;
  if (position > 1.0f) position = 1.0f;

  constexpr float kEstablishEnd = 1.0f / 3.0f;
  constexpr float kDevelopEnd = 2.0f / 3.0f;

  if (position < kEstablishEnd) {
    return kHarmonicRhythmEstablish;
  }
  if (position < kDevelopEnd) {
    return kHarmonicRhythmDevelop;
  }
  return kHarmonicRhythmResolve;
}

void applyRhythmFactors(std::vector<HarmonicEvent>& events,
                        Tick total_duration,
                        const std::vector<Tick>& cadence_ticks) {
  for (auto& event : events) {
    event.rhythm_factor = computeRhythmFactor(event.tick, total_duration, cadence_ticks);
  }
}

float phaseToRhythmFactor(FuguePhase phase, bool near_cadence) {
  if (near_cadence) {
    return kPreCadenceAcceleration;
  }
  switch (phase) {
    case FuguePhase::Establish:
      return kHarmonicRhythmEstablish;
    case FuguePhase::Develop:
      return kHarmonicRhythmDevelop;
    case FuguePhase::Resolve:
      return kHarmonicRhythmResolve;
  }
  return kHarmonicRhythmDevelop;
}

}  // namespace bach
