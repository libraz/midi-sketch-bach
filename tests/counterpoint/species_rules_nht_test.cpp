#include "counterpoint/species_rules.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// --- EscapeTone tests ---

TEST(NonHarmonicToneExpansionTest, EscapeTone_StepInLeapOut) {
  // C4(60)->D4(62)->G3(55): step up then leap down = escape tone
  auto type = classifyNonHarmonicTone(60, 62, 55, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::EscapeTone);
}

TEST(NonHarmonicToneExpansionTest, EscapeTone_StepDownLeapUp) {
  // D4(62)->C4(60)->F4(65): step down then leap up = escape tone
  auto type = classifyNonHarmonicTone(62, 60, 65, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::EscapeTone);
}

TEST(NonHarmonicToneExpansionTest, NotEscapeTone_SameDirection) {
  // C4->D4->E4: same direction = passing tone, not escape
  auto type = classifyNonHarmonicTone(60, 62, 64, false, true, true);
  EXPECT_NE(type, NonHarmonicToneType::EscapeTone);
}

// --- Anticipation tests ---

TEST(NonHarmonicToneExpansionTest, Anticipation_SamePitchAsNext) {
  // C4(60)->D4(62)->D4(62): D arrives early = anticipation
  auto type = classifyNonHarmonicTone(60, 62, 62, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::Anticipation);
}

// --- ChangingTone tests ---

TEST(NonHarmonicToneExpansionTest, ChangingTone_StepUpStepDown) {
  // C4(60)->D4(62)->B3(59): step up then step down, resolves differently
  auto type = classifyNonHarmonicTone(60, 62, 59, false, true, true);
  // This is actually an escape tone (step in, leap out opposite)
  // because 62->59 = 3 semitones = leap
  EXPECT_EQ(type, NonHarmonicToneType::EscapeTone);
}

// --- Existing types still classified correctly ---

TEST(NonHarmonicToneExpansionTest, PassingTone_StillWorks) {
  // C4->D4->E4: stepwise same direction = passing tone
  auto type = classifyNonHarmonicTone(60, 62, 64, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::PassingTone);
}

TEST(NonHarmonicToneExpansionTest, NeighborTone_StillWorks) {
  // C4->D4->C4: step away and return = neighbor tone
  auto type = classifyNonHarmonicTone(60, 62, 60, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::NeighborTone);
}

TEST(NonHarmonicToneExpansionTest, Suspension_StillWorks) {
  // C4->C4->B3: held then resolves down = suspension
  auto type = classifyNonHarmonicTone(60, 60, 59, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::Suspension);
}

TEST(NonHarmonicToneExpansionTest, ChordTone_StillWorks) {
  auto type = classifyNonHarmonicTone(60, 64, 67, true, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::ChordTone);
}

}  // namespace
}  // namespace bach
