// Implementation of chaconne configuration and variation plan generation.

#include "solo_string/arch/chaconne_config.h"

#include <algorithm>

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
void assignBlockTextures(std::vector<ChaconneVariation>& block, int base_complexity) {
  if (block.empty()) return;
  int n = static_cast<int>(block.size());

  // Sub-arc size: 3-4 variations.
  constexpr int kSubArcSize = 3;
  int num_sub_arcs = (n + kSubArcSize - 1) / kSubArcSize;

  for (int arc = 0; arc < num_sub_arcs; ++arc) {
    int arc_start = arc * kSubArcSize;
    int arc_end = std::min(arc_start + kSubArcSize, n);
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

std::vector<ChaconneVariation> createScaledVariationPlan(const KeySignature& key,
                                                          int target_variations) {
  // For small counts, use the standard plan.
  if (target_variations <= 10) {
    return createStandardVariationPlan(key);
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
      VariationType vtype = (idx % 2 == 0) ? VariationType::Rhythmic : VariationType::Lyrical;
      block.push_back({var_num++, VariationRole::Develop, vtype,
                       TextureType::SingleLine, key, false});
    }
    assignBlockTextures(block, 1);  // Start above SingleLine
    for (auto& v : block) plan.push_back(v);
  }

  // --- Destabilize pre-major ---
  {
    std::vector<ChaconneVariation> block;
    for (int idx = 0; idx < destab_pre_count; ++idx) {
      VariationType vtype = (idx % 2 == 0) ? VariationType::Virtuosic : VariationType::Rhythmic;
      block.push_back({var_num++, VariationRole::Destabilize, vtype,
                       TextureType::SingleLine, key, false});
    }
    assignBlockTextures(block, 2);
    for (auto& v : block) plan.push_back(v);
  }

  // --- Illuminate main (major section) ---
  {
    std::vector<ChaconneVariation> block;
    for (int idx = 0; idx < illuminate_main_count; ++idx) {
      VariationType vtype = (idx % 2 == 0) ? VariationType::Lyrical : VariationType::Chordal;
      block.push_back({var_num++, VariationRole::Illuminate, vtype,
                       TextureType::SingleLine, major_key, true});
    }
    assignBlockTextures(block, 0);  // Major section: lighter textures
    for (auto& v : block) plan.push_back(v);
  }

  // --- Post-major interleaved: Destabilize with Illuminate islands ---
  {
    // Insert an Illuminate island of 1-2 variations every ~7 Destabilize variations.
    constexpr int kIslandInterval = 7;
    constexpr int kMaxIslandSize = 2;

    // Calculate total Destabilize vs island variations.
    // We need at least 1 Destabilize at the end (final approach).
    int remaining = post_major_count;
    int island_key_idx = 0;

    while (remaining > 0) {
      // Destabilize segment.
      int destab_seg = std::min(kIslandInterval, remaining);

      // Check if we have room for an island after this segment.
      int after_this = remaining - destab_seg;
      bool insert_island = (after_this >= kMaxIslandSize + 1);  // Need room for island + more

      if (!insert_island) {
        // Use all remaining as Destabilize (final approach).
        for (int idx = 0; idx < remaining; ++idx) {
          VariationType vtype = (idx % 2 == 0) ? VariationType::Virtuosic
                                               : VariationType::Rhythmic;
          plan.push_back({var_num++, VariationRole::Destabilize, vtype,
                          TextureType::ScalePassage, key, false});
        }
        remaining = 0;
      } else {
        // Place Destabilize segment.
        std::vector<ChaconneVariation> destab_block;
        for (int idx = 0; idx < destab_seg; ++idx) {
          VariationType vtype = (idx % 2 == 0) ? VariationType::Virtuosic
                                               : VariationType::Rhythmic;
          destab_block.push_back({var_num++, VariationRole::Destabilize, vtype,
                                  TextureType::SingleLine, key, false});
        }
        assignBlockTextures(destab_block, 2);
        for (auto& v : destab_block) plan.push_back(v);
        remaining -= destab_seg;

        // Place Illuminate island (1-2 variations, is_major_section=false).
        int island_size = std::min(kMaxIslandSize, remaining - 1);  // Keep at least 1 for later
        if (island_size < 1) island_size = 1;

        KeySignature isl_key = island_keys[island_key_idx % island_keys.size()];
        ++island_key_idx;

        for (int idx = 0; idx < island_size; ++idx) {
          VariationType vtype = (idx % 2 == 0) ? VariationType::Lyrical
                                               : VariationType::Chordal;
          plan.push_back({var_num++, VariationRole::Illuminate, vtype,
                          TextureType::Arpeggiated, isl_key, false});
        }
        remaining -= island_size;
      }
    }
  }

  // --- Accumulate (3) ---
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::ImpliedPolyphony, key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Chordal,
                  TextureType::FullChords, key, false});
  plan.push_back({var_num++, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::FullChords, key, false});

  // --- Resolve (1) ---
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
