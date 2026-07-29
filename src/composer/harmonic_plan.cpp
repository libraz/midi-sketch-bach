#include "composer/harmonic_plan.h"

namespace bach::composer {
namespace {

constexpr std::uint8_t kMajorDegrees[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr std::uint8_t kMinorDegrees[7] = {0, 2, 3, 5, 7, 8, 10};

HarmonicFunction functionFor(RomanNumeral degree) {
  switch (degree) {
    case RomanNumeral::I:
    case RomanNumeral::III:
      return HarmonicFunction::T;
    case RomanNumeral::IV:
      return HarmonicFunction::S;
    case RomanNumeral::V:
    case RomanNumeral::VII:
      return HarmonicFunction::D;
    case RomanNumeral::II:
    case RomanNumeral::VI:
      return HarmonicFunction::Pred;
  }
  return HarmonicFunction::T;
}

}  // namespace

bool annotateDiatonicChordMetadata(HarmonicPlan* plan) {
  if (plan == nullptr) {
    return false;
  }
  bool complete = true;
  const auto* scale = plan->is_minor ? kMinorDegrees : kMajorDegrees;
  for (ChordEvent& chord : plan->chords) {
    if (chord.has_degree) {
      continue;
    }
    const std::uint8_t relative = static_cast<std::uint8_t>(
        (static_cast<int>(chord.root_pc) - static_cast<int>(plan->tonic_pc) + 12) % 12);
    bool found = false;
    for (std::uint8_t index = 0; index < 7; ++index) {
      if (relative != scale[index]) {
        continue;
      }
      chord.degree = static_cast<RomanNumeral>(index);
      chord.function = functionFor(chord.degree);
      chord.inversion = ChordInversion::Root;
      chord.has_degree = true;
      found = true;
      break;
    }
    complete = complete && found;
  }
  return complete;
}

}  // namespace bach::composer
