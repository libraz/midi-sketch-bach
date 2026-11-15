#include "core/seed_decision.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

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

// Golden values from an independent model of the block shuffle, as for seedDraw above.
TEST(SeedBlockChoice, MatchesGoldenValues) {
  EXPECT_EQ(seedBlockChoice(0u, SeedDecision::kSubject, 3u), 0u);
  EXPECT_EQ(seedBlockChoice(1u, SeedDecision::kSubject, 3u), 1u);
  EXPECT_EQ(seedBlockChoice(5u, SeedDecision::kSubject, 4u), 1u);
  EXPECT_EQ(seedBlockChoice(42u, SeedDecision::kToccataArchetype, 4u, 2u), 3u);
  EXPECT_EQ(seedBlockChoice(12345u, SeedDecision::kSubject, 10u, 1u), 8u);
  EXPECT_EQ(seedBlockChoice(0xFFFFFFFFu, SeedDecision::kPedalWalk, 7u), 4u);
  static_assert(seedBlockChoice(42u, SeedDecision::kToccataArchetype, 4u, 2u) == 3u,
                "constexpr block choice");
}

constexpr std::array<std::uint32_t, 4> kBlockModuli = {{3u, 4u, 7u, 10u}};
constexpr std::array<SeedDecision, 2> kBlockTags = {
    {SeedDecision::kSubject, SeedDecision::kToccataArchetype}};
constexpr std::array<std::uint32_t, 2> kBlockIndices = {{0u, 3u}};
constexpr std::uint32_t kBlockSeeds = 2000;

TEST(SeedBlockChoice, NeighbouringSeedsNeverShareAValue) {
  for (const std::uint32_t modulus : kBlockModuli) {
    for (const SeedDecision tag : kBlockTags) {
      for (const std::uint32_t index : kBlockIndices) {
        for (std::uint32_t seed = 0; seed + 1 < kBlockSeeds; ++seed) {
          EXPECT_NE(seedBlockChoice(seed, tag, modulus, index),
                    seedBlockChoice(seed + 1, tag, modulus, index))
              << "modulus " << modulus << " seed " << seed;
        }
      }
    }
  }
}

TEST(SeedBlockChoice, EachAlignedBlockUsesEveryValueOnce) {
  for (const std::uint32_t modulus : kBlockModuli) {
    for (const SeedDecision tag : kBlockTags) {
      for (std::uint32_t start = 0; start + modulus <= kBlockSeeds; start += modulus) {
        std::set<std::uint32_t> values;
        for (std::uint32_t seed = start; seed < start + modulus; ++seed) {
          const std::uint32_t value = seedBlockChoice(seed, tag, modulus);
          EXPECT_LT(value, modulus);
          values.insert(value);
        }
        EXPECT_EQ(values.size(), modulus) << "modulus " << modulus << " block at " << start;
      }
    }
  }
}

// The swap never reaches a block's last position, so reading one previous shuffle equals
// carrying the whole chain of blocks from the first.
TEST(SeedBlockChoice, OnePreviousBlockEqualsTheWholeChain) {
  for (const std::uint32_t modulus : kBlockModuli) {
    for (const std::uint32_t index : kBlockIndices) {
      std::vector<std::uint32_t> previous;
      for (std::uint32_t block = 0; block * modulus < kBlockSeeds; ++block) {
        std::vector<std::uint32_t> current;
        for (std::uint32_t pos = 0; pos < modulus; ++pos) {
          current.push_back(
              detail::blockShuffleAt(block, SeedDecision::kSubject, modulus, index, pos));
        }
        if (!previous.empty() && current.front() == previous.back())
          std::swap(current[0], current[1]);
        for (std::uint32_t pos = 0; pos < modulus; ++pos) {
          EXPECT_EQ(seedBlockChoice(block * modulus + pos, SeedDecision::kSubject, modulus, index),
                    current[pos])
              << "modulus " << modulus << " block " << block;
        }
        previous = current;
      }
    }
  }
}

// No lockstep with the raw residues other sites still read: the smallest family meets every
// (value, seed % 4, seed % 9) pairing, which a `(seed / 4) % 3` draw reaches only a third of.
TEST(SeedBlockChoice, SmallestFamilyMeetsEveryResiduePairing) {
  std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> met;
  for (std::uint32_t seed = 1; seed <= 1000; ++seed)
    met.insert({seedBlockChoice(seed, SeedDecision::kSubject, 3u), seed % 4, seed % 9});
  EXPECT_EQ(met.size(), 3u * 4u * 9u);
}

TEST(SeedBlockChoice, IndexSeparatesRepeatedDrawsOfOneDecision) {
  int differ = 0;
  for (std::uint32_t seed = 0; seed < 64; ++seed) {
    if (seedBlockChoice(seed, SeedDecision::kSubject, 7u, 0u) !=
        seedBlockChoice(seed, SeedDecision::kSubject, 7u, 1u)) {
      ++differ;
    }
  }
  EXPECT_GT(differ, 32);
}

}  // namespace
}  // namespace bach::rng
