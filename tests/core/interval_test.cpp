// Tests for core/interval.h -- interval names, compound reduction, category queries.

#include "core/interval.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// compoundToSimple
// ---------------------------------------------------------------------------

TEST(CompoundToSimpleTest, SimpleIntervalsUnchanged) {
  EXPECT_EQ(interval_util::compoundToSimple(0), 0);
  EXPECT_EQ(interval_util::compoundToSimple(1), 1);
  EXPECT_EQ(interval_util::compoundToSimple(7), 7);
  EXPECT_EQ(interval_util::compoundToSimple(11), 11);
}

TEST(CompoundToSimpleTest, CompoundIntervalsReduced) {
  EXPECT_EQ(interval_util::compoundToSimple(12), 0);   // Octave -> unison
  EXPECT_EQ(interval_util::compoundToSimple(13), 1);   // Minor 9th -> minor 2nd
  EXPECT_EQ(interval_util::compoundToSimple(19), 7);   // Compound 5th -> P5
  EXPECT_EQ(interval_util::compoundToSimple(24), 0);   // Double octave -> unison
  EXPECT_EQ(interval_util::compoundToSimple(15), 3);   // Compound m3
}

TEST(CompoundToSimpleTest, NegativeIntervals) {
  EXPECT_EQ(interval_util::compoundToSimple(-3), 3);   // Descending m3
  EXPECT_EQ(interval_util::compoundToSimple(-7), 7);   // Descending P5
  EXPECT_EQ(interval_util::compoundToSimple(-12), 0);  // Descending octave
  EXPECT_EQ(interval_util::compoundToSimple(-19), 7);  // Descending compound 5th
}

// ---------------------------------------------------------------------------
// intervalName
// ---------------------------------------------------------------------------

TEST(IntervalNameTest, SimpleIntervals) {
  EXPECT_STREQ(interval_util::intervalName(0), "Perfect Unison");
  EXPECT_STREQ(interval_util::intervalName(1), "Minor 2nd");
  EXPECT_STREQ(interval_util::intervalName(2), "Major 2nd");
  EXPECT_STREQ(interval_util::intervalName(3), "Minor 3rd");
  EXPECT_STREQ(interval_util::intervalName(4), "Major 3rd");
  EXPECT_STREQ(interval_util::intervalName(5), "Perfect 4th");
  EXPECT_STREQ(interval_util::intervalName(6), "Tritone");
  EXPECT_STREQ(interval_util::intervalName(7), "Perfect 5th");
  EXPECT_STREQ(interval_util::intervalName(8), "Minor 6th");
  EXPECT_STREQ(interval_util::intervalName(9), "Major 6th");
  EXPECT_STREQ(interval_util::intervalName(10), "Minor 7th");
  EXPECT_STREQ(interval_util::intervalName(11), "Major 7th");
}

TEST(IntervalNameTest, CompoundIntervalsReducedBeforeNaming) {
  // Compound intervals reduce to simple, then named.
  EXPECT_STREQ(interval_util::intervalName(12), "Perfect Unison");  // Octave -> unison
  EXPECT_STREQ(interval_util::intervalName(19), "Perfect 5th");     // Compound P5
  EXPECT_STREQ(interval_util::intervalName(15), "Minor 3rd");       // Compound m3
}

TEST(IntervalNameTest, NegativeIntervals) {
  EXPECT_STREQ(interval_util::intervalName(-7), "Perfect 5th");
  EXPECT_STREQ(interval_util::intervalName(-3), "Minor 3rd");
}

// ---------------------------------------------------------------------------
// isPerfectInterval
// ---------------------------------------------------------------------------

TEST(IsPerfectIntervalTest, PerfectIntervals) {
  EXPECT_TRUE(interval_util::isPerfectInterval(0));   // Unison
  EXPECT_TRUE(interval_util::isPerfectInterval(5));   // Perfect 4th
  EXPECT_TRUE(interval_util::isPerfectInterval(7));   // Perfect 5th
  EXPECT_TRUE(interval_util::isPerfectInterval(12));  // Octave (reduces to unison)
}

TEST(IsPerfectIntervalTest, ImperfectIntervals) {
  EXPECT_FALSE(interval_util::isPerfectInterval(3));   // Minor 3rd
  EXPECT_FALSE(interval_util::isPerfectInterval(4));   // Major 3rd
  EXPECT_FALSE(interval_util::isPerfectInterval(8));   // Minor 6th
  EXPECT_FALSE(interval_util::isPerfectInterval(9));   // Major 6th
}

