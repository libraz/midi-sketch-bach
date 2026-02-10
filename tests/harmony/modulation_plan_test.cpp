// Tests for harmony/modulation_plan.h -- modulation plan creation, target key
// lookup, and related key computation.

#include "harmony/modulation_plan.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// createForMajor
// ---------------------------------------------------------------------------

TEST(ModulationPlanMajorTest, CMajor_HasFourTargets) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  EXPECT_EQ(plan.targets.size(), 4u);
}

TEST(ModulationPlanMajorTest, CMajor_FirstTargetIsDominant) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  // Dominant of C major = G (7 semitones above C)
  EXPECT_EQ(plan.targets[0].target_key, Key::G);
  EXPECT_FALSE(plan.targets[0].target_is_minor);
  EXPECT_EQ(plan.targets[0].phase, FuguePhase::Develop);
  EXPECT_EQ(plan.targets[0].entry_cadence, CadenceType::Half);
}

TEST(ModulationPlanMajorTest, CMajor_SecondTargetIsRelativeMinor) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  // Relative minor of C = A minor (9 semitones above C)
  EXPECT_EQ(plan.targets[1].target_key, Key::A);
  EXPECT_TRUE(plan.targets[1].target_is_minor);
  EXPECT_EQ(plan.targets[1].phase, FuguePhase::Develop);
}

TEST(ModulationPlanMajorTest, CMajor_ThirdTargetIsSubdominant) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  // Subdominant of C = F (5 semitones above C)
  EXPECT_EQ(plan.targets[2].target_key, Key::F);
  EXPECT_FALSE(plan.targets[2].target_is_minor);
}

TEST(ModulationPlanMajorTest, CMajor_FourthTargetIsHome) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  EXPECT_EQ(plan.targets[3].target_key, Key::C);
  EXPECT_FALSE(plan.targets[3].target_is_minor);
  EXPECT_EQ(plan.targets[3].phase, FuguePhase::Resolve);
}

TEST(ModulationPlanMajorTest, GMajor_FirstTargetIsD) {
  auto plan = ModulationPlan::createForMajor(Key::G);
  // Dominant of G = D
  EXPECT_EQ(plan.targets[0].target_key, Key::D);
}

// ---------------------------------------------------------------------------
// createForMinor
// ---------------------------------------------------------------------------

TEST(ModulationPlanMinorTest, AMinor_HasFourTargets) {
  auto plan = ModulationPlan::createForMinor(Key::A);
  EXPECT_EQ(plan.targets.size(), 4u);
}

TEST(ModulationPlanMinorTest, AMinor_FirstTargetIsRelativeMajor) {
  auto plan = ModulationPlan::createForMinor(Key::A);
  // Relative major of A minor = C major (3 semitones above A)
  EXPECT_EQ(plan.targets[0].target_key, Key::C);
  EXPECT_FALSE(plan.targets[0].target_is_minor);
  EXPECT_EQ(plan.targets[0].phase, FuguePhase::Develop);
  EXPECT_EQ(plan.targets[0].entry_cadence, CadenceType::Half);
}

TEST(ModulationPlanMinorTest, AMinor_SecondTargetIsDominantMinor) {
  auto plan = ModulationPlan::createForMinor(Key::A);
  // Dominant of A = E minor
  EXPECT_EQ(plan.targets[1].target_key, Key::E);
  EXPECT_TRUE(plan.targets[1].target_is_minor);
}

TEST(ModulationPlanMinorTest, AMinor_ThirdTargetIsSubdominantMinor) {
  auto plan = ModulationPlan::createForMinor(Key::A);
  // Subdominant of A = D minor
  EXPECT_EQ(plan.targets[2].target_key, Key::D);
  EXPECT_TRUE(plan.targets[2].target_is_minor);
}

TEST(ModulationPlanMinorTest, AMinor_FourthTargetIsHome) {
  auto plan = ModulationPlan::createForMinor(Key::A);
  EXPECT_EQ(plan.targets[3].target_key, Key::A);
  EXPECT_TRUE(plan.targets[3].target_is_minor);
  EXPECT_EQ(plan.targets[3].phase, FuguePhase::Resolve);
}

