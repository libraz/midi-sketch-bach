// Tests for harmony/chord_tone_utils.h -- vector overload of nearestChordTone.

#include "harmony/chord_tone_utils.h"

#include <gtest/gtest.h>

#include <vector>

namespace bach {
namespace {

using ChordPitches = std::vector<uint8_t>;

TEST(NearestChordToneVectorTest, EmptyReturnsTarget) {
  EXPECT_EQ(nearestChordTone(60, ChordPitches{}), 60);
}

TEST(NearestChordToneVectorTest, SingleElement) {
  EXPECT_EQ(nearestChordTone(60, ChordPitches{64}), 64);
}

TEST(NearestChordToneVectorTest, ExactMatch) {
  EXPECT_EQ(nearestChordTone(60, ChordPitches{55, 60, 67}), 60);
}

TEST(NearestChordToneVectorTest, ClosestBelow) {
  // Target 62, chord tones 60 and 67. 60 is 2 away, 67 is 5 away.
  EXPECT_EQ(nearestChordTone(62, ChordPitches{60, 67}), 60);
}

TEST(NearestChordToneVectorTest, ClosestAbove) {
  // Target 65, chord tones 60 and 67. 60 is 5 away, 67 is 2 away.
  EXPECT_EQ(nearestChordTone(65, ChordPitches{60, 67}), 67);
}

TEST(NearestChordToneVectorTest, TieBreaksToFirst) {
  // Target 63, chord tones 60 and 66. Both 3 away. First wins.
  EXPECT_EQ(nearestChordTone(63, ChordPitches{60, 66}), 60);
}

TEST(NearestChordToneVectorTest, FullTriad) {
  // C major triad in octave 4: C4=60, E4=64, G4=67
  EXPECT_EQ(nearestChordTone(61, ChordPitches{60, 64, 67}), 60);  // Closer to C
  EXPECT_EQ(nearestChordTone(63, ChordPitches{60, 64, 67}), 64);  // Closer to E
  EXPECT_EQ(nearestChordTone(66, ChordPitches{60, 64, 67}), 67);  // Closer to G
}

}  // namespace
}  // namespace bach
