#include "composer/motif_ops.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/material.h"

namespace bach::composer::motif_ops {
namespace {

constexpr Tick kQuarter = kTicksPerBeat;

MaterialNote mn(Tick start, Tick duration, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = duration;
  n.pitch = pitch;
  return n;
}

// Compact subject motif: C4-D4-E4-F4 at quarter notes, all duration = 1 beat.
std::vector<MaterialNote> ascendingScale4() {
  return {mn(0, kQuarter, 60), mn(kQuarter, kQuarter, 62), mn(2 * kQuarter, kQuarter, 64),
          mn(3 * kQuarter, kQuarter, 65)};
}

}  // namespace

TEST(MotifOpsTest, InvertMelodyReflectsAroundPivot) {
  // Pivot 60 (C4). 60 → 60, 62 → 58, 64 → 56, 65 → 55.
  const auto inv = invertMelody(ascendingScale4(), 60);
  ASSERT_EQ(inv.size(), 4u);
  EXPECT_EQ(inv[0].pitch, 60);
  EXPECT_EQ(inv[1].pitch, 58);
  EXPECT_EQ(inv[2].pitch, 56);
  EXPECT_EQ(inv[3].pitch, 55);
  // Timing unchanged.
  EXPECT_EQ(inv[0].start_tick, 0u);
  EXPECT_EQ(inv[3].start_tick, 3u * kQuarter);
  EXPECT_EQ(inv[2].duration, kQuarter);
}

TEST(MotifOpsTest, InvertMelodyClampsToMidiRange) {
  // Pivot 0 with a high source pitch should clamp result at 0.
  std::vector<MaterialNote> in = {mn(0, kQuarter, 100)};
  const auto inv = invertMelody(in, 0);
  ASSERT_EQ(inv.size(), 1u);
  EXPECT_EQ(inv[0].pitch, 0);
}

TEST(MotifOpsTest, RetrogradePreservesRhythm) {
  // Source: C(60) qtr, D(62) qtr, E(64) half.
  std::vector<MaterialNote> src = {mn(0, kQuarter, 60), mn(kQuarter, kQuarter, 62),
                                   mn(2 * kQuarter, 2 * kQuarter, 64)};
  const auto rev = retrogradeMelody(src, 0);
  ASSERT_EQ(rev.size(), 3u);
  // Pitches reversed.
  EXPECT_EQ(rev[0].pitch, 64);
  EXPECT_EQ(rev[1].pitch, 62);
  EXPECT_EQ(rev[2].pitch, 60);
  // Rhythm reversed: half + qtr + qtr.
  EXPECT_EQ(rev[0].duration, 2u * kQuarter);
  EXPECT_EQ(rev[1].duration, kQuarter);
  EXPECT_EQ(rev[2].duration, kQuarter);
  // Anchors at requested start_tick.
  EXPECT_EQ(rev[0].start_tick, 0u);
  EXPECT_EQ(rev[1].start_tick, 2u * kQuarter);
  EXPECT_EQ(rev[2].start_tick, 3u * kQuarter);
}

TEST(MotifOpsTest, RetrogradeWithStartOffset) {
  const auto src = ascendingScale4();
  const auto rev = retrogradeMelody(src, 4 * kQuarter);
  ASSERT_EQ(rev.size(), 4u);
  EXPECT_EQ(rev[0].start_tick, 4u * kQuarter);
  EXPECT_EQ(rev[3].start_tick, 7u * kQuarter);
  EXPECT_EQ(rev[0].pitch, 65);
  EXPECT_EQ(rev[3].pitch, 60);
}

TEST(MotifOpsTest, RetrogradeEmptyInput) {
  std::vector<MaterialNote> empty;
  const auto rev = retrogradeMelody(empty, 0);
  EXPECT_TRUE(rev.empty());
}

TEST(MotifOpsTest, AugmentDurationDoubles) {
  const auto src = ascendingScale4();
  const auto aug = augmentDuration(src, 0, 2);
  ASSERT_EQ(aug.size(), 4u);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(aug[i].duration, 2u * kQuarter) << "i=" << i;
  }
  // Offsets also doubled: 0, 2q, 4q, 6q.
  EXPECT_EQ(aug[0].start_tick, 0u);
  EXPECT_EQ(aug[1].start_tick, 2u * kQuarter);
  EXPECT_EQ(aug[2].start_tick, 4u * kQuarter);
  EXPECT_EQ(aug[3].start_tick, 6u * kQuarter);
}

