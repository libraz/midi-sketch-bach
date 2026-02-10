// Tests for episode modulation planning -- verifying that ModulationPlan
// integration into fugue episode generation produces correct key selections.

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "fugue/fugue_structure.h"
#include "harmony/modulation_plan.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a FugueConfig with a modulation plan for testing.
/// @param key Home key tonic.
/// @param is_minor Whether the home key is minor.
/// @param seed Random seed.
/// @return FugueConfig with modulation plan attached.
FugueConfig makeModulationTestConfig(Key key, bool is_minor, uint32_t seed = 42) {
  FugueConfig config;
  config.key = key;
  config.is_minor = is_minor;
  config.num_voices = 3;
  config.bpm = 72;
  config.seed = seed;
  config.character = SubjectCharacter::Severe;
  config.max_subject_retries = 10;
  config.develop_pairs = 3;
  config.episode_bars = 2;

  if (is_minor) {
    config.modulation_plan = ModulationPlan::createForMinor(key);
  } else {
    config.modulation_plan = ModulationPlan::createForMajor(key);
  }
  config.has_modulation_plan = true;

  return config;
}

// ---------------------------------------------------------------------------
// ModulationPlanUsedForEpisodeKeys
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, ModulationPlanUsedForEpisodeKeys) {
  // When a modulation plan is set on FugueConfig, the episode sections in the
  // Develop phase should target the keys specified by the plan.
  FugueConfig config = makeModulationTestConfig(Key::C, /*is_minor=*/false);
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // The modulation plan for C major has targets: G, A, F, C.
  // With develop_pairs=3, episodes should target G (idx 0), A (idx 1), F (idx 2).
  auto episodes = result.structure.getSectionsByType(SectionType::Episode);
  // There should be at least develop_pairs episodes (plus the return episode).
  ASSERT_GE(episodes.size(), 3u);

  // First develop episode should target the dominant (G for C major).
  EXPECT_EQ(episodes[0].key, Key::G)
      << "First episode should target dominant key G, got "
      << keyToString(episodes[0].key);

  // Second develop episode should target the relative minor (A for C major).
  EXPECT_EQ(episodes[1].key, Key::A)
      << "Second episode should target relative minor key A, got "
      << keyToString(episodes[1].key);

  // Third develop episode should target the subdominant (F for C major).
  EXPECT_EQ(episodes[2].key, Key::F)
      << "Third episode should target subdominant key F, got "
      << keyToString(episodes[2].key);
}

// ---------------------------------------------------------------------------
// MajorKeyPlanHasCorrectTargets
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, MajorKeyPlanHasCorrectTargets) {
  // Verify that createForMajor produces the standard design values:
  // dominant, relative minor, subdominant, home.
  auto plan = ModulationPlan::createForMajor(Key::D);
  ASSERT_EQ(plan.targets.size(), 4u);

  // Dominant of D = A (D=2, +7=9=A)
  EXPECT_EQ(plan.targets[0].target_key, Key::A);
  EXPECT_FALSE(plan.targets[0].target_is_minor);
  EXPECT_EQ(plan.targets[0].phase, FuguePhase::Develop);

  // Relative minor of D = B (D=2, +9=11=B)
  EXPECT_EQ(plan.targets[1].target_key, Key::B);
  EXPECT_TRUE(plan.targets[1].target_is_minor);
  EXPECT_EQ(plan.targets[1].phase, FuguePhase::Develop);

  // Subdominant of D = G (D=2, +5=7=G)
  EXPECT_EQ(plan.targets[2].target_key, Key::G);
  EXPECT_FALSE(plan.targets[2].target_is_minor);
  EXPECT_EQ(plan.targets[2].phase, FuguePhase::Develop);

  // Home return = D
  EXPECT_EQ(plan.targets[3].target_key, Key::D);
  EXPECT_FALSE(plan.targets[3].target_is_minor);
  EXPECT_EQ(plan.targets[3].phase, FuguePhase::Resolve);
}

