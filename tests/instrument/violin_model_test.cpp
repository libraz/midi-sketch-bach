// Tests for ViolinModel bowed string instrument.

#include "instrument/bowed/violin_model.h"

#include <gtest/gtest.h>

#include "instrument/bowed/cello_model.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Construction and basic properties
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, HasFourStrings) {
  ViolinModel violin;
  EXPECT_EQ(violin.getStringCount(), 4);
}

TEST(ViolinModelTest, TuningIsGDAE) {
  ViolinModel violin;
  const auto& tuning = violin.getTuning();
  ASSERT_EQ(tuning.size(), 4u);
  EXPECT_EQ(tuning[0], 55);  // G3
  EXPECT_EQ(tuning[1], 62);  // D4
  EXPECT_EQ(tuning[2], 69);  // A4
  EXPECT_EQ(tuning[3], 76);  // E5
}

TEST(ViolinModelTest, RangeG3ToC7) {
  ViolinModel violin;
  EXPECT_EQ(violin.getLowestPitch(), 55);   // G3
  EXPECT_EQ(violin.getHighestPitch(), 96);  // C7
}

// ---------------------------------------------------------------------------
// isPitchPlayable
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, OpenStringsArePlayable) {
  ViolinModel violin;
  EXPECT_TRUE(violin.isPitchPlayable(55));  // G3
  EXPECT_TRUE(violin.isPitchPlayable(62));  // D4
  EXPECT_TRUE(violin.isPitchPlayable(69));  // A4
  EXPECT_TRUE(violin.isPitchPlayable(76));  // E5
}

TEST(ViolinModelTest, RangeBoundariesPlayable) {
  ViolinModel violin;
  EXPECT_TRUE(violin.isPitchPlayable(55));  // G3 (lowest)
  EXPECT_TRUE(violin.isPitchPlayable(96));  // C7 (highest)
}

TEST(ViolinModelTest, OutOfRangeNotPlayable) {
  ViolinModel violin;
  EXPECT_FALSE(violin.isPitchPlayable(54));   // Below G3
  EXPECT_FALSE(violin.isPitchPlayable(97));   // Above C7
  EXPECT_FALSE(violin.isPitchPlayable(0));    // Far below
  EXPECT_FALSE(violin.isPitchPlayable(127));  // Far above
}

TEST(ViolinModelTest, MiddleOfRangePlayable) {
  ViolinModel violin;
  EXPECT_TRUE(violin.isPitchPlayable(72));  // C5
  EXPECT_TRUE(violin.isPitchPlayable(84));  // C6
  EXPECT_TRUE(violin.isPitchPlayable(60));  // C4
}

// ---------------------------------------------------------------------------
// isOpenString
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, OpenStringPitchesIdentified) {
  ViolinModel violin;
  EXPECT_TRUE(violin.isOpenString(55));   // G3
  EXPECT_TRUE(violin.isOpenString(62));   // D4
  EXPECT_TRUE(violin.isOpenString(69));   // A4
  EXPECT_TRUE(violin.isOpenString(76));   // E5
}

TEST(ViolinModelTest, NonOpenStringPitchesRejected) {
  ViolinModel violin;
  EXPECT_FALSE(violin.isOpenString(56));  // G#3
  EXPECT_FALSE(violin.isOpenString(60));  // C4
  EXPECT_FALSE(violin.isOpenString(0));
  EXPECT_FALSE(violin.isOpenString(127));
}

// ---------------------------------------------------------------------------
// getPositionsForPitch
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, OpenStringHasPositionZero) {
  ViolinModel violin;
  auto positions = violin.getPositionsForPitch(55);  // G3 (open G string)
  ASSERT_FALSE(positions.empty());

  bool found_open = false;
  for (const auto& pos : positions) {
    if (pos.position == 0) {
      found_open = true;
      EXPECT_EQ(pos.string_idx, 0);  // G string
      EXPECT_EQ(pos.pitch_offset, 0);
    }
  }
  EXPECT_TRUE(found_open);
}

