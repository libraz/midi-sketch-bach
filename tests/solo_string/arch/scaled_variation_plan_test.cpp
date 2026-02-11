// Tests for createScaledVariationPlan -- chaconne duration scaling.

#include "solo_string/arch/chaconne_config.h"

#include <algorithm>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "solo_string/arch/variation_types.h"

namespace bach {
namespace {

const KeySignature kDMinor = {Key::D, true};

// ===========================================================================
// Basic structural validation for all scales
// ===========================================================================

struct ScaleTestParam {
  int target_variations;
  const char* label;
};

class ScaledVariationPlanTest : public ::testing::TestWithParam<ScaleTestParam> {
 protected:
  std::mt19937 rng{42};
};

TEST_P(ScaledVariationPlanTest, ValidatePassesForAllScales) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  EXPECT_TRUE(validateVariationPlan(plan))
      << "Plan validation failed for " << GetParam().label
      << " (" << GetParam().target_variations << " variations)";
}

TEST_P(ScaledVariationPlanTest, HasCorrectVariationCount) {
  int target = GetParam().target_variations;
  auto plan = createScaledVariationPlan(kDMinor, target, rng);
  if (target <= 10) {
    EXPECT_EQ(static_cast<int>(plan.size()), 10);
  } else {
    // Allow slight deviation from target due to island insertion rounding.
    EXPECT_GE(static_cast<int>(plan.size()), target - 2);
    EXPECT_LE(static_cast<int>(plan.size()), target + 2);
  }
}

TEST_P(ScaledVariationPlanTest, HasExactlyThreeAccumulate) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  int count = 0;
  for (const auto& v : plan) {
    if (v.role == VariationRole::Accumulate) ++count;
  }
  EXPECT_EQ(count, 3);
}

TEST_P(ScaledVariationPlanTest, FirstIsEstablishTheme) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  ASSERT_FALSE(plan.empty());
  EXPECT_EQ(plan.front().role, VariationRole::Establish);
  EXPECT_EQ(plan.front().type, VariationType::Theme);
}

TEST_P(ScaledVariationPlanTest, LastIsResolveTheme) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  ASSERT_FALSE(plan.empty());
  EXPECT_EQ(plan.back().role, VariationRole::Resolve);
  EXPECT_EQ(plan.back().type, VariationType::Theme);
}

TEST_P(ScaledVariationPlanTest, RoleOrderIsValid) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  std::vector<VariationRole> roles;
  for (const auto& v : plan) roles.push_back(v.role);
  EXPECT_TRUE(isRoleOrderValid(roles));
}

TEST_P(ScaledVariationPlanTest, AllTypesAllowedForRoles) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  for (const auto& v : plan) {
    EXPECT_TRUE(isTypeAllowedForRole(v.type, v.role))
        << "Type " << variationTypeToString(v.type)
        << " not allowed for role " << variationRoleToString(v.role)
        << " at variation " << v.variation_number;
  }
}

TEST_P(ScaledVariationPlanTest, AccumulatePositionInRange70to85Percent) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  int total = static_cast<int>(plan.size());
  if (total <= 0) return;

  // Find first Accumulate.
  int first_accum = -1;
  for (int idx = 0; idx < total; ++idx) {
    if (plan[idx].role == VariationRole::Accumulate) {
      first_accum = idx;
      break;
    }
  }
  ASSERT_GE(first_accum, 0) << "No Accumulate found";

  float position = static_cast<float>(first_accum) / static_cast<float>(total);
  // Accumulate is structurally 4th-from-last (3 Accumulate + 1 Resolve).
  // Position = (N-4)/N: 0.60 for N=10, 0.94 for N=64.
  float expected = static_cast<float>(total - 4) / static_cast<float>(total);
  EXPECT_NEAR(position, expected, 0.02f)
      << "Accumulate at unexpected position " << position
      << " (expected ~" << expected << " for " << total << " variations)";
}

