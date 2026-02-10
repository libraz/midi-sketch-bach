// Tests for counterpoint/species_rules.h -- species-specific counterpoint
// constraints: notes per beat, dissonance, passing/neighbor tones.

#include "counterpoint/species_rules.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Species to string
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, SpeciesToString) {
  EXPECT_STREQ(speciesToString(SpeciesType::First), "first_species");
  EXPECT_STREQ(speciesToString(SpeciesType::Second), "second_species");
  EXPECT_STREQ(speciesToString(SpeciesType::Third), "third_species");
  EXPECT_STREQ(speciesToString(SpeciesType::Fourth), "fourth_species");
  EXPECT_STREQ(speciesToString(SpeciesType::Fifth), "fifth_species");
}

// ---------------------------------------------------------------------------
// Notes per beat
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, NotesPerBeat) {
  EXPECT_EQ(SpeciesRules(SpeciesType::First).notesPerBeat(), 1);
  EXPECT_EQ(SpeciesRules(SpeciesType::Second).notesPerBeat(), 2);
  EXPECT_EQ(SpeciesRules(SpeciesType::Third).notesPerBeat(), 4);
  EXPECT_EQ(SpeciesRules(SpeciesType::Fourth).notesPerBeat(), 1);
  EXPECT_EQ(SpeciesRules(SpeciesType::Fifth).notesPerBeat(), 0);
}

// ---------------------------------------------------------------------------
// Dissonance allowed
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, FirstSpeciesNoDissonance) {
  SpeciesRules rules(SpeciesType::First);
  EXPECT_FALSE(rules.isDissonanceAllowed(true));   // Strong beat
  EXPECT_FALSE(rules.isDissonanceAllowed(false));  // Weak beat
}

TEST(SpeciesRulesTest, SecondSpeciesWeakBeatDissonance) {
  SpeciesRules rules(SpeciesType::Second);
  EXPECT_FALSE(rules.isDissonanceAllowed(true));   // Strong beat: no
  EXPECT_TRUE(rules.isDissonanceAllowed(false));   // Weak beat: yes
}

TEST(SpeciesRulesTest, ThirdSpeciesWeakBeatDissonance) {
  SpeciesRules rules(SpeciesType::Third);
  EXPECT_FALSE(rules.isDissonanceAllowed(true));   // Strong beat: no
  EXPECT_TRUE(rules.isDissonanceAllowed(false));   // Weak beat: yes
}

TEST(SpeciesRulesTest, FourthSpeciesSuspensionOnStrongBeat) {
  SpeciesRules rules(SpeciesType::Fourth);
  // In 4th species, the suspension (dissonance) occurs on the strong beat.
  EXPECT_TRUE(rules.isDissonanceAllowed(true));
  EXPECT_FALSE(rules.isDissonanceAllowed(false));
}

TEST(SpeciesRulesTest, FifthSpeciesWeakBeatDissonance) {
  SpeciesRules rules(SpeciesType::Fifth);
  EXPECT_FALSE(rules.isDissonanceAllowed(true));
  EXPECT_TRUE(rules.isDissonanceAllowed(false));
}

// ---------------------------------------------------------------------------
// Suspension resolution
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, OnlyFourthSpeciesRequiresSuspension) {
  EXPECT_FALSE(SpeciesRules(SpeciesType::First).requiresSuspensionResolution());
  EXPECT_FALSE(SpeciesRules(SpeciesType::Second).requiresSuspensionResolution());
  EXPECT_FALSE(SpeciesRules(SpeciesType::Third).requiresSuspensionResolution());
  EXPECT_TRUE(SpeciesRules(SpeciesType::Fourth).requiresSuspensionResolution());
  EXPECT_FALSE(SpeciesRules(SpeciesType::Fifth).requiresSuspensionResolution());
}

// ---------------------------------------------------------------------------
// Passing tone validation
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, ValidPassingToneAscending) {
  SpeciesRules rules(SpeciesType::Second);
  // C4(60) -> D4(62) -> E4(64) -- ascending stepwise through D4.
  EXPECT_TRUE(rules.isValidPassingTone(60, 62, 64));
}

TEST(SpeciesRulesTest, ValidPassingToneDescending) {
  SpeciesRules rules(SpeciesType::Second);
  // E4(64) -> D4(62) -> C4(60) -- descending stepwise through D4.
  EXPECT_TRUE(rules.isValidPassingTone(64, 62, 60));
}

TEST(SpeciesRulesTest, ValidPassingToneChromatic) {
  SpeciesRules rules(SpeciesType::Third);
  // C4(60) -> C#4(61) -> D4(62) -- chromatic ascending.
  EXPECT_TRUE(rules.isValidPassingTone(60, 61, 62));
}

TEST(SpeciesRulesTest, InvalidPassingToneNotStepwise) {
  SpeciesRules rules(SpeciesType::Second);
  // C4(60) -> E4(64) -> G4(67) -- leap, not step.
  EXPECT_FALSE(rules.isValidPassingTone(60, 64, 67));
}

