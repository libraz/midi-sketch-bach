// Tests for solo_string/flow/arpeggio_pattern.h -- pattern types, roles, and generation.

#include "solo_string/flow/arpeggio_pattern.h"

#include <algorithm>
#include <random>

#include <gtest/gtest.h>

namespace bach {
namespace {

/// @brief Helper: call generatePattern with default RNG and no persistence.
///
/// Uses a fixed seed for deterministic tests. is_section_start=true bypasses
/// the 70% persistence branch so each call does a fresh weighted selection.
ArpeggioPattern generatePatternForTest(const std::vector<int>& chord_degrees,
                                       ArcPhase phase, PatternRole role,
                                       bool use_open_strings,
                                       uint32_t seed = 42) {
  std::mt19937 rng(seed);
  return generatePattern(chord_degrees, phase, role, use_open_strings,
                         rng, ArpeggioPatternType::Rising,
                         /*is_section_start=*/true);
}

// ---------------------------------------------------------------------------
// ArpeggioPattern defaults
// ---------------------------------------------------------------------------

TEST(ArpeggioPatternTest, DefaultValues) {
  ArpeggioPattern pattern;
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Rising);
  EXPECT_EQ(pattern.role, PatternRole::Drive);
  EXPECT_EQ(pattern.notes_per_beat, 4);
  EXPECT_TRUE(pattern.degrees.empty());
  EXPECT_FALSE(pattern.use_open_string);
}

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

TEST(ArpeggioPatternTypeToStringTest, AllTypes) {
  EXPECT_STREQ(arpeggioPatternTypeToString(ArpeggioPatternType::Rising), "Rising");
  EXPECT_STREQ(arpeggioPatternTypeToString(ArpeggioPatternType::Falling), "Falling");
  EXPECT_STREQ(arpeggioPatternTypeToString(ArpeggioPatternType::Oscillating), "Oscillating");
  EXPECT_STREQ(arpeggioPatternTypeToString(ArpeggioPatternType::PedalPoint), "PedalPoint");
  EXPECT_STREQ(arpeggioPatternTypeToString(ArpeggioPatternType::ScaleFragment), "ScaleFragment");
}

TEST(PatternRoleToStringTest, AllRoles) {
  EXPECT_STREQ(patternRoleToString(PatternRole::Drive), "Drive");
  EXPECT_STREQ(patternRoleToString(PatternRole::Expand), "Expand");
  EXPECT_STREQ(patternRoleToString(PatternRole::Sustain), "Sustain");
  EXPECT_STREQ(patternRoleToString(PatternRole::Release), "Release");
}

// ---------------------------------------------------------------------------
// isPatternRoleOrderValid
// ---------------------------------------------------------------------------

TEST(PatternRoleOrderTest, EmptyIsValid) {
  std::vector<PatternRole> roles;
  EXPECT_TRUE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, SingleRoleIsValid) {
  EXPECT_TRUE(isPatternRoleOrderValid({PatternRole::Drive}));
  EXPECT_TRUE(isPatternRoleOrderValid({PatternRole::Release}));
}

TEST(PatternRoleOrderTest, FullOrderIsValid) {
  std::vector<PatternRole> roles = {
    PatternRole::Drive,
    PatternRole::Expand,
    PatternRole::Sustain,
    PatternRole::Release
  };
  EXPECT_TRUE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, RepeatedRolesValid) {
  std::vector<PatternRole> roles = {
    PatternRole::Drive,
    PatternRole::Drive,
    PatternRole::Expand,
    PatternRole::Sustain,
    PatternRole::Sustain
  };
  EXPECT_TRUE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, SkippedRolesValid) {
  // Drive -> Release (skipping Expand and Sustain) is valid.
  std::vector<PatternRole> roles = {
    PatternRole::Drive,
    PatternRole::Release
  };
  EXPECT_TRUE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, ReversedOrderInvalid) {
  std::vector<PatternRole> roles = {
    PatternRole::Release,
    PatternRole::Drive
  };
  EXPECT_FALSE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, ExpandBeforeDriveInvalid) {
  std::vector<PatternRole> roles = {
    PatternRole::Expand,
    PatternRole::Drive
  };
  EXPECT_FALSE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, SustainBeforeExpandInvalid) {
  std::vector<PatternRole> roles = {
    PatternRole::Drive,
    PatternRole::Sustain,
    PatternRole::Expand
  };
  EXPECT_FALSE(isPatternRoleOrderValid(roles));
}

TEST(PatternRoleOrderTest, ReleaseBeforeSustainInvalid) {
  std::vector<PatternRole> roles = {
    PatternRole::Release,
    PatternRole::Sustain
  };
  EXPECT_FALSE(isPatternRoleOrderValid(roles));
}

// ---------------------------------------------------------------------------
// getAllowedPatternsForPhase
// ---------------------------------------------------------------------------

TEST(GetAllowedPatternsForPhaseTest, AscentPatternsNoFallingNoPedal) {
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Ascent);
  EXPECT_FALSE(allowed.empty());

