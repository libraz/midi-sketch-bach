// Tests for Goldberg config and result types.

#include "forms/goldberg/goldberg_config.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(GoldbergConfigTest, DefaultValues) {
  GoldbergConfig config;
  EXPECT_EQ(config.key.tonic, Key::G);
  EXPECT_FALSE(config.key.is_minor);
  EXPECT_EQ(config.bpm, 60);
  EXPECT_EQ(config.seed, 0u);
  EXPECT_EQ(config.instrument, InstrumentType::Harpsichord);
  EXPECT_EQ(config.scale, DurationScale::Short);
  EXPECT_TRUE(config.apply_repeats);
  EXPECT_FALSE(config.ornament_variation_on_repeat);
}

TEST(GoldbergConfigTest, GeneratorProducesOutput) {
  GoldbergConfig config;
  config.seed = 42;
  auto result = generateGoldbergVariations(config);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.seed_used, 42u);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GoldbergResultTest, DefaultValues) {
  GoldbergResult result;
  EXPECT_TRUE(result.tracks.empty());
  EXPECT_TRUE(result.tempo_events.empty());
  EXPECT_TRUE(result.time_sig_events.empty());
  EXPECT_EQ(result.total_duration_ticks, 0u);
  EXPECT_FALSE(result.success);
}

}  // namespace
}  // namespace bach