TEST(MotifOpsTest, AugmentDurationWithStartOffset) {
  const auto src = ascendingScale4();
  const auto aug = augmentDuration(src, 8 * kQuarter, 2);
  EXPECT_EQ(aug[0].start_tick, 8u * kQuarter);
  EXPECT_EQ(aug[3].start_tick, 14u * kQuarter);
}

TEST(MotifOpsTest, DiminishDurationHalves) {
  // Source with duration=2q each.
  std::vector<MaterialNote> src = {mn(0, 2 * kQuarter, 60), mn(2 * kQuarter, 2 * kQuarter, 62),
                                   mn(4 * kQuarter, 2 * kQuarter, 64)};
  const auto dim = diminishDuration(src, 0, 2);
  ASSERT_EQ(dim.size(), 3u);
  for (const auto& n : dim) {
    EXPECT_EQ(n.duration, kQuarter);
  }
  EXPECT_EQ(dim[0].start_tick, 0u);
  EXPECT_EQ(dim[1].start_tick, kQuarter);
  EXPECT_EQ(dim[2].start_tick, 2u * kQuarter);
}

TEST(MotifOpsTest, DiminishDurationClampsToOneTick) {
  // Tiny input duration (4 ticks) divided by 16 should clamp at 1.
  std::vector<MaterialNote> src = {mn(0, 4, 60)};
  const auto dim = diminishDuration(src, 0, 16);
  ASSERT_EQ(dim.size(), 1u);
  EXPECT_EQ(dim[0].duration, 1u);
}

TEST(MotifOpsTest, ApplyTransformOriginalIsIdentityShifted) {
  const auto src = ascendingScale4();
  const auto out = applyTransform(src, EpisodeMotifTransform::Original, 4 * kQuarter);
  ASSERT_EQ(out.size(), 4u);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(out[i].pitch, src[i].pitch) << "i=" << i;
    EXPECT_EQ(out[i].duration, src[i].duration);
    EXPECT_EQ(out[i].start_tick, 4u * kQuarter + src[i].start_tick);
  }
}

TEST(MotifOpsTest, ApplyTransformInvertUsesPivotAndShift) {
  const auto src = ascendingScale4();
  // Invert around C4 then shift to bar 2.
  const auto out = applyTransform(src, EpisodeMotifTransform::Invert, 8 * kQuarter, 60);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[0].pitch, 60);
  EXPECT_EQ(out[1].pitch, 58);
  EXPECT_EQ(out[2].pitch, 56);
  EXPECT_EQ(out[3].pitch, 55);
  EXPECT_EQ(out[0].start_tick, 8u * kQuarter);
  EXPECT_EQ(out[3].start_tick, 11u * kQuarter);
}

TEST(MotifOpsTest, ApplyTransformRetrogradePreservesRhythm) {
  const auto src = ascendingScale4();
  const auto out = applyTransform(src, EpisodeMotifTransform::Retrograde, 0);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[0].pitch, 65);
  EXPECT_EQ(out[3].pitch, 60);
}

TEST(MotifOpsTest, ApplyTransformAugmentUsesFactor) {
  const auto src = ascendingScale4();
  const auto out = applyTransform(src, EpisodeMotifTransform::Augment, 0, 60, 3);
  ASSERT_EQ(out.size(), 4u);
  for (const auto& n : out) {
    EXPECT_EQ(n.duration, 3u * kQuarter);
  }
}

TEST(MotifOpsTest, ApplyTransformDiminishUsesFactor) {
  std::vector<MaterialNote> src = {mn(0, 4 * kQuarter, 60), mn(4 * kQuarter, 4 * kQuarter, 62)};
  const auto out = applyTransform(src, EpisodeMotifTransform::Diminish, 0, 60, 4);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].duration, kQuarter);
  EXPECT_EQ(out[1].duration, kQuarter);
  EXPECT_EQ(out[0].start_tick, 0u);
  EXPECT_EQ(out[1].start_tick, kQuarter);
}

}  // namespace bach::composer::motif_ops
