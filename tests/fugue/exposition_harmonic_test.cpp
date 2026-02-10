// Tests for exposition harmonic validation (Phase 4a).

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"

namespace bach {
namespace {

TEST(ExpositionHarmonicTest, FugueGenerationSucceeds) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 42;

  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
}

TEST(ExpositionHarmonicTest, MultipleSeedsAndCharacters) {
  SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless};

  for (auto character : characters) {
    for (uint32_t seed = 200; seed < 205; ++seed) {
      FugueConfig config;
      config.key = Key::C;
      config.num_voices = 3;
      config.character = character;
      config.seed = seed;

      FugueResult result = generateFugue(config);
      EXPECT_TRUE(result.success)
          << "Failed for character=" << static_cast<int>(character)
          << " seed=" << seed;
    }
  }
}

}  // namespace
}  // namespace bach
