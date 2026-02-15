#include "core/figure_match.h"

#include <gtest/gtest.h>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "core/scale.h"

namespace bach {
namespace {

// Test exact match with descending run in C major
TEST(FigureMatchTest, ExactDescRun4InCMajor) {
  // C5, B4, A4, G4 = descending scale run
  uint8_t pitches[] = {72, 71, 69, 67};
  float score = figure_match::matchFigure(
      pitches, 4, kDescRun4, Key::C, ScaleType::Major);
  // Each interval is exactly -1 degree, should be very high score
  EXPECT_GE(score, 0.9f);
}

// Test exact match with ascending run in C major
TEST(FigureMatchTest, ExactAscRun4InCMajor) {
  // C4, D4, E4, F4 = ascending scale run
  uint8_t pitches[] = {60, 62, 64, 65};
  float score = figure_match::matchFigure(
      pitches, 4, kAscRun4, Key::C, ScaleType::Major);
  EXPECT_GE(score, 0.9f);
}

// Test transposition: descending run in G major
TEST(FigureMatchTest, DescRun4InGMajor) {
  // G4, F#4, E4, D4
  uint8_t pitches[] = {67, 66, 64, 62};
  float score = figure_match::matchFigure(
      pitches, 4, kDescRun4, Key::G, ScaleType::Major);
  EXPECT_GE(score, 0.9f);
}

// Test direction mismatch: ascending instead of descending
TEST(FigureMatchTest, DirectionMismatchScoresZero) {
  // C4, D4, E4, F4 (ascending) vs kDescRun4 (descending)
  uint8_t pitches[] = {60, 62, 64, 65};
  float score = figure_match::matchFigure(
      pitches, 4, kDescRun4, Key::C, ScaleType::Major);
  EXPECT_LE(score, 0.15f);  // All directions wrong
}

// Test semitone mode: mordent
TEST(FigureMatchTest, ExactMordentMatch) {
  // C4, B3, C4 = lower mordent (-1, +1 semitone)
  uint8_t pitches[] = {60, 59, 60};
  float score = figure_match::matchFigure(
      pitches, 3, kMordent, Key::C, ScaleType::Major);
  EXPECT_GE(score, 0.9f);
}

// Test semitone mode: near match (+/-1)
TEST(FigureMatchTest, NearMordentMatch) {
  // C4, Bb3, C4 = similar to mordent but -2 instead of -1
  uint8_t pitches[] = {60, 58, 60};
  float score = figure_match::matchFigure(
      pitches, 3, kMordent, Key::C, ScaleType::Major);
  // First interval: expected -1, actual -2, diff=1 -> 0.3
  // Second interval: expected +1, actual +2, diff=1 -> 0.3
  EXPECT_NEAR(score, 0.3f, 0.05f);
}

// Test count mismatch returns 0
TEST(FigureMatchTest, CountMismatchReturnsZero) {
  uint8_t pitches[] = {60, 62, 64};
  float score = figure_match::matchFigure(
      pitches, 3, kDescRun4, Key::C, ScaleType::Major);  // note_count=4
  EXPECT_EQ(score, 0.0f);
}

// Test same direction but +/-1 degree off
TEST(FigureMatchTest, SameDirectionSlightlyOff) {
  // C5(72), A4(69), G4(67), F4(65)
  // deg diffs: -2, -1, -1 vs expected -1, -1, -1
  // First: same dir, off by 1 -> 0.3; Second: exact -> 1.0+; Third: exact -> 1.0+
  uint8_t pitches[] = {72, 69, 67, 65};
  float score = figure_match::matchFigure(
      pitches, 4, kDescRun4, Key::C, ScaleType::Major);
  EXPECT_GT(score, 0.4f);
  EXPECT_LT(score, 0.9f);
}

// Test findBestFigure returns correct index
TEST(FigureMatchTest, FindBestFigureReturnsCorrectIndex) {
  // Descending scale run should match kDescRun4 (index 0 in kCommonFigures)
  uint8_t pitches[] = {72, 71, 69, 67};
  int idx = figure_match::findBestFigure(
      pitches, 4, kCommonFigures, kCommonFigureCount,
      Key::C, ScaleType::Major, 0.7f);
  EXPECT_EQ(idx, 0);  // kDescRun4 is first in kCommonFigures
}

// Test findBestFigure returns -1 when nothing matches
TEST(FigureMatchTest, FindBestFigureReturnsNegativeWhenNoMatch) {
  // Random chromatic notes that don't form any pattern
  uint8_t pitches[] = {60, 73, 50, 85};
  int idx = figure_match::findBestFigure(
      pitches, 4, kCommonFigures, kCommonFigureCount,
      Key::C, ScaleType::Major, 0.7f);
  EXPECT_EQ(idx, -1);
}

// Test degree mode with cambiata pattern
TEST(FigureMatchTest, CambiataDownMatch) {
  // E4(64), D4(62), C4(60), D4(62) = -1, -1, +1 degrees (cambiata_down)
  uint8_t pitches[] = {64, 62, 60, 62};
  float score = figure_match::matchFigure(
      pitches, 4, kCambiataDown, Key::C, ScaleType::Major);
  EXPECT_GE(score, 0.9f);
}

// Test with minor key
TEST(FigureMatchTest, DescRun4InAMinor) {
  // A4, G4, F4, E4 = descending in A natural minor
  uint8_t pitches[] = {69, 67, 65, 64};
  float score = figure_match::matchFigure(
      pitches, 4, kDescRun4, Key::A, ScaleType::NaturalMinor);
  EXPECT_GE(score, 0.9f);
}

}  // namespace
}  // namespace bach
