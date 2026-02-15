// Pedal keyboard constraint utilities for pipe organ.

#ifndef BACH_ORGAN_PEDAL_CONSTRAINTS_H
#define BACH_ORGAN_PEDAL_CONSTRAINTS_H

#include <cstdint>

#include "core/pitch_utils.h"

namespace bach {

/// Ideal pedal range (no penalty zone). Pitches within this range
/// receive zero penalty; pitches outside receive a linearly increasing cost.
constexpr uint8_t kPedalIdealLow = 24;   // C1
constexpr uint8_t kPedalIdealHigh = 50;  // D3

/// Cost penalty per semitone outside the ideal pedal range.
/// This is a soft penalty (not hard rejection) to gradually discourage
/// out-of-range pedal notes without completely forbidding them.
constexpr float kPedalPenaltyPerSemitone = 5.0f;

/// @brief Calculate soft penalty for a pedal pitch.
///
/// Returns 0.0 if the pitch falls within the ideal pedal range [C1, D3].
/// For pitches outside, penalty increases linearly at
/// kPedalPenaltyPerSemitone per semitone of distance from the ideal boundary.
///
/// @param pitch MIDI pitch number (0-127).
/// @return Penalty value (0.0 = no penalty, positive = outside range).
inline float calculatePedalPenalty(uint8_t pitch) {
  if (pitch >= kPedalIdealLow && pitch <= kPedalIdealHigh) {
    return 0.0f;
  }
  if (pitch < kPedalIdealLow) {
    return static_cast<float>(kPedalIdealLow - pitch) * kPedalPenaltyPerSemitone;
  }
  return static_cast<float>(pitch - kPedalIdealHigh) * kPedalPenaltyPerSemitone;
}

}  // namespace bach

#endif  // BACH_ORGAN_PEDAL_CONSTRAINTS_H
