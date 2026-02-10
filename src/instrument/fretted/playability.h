// Fretted instrument playability cost calculation for note transitions.
//
// Complements the single-note PlayabilityCost in fretted_instrument.h by
// providing transition cost between consecutive fret positions.

#ifndef BACH_INSTRUMENT_FRETTED_PLAYABILITY_H
#define BACH_INSTRUMENT_FRETTED_PLAYABILITY_H

#include <cstdint>

namespace bach {

/// @brief Ergonomic cost of transitioning between two fret positions.
///
/// Used by the arpeggio and texture generators to penalize difficult
/// left-hand movements on fretted instruments.
struct TransitionCost {
  float stretch_cost = 0.0f;          // Finger stretch within one position
  float position_shift_cost = 0.0f;   // Cost of moving the hand to a new position
  float string_crossing_cost = 0.0f;  // Cost of crossing strings

  /// @brief Total combined cost.
  float total() const {
    return stretch_cost + position_shift_cost + string_crossing_cost;
  }
};

/// @brief Calculate the ergonomic transition cost between two fret positions.
/// @param from_fret Starting fret number (0 = open string).
/// @param to_fret Destination fret number.
/// @param string_span Number of strings crossed (0 = same string, 1 = adjacent).
/// @return TransitionCost with component breakdown.
///
/// Cost model:
/// - Stretch: fret distance within a 4-fret span is low cost; beyond is high.
/// - Position shift: any movement > 4 frets requires a hand position change.
/// - String crossing: each crossed string adds a small cost.
inline TransitionCost calculateTransitionCost(uint8_t from_fret, uint8_t to_fret,
                                              uint8_t string_span) {
  TransitionCost cost;

  // Fret distance
  int fret_distance = static_cast<int>(to_fret) - static_cast<int>(from_fret);
  int abs_fret_dist = fret_distance < 0 ? -fret_distance : fret_distance;

  // Stretch cost: within 4-fret span is comfortable, beyond is costly.
  constexpr int kComfortableSpan = 4;
  if (abs_fret_dist <= kComfortableSpan) {
    // Linear cost within comfortable range.
    cost.stretch_cost = static_cast<float>(abs_fret_dist) * 0.1f;
  } else {
    // Exponential penalty beyond comfortable range.
    int excess = abs_fret_dist - kComfortableSpan;
    cost.stretch_cost = 0.4f + static_cast<float>(excess) * 0.3f;
  }

  // Position shift cost: significant if moving beyond 4 frets.
  if (abs_fret_dist > kComfortableSpan) {
    cost.position_shift_cost = 0.5f + static_cast<float>(abs_fret_dist - kComfortableSpan) * 0.2f;
  }

  // String crossing cost: small penalty per string crossed.
  constexpr float kStringCrossCost = 0.05f;
  cost.string_crossing_cost = static_cast<float>(string_span) * kStringCrossCost;

  return cost;
}

}  // namespace bach

#endif  // BACH_INSTRUMENT_FRETTED_PLAYABILITY_H