  // Rising, Oscillating, ScaleFragment should be present.
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Rising),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Oscillating),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::ScaleFragment),
            allowed.end());

  // Falling, PedalPoint should NOT be present.
  EXPECT_EQ(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Falling),
            allowed.end());
  EXPECT_EQ(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::PedalPoint),
            allowed.end());
}

TEST(GetAllowedPatternsForPhaseTest, PeakAllTypesAllowed) {
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Peak);
  EXPECT_EQ(allowed.size(), 5u);  // All 5 types

  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Rising),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Falling),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Oscillating),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::PedalPoint),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::ScaleFragment),
            allowed.end());
}

TEST(GetAllowedPatternsForPhaseTest, DescentNoRisingNoScaleFragment) {
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Descent);
  EXPECT_FALSE(allowed.empty());

  // Falling, Oscillating, PedalPoint should be present.
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Falling),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Oscillating),
            allowed.end());
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::PedalPoint),
            allowed.end());

  // Rising, ScaleFragment should NOT be present.
  EXPECT_EQ(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::Rising),
            allowed.end());
  EXPECT_EQ(std::find(allowed.begin(), allowed.end(), ArpeggioPatternType::ScaleFragment),
            allowed.end());
}

// ---------------------------------------------------------------------------
// generatePattern -- basic behavior
// ---------------------------------------------------------------------------

TEST(GeneratePatternTest, DriveAscentProducesAllowedType) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Ascent, PatternRole::Drive, false);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Ascent);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
  EXPECT_EQ(pattern.role, PatternRole::Drive);
  EXPECT_FALSE(pattern.use_open_string);
  EXPECT_EQ(pattern.notes_per_beat, 4);
  ASSERT_FALSE(pattern.degrees.empty());
}

TEST(GeneratePatternTest, ReleaseDescentProducesAllowedType) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Descent, PatternRole::Release, false);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Descent);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
  EXPECT_EQ(pattern.role, PatternRole::Release);
  ASSERT_FALSE(pattern.degrees.empty());
}

TEST(GeneratePatternTest, ExpandPeakProducesAllowedType) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Peak, PatternRole::Expand, false);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Peak);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
  EXPECT_EQ(pattern.role, PatternRole::Expand);
  EXPECT_FALSE(pattern.degrees.empty());
}

TEST(GeneratePatternTest, SustainPeakProducesAllowedType) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Peak, PatternRole::Sustain, false);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Peak);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
  EXPECT_EQ(pattern.role, PatternRole::Sustain);
  ASSERT_FALSE(pattern.degrees.empty());
}

TEST(GeneratePatternTest, OpenStringFlagPropagated) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Ascent, PatternRole::Drive, true);
  EXPECT_TRUE(pattern.use_open_string);
}

TEST(GeneratePatternTest, EmptyChordDegreesUsesDefault) {
  auto pattern = generatePatternForTest(
      {}, ArcPhase::Ascent, PatternRole::Drive, false);
  // Should use default triad {0, 2, 4}.
  EXPECT_FALSE(pattern.degrees.empty());
}

// ---------------------------------------------------------------------------
// generatePattern -- phase constraint enforcement
// ---------------------------------------------------------------------------

TEST(GeneratePatternTest, DescentDriveSelectsAllowedType) {
  // In Descent phase, Drive should not use Rising (forbidden).
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Descent, PatternRole::Drive, false);
  EXPECT_NE(pattern.type, ArpeggioPatternType::Rising);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Descent);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
}

TEST(GeneratePatternTest, AscentReleaseSelectsAllowedType) {
  // In Ascent phase, Release role should select an allowed type (no Falling).
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Ascent, PatternRole::Release, false);
  EXPECT_NE(pattern.type, ArpeggioPatternType::Falling);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Ascent);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
}

TEST(GeneratePatternTest, DescentSustainSelectsAllowedType) {
  auto pattern = generatePatternForTest(
      {0, 4, 7}, ArcPhase::Descent, PatternRole::Sustain, false);
  auto allowed = getAllowedPatternsForPhase(ArcPhase::Descent);
  EXPECT_NE(std::find(allowed.begin(), allowed.end(), pattern.type), allowed.end());
}

