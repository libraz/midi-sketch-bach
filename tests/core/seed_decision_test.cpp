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

TEST(SeedBlockChoice, AClearanceOfThreeKeepsEveryValueThreeSeedsApart) {
  constexpr std::uint32_t kModulus = 12;
  constexpr std::uint32_t kClear = 3;
  for (const SeedDecision tag : kBlockTags) {
    for (std::uint32_t seed = 0; seed + kClear < kBlockSeeds; ++seed) {
      const std::uint32_t value = seedBlockChoice(seed, tag, kModulus, 0, kClear);
      for (std::uint32_t gap = 1; gap <= kClear; ++gap) {
        EXPECT_NE(value, seedBlockChoice(seed + gap, tag, kModulus, 0, kClear))
            << "seed " << seed << " gap " << gap;
      }
    }
    for (std::uint32_t start = 0; start + kModulus <= kBlockSeeds; start += kModulus) {
      std::set<std::uint32_t> values;
      for (std::uint32_t seed = start; seed < start + kModulus; ++seed)
        values.insert(seedBlockChoice(seed, tag, kModulus, 0, kClear));
      EXPECT_EQ(values.size(), kModulus) << "block at " << start;
    }
  }
}

// A clearance of one is the neighbour rule: a block whose first value repeats the
// previous block's last trades its first two.
TEST(SeedBlockChoice, AClearanceOfOneTradesOnlyTheFirstTwo) {
  for (const std::uint32_t modulus : kBlockModuli) {
    for (const SeedDecision tag : kBlockTags) {
      for (std::uint32_t seed = modulus; seed < kBlockSeeds; ++seed) {
        const std::uint32_t block = seed / modulus;
        const std::uint32_t position = seed % modulus;
        const std::uint32_t first = detail::blockShuffleAt(block, tag, modulus, 0, 0);
        const bool repeats =
            first == detail::blockShuffleAt(block - 1, tag, modulus, 0, modulus - 1);
        std::uint32_t expected = detail::blockShuffleAt(block, tag, modulus, 0, position);
        if (repeats && position < 2)
          expected = detail::blockShuffleAt(block, tag, modulus, 0, 1 - position);
        EXPECT_EQ(seedBlockChoice(seed, tag, modulus), expected)
            << "modulus " << modulus << " seed " << seed;
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

// The (width, wide coordinates, last size) shapes the fugue-family forms draw.
struct TupleShape {
  std::uint32_t width;
  std::uint32_t wide_count;
  std::uint32_t size;
};
constexpr std::array<TupleShape, 12> kTupleShapes = {{{4, 1, 3},
                                                      {4, 1, 4},
                                                      {4, 1, 7},
                                                      {4, 1, 10},
                                                      {4, 2, 3},
                                                      {4, 2, 4},
                                                      {4, 2, 7},
                                                      {4, 2, 10},
                                                      {4, 4, 3},
                                                      {4, 4, 4},
                                                      {4, 4, 7},
                                                      {4, 4, 10}}};

std::uint32_t blockSizeOf(const TupleShape& shape) {
  std::uint32_t span = shape.size;
  for (std::uint32_t coord = 0; coord < shape.wide_count; ++coord)
    span *= shape.width;
  return span;
}

std::vector<std::uint32_t> tupleAt(std::uint32_t seed, const TupleShape& shape,
                                   std::uint32_t index) {
  const TupleChoice choice = seedTupleChoice(seed, SeedDecision::kSubject, shape.width,
                                             shape.wide_count, shape.size, index);
  std::vector<std::uint32_t> values(choice.wide.begin(),
                                    choice.wide.begin() + static_cast<int>(shape.wide_count));
  values.push_back(choice.last);
  return values;
}

TEST(SeedTupleChoice, MatchesGoldenValues) {
  const TupleChoice first = seedTupleChoice(0u, SeedDecision::kSubject, 4u, 2u, 3u, 4u);
  EXPECT_EQ(first.wide[0], 3u);
  EXPECT_EQ(first.wide[1], 2u);
  EXPECT_EQ(first.last, 2u);
  const TupleChoice second = seedTupleChoice(1u, SeedDecision::kSubject, 4u, 2u, 3u, 4u);
  EXPECT_EQ(second.wide[0], 0u);
  EXPECT_EQ(second.wide[1], 1u);
  EXPECT_EQ(second.last, 0u);
  const TupleChoice block_end = seedTupleChoice(47u, SeedDecision::kSubject, 4u, 2u, 3u, 4u);
  EXPECT_EQ(block_end.wide[0], 0u);
  EXPECT_EQ(block_end.wide[1], 0u);
  EXPECT_EQ(block_end.last, 0u);
  const TupleChoice next_block = seedTupleChoice(48u, SeedDecision::kSubject, 4u, 2u, 3u, 4u);
  EXPECT_EQ(next_block.wide[0], 2u);
  EXPECT_EQ(next_block.wide[1], 3u);
  EXPECT_EQ(next_block.last, 2u);
  const TupleChoice wide = seedTupleChoice(12345u, SeedDecision::kSubject, 4u, 4u, 10u, 11u);
  EXPECT_EQ(wide.wide, (std::array<std::uint32_t, kMaxTupleWide>{2u, 1u, 1u, 0u}));
  EXPECT_EQ(wide.last, 7u);
  const TupleChoice large = seedTupleChoice(0x0FFFFFFFu, SeedDecision::kSubject, 4u, 1u, 7u);
  EXPECT_EQ(large.wide[0], 3u);
  EXPECT_EQ(large.last, 0u);
  static_assert(seedTupleChoice(1u, SeedDecision::kSubject, 4u, 2u, 3u, 4u).wide[1] == 1u,
                "seedTupleChoice must be a constant expression");
}

TEST(SeedTupleChoice, EachAlignedBlockTakesEveryTupleOnce) {
  for (const TupleShape& shape : kTupleShapes) {
    const std::uint32_t block_size = blockSizeOf(shape);
    for (std::uint32_t block = 0; block < 3; ++block) {
      std::set<std::vector<std::uint32_t>> seen;
      for (std::uint32_t pos = 0; pos < block_size; ++pos)
        seen.insert(tupleAt(block * block_size + pos, shape, 3u));
      EXPECT_EQ(seen.size(), block_size)
          << "size " << shape.size << " wide " << shape.wide_count << " block " << block;
    }
  }
}

TEST(SeedTupleChoice, ConsecutiveSeedsDifferInEveryCoordinate) {
  for (const TupleShape& shape : kTupleShapes) {
    const std::uint32_t block_size = blockSizeOf(shape);
    std::vector<std::uint32_t> prev = tupleAt(0u, shape, 5u);
    // Three blocks and two edges, plus a far block edge.
    for (std::uint32_t seed = 1; seed < 3 * block_size + 2; ++seed) {
      const std::vector<std::uint32_t> curr = tupleAt(seed, shape, 5u);
      for (std::size_t coord = 0; coord < curr.size(); ++coord)
        EXPECT_NE(prev[coord], curr[coord]) << "size " << shape.size << " seed " << seed;
      prev = curr;
    }
    const std::uint32_t far_edge = 1000u * block_size;
    const std::vector<std::uint32_t> before = tupleAt(far_edge - 1, shape, 5u);
    const std::vector<std::uint32_t> after = tupleAt(far_edge, shape, 5u);
    for (std::size_t coord = 0; coord < after.size(); ++coord)
      EXPECT_NE(before[coord], after[coord]) << "size " << shape.size << " far edge";
  }
}

TEST(SeedTupleChoice, EveryWideCoordinatePermutesEachBlockOfItsWidth) {
  for (const TupleShape& shape : kTupleShapes) {
    for (std::uint32_t first = 0; first < 2 * blockSizeOf(shape); first += shape.width) {
      for (std::uint32_t coord = 0; coord < shape.wide_count; ++coord) {
        std::set<std::uint32_t> values;
        for (std::uint32_t seed = first; seed < first + shape.width; ++seed)
          values.insert(tupleAt(seed, shape, 7u)[coord]);
        EXPECT_EQ(values.size(), shape.width) << "size " << shape.size << " seed " << first;
      }
    }
  }
}

// The edge trade never reaches a block's last position: there every coordinate reads its
// block's plain shuffle, so a lookup needs one previous block and never the chain before it.
TEST(SeedTupleChoice, ABlocksLastTupleIsItsPlainShuffle) {
  for (const TupleShape& shape : kTupleShapes) {
    const std::uint32_t block_size = blockSizeOf(shape);
    std::uint32_t mult = 0;
    if (shape.wide_count > 1)
      mult = detail::tupleMultiplier(shape.width, shape.wide_count, shape.size);
    for (std::uint32_t block = 0; block < 6; ++block) {
      const TupleChoice last =
          seedTupleChoice(block * block_size + block_size - 1, SeedDecision::kSubject, shape.width,
                          shape.wide_count, shape.size, 9u);
      const auto last_raw = [&](std::uint32_t coord) {
        return detail::tupleRaw(block_size - 1, shape.width, shape.wide_count, shape.size, mult,
                                coord);
      };
      for (std::uint32_t coord = 0; coord < shape.wide_count; ++coord) {
        EXPECT_EQ(last.wide[coord],
                  detail::blockShuffleAt(block, SeedDecision::kSubject, shape.width,
                                         (9u << 4) | coord, last_raw(coord)))
            << "size " << shape.size << " block " << block;
      }
      EXPECT_EQ(last.last, detail::blockShuffleAt(block, SeedDecision::kSubject, shape.size,
                                                  (9u << 4) | 15u, last_raw(shape.wide_count)))
          << "size " << shape.size << " block " << block;
    }
  }
}

// No coordinate is a function of seed % width over a block, so no choice repeats with the
// width's period. The other properties are read over the same shapes: every wide count, and
// every size a draw class may have.
TEST(SeedTupleChoice, NoCoordinateFollowsTheSeedResidueOverABlock) {
  int shapes = 0;
  for (std::uint32_t wide_count = 1; wide_count <= kMaxTupleWide; ++wide_count) {
    for (std::uint32_t size = 3; size <= 12; ++size) {
      const TupleShape shape{4, wide_count, size};
      const std::uint32_t block_size = blockSizeOf(shape);
      for (std::uint32_t block = 0; block < 2; ++block) {
        std::set<std::vector<std::uint32_t>> seen;
        std::vector<std::vector<std::uint32_t>> tuples;
        for (std::uint32_t pos = 0; pos < block_size; ++pos)
          tuples.push_back(tupleAt(block * block_size + pos, shape, 2u));
        seen.insert(tuples.begin(), tuples.end());
        EXPECT_EQ(seen.size(), block_size) << "wide " << wide_count << " size " << size;
        for (std::size_t coord = 0; coord <= wide_count; ++coord) {
          bool follows = true;
          for (std::uint32_t pos = shape.width; pos < block_size && follows; ++pos)
            follows = tuples[pos][coord] == tuples[pos % shape.width][coord];
          EXPECT_FALSE(follows) << "wide " << wide_count << " size " << size << " coord " << coord;
        }
        for (std::uint32_t pos = 1; pos < block_size; ++pos) {
          for (std::size_t coord = 0; coord <= wide_count; ++coord)
            EXPECT_NE(tuples[pos - 1][coord], tuples[pos][coord])
                << "wide " << wide_count << " size " << size << " pos " << pos;
        }
      }
      ++shapes;
    }
  }
  EXPECT_EQ(shapes, 40);
}

TEST(SeedTupleChoice, IndexSeparatesRepeatedDrawsOfOneDecision) {
  const TupleShape shape{4, 2, 3};
  int same = 0;
  for (std::uint32_t seed = 0; seed < 480; ++seed) {
    if (tupleAt(seed, shape, 0u) == tupleAt(seed, shape, 1u))
      ++same;
  }
  // Two independent draws over 48 tuples coincide at about one seed in 48.
  EXPECT_LT(same, 40);
}

}  // namespace
}  // namespace bach::rng
