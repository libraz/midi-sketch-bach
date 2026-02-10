// Tests for solo_string/flow/arpeggio_pattern.h -- pattern types, roles, and generation.

#include "solo_string/flow/arpeggio_pattern.h"

#include <algorithm>

#include <gtest/gtest.h>

namespace bach {
namespace {

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

TEST(GeneratePatternTest, RisingDriveAscent) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Ascent, PatternRole::Drive, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Rising);
  EXPECT_EQ(pattern.role, PatternRole::Drive);
  EXPECT_FALSE(pattern.use_open_string);
  EXPECT_EQ(pattern.notes_per_beat, 4);

  // Degrees should be sorted ascending.
  ASSERT_FALSE(pattern.degrees.empty());
  for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
    EXPECT_LE(pattern.degrees[idx - 1], pattern.degrees[idx]);
  }
}

TEST(GeneratePatternTest, FallingReleaseDescent) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Descent, PatternRole::Release, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Falling);
  EXPECT_EQ(pattern.role, PatternRole::Release);

  // Degrees should be sorted descending.
  ASSERT_FALSE(pattern.degrees.empty());
  for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
    EXPECT_GE(pattern.degrees[idx - 1], pattern.degrees[idx]);
  }
}

TEST(GeneratePatternTest, OscillatingExpandAny) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Peak, PatternRole::Expand, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Oscillating);
  EXPECT_EQ(pattern.role, PatternRole::Expand);
  EXPECT_FALSE(pattern.degrees.empty());
}

TEST(GeneratePatternTest, PedalPointSustainAny) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Peak, PatternRole::Sustain, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::PedalPoint);
  EXPECT_EQ(pattern.role, PatternRole::Sustain);

  // PedalPoint: lowest degree interleaved. First element should be the lowest.
  ASSERT_GE(pattern.degrees.size(), 2u);
  EXPECT_EQ(pattern.degrees[0], 0);  // Pedal (lowest)
}

TEST(GeneratePatternTest, OpenStringFlagPropagated) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Ascent, PatternRole::Drive, true);
  EXPECT_TRUE(pattern.use_open_string);
}

TEST(GeneratePatternTest, EmptyChordDegreesUsesDefault) {
  auto pattern = generatePattern({}, ArcPhase::Ascent, PatternRole::Drive, false);
  // Should use default triad {0, 2, 4}.
  EXPECT_FALSE(pattern.degrees.empty());
}

// ---------------------------------------------------------------------------
// generatePattern -- phase constraint enforcement
// ---------------------------------------------------------------------------

TEST(GeneratePatternTest, DescentDriveSelectsFalling) {
  // In Descent phase, Drive should not use Rising (forbidden). Falling is used instead.
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Descent, PatternRole::Drive, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Falling);
}

TEST(GeneratePatternTest, AscentReleaseSelectsScaleFragment) {
  // In Ascent phase, Release role selects ScaleFragment.
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Ascent, PatternRole::Release, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::ScaleFragment);
}

TEST(GeneratePatternTest, DescentSustainSelectsPedalPoint) {
  // PedalPoint is allowed in Descent.
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Descent, PatternRole::Sustain, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::PedalPoint);
}

TEST(GeneratePatternTest, ResultTypeAlwaysAllowedForPhase) {
  // Exhaustively check that the generated pattern type is in the allowed set.
  ArcPhase phases[] = {ArcPhase::Ascent, ArcPhase::Peak, ArcPhase::Descent};
  PatternRole roles[] = {PatternRole::Drive, PatternRole::Expand,
                         PatternRole::Sustain, PatternRole::Release};

  for (auto phase : phases) {
    auto allowed = getAllowedPatternsForPhase(phase);
    for (auto role : roles) {
      auto pattern = generatePattern({0, 4, 7}, phase, role, false);
      bool found = std::find(allowed.begin(), allowed.end(), pattern.type) != allowed.end();
      EXPECT_TRUE(found) << "Pattern type " << arpeggioPatternTypeToString(pattern.type)
                         << " not allowed for phase " << arcPhaseToString(phase)
                         << " (role=" << patternRoleToString(role) << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// generatePattern -- degree arrangement verification
// ---------------------------------------------------------------------------

TEST(GeneratePatternTest, RisingDegreesAreSorted) {
  auto pattern = generatePattern({7, 0, 4, 2}, ArcPhase::Ascent, PatternRole::Drive, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Rising);
  ASSERT_FALSE(pattern.degrees.empty());
  for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
    EXPECT_LE(pattern.degrees[idx - 1], pattern.degrees[idx])
        << "Rising pattern degrees not sorted at index " << idx;
  }
}

TEST(GeneratePatternTest, FallingDegreesAreSortedDescending) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Descent, PatternRole::Drive, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::Falling);
  ASSERT_FALSE(pattern.degrees.empty());
  for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
    EXPECT_GE(pattern.degrees[idx - 1], pattern.degrees[idx])
        << "Falling pattern degrees not descending at index " << idx;
  }
}

TEST(GeneratePatternTest, PedalPointHasLowestRepeated) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Peak, PatternRole::Sustain, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::PedalPoint);

  // In pedal point, the lowest degree (0) should appear at even indices.
  ASSERT_GE(pattern.degrees.size(), 4u);
  EXPECT_EQ(pattern.degrees[0], 0);
  EXPECT_EQ(pattern.degrees[2], 0);
}

TEST(GeneratePatternTest, ScaleFragmentHasConsecutiveDegrees) {
  auto pattern = generatePattern({0, 4, 7}, ArcPhase::Ascent, PatternRole::Release, false);
  EXPECT_EQ(pattern.type, ArpeggioPatternType::ScaleFragment);

  // ScaleFragment fills stepwise between min and max of input.
  ASSERT_FALSE(pattern.degrees.empty());
  for (size_t idx = 1; idx < pattern.degrees.size(); ++idx) {
    EXPECT_EQ(pattern.degrees[idx] - pattern.degrees[idx - 1], 1)
        << "ScaleFragment degrees not consecutive at index " << idx;
  }
}

TEST(GeneratePatternTest, SingleDegreeHandled) {
  auto pattern = generatePattern({5}, ArcPhase::Ascent, PatternRole::Drive, false);
  ASSERT_FALSE(pattern.degrees.empty());
  EXPECT_EQ(pattern.degrees[0], 5);
}

}  // namespace
}  // namespace bach