// ---------------------------------------------------------------------------
// MinorKeyPlanHasCorrectTargets
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, MinorKeyPlanHasCorrectTargets) {
  // Verify that createForMinor produces the standard design values:
  // relative major, dominant minor, subdominant minor, home.
  auto plan = ModulationPlan::createForMinor(Key::G);
  ASSERT_EQ(plan.targets.size(), 4u);

  // Relative major of G minor = Bb (G=7, +3=10=Bb)
  EXPECT_EQ(plan.targets[0].target_key, Key::Bb);
  EXPECT_FALSE(plan.targets[0].target_is_minor);
  EXPECT_EQ(plan.targets[0].phase, FuguePhase::Develop);

  // Dominant of G minor = D minor (G=7, +7=14%12=2=D)
  EXPECT_EQ(plan.targets[1].target_key, Key::D);
  EXPECT_TRUE(plan.targets[1].target_is_minor);
  EXPECT_EQ(plan.targets[1].phase, FuguePhase::Develop);

  // Subdominant of G minor = C minor (G=7, +5=12%12=0=C)
  EXPECT_EQ(plan.targets[2].target_key, Key::C);
  EXPECT_TRUE(plan.targets[2].target_is_minor);
  EXPECT_EQ(plan.targets[2].phase, FuguePhase::Develop);

  // Home return = G minor
  EXPECT_EQ(plan.targets[3].target_key, Key::G);
  EXPECT_TRUE(plan.targets[3].target_is_minor);
  EXPECT_EQ(plan.targets[3].phase, FuguePhase::Resolve);
}

// ---------------------------------------------------------------------------
// FallbackToHomeKey
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, FallbackToHomeKey) {
  // When episode_index exceeds the plan's target count, getTargetKey should
  // fall back to the home key.
  auto plan = ModulationPlan::createForMajor(Key::Eb);
  ASSERT_EQ(plan.targets.size(), 4u);

  // Index 4 is out of range; should return the provided home key.
  EXPECT_EQ(plan.getTargetKey(4, Key::Eb), Key::Eb);
  EXPECT_EQ(plan.getTargetKey(10, Key::Eb), Key::Eb);
  EXPECT_EQ(plan.getTargetKey(100, Key::Eb), Key::Eb);

  // Negative index should also fall back.
  EXPECT_EQ(plan.getTargetKey(-1, Key::Eb), Key::Eb);
}

TEST(EpisodeModulationTest, FallbackUsesProvidedHomeKey) {
  // The fallback returns the home_key argument, not necessarily the plan's home.
  auto plan = ModulationPlan::createForMajor(Key::C);

  // Pass a different home_key as fallback; it should be returned for out-of-range.
  EXPECT_EQ(plan.getTargetKey(99, Key::F), Key::F);
}

// ---------------------------------------------------------------------------
// FugueWithModulationPlanSucceeds
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, FugueWithModulationPlanSucceeds) {
  // Full end-to-end: a fugue with an explicitly-set modulation plan should
  // generate successfully and maintain valid structure.
  FugueConfig config = makeModulationTestConfig(Key::G, /*is_minor=*/true, 77);
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());

  // Structure should validate without violations.
  auto violations = result.structure.validate();
  EXPECT_TRUE(violations.empty())
      << "Structure validation failed with " << violations.size() << " violation(s)";

  // Must have all three phases.
  auto establish = result.structure.getSectionsByPhase(FuguePhase::Establish);
  auto develop = result.structure.getSectionsByPhase(FuguePhase::Develop);
  auto resolve = result.structure.getSectionsByPhase(FuguePhase::Resolve);
  EXPECT_GE(establish.size(), 1u);
  EXPECT_GE(develop.size(), 1u);
  EXPECT_GE(resolve.size(), 1u);

  // All tracks should have notes.
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " has no notes";
  }
}

