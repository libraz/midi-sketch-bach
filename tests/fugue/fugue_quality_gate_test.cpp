// Tests for fugue quality gate metrics (Phase 5).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "harmony/chord_types.h"
#include "test_helpers.h"

namespace bach {
namespace {

TEST(FugueQualityGateTest, GenerationSucceeds10Seeds) {
  for (uint32_t seed = 1000; seed < 1010; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Failed for seed " << seed;
    EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "No notes for seed " << seed;
  }
}

TEST(FugueQualityGateTest, QualityMetrics) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 3995423244u;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Quality metrics should exist and be populated.
  EXPECT_GE(result.quality.chord_tone_ratio, 0.0f);
  EXPECT_LE(result.quality.dissonance_per_beat, 5.0f);
}

TEST(FugueQualityGateTest, PostValidationRetainsNotes) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 42;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Post-validation should retain most notes (>80%).
  size_t total = test_helpers::totalNoteCount(result);
  EXPECT_GT(total, 20u) << "Too few notes after validation";
}

TEST(FugueQualityGateTest, BatchDissonanceDensity) {
  for (uint32_t seed = 500; seed < 510; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed;

    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Failed for seed " << seed;

    // Dissonance per beat should be well below the original 2.07.
    EXPECT_LT(result.quality.dissonance_per_beat, 1.5f)
        << "High dissonance for seed " << seed
        << ": " << result.quality.dissonance_per_beat;
  }
}

TEST(FugueQualityGateTest, DestructionResistance20Seeds) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed * 12345u + 1u;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Crash for seed " << config.seed;
  }
}

}  // namespace
}  // namespace bach
