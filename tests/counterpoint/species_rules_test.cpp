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

}  // namespace
}  // namespace bach