TEST(GeneratePatternTest, ResultTypeAlwaysAllowedForPhase) {
  // Exhaustively check that the generated pattern type is in the allowed set
  // across multiple seeds.
  ArcPhase phases[] = {ArcPhase::Ascent, ArcPhase::Peak, ArcPhase::Descent};
  PatternRole roles[] = {PatternRole::Drive, PatternRole::Expand,
                         PatternRole::Sustain, PatternRole::Release};

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    for (auto phase : phases) {
      auto allowed = getAllowedPatternsForPhase(phase);
      for (auto role : roles) {
        auto pattern = generatePatternForTest({0, 4, 7}, phase, role, false, seed);
        bool found = std::find(allowed.begin(), allowed.end(), pattern.type) != allowed.end();
        EXPECT_TRUE(found) << "Pattern type " << arpeggioPatternTypeToString(pattern.type)
                           << " not allowed for phase " << arcPhaseToString(phase)
                           << " (role=" << patternRoleToString(role)
                           << ", seed=" << seed << ")";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// generatePattern -- degree arrangement verification
// ---------------------------------------------------------------------------

TEST(GeneratePatternTest, RisingDegreesAreSorted) {
  // Try multiple seeds until we get a Rising pattern to test arrangement.
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    auto pattern = generatePatternForTest(
        {7, 0, 4, 2}, ArcPhase::Ascent, PatternRole::Drive, false, seed);
    if (pattern.type == ArpeggioPatternType::Rising) {
      ASSERT_FALSE(pattern.degrees.empty());
      for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
        EXPECT_LE(pattern.degrees[idx - 1], pattern.degrees[idx])
            << "Rising pattern degrees not sorted at index " << idx;
      }
      return;
    }
  }
  FAIL() << "Could not produce a Rising pattern after 100 seeds";
}

TEST(GeneratePatternTest, FallingDegreesAreSortedDescending) {
  // Try multiple seeds until we get a Falling pattern to test arrangement.
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    auto pattern = generatePatternForTest(
        {0, 4, 7}, ArcPhase::Descent, PatternRole::Drive, false, seed);
    if (pattern.type == ArpeggioPatternType::Falling) {
      ASSERT_FALSE(pattern.degrees.empty());
      for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
        EXPECT_GE(pattern.degrees[idx - 1], pattern.degrees[idx])
            << "Falling pattern degrees not descending at index " << idx;
      }
      return;
    }
  }
  FAIL() << "Could not produce a Falling pattern after 100 seeds";
}

TEST(GeneratePatternTest, PedalPointHasLowestRepeated) {
  // Try multiple seeds until we get a PedalPoint pattern.
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    auto pattern = generatePatternForTest(
        {0, 4, 7}, ArcPhase::Peak, PatternRole::Sustain, false, seed);
    if (pattern.type == ArpeggioPatternType::PedalPoint) {
      // In pedal point, the lowest degree (0) should appear at even indices.
      ASSERT_GE(pattern.degrees.size(), 4u);
      EXPECT_EQ(pattern.degrees[0], 0);
      EXPECT_EQ(pattern.degrees[2], 0);
      return;
    }
  }
  FAIL() << "Could not produce a PedalPoint pattern after 100 seeds";
}

TEST(GeneratePatternTest, ScaleFragmentHasConsecutiveDegrees) {
  // Try multiple seeds until we get a ScaleFragment pattern.
  for (uint32_t seed = 1; seed <= 100; ++seed) {
    auto pattern = generatePatternForTest(
        {0, 4, 7}, ArcPhase::Ascent, PatternRole::Release, false, seed);
    if (pattern.type == ArpeggioPatternType::ScaleFragment) {
      // ScaleFragment fills stepwise between min and max of input.
      ASSERT_FALSE(pattern.degrees.empty());
      for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
        EXPECT_EQ(pattern.degrees[idx] - pattern.degrees[idx - 1], 1)
            << "ScaleFragment degrees not consecutive at index " << idx;
      }
      return;
    }
  }
  FAIL() << "Could not produce a ScaleFragment pattern after 100 seeds";
}

TEST(GeneratePatternTest, SingleDegreeHandled) {
  auto pattern = generatePatternForTest(
      {5}, ArcPhase::Ascent, PatternRole::Drive, false);
  ASSERT_FALSE(pattern.degrees.empty());
  EXPECT_EQ(pattern.degrees[0], 5);
}

}  // namespace
}  // namespace bach