TEST(ModulationPlanMinorTest, DMinor_FirstTargetIsFMajor) {
  auto plan = ModulationPlan::createForMinor(Key::D);
  // Relative major of D minor = F major (D=2, +3=5=F)
  EXPECT_EQ(plan.targets[0].target_key, Key::F);
}

// ---------------------------------------------------------------------------
// getTargetKey
// ---------------------------------------------------------------------------

TEST(GetTargetKeyTest, ValidIndex_ReturnsTarget) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  EXPECT_EQ(plan.getTargetKey(0, Key::C), Key::G);
  EXPECT_EQ(plan.getTargetKey(1, Key::C), Key::A);
  EXPECT_EQ(plan.getTargetKey(2, Key::C), Key::F);
  EXPECT_EQ(plan.getTargetKey(3, Key::C), Key::C);
}

TEST(GetTargetKeyTest, NegativeIndex_ReturnsHomeKey) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  EXPECT_EQ(plan.getTargetKey(-1, Key::G), Key::G);
}

TEST(GetTargetKeyTest, OutOfRangeIndex_ReturnsHomeKey) {
  auto plan = ModulationPlan::createForMajor(Key::C);
  EXPECT_EQ(plan.getTargetKey(10, Key::D), Key::D);
}

// ---------------------------------------------------------------------------
// getRelatedKeys
// ---------------------------------------------------------------------------

TEST(GetRelatedKeysTest, CMajor_ReturnsFourKeys) {
  auto keys = getRelatedKeys(Key::C, false);
  EXPECT_EQ(keys.size(), 4u);
}

TEST(GetRelatedKeysTest, CMajor_ContainsDominant) {
  auto keys = getRelatedKeys(Key::C, false);
  // Dominant of C major = G major
  bool found = false;
  for (const auto& key_sig : keys) {
    if (key_sig.tonic == Key::G && !key_sig.is_minor) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected G major (dominant) in related keys";
}

TEST(GetRelatedKeysTest, CMajor_ContainsSubdominant) {
  auto keys = getRelatedKeys(Key::C, false);
  // Subdominant of C major = F major
  bool found = false;
  for (const auto& key_sig : keys) {
    if (key_sig.tonic == Key::F && !key_sig.is_minor) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected F major (subdominant) in related keys";
}

TEST(GetRelatedKeysTest, CMajor_ContainsRelativeMinor) {
  auto keys = getRelatedKeys(Key::C, false);
  // Relative minor of C major = A minor
  bool found = false;
  for (const auto& key_sig : keys) {
    if (key_sig.tonic == Key::A && key_sig.is_minor) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected A minor (relative minor) in related keys";
}

TEST(GetRelatedKeysTest, CMajor_ContainsParallelMinor) {
  auto keys = getRelatedKeys(Key::C, false);
  // Parallel minor of C major = C minor
  bool found = false;
  for (const auto& key_sig : keys) {
    if (key_sig.tonic == Key::C && key_sig.is_minor) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected C minor (parallel) in related keys";
}

TEST(GetRelatedKeysTest, AMinor_ReturnsFourKeys) {
  auto keys = getRelatedKeys(Key::A, true);
  EXPECT_EQ(keys.size(), 4u);
}

// ---------------------------------------------------------------------------
// All targets have valid keys
// ---------------------------------------------------------------------------

TEST(ModulationPlanTest, AllTargetsHaveValidKeys) {
  // Test all 12 major keys.
  for (int key_val = 0; key_val < 12; ++key_val) {
    Key home = static_cast<Key>(key_val);
    auto plan = ModulationPlan::createForMajor(home);
    for (const auto& target : plan.targets) {
      EXPECT_GE(static_cast<int>(target.target_key), 0);
      EXPECT_LT(static_cast<int>(target.target_key), 12);
    }
  }
  // Test all 12 minor keys.
  for (int key_val = 0; key_val < 12; ++key_val) {
    Key home = static_cast<Key>(key_val);
    auto plan = ModulationPlan::createForMinor(home);
    for (const auto& target : plan.targets) {
      EXPECT_GE(static_cast<int>(target.target_key), 0);
      EXPECT_LT(static_cast<int>(target.target_key), 12);
    }
  }
}

}  // namespace
}  // namespace bach
