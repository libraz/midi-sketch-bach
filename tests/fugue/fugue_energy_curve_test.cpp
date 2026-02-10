// Tests for FugueEnergyCurve -- energy design values and rhythm density.

#include "fugue/fugue_config.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// FugueEnergyCurve::getLevel -- phase-based energy levels
// ---------------------------------------------------------------------------

TEST(FugueEnergyCurveTest, EstablishPhaseReturnsSteadyHalf) {
  // 0-25% should be 0.5
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(0, 1000), 0.5f);
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(100, 1000), 0.5f);
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(249, 1000), 0.5f);
}

TEST(FugueEnergyCurveTest, DevelopPhaseRampsUpward) {
  // 25-70% should ramp from 0.5 to 0.7
  float start = FugueEnergyCurve::getLevel(250, 1000);
  float end = FugueEnergyCurve::getLevel(699, 1000);
  EXPECT_GE(start, 0.5f);
  EXPECT_LE(end, 0.71f);
  EXPECT_GT(end, start);
}

TEST(FugueEnergyCurveTest, StrettoPhaseHighEnergy) {
  // 70-90% should ramp from 0.8 to 1.0
  float start = FugueEnergyCurve::getLevel(700, 1000);
  float end = FugueEnergyCurve::getLevel(899, 1000);
  EXPECT_GE(start, 0.79f);
  EXPECT_LE(end, 1.01f);
}

TEST(FugueEnergyCurveTest, CodaPhaseSettlesAtNinetyPercent) {
  // 90-100% should be 0.9
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(900, 1000), 0.9f);
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(999, 1000), 0.9f);
}

TEST(FugueEnergyCurveTest, ZeroDurationReturnsFallback) {
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(0, 0), 0.5f);
}

TEST(FugueEnergyCurveTest, ExactBoundaryAt25Percent) {
  // At exactly 25%, we should be in the Develop phase (pos >= 0.25 but < 0.70).
  float level = FugueEnergyCurve::getLevel(250, 1000);
  EXPECT_GE(level, 0.5f);
  EXPECT_LT(level, 0.7f);
}

TEST(FugueEnergyCurveTest, ExactBoundaryAt70Percent) {
  // At exactly 70%, we should enter the Stretto phase.
  float level = FugueEnergyCurve::getLevel(700, 1000);
  EXPECT_GE(level, 0.8f);
}

TEST(FugueEnergyCurveTest, ExactBoundaryAt90Percent) {
  // At exactly 90%, we should enter the Coda phase.
  EXPECT_FLOAT_EQ(FugueEnergyCurve::getLevel(900, 1000), 0.9f);
}

TEST(FugueEnergyCurveTest, MonotonicallyNonDecreasingThroughDevelopAndStretto) {
  // Energy should never decrease within Develop (25-70%) and Stretto (70-90%).
  float prev = 0.0f;
  for (Tick tick = 250; tick < 900; tick += 10) {
    float level = FugueEnergyCurve::getLevel(tick, 1000);
    EXPECT_GE(level, prev) << "Energy decreased at tick " << tick;
    prev = level;
  }
}

TEST(FugueEnergyCurveTest, TickBeyondTotalDurationClamped) {
  // Tick beyond total duration should clamp pos to 1.0 -> coda = 0.9
  float level = FugueEnergyCurve::getLevel(2000, 1000);
  EXPECT_FLOAT_EQ(level, 0.9f);
}

// ---------------------------------------------------------------------------
// FugueEnergyCurve::minDuration -- rhythm density thresholds
// ---------------------------------------------------------------------------

TEST(FugueEnergyCurveTest, MinDurationLowEnergyIsQuarterNote) {
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.2f), kTicksPerBeat);
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.0f), kTicksPerBeat);
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.39f), kTicksPerBeat);
}

TEST(FugueEnergyCurveTest, MinDurationMidEnergyIsEighthNote) {
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.5f), kTicksPerBeat / 2);
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.4f), kTicksPerBeat / 2);
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.69f), kTicksPerBeat / 2);
}

TEST(FugueEnergyCurveTest, MinDurationHighEnergyIsSixteenthNote) {
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.8f), kTicksPerBeat / 4);
  EXPECT_EQ(FugueEnergyCurve::minDuration(0.7f), kTicksPerBeat / 4);
  EXPECT_EQ(FugueEnergyCurve::minDuration(1.0f), kTicksPerBeat / 4);
}

}  // namespace
}  // namespace bach
