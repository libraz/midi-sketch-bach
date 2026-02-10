// Tests for harmony/harmonic_event.h -- struct field defaults and access.

#include "harmony/harmonic_event.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

TEST(HarmonicEventTest, DefaultValues) {
  HarmonicEvent event;
  EXPECT_EQ(event.tick, 0u);
  EXPECT_EQ(event.end_tick, 0u);
  EXPECT_EQ(event.key, Key::C);
  EXPECT_FALSE(event.is_minor);
  EXPECT_EQ(event.bass_pitch, 0);
  EXPECT_FLOAT_EQ(event.weight, 1.0f);
  EXPECT_FALSE(event.is_immutable);
}

// ---------------------------------------------------------------------------
// Field assignment
// ---------------------------------------------------------------------------

TEST(HarmonicEventTest, FieldAssignment) {
  HarmonicEvent event;
  event.tick = 1920;
  event.end_tick = 3840;
  event.key = Key::G;
  event.is_minor = true;
  event.chord.degree = ChordDegree::V;
  event.chord.quality = ChordQuality::Dominant7;
  event.chord.root_pitch = 67;
  event.chord.inversion = 1;
  event.bass_pitch = 43;  // G2
  event.weight = 0.75f;
  event.is_immutable = true;

  EXPECT_EQ(event.tick, 1920u);
  EXPECT_EQ(event.end_tick, 3840u);
  EXPECT_EQ(event.key, Key::G);
  EXPECT_TRUE(event.is_minor);
  EXPECT_EQ(event.chord.degree, ChordDegree::V);
  EXPECT_EQ(event.chord.quality, ChordQuality::Dominant7);
  EXPECT_EQ(event.chord.root_pitch, 67);
  EXPECT_EQ(event.chord.inversion, 1);
  EXPECT_EQ(event.bass_pitch, 43);
  EXPECT_FLOAT_EQ(event.weight, 0.75f);
  EXPECT_TRUE(event.is_immutable);
}

// ---------------------------------------------------------------------------
// Weight defaults
// ---------------------------------------------------------------------------

TEST(HarmonicEventTest, WeightDefaultIsOne) {
  HarmonicEvent event;
  EXPECT_FLOAT_EQ(event.weight, 1.0f);
}

TEST(HarmonicEventTest, WeightCanBeSetToWeak) {
  HarmonicEvent event;
  event.weight = 0.5f;
  EXPECT_FLOAT_EQ(event.weight, 0.5f);
}

}  // namespace
}  // namespace bach
