#include "core/seed_decision.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace bach::rng {
namespace {

// Golden values computed by an independent model of the same arithmetic. They pin the draw so a
// native and a WASM build cannot drift apart silently, and so a tag cannot be renumbered unnoticed.
TEST(SeedDecision, DrawMatchesGoldenValues) {
  EXPECT_EQ(seedDraw(0u, SeedDecision::kSubject), 0xC42C5A1Au);
  EXPECT_EQ(seedDraw(1u, SeedDecision::kSubject), 0x204391A6u);
  EXPECT_EQ(seedDraw(0xFFFFFFFFu, SeedDecision::kSubject), 0xB0CAED0Au);
  EXPECT_EQ(seedDraw(42u, SeedDecision::kLinearFigure, 3u), 0x6D6DE02Fu);
  EXPECT_EQ(seedDraw(7u, SeedDecision::kGroundAnchor), 0xD59D2325u);
  EXPECT_EQ(seedDraw(12345u, SeedDecision::kEntryInterval, 1u), 0x975FEC67u);
}

TEST(SeedDecision, ChoiceMatchesGoldenValues) {
  EXPECT_EQ(seedChoice(0u, SeedDecision::kSubject, 1u), 0u);
  EXPECT_EQ(seedChoice(1u, SeedDecision::kCadenceBlocks, 3u), 2u);
  EXPECT_EQ(seedChoice(42u, SeedDecision::kPedalWalk, 23u), 9u);
  EXPECT_EQ(seedChoice(0xFFFFFFFFu, SeedDecision::kGroundDirection, 3u, 5u), 0u);
}

TEST(SeedDecision, DrawIsAConstantExpression) {
  static_assert(seedDraw(0u, SeedDecision::kSubject) == 0xC42C5A1Au, "constexpr draw");
  static_assert(seedChoice(42u, SeedDecision::kPedalWalk, 23u) == 9u, "constexpr choice");
}

TEST(SeedDecision, DrawIsDeterministic) {
  for (std::uint32_t seed = 0; seed < 64; ++seed) {
    EXPECT_EQ(seedDraw(seed, SeedDecision::kFigurationPhase, 2u),
              seedDraw(seed, SeedDecision::kFigurationPhase, 2u));
  }
}

TEST(SeedDecision, ChoiceStaysInRangeForSmallModuli) {
  for (std::uint32_t modulus = 1; modulus <= 32; ++modulus) {
    for (std::uint32_t seed = 0; seed < 512; ++seed) {
      EXPECT_LT(seedChoice(seed, SeedDecision::kLinearFigure, modulus), modulus);
    }
  }
}

// Multiply-shift bias is at most modulus / 2^32 per value, far below sampling noise; this checks
// that every bucket is reached about equally over consecutive seeds.
TEST(SeedDecision, ChoiceCoversEveryBucketEvenly) {
  constexpr std::uint32_t kModulus = 4;
  constexpr std::uint32_t kSamples = 4000;
  std::array<int, kModulus> counts{};
  for (std::uint32_t seed = 1; seed <= kSamples; ++seed) {
    ++counts[seedChoice(seed, SeedDecision::kSubject, kModulus)];
  }
  for (const int count : counts) {
    EXPECT_GT(count, 900);
    EXPECT_LT(count, 1100);
  }
}

// Two tags drawn from the same seed must not move in lockstep: the raw `seed % N` sites they
// replace agree on every seed, which is the repetition period this helper removes.
TEST(SeedDecision, DifferentTagsAreIndependentForTheSameSeed) {
  constexpr std::uint32_t kModulus = 4;
  constexpr std::uint32_t kSamples = 2000;
  int agree = 0;
  for (std::uint32_t seed = 1; seed <= kSamples; ++seed) {
    if (seedChoice(seed, SeedDecision::kLinearPedalTurn, kModulus) ==
        seedChoice(seed, SeedDecision::kLinearFigure, kModulus)) {
      ++agree;
    }
  }
  // Independent draws agree on about 1 / kModulus of the seeds.
  EXPECT_GT(agree, 400);
  EXPECT_LT(agree, 600);
}

TEST(SeedDecision, IndexSeparatesRepeatedDrawsOfOneDecision) {
  int differ = 0;
  for (std::uint32_t idx = 0; idx < 64; ++idx) {
    if (seedDraw(5u, SeedDecision::kFigurationPhase, idx) !=
        seedDraw(5u, SeedDecision::kFigurationPhase, idx + 1)) {
      ++differ;
    }
  }
  EXPECT_EQ(differ, 64);
}

}  // namespace
}  // namespace bach::rng
