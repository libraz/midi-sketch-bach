// Implementation of chaconne configuration and variation plan generation.

#include "solo_string/arch/chaconne_config.h"

#include <algorithm>
#include <random>
#include <string>

#include "analysis/fail_report.h"
#include "core/rng_util.h"
#include "harmony/key.h"

namespace bach {

namespace {

/// @brief Texture complexity ordering for wave-pattern assignment within blocks.
/// Order: SingleLine < Bariolage < Arpeggiated < ScalePassage < ImpliedPolyphony
constexpr TextureType kTextureComplexityOrder[] = {
    TextureType::SingleLine,
    TextureType::Bariolage,
    TextureType::Arpeggiated,
    TextureType::ScalePassage,
    TextureType::ImpliedPolyphony,
};
constexpr int kTextureComplexityCount = 5;

/// @brief Select a texture by complexity index (clamped).
TextureType textureByComplexity(int index) {
  if (index < 0) index = 0;
  if (index >= kTextureComplexityCount) index = kTextureComplexityCount - 1;
  return kTextureComplexityOrder[index];
}

/// @brief Assign textures to a block of variations using a wave pattern.
///
/// Divides the block into sub-arcs of ~3-4 variations. Each sub-arc follows
/// low→high→mid complexity. Successive sub-arcs start at increasingly higher
/// base complexity.
/// @brief Select a VariationType using role-specific weights.
VariationType selectTypeForRole(std::mt19937& rng, VariationRole role) {
  using VT = VariationType;
  switch (role) {
    case VariationRole::Develop:
      return rng::selectWeighted(rng, std::vector<VT>{VT::Rhythmic, VT::Lyrical},
                                 {0.60f, 0.40f});
    case VariationRole::Destabilize:
      return rng::selectWeighted(rng, std::vector<VT>{VT::Virtuosic, VT::Rhythmic},
                                 {0.70f, 0.30f});
    case VariationRole::Illuminate:
      return rng::selectWeighted(rng, std::vector<VT>{VT::Lyrical, VT::Chordal},
                                 {0.55f, 0.45f});
    case VariationRole::Accumulate:
      return rng::selectWeighted(rng, std::vector<VT>{VT::Virtuosic, VT::Chordal},
                                 {0.50f, 0.50f});
    default:
      return VT::Theme;
  }
}

/// @brief Select a TextureType based on VariationRole using weighted probabilities.
///
/// Anchor roles (Establish, Resolve) always use SingleLine.
/// Other roles select from role-appropriate texture palettes with RNG.
/// Accumulate is handled separately in buildTextureContext.
///
/// @param rng Mersenne Twister RNG.
/// @param role The variation's structural role.
/// @return Selected TextureType.
TextureType selectTextureForRole(std::mt19937& rng, VariationRole role) {
  using TT = TextureType;
  switch (role) {
    case VariationRole::Establish:
    case VariationRole::Resolve:
      return TT::SingleLine;
    case VariationRole::Develop:
      return rng::selectWeighted(
          rng, std::vector<TT>{TT::ImpliedPolyphony, TT::Arpeggiated, TT::Bariolage},
          {0.40f, 0.30f, 0.30f});
    case VariationRole::Destabilize:
      return rng::selectWeighted(
          rng,
          std::vector<TT>{TT::ScalePassage, TT::ImpliedPolyphony, TT::Bariolage,
                          TT::Arpeggiated},
          {0.35f, 0.30f, 0.20f, 0.15f});
    case VariationRole::Illuminate:
      return rng::selectWeighted(
          rng,
          std::vector<TT>{TT::SingleLine, TT::Arpeggiated, TT::Bariolage, TT::ScalePassage},
          {0.30f, 0.30f, 0.25f, 0.15f});
    case VariationRole::Accumulate:
      // Accumulate texture is handled in buildTextureContext (Step 4).
      return TT::ImpliedPolyphony;
  }
  return TT::SingleLine;
}

void assignBlockTextures(std::mt19937& rng, std::vector<ChaconneVariation>& block,
                         int base_complexity) {
  if (block.empty()) return;
  int n = static_cast<int>(block.size());

  // Sub-arc size: 2-4 variations (randomized).
  int sub_arc_size = rng::rollRange(rng, 2, 4);
  int num_sub_arcs = (n + sub_arc_size - 1) / sub_arc_size;

  for (int arc = 0; arc < num_sub_arcs; ++arc) {
    int arc_start = arc * sub_arc_size;
    int arc_end = std::min(arc_start + sub_arc_size, n);
    int arc_len = arc_end - arc_start;
    int arc_base = base_complexity + arc;  // Gradually raise base

    for (int idx = 0; idx < arc_len; ++idx) {
      int complexity;
      if (arc_len == 1) {
        complexity = arc_base;
      } else if (idx < arc_len / 2 + 1) {
        // Rising phase: low → high
        complexity = arc_base + idx;
      } else {
        // Falling phase: high → mid
        complexity = arc_base + (arc_len - 1 - idx);
      }
      block[arc_start + idx].primary_texture = textureByComplexity(complexity);
    }
  }
}

/// @brief Get related major keys for Illuminate islands.
/// Returns: relative major, subdominant of relative, dominant.
std::vector<KeySignature> getIslandKeys(const KeySignature& key) {
  std::vector<KeySignature> keys;
  KeySignature relative = getRelative(key);
  keys.push_back(relative);                          // e.g. F major
  keys.push_back(getSubdominant(relative));           // e.g. Bb major
  KeySignature dom = getDominant(key);
  dom.is_minor = false;  // Force major for island contrast.
  keys.push_back(dom);                                // e.g. A major
  return keys;
}

}  // namespace

std::vector<ChaconneVariation> createStandardVariationPlan(const KeySignature& key,
                                                            std::mt19937& rng) {
  // Standard chaconne variation plan (~10 variations).
  // Fixed order: Establish -> Develop -> Destabilize -> Illuminate
  //              -> Destabilize -> Accumulate(x3) -> Resolve
  //
  // The structural arc (Role sequence) is config-fixed and seed-independent.
  // VariationType within each role is RNG-driven.

  KeySignature major_key = getParallel(key);

  std::vector<ChaconneVariation> plan;
  plan.reserve(10);

  int var_num = 0;

  // --- Minor front section ---

  // Variation 0: Establish (Theme) -- opening statement (fixed)
  plan.push_back({var_num++, VariationRole::Establish, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  // Variation 1: Develop -- builds energy
  plan.push_back({var_num++, VariationRole::Develop,
                  selectTypeForRole(rng, VariationRole::Develop),
                  selectTextureForRole(rng, VariationRole::Develop), key, false});

  // Variation 2: Destabilize -- pre-major tension
  plan.push_back({var_num++, VariationRole::Destabilize,
                  selectTypeForRole(rng, VariationRole::Destabilize),
                  selectTextureForRole(rng, VariationRole::Destabilize), key, false});

  // --- Major section (separate personality) ---

  // Variation 3: Illuminate -- major key
  plan.push_back({var_num++, VariationRole::Illuminate,
                  selectTypeForRole(rng, VariationRole::Illuminate),
                  selectTextureForRole(rng, VariationRole::Illuminate), major_key, true});

  // Variation 4: Illuminate -- major key
  plan.push_back({var_num++, VariationRole::Illuminate,
                  selectTypeForRole(rng, VariationRole::Illuminate),
                  selectTextureForRole(rng, VariationRole::Illuminate), major_key, true});

  // --- Minor back section ---

  // Variation 5: Destabilize -- return to minor, rebuilding tension
  plan.push_back({var_num++, VariationRole::Destabilize,
                  selectTypeForRole(rng, VariationRole::Destabilize),
                  selectTextureForRole(rng, VariationRole::Destabilize), key, false});

  // Variation 6-8: Accumulate -- climax (Principle 4: fixed design values)
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Chordal,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});

  // Variation 9: Resolve (Theme) -- return to opening, closure (fixed)
  plan.push_back({var_num++, VariationRole::Resolve, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  return plan;
}

std::vector<ChaconneVariation> createScaledVariationPlan(const KeySignature& key,
                                                          int target_variations,
                                                          std::mt19937& rng) {
  // For small counts, use the standard plan.
  if (target_variations <= 10) {
    return createStandardVariationPlan(key, rng);
  }

  KeySignature major_key = getParallel(key);
  std::vector<KeySignature> island_keys = getIslandKeys(key);

  // Fixed roles: Establish(1) + Accumulate(3) + Resolve(1) = 5
  constexpr int kFixed = 5;
  int scalable = target_variations - kFixed;

  // Distribute scalable variations by BWV1004 proportions.
  // Develop: 20%, Destabilize-pre: 5%, Illuminate-main: 22%, Post-major: 53%
  int develop_count = std::max(1, static_cast<int>(scalable * 0.20 + 0.5));
  int destab_pre_count = std::max(1, static_cast<int>(scalable * 0.05 + 0.5));
  int illuminate_main_count = std::max(1, static_cast<int>(scalable * 0.22 + 0.5));
  int post_major_count = scalable - develop_count - destab_pre_count - illuminate_main_count;
  if (post_major_count < 1) post_major_count = 1;

  std::vector<ChaconneVariation> plan;
  plan.reserve(target_variations);
  int var_num = 0;

  // --- Establish (1) ---
  plan.push_back({var_num++, VariationRole::Establish, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  // --- Develop block ---
  {
    std::vector<ChaconneVariation> block;
    for (int idx = 0; idx < develop_count; ++idx) {
      block.push_back({var_num++, VariationRole::Develop,
                       selectTypeForRole(rng, VariationRole::Develop),
                       TextureType::SingleLine, key, false});
    }
    assignBlockTextures(rng, block, 1);  // Start above SingleLine
    for (auto& v : block) plan.push_back(v);
  }

  // --- Destabilize pre-major ---
  {
    std::vector<ChaconneVariation> block;
    for (int idx = 0; idx < destab_pre_count; ++idx) {
      block.push_back({var_num++, VariationRole::Destabilize,
                       selectTypeForRole(rng, VariationRole::Destabilize),
                       TextureType::SingleLine, key, false});
    }
    assignBlockTextures(rng, block, 2);
    for (auto& v : block) plan.push_back(v);
  }

  // --- Illuminate main (major section) ---
  {
    std::vector<ChaconneVariation> block;
    for (int idx = 0; idx < illuminate_main_count; ++idx) {
      block.push_back({var_num++, VariationRole::Illuminate,
                       selectTypeForRole(rng, VariationRole::Illuminate),
                       TextureType::SingleLine, major_key, true});
    }
    assignBlockTextures(rng, block, 0);  // Major section: lighter textures
    for (auto& v : block) plan.push_back(v);
  }

  // --- Post-major interleaved: Destabilize with Illuminate islands ---
  {
    // Insert Illuminate islands within Destabilize blocks at randomized intervals.
    int island_interval = rng::rollRange(rng, 6, 8);
    int max_island_size = rng::rollRange(rng, 1, 2);

    int remaining = post_major_count;
    int island_key_idx = 0;

    while (remaining > 0) {
      // Destabilize segment.
      int destab_seg = std::min(island_interval, remaining);

      // Check if we have room for an island after this segment.
      int after_this = remaining - destab_seg;
      bool insert_island = (after_this >= max_island_size + 1);  // Need room for island + more

      if (!insert_island) {
        // Use all remaining as Destabilize (final approach).
        for (int idx = 0; idx < remaining; ++idx) {
          plan.push_back({var_num++, VariationRole::Destabilize,
                          selectTypeForRole(rng, VariationRole::Destabilize),
                          TextureType::ScalePassage, key, false});
        }
        remaining = 0;
      } else {
        // Place Destabilize segment.
        std::vector<ChaconneVariation> destab_block;
        for (int idx = 0; idx < destab_seg; ++idx) {
          destab_block.push_back({var_num++, VariationRole::Destabilize,
                                  selectTypeForRole(rng, VariationRole::Destabilize),
                                  TextureType::SingleLine, key, false});
        }
        assignBlockTextures(rng, destab_block, 2);
        for (auto& v : destab_block) plan.push_back(v);
        remaining -= destab_seg;

        // Place Illuminate island (1-2 variations, is_major_section=false).
        int island_size = std::min(max_island_size, remaining - 1);  // Keep at least 1 for later
        if (island_size < 1) island_size = 1;

        KeySignature isl_key = island_keys[island_key_idx % island_keys.size()];
        ++island_key_idx;

        for (int idx = 0; idx < island_size; ++idx) {
          plan.push_back({var_num++, VariationRole::Illuminate,
                          selectTypeForRole(rng, VariationRole::Illuminate),
                          TextureType::Arpeggiated, isl_key, false});
        }
        remaining -= island_size;
      }
    }
  }

  // --- Accumulate (3) ---
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Chordal,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  selectTextureForRole(rng, VariationRole::Accumulate), key, false});

  // --- Resolve (1) ---
  plan.push_back({var_num++, VariationRole::Resolve, VariationType::Theme,
                  TextureType::SingleLine, key, false});

  return plan;
}

FailReport validateVariationPlanReport(const std::vector<ChaconneVariation>& plan) {
  FailReport report;

  // Check 1: Empty plan.
  if (plan.empty()) {
    report.addIssue({FailKind::ConfigFail, FailSeverity::Critical,
                     /*tick=*/0, /*bar=*/0, /*beat=*/0,
                     /*voice_a=*/0, /*voice_b=*/0,
                     "empty_plan", "Variation plan is empty"});
    return report;
  }

  // Extract roles for order validation and check type constraints.
  std::vector<VariationRole> roles;
  roles.reserve(plan.size());

  int accumulate_count = 0;

  for (const auto& var : plan) {
    roles.push_back(var.role);

    if (var.role == VariationRole::Accumulate) {
      ++accumulate_count;
    }

    // Check 2: Each variation's type must be allowed for its role.
    if (!isTypeAllowedForRole(var.type, var.role)) {
      report.addIssue({FailKind::ConfigFail, FailSeverity::Critical,
                       /*tick=*/0, /*bar=*/0, /*beat=*/0,
                       /*voice_a=*/0, /*voice_b=*/0,
                       "invalid_type_for_role",
                       "Variation " + std::to_string(var.variation_number) + ": type " +
                           variationTypeToString(var.type) + " not allowed for role " +
                           variationRoleToString(var.role)});
    }
  }

  // Check 3: Accumulate must appear exactly 3 times.
  if (accumulate_count != 3) {
    report.addIssue({FailKind::ConfigFail, FailSeverity::Critical,
                     /*tick=*/0, /*bar=*/0, /*beat=*/0,
                     /*voice_a=*/0, /*voice_b=*/0,
                     "accumulate_count",
                     "Expected 3 Accumulate, found " + std::to_string(accumulate_count)});
  }

  // Check 4: Final variation must be Resolve with Theme type.
  const auto& last = plan.back();
  if (last.role != VariationRole::Resolve || last.type != VariationType::Theme) {
    report.addIssue({FailKind::ConfigFail, FailSeverity::Critical,
                     /*tick=*/0, /*bar=*/0, /*beat=*/0,
                     /*voice_a=*/0, /*voice_b=*/0,
                     "final_not_resolve",
                     "Last variation must be Resolve with Theme type"});
  }

  // Check 5: Role order validity.
  if (!isRoleOrderValid(roles)) {
    report.addIssue({FailKind::ConfigFail, FailSeverity::Critical,
                     /*tick=*/0, /*bar=*/0, /*beat=*/0,
                     /*voice_a=*/0, /*voice_b=*/0,
                     "invalid_role_order",
                     "Variation role sequence violates ordering constraints"});
  }

  return report;
}

bool validateVariationPlan(const std::vector<ChaconneVariation>& plan) {
  return !validateVariationPlanReport(plan).hasCritical();
}

}  // namespace bach
