#include "composer/harmonic_plan.h"

#include "composer/rule_helpers.h"

namespace bach::composer {
namespace {

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
  for (ChordEvent& chord : plan->chords) {
    if (chord.has_degree) {
      continue;
    }
    // Degrees count from the key sounding at the chord, so a modulated area's
    // own dominant is labelled V there.
    const rule_helpers::KeyContext key = rule_helpers::keyAt(*plan, chord.start_tick);
    const detail::ScaleSpelling scale =
        detail::spellScale(0, key.is_minor ? detail::Mode::Minor : detail::Mode::Major, 0);
    const std::uint8_t relative = static_cast<std::uint8_t>(
        (static_cast<int>(chord.root_pc) - static_cast<int>(key.tonic_pc) + 12) % 12);
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