TEST(ViolinModelTest, MultipleFingerings) {
  ViolinModel violin;
  // D4 (MIDI 62) can be played on D string (open) or G string (7 semitones up).
  auto positions = violin.getPositionsForPitch(62);
  EXPECT_GE(positions.size(), 2u);
}

TEST(ViolinModelTest, OutOfRangeReturnsEmpty) {
  ViolinModel violin;
  auto positions = violin.getPositionsForPitch(54);  // Below range
  EXPECT_TRUE(positions.empty());

  positions = violin.getPositionsForPitch(97);  // Above range
  EXPECT_TRUE(positions.empty());
}

TEST(ViolinModelTest, PositionsSortedByPreference) {
  ViolinModel violin;
  auto positions = violin.getPositionsForPitch(62);  // D4
  ASSERT_GE(positions.size(), 2u);

  // First position should have lowest position number (most comfortable).
  EXPECT_LE(positions[0].position, positions[1].position);
}

// ---------------------------------------------------------------------------
// Double stops
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, AdjacentOpenStringsDoubleStopFeasible) {
  ViolinModel violin;
  // G3 (string 0) + D4 (string 1).
  EXPECT_TRUE(violin.isDoubleStopFeasible(55, 62));
  // D4 (string 1) + A4 (string 2).
  EXPECT_TRUE(violin.isDoubleStopFeasible(62, 69));
  // A4 (string 2) + E5 (string 3).
  EXPECT_TRUE(violin.isDoubleStopFeasible(69, 76));
}

TEST(ViolinModelTest, SamePitchNotDoubleStop) {
  ViolinModel violin;
  EXPECT_FALSE(violin.isDoubleStopFeasible(72, 72));
}

TEST(ViolinModelTest, UnplayablePitchDoubleStopInfeasible) {
  ViolinModel violin;
  EXPECT_FALSE(violin.isDoubleStopFeasible(55, 127));
}

TEST(ViolinModelTest, DoubleStopCostFeasibleIsFinite) {
  ViolinModel violin;
  float cost = violin.doubleStopCost(55, 62);  // G3 + D4 (open adjacent)
  EXPECT_LT(cost, 1.0f);
  EXPECT_GE(cost, 0.0f);
}

TEST(ViolinModelTest, DoubleStopCostInfeasibleIsHigh) {
  ViolinModel violin;
  float cost = violin.doubleStopCost(55, 127);
  EXPECT_GT(cost, 1e5f);
}

TEST(ViolinModelTest, OpenStringDoubleStopCheaperThanFingered) {
  ViolinModel violin;
  float open_cost = violin.doubleStopCost(55, 62);  // G3 + D4 (open)
  float fingered_cost = violin.doubleStopCost(67, 74);  // G4 + D5 (fingered)
  EXPECT_LE(open_cost, fingered_cost);
}

// ---------------------------------------------------------------------------
// requiresArpeggiation
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, SingleNoteNoArpeggiation) {
  ViolinModel violin;
  EXPECT_FALSE(violin.requiresArpeggiation({72}));
}

TEST(ViolinModelTest, TwoNotesNoArpeggiation) {
  ViolinModel violin;
  EXPECT_FALSE(violin.requiresArpeggiation({55, 62}));
}

TEST(ViolinModelTest, ThreeNotesRequireArpeggiation) {
  ViolinModel violin;
  EXPECT_TRUE(violin.requiresArpeggiation({55, 62, 69}));
}

TEST(ViolinModelTest, FourNotesRequireArpeggiation) {
  ViolinModel violin;
  EXPECT_TRUE(violin.requiresArpeggiation({55, 62, 69, 76}));
}

