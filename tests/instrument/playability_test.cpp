// Tests for instrument/fretted/playability.h -- transition cost calculation.

#include "instrument/fretted/playability.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// TransitionCost struct
// ---------------------------------------------------------------------------

TEST(TransitionCostTest, TotalSumsComponents) {
  TransitionCost cost;
  cost.stretch_cost = 0.3f;
  cost.position_shift_cost = 0.5f;
  cost.string_crossing_cost = 0.1f;
  EXPECT_FLOAT_EQ(cost.total(), 0.9f);
}

TEST(TransitionCostTest, DefaultIsZero) {
  TransitionCost cost;
  EXPECT_FLOAT_EQ(cost.total(), 0.0f);
}

// ---------------------------------------------------------------------------
// calculateTransitionCost -- same position (within 4 frets)
// ---------------------------------------------------------------------------

TEST(CalculateTransitionCostTest, SameFretZeroCost) {
  auto cost = calculateTransitionCost(5, 5, 0);
  EXPECT_FLOAT_EQ(cost.stretch_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.position_shift_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.string_crossing_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.total(), 0.0f);
}

TEST(CalculateTransitionCostTest, AdjacentFretLowCost) {
  auto cost = calculateTransitionCost(3, 4, 0);
  EXPECT_GT(cost.stretch_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.position_shift_cost, 0.0f);  // Within comfortable span
  EXPECT_FLOAT_EQ(cost.string_crossing_cost, 0.0f);  // Same string
}

TEST(CalculateTransitionCostTest, FourFretSpanComfortable) {
  auto cost = calculateTransitionCost(1, 5, 0);
  // 4-fret distance: should be within comfortable span
  EXPECT_FLOAT_EQ(cost.stretch_cost, 0.4f);  // 4 * 0.1
  EXPECT_FLOAT_EQ(cost.position_shift_cost, 0.0f);
}

// ---------------------------------------------------------------------------
// calculateTransitionCost -- position shift (beyond 4 frets)
// ---------------------------------------------------------------------------

TEST(CalculateTransitionCostTest, FiveFretShift) {
  auto cost = calculateTransitionCost(1, 6, 0);
  // 5-fret distance: beyond comfortable span by 1
  EXPECT_GT(cost.stretch_cost, 0.4f);          // More than 4-fret comfortable cost
  EXPECT_GT(cost.position_shift_cost, 0.0f);   // Requires position shift
}

TEST(CalculateTransitionCostTest, LargeShiftHighCost) {
  auto cost_small = calculateTransitionCost(1, 3, 0);   // 2-fret shift
  auto cost_large = calculateTransitionCost(1, 12, 0);  // 11-fret shift

  // Large shift should cost significantly more.
  EXPECT_GT(cost_large.total(), cost_small.total() * 2.0f);
  EXPECT_GT(cost_large.position_shift_cost, 0.0f);
}

TEST(CalculateTransitionCostTest, DescendingFretMovement) {
  // Going from high fret to low fret should cost the same as ascending.
  auto cost_asc = calculateTransitionCost(3, 10, 0);
  auto cost_desc = calculateTransitionCost(10, 3, 0);
  EXPECT_FLOAT_EQ(cost_asc.total(), cost_desc.total());
}

// ---------------------------------------------------------------------------
// calculateTransitionCost -- string crossing
// ---------------------------------------------------------------------------

TEST(CalculateTransitionCostTest, SingleStringCross) {
  auto cost = calculateTransitionCost(5, 5, 1);
  EXPECT_GT(cost.string_crossing_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.stretch_cost, 0.0f);  // Same fret
}

TEST(CalculateTransitionCostTest, MultipleStringCross) {
  auto cost_one = calculateTransitionCost(5, 5, 1);
  auto cost_three = calculateTransitionCost(5, 5, 3);
  EXPECT_GT(cost_three.string_crossing_cost, cost_one.string_crossing_cost);
}

TEST(CalculateTransitionCostTest, CombinedShiftAndStringCross) {
  auto cost = calculateTransitionCost(1, 8, 2);
  // Should have all three cost components.
  EXPECT_GT(cost.stretch_cost, 0.0f);
  EXPECT_GT(cost.position_shift_cost, 0.0f);
  EXPECT_GT(cost.string_crossing_cost, 0.0f);
  EXPECT_GT(cost.total(), 0.0f);
}

// ---------------------------------------------------------------------------
// calculateTransitionCost -- open string
// ---------------------------------------------------------------------------

TEST(CalculateTransitionCostTest, OpenStringToFret) {
  auto cost = calculateTransitionCost(0, 3, 0);
  // 3-fret distance from open string: within comfortable span.
  EXPECT_FLOAT_EQ(cost.stretch_cost, 0.3f);  // 3 * 0.1
  EXPECT_FLOAT_EQ(cost.position_shift_cost, 0.0f);
}

TEST(CalculateTransitionCostTest, OpenStringToHighFret) {
  auto cost = calculateTransitionCost(0, 7, 0);
  // 7-fret distance: beyond comfortable span.
  EXPECT_GT(cost.position_shift_cost, 0.0f);
}

}  // namespace
}  // namespace bach
