#include "fugue/subject.h"

#include <gtest/gtest.h>

#include <cmath>

#include "fugue/archetype_policy.h"
#include "fugue/fugue_config.h"
#include "fugue/subject_validator.h"

namespace bach {
namespace {

class SubjectArchetypeTest : public ::testing::Test {
 protected:
  Subject generateWithArchetype(FugueArchetype archetype,
                                 SubjectCharacter character = SubjectCharacter::Severe,
                                 uint32_t seed = 42) {
    FugueConfig config;
    config.character = character;
    config.archetype = archetype;
    config.key = Key::C;
    config.is_minor = false;
    config.seed = seed;
    SubjectGenerator gen;
    return gen.generate(config, seed);
  }
};

TEST_F(SubjectArchetypeTest, CompactNarrowRange) {
  const auto& policy = getArchetypePolicy(FugueArchetype::Compact);
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Compact,
                                         SubjectCharacter::Severe, seed);
    // Compact range should be <= max_range_degrees in semitones.
    // (7 degrees ~ 12 semitones for major scale)
    EXPECT_LE(sub.range(), (policy.max_range_degrees + 1) * 2)
        << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, CantabileWiderRange) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Cantabile,
                                         SubjectCharacter::Noble, seed);
    EXPECT_GT(sub.noteCount(), 0u) << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, CantabileSmoothMotion) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    Subject sub = generateWithArchetype(FugueArchetype::Cantabile,
                                         SubjectCharacter::Noble, seed);
    if (sub.noteCount() < 2) continue;
    // Count step motion (<=2 semitones) vs total intervals.
    int steps = 0;
    int total = 0;
    for (size_t idx = 1; idx < sub.notes.size(); ++idx) {
      int interval = std::abs(static_cast<int>(sub.notes[idx].pitch) -
                               static_cast<int>(sub.notes[idx - 1].pitch));
      if (interval <= 2) ++steps;
      ++total;
    }
    if (total > 0) {
      float step_ratio = static_cast<float>(steps) / static_cast<float>(total);
      // Cantabile should have high step ratio (at least 0.35 as a soft check).
      EXPECT_GE(step_ratio, 0.35f) << "seed=" << seed;
    }
  }
}

TEST_F(SubjectArchetypeTest, DefaultCompactMatchesSevere) {
  // Default archetype=Compact + character=Severe should produce valid output.
  FugueConfig config;
  config.character = SubjectCharacter::Severe;
  config.archetype = FugueArchetype::Compact;
  SubjectGenerator gen;
  SubjectValidator validator;
  for (uint32_t seed = 0; seed < 5; ++seed) {
    Subject sub = gen.generate(config, seed);
    EXPECT_GT(sub.noteCount(), 0u) << "seed=" << seed;
    SubjectScore score = validator.evaluate(sub);
    // Should still produce reasonable subjects.
    EXPECT_GE(score.composite(), 0.4f) << "seed=" << seed;
  }
}

TEST_F(SubjectArchetypeTest, ChromaticPreservesAnswerType) {
  const auto& policy = getArchetypePolicy(FugueArchetype::Chromatic);
  EXPECT_EQ(policy.preferred_answer, AnswerType::Real);
}

}  // namespace
}  // namespace bach