// ---------------------------------------------------------------------------
// String crossing
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, SameStringZeroCrossingCost) {
  ViolinModel violin;
  EXPECT_FLOAT_EQ(violin.stringCrossingCost(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(violin.stringCrossingCost(3, 3), 0.0f);
}

TEST(ViolinModelTest, AdjacentStringCrossingLowCost) {
  ViolinModel violin;
  float cost = violin.stringCrossingCost(0, 1);
  EXPECT_GT(cost, 0.0f);
  EXPECT_LT(cost, 0.5f);
}

TEST(ViolinModelTest, SkippingStringsHigherCost) {
  ViolinModel violin;
  float adjacent_cost = violin.stringCrossingCost(0, 1);
  float skip_one_cost = violin.stringCrossingCost(0, 2);
  float skip_two_cost = violin.stringCrossingCost(0, 3);

  EXPECT_GT(skip_one_cost, adjacent_cost);
  EXPECT_GT(skip_two_cost, skip_one_cost);
}

TEST(ViolinModelTest, StringCrossingSymmetric) {
  ViolinModel violin;
  EXPECT_FLOAT_EQ(violin.stringCrossingCost(0, 2), violin.stringCrossingCost(2, 0));
  EXPECT_FLOAT_EQ(violin.stringCrossingCost(1, 3), violin.stringCrossingCost(3, 1));
}

TEST(ViolinModelTest, ViolinCrossingCheaperThanCello) {
  ViolinModel violin;
  CelloModel cello;
  // Adjacent string crossing should be cheaper on violin (flatter bridge).
  float violin_cost = violin.stringCrossingCost(0, 1);
  float cello_cost = cello.stringCrossingCost(0, 1);
  EXPECT_LE(violin_cost, cello_cost);
}

TEST(ViolinModelTest, InvalidStringCrossingHighCost) {
  ViolinModel violin;
  EXPECT_GT(violin.stringCrossingCost(4, 0), 1e5f);
  EXPECT_GT(violin.stringCrossingCost(0, 5), 1e5f);
}

// ---------------------------------------------------------------------------
// Bariolage
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, SupportsBariolage) {
  ViolinModel violin;
  EXPECT_TRUE(violin.supportsBariolage());
}

// ---------------------------------------------------------------------------
// High position
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, OpenStringsNotHighPosition) {
  ViolinModel violin;
  EXPECT_FALSE(violin.isHighPosition(55));  // G3
  EXPECT_FALSE(violin.isHighPosition(62));  // D4
  EXPECT_FALSE(violin.isHighPosition(69));  // A4
  EXPECT_FALSE(violin.isHighPosition(76));  // E5
}

TEST(ViolinModelTest, HighPitchesAreHighPosition) {
  ViolinModel violin;
  // Pitches well above the high-position threshold.
  EXPECT_TRUE(violin.isHighPosition(90));  // High on E string
  EXPECT_TRUE(violin.isHighPosition(96));  // C7 (highest note)
}

// ---------------------------------------------------------------------------
// calculateCost
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, OpenStringZeroCost) {
  ViolinModel violin;
  auto cost = violin.calculateCost(55);  // G3 (open G string)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_FLOAT_EQ(cost.total, 0.0f);
  EXPECT_FLOAT_EQ(cost.left_hand_cost, 0.0f);
}

TEST(ViolinModelTest, LowPositionLowCost) {
  ViolinModel violin;
  auto cost = violin.calculateCost(57);  // A3 (2 semitones above G3, 1st position)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.total, 0.0f);
  EXPECT_LT(cost.total, 0.5f);
}

TEST(ViolinModelTest, HighPositionHigherCost) {
  ViolinModel violin;
  auto cost_low = violin.calculateCost(57);   // Low position
  auto cost_high = violin.calculateCost(93);  // High on E string
  EXPECT_GT(cost_high.total, cost_low.total);
}

TEST(ViolinModelTest, UnplayablePitchHighCost) {
  ViolinModel violin;
  auto cost = violin.calculateCost(127);
  EXPECT_FALSE(cost.is_playable);
  EXPECT_GT(cost.total, 1e5f);
}

