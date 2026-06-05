#ifndef BACH_COMPOSER_ARC_H
#define BACH_COMPOSER_ARC_H

#include <cstddef>
#include <cstdint>

namespace bach::composer {

// Macro-form stage a single arc cycle belongs to. The order Establish ->
// Develop -> Climax -> Resolve is the seed-independent "meaning axis": the
// climax is a design value, never searched for.
enum class ArcStage : std::uint8_t { Establish, Develop, Climax, Resolve };

// Design-value snapshot for one arc cycle. The upcoming per-form fixture
// builders read these to shape figure density, register, and dynamics across
// the piece without consulting any RNG.
struct ArcPoint {
  ArcStage stage;
  // Rhythmic activity tier: 0 = quarter, 1 = eighth, 2 = sixteenth, 3 = peak.
  std::uint8_t density_tier;
  // Semitone offset applied to figure centers (rises into the climax, returns
  // to 0 by the final resolve cycle).
  std::int8_t register_shift;
  // Dynamic tier: 0 = soft, 1 = medium, 2 = forte, 3 = peak.
  std::uint8_t velocity_tier;
  // True on exactly the climax cycle(s) of the curve.
  bool is_climax;
};

/**
 * @brief Resolve the deterministic arc design-values for one cycle.
 * @param cycle_index Zero-based index of the cycle, in [0, cycle_count).
 * @param cycle_count Total number of cycles in the piece (>= 1).
 * @return The ArcPoint for the requested cycle.
 * @note Pure function of its arguments: no RNG, identical output every call.
 *       Establish spans roughly the first quarter, Develop rises toward the
 *       climax placed at ~75-85% of the span, and Resolve falls back at the
 *       end. Sensible for cycle_count from 1 up to ~32.
 */
ArcPoint arcPoint(std::size_t cycle_index, std::size_t cycle_count);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_ARC_H
