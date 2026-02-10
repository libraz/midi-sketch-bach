// Tests for voice register ranges.

#include <gtest/gtest.h>

#include "fugue/voice_registers.h"

namespace bach {
namespace {

TEST(VoiceRegistersTest, ThreeVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 3);
  EXPECT_EQ(lo0, 60u);
  EXPECT_EQ(hi0, 96u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 3);
  EXPECT_EQ(lo1, 55u);
  EXPECT_EQ(hi1, 79u);

  auto [lo2, hi2] = getFugueVoiceRange(2, 3);
  EXPECT_EQ(lo2, 36u);
  EXPECT_EQ(hi2, 60u);
}

TEST(VoiceRegistersTest, TwoVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 2);
  EXPECT_EQ(lo0, 55u);
  EXPECT_EQ(hi0, 84u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 2);
  EXPECT_EQ(lo1, 36u);
  EXPECT_EQ(hi1, 67u);
}

TEST(VoiceRegistersTest, FourVoiceRanges) {
  auto [lo0, hi0] = getFugueVoiceRange(0, 4);
  EXPECT_EQ(lo0, 60u);
  EXPECT_EQ(hi0, 96u);

  auto [lo1, hi1] = getFugueVoiceRange(1, 4);
  EXPECT_EQ(lo1, 55u);
  EXPECT_EQ(hi1, 79u);

  auto [lo2, hi2] = getFugueVoiceRange(2, 4);
  EXPECT_EQ(lo2, 48u);
  EXPECT_EQ(hi2, 72u);

  auto [lo3, hi3] = getFugueVoiceRange(3, 4);
  EXPECT_EQ(lo3, 24u);
  EXPECT_EQ(hi3, 50u);
}

TEST(VoiceRegistersTest, FiveVoiceUseFourVoiceRanges) {
  // Voice 4 should clamp to voice 3 ranges (bass).
  auto [lo4, hi4] = getFugueVoiceRange(4, 5);
  EXPECT_EQ(lo4, 24u);
  EXPECT_EQ(hi4, 50u);
}

TEST(VoiceRegistersTest, VoiceRangesNonOverlapping) {
  // For 3 voices, ranges should have minimal overlap.
  auto [lo0, hi0] = getFugueVoiceRange(0, 3);
  auto [lo1, hi1] = getFugueVoiceRange(1, 3);
  auto [lo2, hi2] = getFugueVoiceRange(2, 3);

  // Upper voice should be higher than lower voice ranges.
  EXPECT_GT(lo0, lo1);
  EXPECT_GT(lo1, lo2);
}

}  // namespace
}  // namespace bach
