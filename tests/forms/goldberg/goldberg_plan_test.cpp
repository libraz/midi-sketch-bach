// Tests for Goldberg Variations plan.

#include "forms/goldberg/goldberg_plan.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(GoldbergPlanTest, PlanHas32Entries) {
  auto plan = createGoldbergPlan();
  EXPECT_EQ(plan.size(), 32u);
}

TEST(GoldbergPlanTest, Entry0IsAria) {
  auto plan = createGoldbergPlan();
  EXPECT_EQ(plan[0].variation_number, 0);
  EXPECT_EQ(plan[0].type, GoldbergVariationType::Aria);
}

TEST(GoldbergPlanTest, Entry31IsDaCapoAria) {
  auto plan = createGoldbergPlan();
  EXPECT_EQ(plan[31].variation_number, 31);
  EXPECT_EQ(plan[31].type, GoldbergVariationType::Aria);
}

TEST(GoldbergPlanTest, EveryThirdVariationIsCanon) {
  auto plan = createGoldbergPlan();
  for (int idx = 3; idx <= 27; idx += 3) {
    EXPECT_EQ(plan[idx].type, GoldbergVariationType::Canon)
        << "Var " << idx << " should be Canon";
  }
}

TEST(GoldbergPlanTest, CanonIntervals) {
  auto plan = createGoldbergPlan();
  // Var 3 = unison (0), Var 6 = 2nd (2), Var 9 = 3rd (4)
  // Var 12 = 4th (5), Var 15 = 5th inverted (7), Var 18 = 6th (9)
  // Var 21 = 7th (11), Var 24 = 8th (12), Var 27 = 9th (14)
  EXPECT_EQ(plan[3].canon.interval_semitones, 0);
  EXPECT_EQ(plan[6].canon.interval_semitones, 2);
  EXPECT_EQ(plan[9].canon.interval_semitones, 4);
  EXPECT_EQ(plan[12].canon.interval_semitones, 5);
  EXPECT_EQ(plan[15].canon.interval_semitones, 7);
  EXPECT_TRUE(plan[15].canon.is_inverted);
  EXPECT_EQ(plan[18].canon.interval_semitones, 9);
  EXPECT_EQ(plan[21].canon.interval_semitones, 11);
  EXPECT_EQ(plan[24].canon.interval_semitones, 12);
  EXPECT_EQ(plan[27].canon.interval_semitones, 14);
}

TEST(GoldbergPlanTest, MinorVariations) {
  auto plan = createGoldbergPlan();
  EXPECT_TRUE(plan[15].key.is_minor);   // Var 15
  EXPECT_TRUE(plan[21].key.is_minor);   // Var 21
  EXPECT_TRUE(plan[25].key.is_minor);   // Var 25
  EXPECT_FALSE(plan[1].key.is_minor);   // Var 1 = major
  EXPECT_FALSE(plan[30].key.is_minor);  // Var 30 = major
}

TEST(GoldbergPlanTest, Var30IsQuodlibet) {
  auto plan = createGoldbergPlan();
  EXPECT_EQ(plan[30].type, GoldbergVariationType::Quodlibet);
}

TEST(GoldbergPlanTest, AllKeysAreG) {
  auto plan = createGoldbergPlan();
  for (const auto& desc : plan) {
    EXPECT_EQ(desc.key.tonic, Key::G)
        << "Var " << desc.variation_number << " should be in G";
  }
}

TEST(GoldbergPlanTest, VariationTypeToString) {
  EXPECT_STREQ(goldbergVariationTypeToString(GoldbergVariationType::Aria), "Aria");
  EXPECT_STREQ(goldbergVariationTypeToString(GoldbergVariationType::Canon), "Canon");
  EXPECT_STREQ(goldbergVariationTypeToString(GoldbergVariationType::Quodlibet), "Quodlibet");
}

}  // namespace
}  // namespace bach