TEST(IsPerfectIntervalTest, Dissonances) {
  EXPECT_FALSE(interval_util::isPerfectInterval(1));   // Minor 2nd
  EXPECT_FALSE(interval_util::isPerfectInterval(2));   // Major 2nd
  EXPECT_FALSE(interval_util::isPerfectInterval(6));   // Tritone
  EXPECT_FALSE(interval_util::isPerfectInterval(10));  // Minor 7th
  EXPECT_FALSE(interval_util::isPerfectInterval(11));  // Major 7th
}

TEST(IsPerfectIntervalTest, CompoundPerfect) {
  EXPECT_TRUE(interval_util::isPerfectInterval(17));   // Compound P4 (17 % 12 = 5)
  EXPECT_TRUE(interval_util::isPerfectInterval(19));   // Compound P5 (19 % 12 = 7)
  EXPECT_TRUE(interval_util::isPerfectInterval(24));   // Double octave -> unison
}

TEST(IsPerfectIntervalTest, NegativeIntervals) {
  EXPECT_TRUE(interval_util::isPerfectInterval(-5));   // Descending P4
  EXPECT_TRUE(interval_util::isPerfectInterval(-7));   // Descending P5
  EXPECT_FALSE(interval_util::isPerfectInterval(-3));  // Descending m3
}

// ---------------------------------------------------------------------------
// isConsonance
// ---------------------------------------------------------------------------

TEST(IsConsonanceTest, PerfectConsonances) {
  EXPECT_TRUE(interval_util::isConsonance(0));   // Unison
  EXPECT_TRUE(interval_util::isConsonance(7));   // Perfect 5th
  EXPECT_TRUE(interval_util::isConsonance(12));  // Octave
}

TEST(IsConsonanceTest, ImperfectConsonances) {
  EXPECT_TRUE(interval_util::isConsonance(3));   // Minor 3rd
  EXPECT_TRUE(interval_util::isConsonance(4));   // Major 3rd
  EXPECT_TRUE(interval_util::isConsonance(8));   // Minor 6th
  EXPECT_TRUE(interval_util::isConsonance(9));   // Major 6th
}

TEST(IsConsonanceTest, Dissonances) {
  EXPECT_FALSE(interval_util::isConsonance(1));   // Minor 2nd
  EXPECT_FALSE(interval_util::isConsonance(2));   // Major 2nd
  EXPECT_FALSE(interval_util::isConsonance(5));   // Perfect 4th (dissonant in 2-voice)
  EXPECT_FALSE(interval_util::isConsonance(6));   // Tritone
  EXPECT_FALSE(interval_util::isConsonance(10));  // Minor 7th
  EXPECT_FALSE(interval_util::isConsonance(11));  // Major 7th
}

// ---------------------------------------------------------------------------
// invertInterval
// ---------------------------------------------------------------------------

TEST(InvertIntervalTest, StandardInversions) {
  EXPECT_EQ(interval_util::invertInterval(0), 0);    // Unison -> unison
  EXPECT_EQ(interval_util::invertInterval(1), 11);   // m2 -> M7
  EXPECT_EQ(interval_util::invertInterval(2), 10);   // M2 -> m7
  EXPECT_EQ(interval_util::invertInterval(3), 9);    // m3 -> M6
  EXPECT_EQ(interval_util::invertInterval(4), 8);    // M3 -> m6
  EXPECT_EQ(interval_util::invertInterval(5), 7);    // P4 -> P5
  EXPECT_EQ(interval_util::invertInterval(6), 6);    // Tritone -> tritone
  EXPECT_EQ(interval_util::invertInterval(7), 5);    // P5 -> P4
  EXPECT_EQ(interval_util::invertInterval(8), 4);    // m6 -> M3
  EXPECT_EQ(interval_util::invertInterval(9), 3);    // M6 -> m3
  EXPECT_EQ(interval_util::invertInterval(10), 2);   // m7 -> M2
  EXPECT_EQ(interval_util::invertInterval(11), 1);   // M7 -> m2
}

TEST(InvertIntervalTest, CompoundIntervalsReducedFirst) {
  // Compound P5 (19) reduces to 7, inverts to 5.
  EXPECT_EQ(interval_util::invertInterval(19), 5);
  // Octave (12) reduces to 0, inverts to 0.
  EXPECT_EQ(interval_util::invertInterval(12), 0);
}

TEST(InvertIntervalTest, NegativeIntervals) {
  // Negative m3 (-3) reduces to 3, inverts to 9.
  EXPECT_EQ(interval_util::invertInterval(-3), 9);
}

}  // namespace
}  // namespace bach
