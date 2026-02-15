// Tests for vocabulary slot pattern integration in arpeggio generation.

#include <gtest/gtest.h>
#include <random>

#include "core/basic_types.h"
#include "solo_string/flow/arpeggio_pattern.h"
#include "solo_string/solo_vocabulary.h"

namespace bach {
namespace {

TEST(ArpeggioVocabularyTest, GeneratePatternDoesNotCrash) {
  std::mt19937 rng(42);
  auto pattern = generatePattern({0, 2, 4}, ArcPhase::Ascent,
                                  PatternRole::Drive, false, rng,
                                  ArpeggioPatternType::Rising, false);
  EXPECT_FALSE(pattern.degrees.empty());
}

TEST(ArpeggioVocabularyTest, SlotPatternVariety) {
  // Run 50 seeds; at least some should use vocabulary slots (40% prob).
  int vocab_count = 0;
  for (uint32_t seed = 1; seed <= 50; ++seed) {
    std::mt19937 rng(seed);
    auto pattern = generatePattern({0, 2, 4}, ArcPhase::Ascent,
                                    PatternRole::Drive, false, rng,
                                    ArpeggioPatternType::Rising, true);
    // Vocabulary patterns may have different degree ordering.
    if (!pattern.degrees.empty()) {
      // Check if it differs from simple rising (0, 2, 4).
      if (pattern.degrees != std::vector<int>{0, 2, 4}) {
        vocab_count++;
      }
    }
  }
  // At least some patterns should differ from default.
  EXPECT_GT(vocab_count, 0);
}

TEST(ArpeggioVocabularyTest, EmptyDegreesUseDefault) {
  std::mt19937 rng(7);
  auto pattern = generatePattern({}, ArcPhase::Peak,
                                  PatternRole::Drive, false, rng,
                                  ArpeggioPatternType::Falling, false);
  EXPECT_FALSE(pattern.degrees.empty());
}

TEST(ArpeggioVocabularyTest, FourVoicePattern) {
  std::mt19937 rng(99);
  auto pattern = generatePattern({0, 2, 4, 7}, ArcPhase::Descent,
                                  PatternRole::Release, false, rng,
                                  ArpeggioPatternType::Oscillating, false);
  EXPECT_FALSE(pattern.degrees.empty());
}

TEST(ArpeggioVocabularyTest, ThirtySeeds) {
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    std::mt19937 rng(seed);
    auto pattern = generatePattern({0, 2, 4}, ArcPhase::Ascent,
                                    PatternRole::Drive, false, rng,
                                    ArpeggioPatternType::Rising, false);
    EXPECT_FALSE(pattern.degrees.empty()) << "seed=" << seed;
  }
}

}  // namespace
}  // namespace bach