TEST(ViolinModelTest, ViolinShiftCostPerPositionCheaperThanCello) {
  // Violin has shorter strings, so position shifts cost less per position.
  ViolinModel violin;
  CelloModel cello;

  auto violin_state = violin.createInitialState();
  violin.updateState(violin_state, 55);  // Start on open G string
  auto cello_state = cello.createInitialState();
  cello.updateState(cello_state, 36);    // Start on open C string

  // Large shift on each instrument: shift cost should be lower on violin.
  // Violin: G3 (55) -> B4 (71), 16 semitones up on G string.
  auto violin_shift = violin.calculateTransitionCost(55, 71, violin_state);
  // Cello: C2 (36) -> E3 (52), 16 semitones up on C string.
  auto cello_shift = cello.calculateTransitionCost(36, 52, cello_state);

  EXPECT_LT(violin_shift.shift_cost, cello_shift.shift_cost);
}

// ---------------------------------------------------------------------------
// Transition cost
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, SameNoteTransitionLowCost) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  violin.updateState(state, 55);

  auto cost = violin.calculateTransitionCost(55, 55, state);
  EXPECT_TRUE(cost.is_playable);
  EXPECT_FLOAT_EQ(cost.string_crossing_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.shift_cost, 0.0f);
}

TEST(ViolinModelTest, AdjacentStringTransitionModestCost) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  violin.updateState(state, 55);  // G3 on G string

  auto cost = violin.calculateTransitionCost(55, 62, state);  // To D4 (open D string)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.string_crossing_cost, 0.0f);
}

TEST(ViolinModelTest, LargePositionShiftHighCost) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  violin.updateState(state, 57);  // A3 (1st position on G string)

  auto cost = violin.calculateTransitionCost(57, 93, state);  // Large shift up
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.shift_cost, 0.0f);
  EXPECT_GT(cost.total, 0.2f);
}

TEST(ViolinModelTest, UnplayableTransitionHighCost) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  auto cost = violin.calculateTransitionCost(55, 127, state);
  EXPECT_FALSE(cost.is_playable);
  EXPECT_GT(cost.total, 1e5f);
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

TEST(ViolinModelTest, InitialStateIsDownbowOnLowestString) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
  EXPECT_EQ(state.current_string, 0);
  EXPECT_EQ(state.current_position, 0);
  EXPECT_EQ(state.last_pitch, 0);
  EXPECT_FLOAT_EQ(state.fatigue, 0.0f);
}

TEST(ViolinModelTest, UpdateStateAlternatesBowDirection) {
  ViolinModel violin;
  auto state = violin.createInitialState();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);

  violin.updateState(state, 55);
  EXPECT_EQ(state.bow_direction, BowDirection::Up);

  violin.updateState(state, 57);
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
}

TEST(ViolinModelTest, UpdateStateTracksString) {
  ViolinModel violin;
  auto state = violin.createInitialState();

  violin.updateState(state, 76);  // E5 (open E string, idx 3)
  EXPECT_EQ(state.current_string, 3);

  violin.updateState(state, 55);  // G3 (open G string, idx 0)
  EXPECT_EQ(state.current_string, 0);
}

TEST(ViolinModelTest, UpdateStateTracksPitch) {
  ViolinModel violin;
  auto state = violin.createInitialState();

  violin.updateState(state, 72);
  EXPECT_EQ(state.last_pitch, 72);

  violin.updateState(state, 84);
  EXPECT_EQ(state.last_pitch, 84);
}

TEST(ViolinModelTest, ResetStateClearsAll) {
  ViolinModel violin;
  auto state = violin.createInitialState();

  violin.updateState(state, 72);
  violin.updateState(state, 84);
  EXPECT_NE(state.last_pitch, 0);

  state.reset();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
  EXPECT_EQ(state.current_string, 0);
  EXPECT_EQ(state.current_position, 0);
  EXPECT_EQ(state.last_pitch, 0);
  EXPECT_FLOAT_EQ(state.fatigue, 0.0f);
}

}  // namespace
}  // namespace bach