TEST(SpeciesRulesTest, InvalidPassingToneDirectionChange) {
  SpeciesRules rules(SpeciesType::Second);
  // C4(60) -> D4(62) -> C4(60) -- direction reversal (neighbor, not passing).
  EXPECT_FALSE(rules.isValidPassingTone(60, 62, 60));
}

TEST(SpeciesRulesTest, PassingToneNotAllowedInFirstSpecies) {
  SpeciesRules rules(SpeciesType::First);
  EXPECT_FALSE(rules.isValidPassingTone(60, 62, 64));
}

// ---------------------------------------------------------------------------
// Neighbor tone validation
// ---------------------------------------------------------------------------

TEST(SpeciesRulesTest, ValidUpperNeighborTone) {
  SpeciesRules rules(SpeciesType::Third);
  // C4(60) -> D4(62) -> C4(60) -- upper neighbor.
  EXPECT_TRUE(rules.isValidNeighborTone(60, 62, 60));
}

TEST(SpeciesRulesTest, ValidLowerNeighborTone) {
  SpeciesRules rules(SpeciesType::Third);
  // C4(60) -> B3(59) -> C4(60) -- lower neighbor.
  EXPECT_TRUE(rules.isValidNeighborTone(60, 59, 60));
}

TEST(SpeciesRulesTest, InvalidNeighborToneDoesNotReturn) {
  SpeciesRules rules(SpeciesType::Third);
  // C4(60) -> D4(62) -> E4(64) -- does not return.
  EXPECT_FALSE(rules.isValidNeighborTone(60, 62, 64));
}

TEST(SpeciesRulesTest, InvalidNeighborToneLeap) {
  SpeciesRules rules(SpeciesType::Third);
  // C4(60) -> E4(64) -> C4(60) -- leap, not step.
  EXPECT_FALSE(rules.isValidNeighborTone(60, 64, 60));
}

TEST(SpeciesRulesTest, NeighborToneNotAllowedInFirstSpecies) {
  SpeciesRules rules(SpeciesType::First);
  EXPECT_FALSE(rules.isValidNeighborTone(60, 62, 60));
}

TEST(SpeciesRulesTest, NeighborToneNotAllowedInSecondSpecies) {
  SpeciesRules rules(SpeciesType::Second);
  EXPECT_FALSE(rules.isValidNeighborTone(60, 62, 60));
}

// ---------------------------------------------------------------------------
// classifyNonHarmonicTone
// ---------------------------------------------------------------------------

TEST(ClassifyNonHarmonicToneTest, ChordTone) {
  auto type = classifyNonHarmonicTone(60, 64, 67, true, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::ChordTone);
}

TEST(ClassifyNonHarmonicToneTest, PassingTone) {
  // C->D->E where D is not a chord tone but C and E are.
  auto type = classifyNonHarmonicTone(60, 62, 64, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::PassingTone);
}

TEST(ClassifyNonHarmonicToneTest, NeighborTone) {
  // C->D->C where D is not a chord tone.
  auto type = classifyNonHarmonicTone(60, 62, 60, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::NeighborTone);
}

TEST(ClassifyNonHarmonicToneTest, Suspension) {
  // D held from prev beat, dissonant, resolves to C.
  auto type = classifyNonHarmonicTone(62, 62, 60, false, false, true);
  EXPECT_EQ(type, NonHarmonicToneType::Suspension);
}

TEST(ClassifyNonHarmonicToneTest, UnknownDissonance) {
  // Large leap, not a passing or neighbor tone pattern.
  auto type = classifyNonHarmonicTone(60, 66, 72, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::Unknown);
}

TEST(ClassifyNonHarmonicToneTest, PassingToneDescending) {
  // E->D->C descending passing tone.
  auto type = classifyNonHarmonicTone(64, 62, 60, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::PassingTone);
}

TEST(ClassifyNonHarmonicToneTest, NoPrevPitch) {
  auto type = classifyNonHarmonicTone(0, 62, 64, false, false, true);
  EXPECT_EQ(type, NonHarmonicToneType::Unknown);
}

TEST(ClassifyNonHarmonicToneTest, NoNextPitch) {
  auto type = classifyNonHarmonicTone(60, 62, 0, false, true, false);
  EXPECT_EQ(type, NonHarmonicToneType::Unknown);
}

TEST(ClassifyNonHarmonicToneTest, LowerNeighborTone) {
  // E->D->E (D is lower neighbor).
  auto type = classifyNonHarmonicTone(64, 62, 64, false, true, true);
  EXPECT_EQ(type, NonHarmonicToneType::NeighborTone);
}

TEST(ClassifyNonHarmonicToneTest, SuspensionNonStepResolutionIsUnknown) {
  // Held D but resolves to A (not stepwise).
  auto type = classifyNonHarmonicTone(62, 62, 57, false, false, true);
  EXPECT_EQ(type, NonHarmonicToneType::Unknown);
}

}  // namespace
}  // namespace bach
