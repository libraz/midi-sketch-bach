// Tests for core/rng_util.h -- random number generation utilities.

#include "core/rng_util.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace bach {
namespace rng {
namespace {

// ---------------------------------------------------------------------------
// rollProbability
// ---------------------------------------------------------------------------

TEST(RngUtilTest, RollProbabilityAlwaysTrue) {
  std::mt19937 gen(42);
  // Threshold 1.0 should almost always return true
  int true_count = 0;
  for (int idx = 0; idx < 100; ++idx) {
    if (rollProbability(gen, 1.0f)) {
      ++true_count;
    }
  }
  EXPECT_EQ(true_count, 100);
}

TEST(RngUtilTest, RollProbabilityAlwaysFalse) {
  std::mt19937 gen(42);
  // Threshold 0.0 should always return false
  int true_count = 0;
  for (int idx = 0; idx < 100; ++idx) {
    if (rollProbability(gen, 0.0f)) {
      ++true_count;
    }
  }
  EXPECT_EQ(true_count, 0);
}

TEST(RngUtilTest, RollProbabilityDistribution) {
  std::mt19937 gen(42);
  int true_count = 0;
  const int num_trials = 10000;
  for (int idx = 0; idx < num_trials; ++idx) {
    if (rollProbability(gen, 0.5f)) {
      ++true_count;
    }
  }
  // With 50% probability, expect roughly 5000 hits. Allow generous margin.
  EXPECT_GT(true_count, 4500);
  EXPECT_LT(true_count, 5500);
}

// ---------------------------------------------------------------------------
// rollRange
// ---------------------------------------------------------------------------

TEST(RngUtilTest, RollRangeWithinBounds) {
  std::mt19937 gen(42);
  for (int idx = 0; idx < 1000; ++idx) {
    int val = rollRange(gen, 10, 20);
    EXPECT_GE(val, 10);
    EXPECT_LE(val, 20);
  }
}

TEST(RngUtilTest, RollRangeSingleValue) {
  std::mt19937 gen(42);
  // When min == max, should always return that value
  for (int idx = 0; idx < 100; ++idx) {
    EXPECT_EQ(rollRange(gen, 5, 5), 5);
  }
}

TEST(RngUtilTest, RollRangeCoversFullRange) {
  std::mt19937 gen(42);
  std::set<int> seen;
  // Roll [0,3] many times; all values should appear
  for (int idx = 0; idx < 1000; ++idx) {
    seen.insert(rollRange(gen, 0, 3));
  }
  EXPECT_EQ(seen.size(), 4u);  // {0, 1, 2, 3}
}

// ---------------------------------------------------------------------------
// rollFloat
// ---------------------------------------------------------------------------

TEST(RngUtilTest, RollFloatWithinBounds) {
  std::mt19937 gen(42);
  for (int idx = 0; idx < 1000; ++idx) {
    float val = rollFloat(gen, 0.0f, 1.0f);
    EXPECT_GE(val, 0.0f);
    EXPECT_LE(val, 1.0f);
  }
}

TEST(RngUtilTest, RollFloatCustomRange) {
  std::mt19937 gen(42);
  for (int idx = 0; idx < 1000; ++idx) {
    float val = rollFloat(gen, -10.0f, 10.0f);
    EXPECT_GE(val, -10.0f);
    EXPECT_LE(val, 10.0f);
  }
}

// ---------------------------------------------------------------------------
// selectRandom
// ---------------------------------------------------------------------------

TEST(RngUtilTest, SelectRandomFromVector) {
  std::mt19937 gen(42);
  std::vector<int> values = {10, 20, 30, 40, 50};
  std::set<int> seen;
  for (int idx = 0; idx < 1000; ++idx) {
    seen.insert(selectRandom(gen, values));
  }
  // All values should be selected at least once
  EXPECT_EQ(seen.size(), 5u);
}

TEST(RngUtilTest, SelectRandomFromConstVector) {
  std::mt19937 gen(42);
  const std::vector<std::string> words = {"bach", "fugue", "organ"};
  std::set<std::string> seen;
  for (int idx = 0; idx < 1000; ++idx) {
    seen.insert(selectRandom(gen, words));
  }
  EXPECT_EQ(seen.size(), 3u);
}

TEST(RngUtilTest, SelectRandomSingleElement) {
  std::mt19937 gen(42);
  std::vector<int> single = {99};
  EXPECT_EQ(selectRandom(gen, single), 99);
}

// ---------------------------------------------------------------------------
// selectRandomIndex
// ---------------------------------------------------------------------------

TEST(RngUtilTest, SelectRandomIndexWithinBounds) {
  std::mt19937 gen(42);
  std::vector<int> values = {10, 20, 30};
  for (int idx = 0; idx < 1000; ++idx) {
    size_t selected_index = selectRandomIndex(gen, values);
    EXPECT_LT(selected_index, values.size());
  }
}

TEST(RngUtilTest, SelectRandomIndexCoversAll) {
  std::mt19937 gen(42);
  std::vector<int> values = {0, 1, 2, 3};
  std::set<size_t> seen;
  for (int idx = 0; idx < 1000; ++idx) {
    seen.insert(selectRandomIndex(gen, values));
  }
  EXPECT_EQ(seen.size(), 4u);
}

// ---------------------------------------------------------------------------
// Determinism: same seed produces same sequence
// ---------------------------------------------------------------------------

TEST(RngUtilTest, Determinism) {
  std::mt19937 gen1(12345);
  std::mt19937 gen2(12345);

  for (int idx = 0; idx < 100; ++idx) {
    EXPECT_EQ(rollRange(gen1, 0, 1000), rollRange(gen2, 0, 1000));
  }
}

TEST(RngUtilTest, DeterminismFloat) {
  std::mt19937 gen1(67890);
  std::mt19937 gen2(67890);

  for (int idx = 0; idx < 100; ++idx) {
    EXPECT_FLOAT_EQ(rollFloat(gen1, 0.0f, 1.0f), rollFloat(gen2, 0.0f, 1.0f));
  }
}

}  // namespace
}  // namespace rng
}  // namespace bach
