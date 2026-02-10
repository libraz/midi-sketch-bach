// Implementation of chaconne configuration and variation plan generation.

#include "solo_string/arch/chaconne_config.h"

namespace bach {

std::vector<ChaconneVariation> createStandardVariationPlan(const KeySignature& key) {
  // Standard chaconne variation plan (~10 variations).
  // Fixed order: Establish -> Develop -> Destabilize -> Illuminate
  //              -> Destabilize -> Accumulate(x3) -> Resolve
  //
  // This is a design decision (Principle 4: Trust Design Values).
  // The structural arc is config-fixed and seed-independent.

  KeySignature major_key = getParallel(key);

  std::vector<ChaconneVariation> plan;
  plan.reserve(10);

  int var_num = 0;

  // --- Minor front section ---

  // Variation 0: Establish (Theme) -- opening statement
  plan.push_back({var_num++, VariationRole::Establish, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  // Variation 1: Develop (Rhythmic) -- builds energy
  plan.push_back({var_num++, VariationRole::Develop, VariationType::Rhythmic,
                  TextureType::ImpliedPolyphony, key, false});

  // Variation 2: Destabilize (Virtuosic) -- pre-major tension
  plan.push_back({var_num++, VariationRole::Destabilize, VariationType::Virtuosic,
                  TextureType::ScalePassage, key, false});

  // --- Major section (separate personality) ---

  // Variation 3: Illuminate (Lyrical) -- major key, singing character
  plan.push_back({var_num++, VariationRole::Illuminate, VariationType::Lyrical,
                  TextureType::SingleLine, major_key, true});

  // Variation 4: Illuminate (Chordal) -- major key, harmonic warmth
  plan.push_back({var_num++, VariationRole::Illuminate, VariationType::Chordal,
                  TextureType::Arpeggiated, major_key, true});

  // --- Minor back section ---

  // Variation 5: Destabilize (Virtuosic) -- return to minor, rebuilding tension
  plan.push_back({var_num++, VariationRole::Destabilize, VariationType::Virtuosic,
                  TextureType::ScalePassage, key, false});

  // Variation 6: Accumulate (Virtuosic) -- climax buildup 1/3
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::ImpliedPolyphony, key, false});

  // Variation 7: Accumulate (Chordal) -- climax buildup 2/3
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Chordal,
                  TextureType::FullChords, key, false});

  // Variation 8: Accumulate (Virtuosic) -- climax peak 3/3
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::FullChords, key, false});

  // Variation 9: Resolve (Theme) -- return to opening, closure
  plan.push_back({var_num++, VariationRole::Resolve, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  return plan;
}

bool validateVariationPlan(const std::vector<ChaconneVariation>& plan) {
  if (plan.empty()) {
    return false;
  }

  // Extract roles for order validation.
  std::vector<VariationRole> roles;
  roles.reserve(plan.size());

  int accumulate_count = 0;

  for (const auto& var : plan) {
    roles.push_back(var.role);

    if (var.role == VariationRole::Accumulate) {
      ++accumulate_count;
    }

    // Check that each variation's type is allowed for its role.
    if (!isTypeAllowedForRole(var.type, var.role)) {
      return false;
    }
  }

  // Accumulate must appear exactly 3 times.
  if (accumulate_count != 3) {
    return false;
  }

  // Final variation must be Resolve with Theme type.
  const auto& last = plan.back();
  if (last.role != VariationRole::Resolve || last.type != VariationType::Theme) {
    return false;
  }

  // Check role order validity.
  if (!isRoleOrderValid(roles)) {
    return false;
  }

  return true;
}

}  // namespace bach
