#include "composer/minor_material.h"

#include "composer/figuration.h"

namespace bach::composer::detail {

int minorScaleUp(int midi, int steps, bool harmonic_context) {
  constexpr ChordSpec kDominant{7, false};
  return scaleUp(midi, steps, Mode::Minor, harmonic_context ? &kDominant : nullptr);
}

bool usePicardy(std::uint32_t seed) {
  // Even seed selects the Picardy (major) third; deterministic per seed.
  return (seed & 1u) == 0u;
}

}  // namespace bach::composer::detail
