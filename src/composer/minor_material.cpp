#include "composer/minor_material.h"

#include "composer/figuration.h"

namespace bach::composer::detail {

namespace {

// C melodic-minor ASCENDING membership (pitch class): C D Eb F G A B.
// Degrees 6 and 7 are raised (A natural = 9, B natural = 11) relative to
// natural minor, which removes the Ab(8)->B(11) augmented 2nd from an
// ascending line reaching the leading tone.
constexpr bool melodicMinorAscInScale(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 3 || p == 5 || p == 7 || p == 9 || p == 11;
}

}  // namespace

int minorScaleUp(int midi, int steps, bool harmonic_context) {
  int cur = midi;
  for (int step = 0; step < steps; ++step) {
    for (int add = 1; add <= 12; ++add) {
      const bool member =
          harmonic_context ? melodicMinorAscInScale(cur + add) : chaconneInScale(cur + add);
      if (member) {
        cur += add;
        break;
      }
    }
  }
  return cur;
}

bool usePicardy(std::uint32_t seed) {
  // Even seed selects the Picardy (major) third; deterministic per seed.
  return (seed & 1u) == 0u;
}

}  // namespace bach::composer::detail
