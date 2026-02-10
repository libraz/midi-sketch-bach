// Tests for DurationScale enum conversion utilities.

#include "core/basic_types.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ===========================================================================
// durationScaleToString
// ===========================================================================

TEST(DurationScaleTest, ToStringShort) {
  EXPECT_STREQ(durationScaleToString(DurationScale::Short), "short");
}

TEST(DurationScaleTest, ToStringMedium) {
  EXPECT_STREQ(durationScaleToString(DurationScale::Medium), "medium");
}

TEST(DurationScaleTest, ToStringLong) {
  EXPECT_STREQ(durationScaleToString(DurationScale::Long), "long");
}

TEST(DurationScaleTest, ToStringFull) {
  EXPECT_STREQ(durationScaleToString(DurationScale::Full), "full");
}

// ===========================================================================
// durationScaleFromString
// ===========================================================================

TEST(DurationScaleTest, FromStringShort) {
  EXPECT_EQ(durationScaleFromString("short"), DurationScale::Short);
}

TEST(DurationScaleTest, FromStringMedium) {
  EXPECT_EQ(durationScaleFromString("medium"), DurationScale::Medium);
}

TEST(DurationScaleTest, FromStringLong) {
  EXPECT_EQ(durationScaleFromString("long"), DurationScale::Long);
}

TEST(DurationScaleTest, FromStringFull) {
  EXPECT_EQ(durationScaleFromString("full"), DurationScale::Full);
}

TEST(DurationScaleTest, FromStringUnrecognizedDefaultsToShort) {
  EXPECT_EQ(durationScaleFromString("invalid"), DurationScale::Short);
  EXPECT_EQ(durationScaleFromString(""), DurationScale::Short);
  EXPECT_EQ(durationScaleFromString("FULL"), DurationScale::Short);
}

// ===========================================================================
// Roundtrip
// ===========================================================================

TEST(DurationScaleTest, RoundtripAllValues) {
  for (auto scale : {DurationScale::Short, DurationScale::Medium,
                     DurationScale::Long, DurationScale::Full}) {
    EXPECT_EQ(durationScaleFromString(durationScaleToString(scale)), scale);
  }
}

}  // namespace
}  // namespace bach