TEST(EpisodeModulationTest, FugueWithModulationPlan_MajorKey) {
  // Major key fugue with modulation plan should also succeed.
  FugueConfig config = makeModulationTestConfig(Key::F, /*is_minor=*/false, 123);
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto violations = result.structure.validate();
  EXPECT_TRUE(violations.empty());
}

// ---------------------------------------------------------------------------
// Auto-created plan when has_modulation_plan is false
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, AutoCreatedPlanWhenNotExplicit) {
  // When has_modulation_plan is false, the generator should auto-create a plan.
  // The fugue should still succeed and have proper structure.
  FugueConfig config;
  config.key = Key::D;
  config.is_minor = false;
  config.num_voices = 3;
  config.bpm = 72;
  config.seed = 55;
  config.character = SubjectCharacter::Severe;
  config.max_subject_retries = 10;
  config.develop_pairs = 2;
  config.episode_bars = 2;
  config.has_modulation_plan = false;  // No explicit plan.

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto episodes = result.structure.getSectionsByType(SectionType::Episode);
  ASSERT_GE(episodes.size(), 2u);

  // Auto-created plan for D major: first target = A (dominant).
  EXPECT_EQ(episodes[0].key, Key::A)
      << "Auto-created plan: first episode should target dominant A, got "
      << keyToString(episodes[0].key);
}

TEST(EpisodeModulationTest, AutoCreatedPlanMinorKey) {
  // Auto-created plan for a minor key.
  FugueConfig config;
  config.key = Key::A;
  config.is_minor = true;
  config.num_voices = 3;
  config.bpm = 72;
  config.seed = 66;
  config.character = SubjectCharacter::Severe;
  config.max_subject_retries = 10;
  config.develop_pairs = 2;
  config.episode_bars = 2;
  config.has_modulation_plan = false;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto episodes = result.structure.getSectionsByType(SectionType::Episode);
  ASSERT_GE(episodes.size(), 2u);

  // Auto-created plan for A minor: first target = C (relative major).
  EXPECT_EQ(episodes[0].key, Key::C)
      << "Auto-created plan: first episode should target relative major C, got "
      << keyToString(episodes[0].key);
}

// ---------------------------------------------------------------------------
// Determinism: same config produces same episode keys
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, DeterministicEpisodeKeys) {
  FugueConfig config = makeModulationTestConfig(Key::Bb, /*is_minor=*/false, 999);
  FugueResult result1 = generateFugue(config);
  FugueResult result2 = generateFugue(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  auto episodes1 = result1.structure.getSectionsByType(SectionType::Episode);
  auto episodes2 = result2.structure.getSectionsByType(SectionType::Episode);
  ASSERT_EQ(episodes1.size(), episodes2.size());

  for (size_t idx = 0; idx < episodes1.size(); ++idx) {
    EXPECT_EQ(episodes1[idx].key, episodes2[idx].key)
        << "Episode " << idx << " key differs between runs";
  }
}

// ---------------------------------------------------------------------------
// All 12 major keys produce valid fugues with modulation plans
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, AllMajorKeysSucceed) {
  for (int key_val = 0; key_val < 12; ++key_val) {
    Key home = static_cast<Key>(key_val);
    FugueConfig config = makeModulationTestConfig(home, /*is_minor=*/false,
                                                  static_cast<uint32_t>(key_val + 100));
    config.develop_pairs = 2;  // Keep it short for speed.
    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success)
        << "Fugue generation failed for major key " << keyToString(home);
  }
}

// ---------------------------------------------------------------------------
// All 12 minor keys produce valid fugues with modulation plans
// ---------------------------------------------------------------------------

TEST(EpisodeModulationTest, AllMinorKeysSucceed) {
  for (int key_val = 0; key_val < 12; ++key_val) {
    Key home = static_cast<Key>(key_val);
    FugueConfig config = makeModulationTestConfig(home, /*is_minor=*/true,
                                                  static_cast<uint32_t>(key_val + 200));
    config.develop_pairs = 2;
    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success)
        << "Fugue generation failed for minor key " << keyToString(home);
  }
}

}  // namespace
}  // namespace bach