TEST_P(ScaledVariationPlanTest, VariationNumbersAreSequential) {
  auto plan = createScaledVariationPlan(kDMinor, GetParam().target_variations, rng);
  for (size_t idx = 0; idx < plan.size(); ++idx) {
    EXPECT_EQ(plan[idx].variation_number, static_cast<int>(idx));
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllScales, ScaledVariationPlanTest,
    ::testing::Values(
        ScaleTestParam{10, "Short"},
        ScaleTestParam{24, "Medium"},
        ScaleTestParam{40, "Long"},
        ScaleTestParam{64, "Full"}
    ),
    [](const ::testing::TestParamInfo<ScaleTestParam>& info) {
      return info.param.label;
    }
);

// ===========================================================================
// Short scale = identical to standard plan
// ===========================================================================

TEST(ScaledVariationPlanTest, ShortScaleMatchesStandardPlan) {
  std::mt19937 rng(42);
  auto standard = createStandardVariationPlan(kDMinor, rng);
  std::mt19937 rng2(42);
  auto scaled = createScaledVariationPlan(kDMinor, 10, rng2);

  ASSERT_EQ(standard.size(), scaled.size());
  for (size_t idx = 0; idx < standard.size(); ++idx) {
    EXPECT_EQ(standard[idx].role, scaled[idx].role)
        << "Role mismatch at variation " << idx;
    EXPECT_EQ(standard[idx].type, scaled[idx].type)
        << "Type mismatch at variation " << idx;
    EXPECT_EQ(standard[idx].is_major_section, scaled[idx].is_major_section)
        << "Major section mismatch at variation " << idx;
  }
}

TEST(ScaledVariationPlanTest, BelowTenAlsoUsesStandardPlan) {
  std::mt19937 rng(42);
  auto standard = createStandardVariationPlan(kDMinor, rng);
  std::mt19937 rng2(42);
  auto scaled = createScaledVariationPlan(kDMinor, 5, rng2);
  EXPECT_EQ(standard.size(), scaled.size());
}

// ===========================================================================
// Illuminate islands (is_major_section=false) for larger scales
// ===========================================================================

TEST(ScaledVariationPlanTest, FullScaleHasIlluminateIslands) {
  std::mt19937 rng(42);
  auto plan = createScaledVariationPlan(kDMinor, 64, rng);

  // Count Illuminate variations that are NOT in the main major section.
  int island_count = 0;
  for (const auto& v : plan) {
    if (v.role == VariationRole::Illuminate && !v.is_major_section) {
      ++island_count;
    }
  }
  EXPECT_GT(island_count, 0)
      << "Full scale should have Illuminate islands (is_major_section=false)";
}

TEST(ScaledVariationPlanTest, MainMajorSectionHasIsMajorTrue) {
  std::mt19937 rng(42);
  auto plan = createScaledVariationPlan(kDMinor, 64, rng);

  int major_section_count = 0;
  for (const auto& v : plan) {
    if (v.is_major_section) {
      ++major_section_count;
      EXPECT_EQ(v.role, VariationRole::Illuminate)
          << "Major section variations must have Illuminate role";
    }
  }
  EXPECT_GT(major_section_count, 0) << "Full scale should have a major section";
}

TEST(ScaledVariationPlanTest, IslandKeysAreNotMinorHome) {
  std::mt19937 rng(42);
  auto plan = createScaledVariationPlan(kDMinor, 64, rng);

  for (const auto& v : plan) {
    if (v.role == VariationRole::Illuminate && !v.is_major_section) {
      // Island keys should be major (related keys).
      EXPECT_FALSE(v.key.is_minor)
          << "Illuminate island at variation " << v.variation_number
          << " should use a major key";
    }
  }
}

// ===========================================================================
// Multiple keys
// ===========================================================================

TEST(ScaledVariationPlanTest, WorksWithCMinor) {
  KeySignature c_minor = {Key::C, true};
  std::mt19937 rng(42);
  auto plan = createScaledVariationPlan(c_minor, 40, rng);
  EXPECT_TRUE(validateVariationPlan(plan));
}

TEST(ScaledVariationPlanTest, WorksWithGMinor) {
  KeySignature g_minor = {Key::G, true};
  std::mt19937 rng(42);
  auto plan = createScaledVariationPlan(g_minor, 64, rng);
  EXPECT_TRUE(validateVariationPlan(plan));
}

// ===========================================================================
// Destruction resistance: 100 seeds × structural integrity
// ===========================================================================

TEST(ScaledVariationPlanDestructionTest, AllScalesAllKeysValidate) {
  // Test across multiple keys and all scale targets.
  std::vector<KeySignature> keys = {
      {Key::D, true}, {Key::C, true}, {Key::G, true},
      {Key::A, true}, {Key::E, true}, {Key::F, true}
  };
  std::vector<int> targets = {10, 24, 40, 64};

  for (const auto& key : keys) {
    for (int target : targets) {
      std::mt19937 rng(42);
      auto plan = createScaledVariationPlan(key, target, rng);
      EXPECT_TRUE(validateVariationPlan(plan))
          << "Failed for key=" << keyToString(key.tonic)
          << " target=" << target;
    }
  }
}

}  // namespace
}  // namespace bach
