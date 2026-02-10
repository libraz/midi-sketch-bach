// Tests for stretto harmonic validation (Phase 4b).

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"

namespace bach {
namespace {

TEST(StrettoHarmonicTest, FugueGenerationSucceeds) {
  FugueConfig config;
  config.key = Key::G;
  config.num_voices = 4;
  config.character = SubjectCharacter::Noble;
  config.seed = 12345;

  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
}

TEST(StrettoHarmonicTest, MultipleVoiceCounts) {
  for (uint8_t voices = 2; voices <= 5; ++voices) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = voices;
    config.character = SubjectCharacter::Severe;
    config.seed = 55555;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Failed for " << static_cast<int>(voices) << " voices";
  }
}

}  // namespace
}  // namespace bach
