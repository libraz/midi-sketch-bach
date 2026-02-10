// Tests for CelloModel bowed string instrument.

#include "instrument/bowed/cello_model.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Construction and basic properties
// ---------------------------------------------------------------------------

TEST(CelloModelTest, HasFourStrings) {
  CelloModel cello;
  EXPECT_EQ(cello.getStringCount(), 4);
}

TEST(CelloModelTest, TuningIsCGDA) {
  CelloModel cello;
  const auto& tuning = cello.getTuning();
  ASSERT_EQ(tuning.size(), 4u);
  EXPECT_EQ(tuning[0], 36);  // C2
  EXPECT_EQ(tuning[1], 43);  // G2
  EXPECT_EQ(tuning[2], 50);  // D3
  EXPECT_EQ(tuning[3], 57);  // A3
}

TEST(CelloModelTest, RangeC2ToA5) {
  CelloModel cello;
  EXPECT_EQ(cello.getLowestPitch(), 36);   // C2
  EXPECT_EQ(cello.getHighestPitch(), 81);  // A5
}

// ---------------------------------------------------------------------------
// isPitchPlayable
// ---------------------------------------------------------------------------

TEST(CelloModelTest, OpenStringsArePlayable) {
  CelloModel cello;
  EXPECT_TRUE(cello.isPitchPlayable(36));  // C2
  EXPECT_TRUE(cello.isPitchPlayable(43));  // G2
  EXPECT_TRUE(cello.isPitchPlayable(50));  // D3
  EXPECT_TRUE(cello.isPitchPlayable(57));  // A3
}

TEST(CelloModelTest, RangeBoundariesPlayable) {
  CelloModel cello;
  EXPECT_TRUE(cello.isPitchPlayable(36));  // C2 (lowest)
  EXPECT_TRUE(cello.isPitchPlayable(81));  // A5 (highest)
}

TEST(CelloModelTest, OutOfRangeNotPlayable) {
  CelloModel cello;
  EXPECT_FALSE(cello.isPitchPlayable(35));   // Below C2
  EXPECT_FALSE(cello.isPitchPlayable(82));   // Above A5
  EXPECT_FALSE(cello.isPitchPlayable(0));    // Far below
  EXPECT_FALSE(cello.isPitchPlayable(127));  // Far above
}

TEST(CelloModelTest, MiddleOfRangePlayable) {
  CelloModel cello;
  EXPECT_TRUE(cello.isPitchPlayable(60));  // C4
  EXPECT_TRUE(cello.isPitchPlayable(48));  // C3
  EXPECT_TRUE(cello.isPitchPlayable(72));  // C5
}

// ---------------------------------------------------------------------------
// isOpenString
// ---------------------------------------------------------------------------

TEST(CelloModelTest, OpenStringPitchesIdentified) {
  CelloModel cello;
  EXPECT_TRUE(cello.isOpenString(36));   // C2
  EXPECT_TRUE(cello.isOpenString(43));   // G2
  EXPECT_TRUE(cello.isOpenString(50));   // D3
  EXPECT_TRUE(cello.isOpenString(57));   // A3
}

TEST(CelloModelTest, NonOpenStringPitchesRejected) {
  CelloModel cello;
  EXPECT_FALSE(cello.isOpenString(37));  // C#2
  EXPECT_FALSE(cello.isOpenString(60));  // C4
  EXPECT_FALSE(cello.isOpenString(0));
  EXPECT_FALSE(cello.isOpenString(127));
}

// ---------------------------------------------------------------------------
// getPositionsForPitch
// ---------------------------------------------------------------------------

TEST(CelloModelTest, OpenStringHasPositionZero) {
  CelloModel cello;
  auto positions = cello.getPositionsForPitch(36);  // C2 (open C string)
  ASSERT_FALSE(positions.empty());

  // Should have at least one position with position=0 (open).
  bool found_open = false;
  for (const auto& pos : positions) {
    if (pos.position == 0) {
      found_open = true;
      EXPECT_EQ(pos.string_idx, 0);  // C string
      EXPECT_EQ(pos.pitch_offset, 0);
    }
  }
  EXPECT_TRUE(found_open);
}

TEST(CelloModelTest, MultipleFingerings) {
  CelloModel cello;
  // D3 (MIDI 50) can be played on D string (open) or G string (7th fret).
  auto positions = cello.getPositionsForPitch(50);
  EXPECT_GE(positions.size(), 2u);
}

TEST(CelloModelTest, OutOfRangeReturnsEmpty) {
  CelloModel cello;
  auto positions = cello.getPositionsForPitch(35);  // Below range
  EXPECT_TRUE(positions.empty());

  positions = cello.getPositionsForPitch(82);  // Above range
  EXPECT_TRUE(positions.empty());
}

TEST(CelloModelTest, PositionsSortedByPreference) {
  CelloModel cello;
  auto positions = cello.getPositionsForPitch(50);  // D3
  ASSERT_GE(positions.size(), 2u);

  // First position should have lowest position number (most comfortable).
  EXPECT_LE(positions[0].position, positions[1].position);
}

// ---------------------------------------------------------------------------
// Double stops
// ---------------------------------------------------------------------------

TEST(CelloModelTest, AdjacentOpenStringsDoubleStopFeasible) {
  CelloModel cello;
  // C2 (string 0) + G2 (string 1) -- adjacent open strings.
  EXPECT_TRUE(cello.isDoubleStopFeasible(36, 43));
  // G2 (string 1) + D3 (string 2).
  EXPECT_TRUE(cello.isDoubleStopFeasible(43, 50));
  // D3 (string 2) + A3 (string 3).
  EXPECT_TRUE(cello.isDoubleStopFeasible(50, 57));
}

TEST(CelloModelTest, SamePitchNotDoubleStop) {
  CelloModel cello;
  EXPECT_FALSE(cello.isDoubleStopFeasible(60, 60));
}

TEST(CelloModelTest, UnplayablePitchDoubleStopInfeasible) {
  CelloModel cello;
  EXPECT_FALSE(cello.isDoubleStopFeasible(36, 127));  // 127 not playable
}

TEST(CelloModelTest, DoubleStopCostFeasibleIsFinite) {
  CelloModel cello;
  float cost = cello.doubleStopCost(36, 43);  // C2 + G2 (open adjacent)
  EXPECT_LT(cost, 1.0f);
  EXPECT_GE(cost, 0.0f);
}

TEST(CelloModelTest, DoubleStopCostInfeasibleIsHigh) {
  CelloModel cello;
  float cost = cello.doubleStopCost(36, 127);
  EXPECT_GT(cost, 1e5f);
}

TEST(CelloModelTest, OpenStringDoubleStopCheaperThanFingered) {
  CelloModel cello;
  // Open string double stop.
  float open_cost = cello.doubleStopCost(36, 43);  // C2 + G2 (open)
  // Fingered double stop in higher position.
  float fingered_cost = cello.doubleStopCost(48, 55);  // C3 + G3 (fingered)
  EXPECT_LE(open_cost, fingered_cost);
}

// ---------------------------------------------------------------------------
// requiresArpeggiation
// ---------------------------------------------------------------------------

TEST(CelloModelTest, SingleNoteNoArpeggiation) {
  CelloModel cello;
  EXPECT_FALSE(cello.requiresArpeggiation({60}));
}

TEST(CelloModelTest, TwoNotesNoArpeggiation) {
  CelloModel cello;
  EXPECT_FALSE(cello.requiresArpeggiation({36, 43}));
}

TEST(CelloModelTest, ThreeNotesRequireArpeggiation) {
  CelloModel cello;
  EXPECT_TRUE(cello.requiresArpeggiation({36, 43, 50}));
}

TEST(CelloModelTest, FourNotesRequireArpeggiation) {
  CelloModel cello;
  EXPECT_TRUE(cello.requiresArpeggiation({36, 43, 50, 57}));
}

// ---------------------------------------------------------------------------
// String crossing
// ---------------------------------------------------------------------------

TEST(CelloModelTest, SameStringZeroCrossingCost) {
  CelloModel cello;
  EXPECT_FLOAT_EQ(cello.stringCrossingCost(0, 0), 0.0f);
  EXPECT_FLOAT_EQ(cello.stringCrossingCost(2, 2), 0.0f);
}

TEST(CelloModelTest, AdjacentStringCrossingLowCost) {
  CelloModel cello;
  float cost = cello.stringCrossingCost(0, 1);
  EXPECT_GT(cost, 0.0f);
  EXPECT_LT(cost, 0.5f);
}

TEST(CelloModelTest, SkippingStringsHigherCost) {
  CelloModel cello;
  float adjacent_cost = cello.stringCrossingCost(0, 1);
  float skip_one_cost = cello.stringCrossingCost(0, 2);
  float skip_two_cost = cello.stringCrossingCost(0, 3);

  EXPECT_GT(skip_one_cost, adjacent_cost);
  EXPECT_GT(skip_two_cost, skip_one_cost);
}

TEST(CelloModelTest, StringCrossingSymmetric) {
  CelloModel cello;
  EXPECT_FLOAT_EQ(cello.stringCrossingCost(0, 2), cello.stringCrossingCost(2, 0));
  EXPECT_FLOAT_EQ(cello.stringCrossingCost(1, 3), cello.stringCrossingCost(3, 1));
}

TEST(CelloModelTest, InvalidStringCrossingHighCost) {
  CelloModel cello;
  EXPECT_GT(cello.stringCrossingCost(4, 0), 1e5f);
  EXPECT_GT(cello.stringCrossingCost(0, 5), 1e5f);
}

// ---------------------------------------------------------------------------
// Bariolage
// ---------------------------------------------------------------------------

TEST(CelloModelTest, SupportsBariolage) {
  CelloModel cello;
  EXPECT_TRUE(cello.supportsBariolage());
}

// ---------------------------------------------------------------------------
// Thumb position
// ---------------------------------------------------------------------------

TEST(CelloModelTest, LowPitchesNoThumbPosition) {
  CelloModel cello;
  EXPECT_FALSE(cello.requiresThumbPosition(36));  // C2
  EXPECT_FALSE(cello.requiresThumbPosition(43));  // G2
  EXPECT_FALSE(cello.requiresThumbPosition(50));  // D3
}

TEST(CelloModelTest, HighPitchesRequireThumbPosition) {
  CelloModel cello;
  // Pitches at or above the thumb position threshold on the highest string.
  EXPECT_TRUE(cello.requiresThumbPosition(75));  // Well above A3 string threshold
  EXPECT_TRUE(cello.requiresThumbPosition(81));  // A5 (highest note)
}

// ---------------------------------------------------------------------------
// calculateCost
// ---------------------------------------------------------------------------

TEST(CelloModelTest, OpenStringZeroCost) {
  CelloModel cello;
  auto cost = cello.calculateCost(36);  // C2 (open C string)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_FLOAT_EQ(cost.total, 0.0f);
  EXPECT_FLOAT_EQ(cost.left_hand_cost, 0.0f);
}

TEST(CelloModelTest, LowPositionLowCost) {
  CelloModel cello;
  auto cost = cello.calculateCost(38);  // D2 (2 semitones above C2, 1st position)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.total, 0.0f);
  EXPECT_LT(cost.total, 0.5f);
}

TEST(CelloModelTest, HighPositionHigherCost) {
  CelloModel cello;
  auto cost_low = cello.calculateCost(38);   // D2 (low position)
  auto cost_high = cello.calculateCost(75);  // High on A string
  EXPECT_GT(cost_high.total, cost_low.total);
}

TEST(CelloModelTest, UnplayablePitchHighCost) {
  CelloModel cello;
  auto cost = cello.calculateCost(127);
  EXPECT_FALSE(cost.is_playable);
  EXPECT_GT(cost.total, 1e5f);
}

// ---------------------------------------------------------------------------
// Transition cost
// ---------------------------------------------------------------------------

TEST(CelloModelTest, SameNoteTransitionLowCost) {
  CelloModel cello;
  auto state = cello.createInitialState();
  cello.updateState(state, 36);

  auto cost = cello.calculateTransitionCost(36, 36, state);
  EXPECT_TRUE(cost.is_playable);
  EXPECT_FLOAT_EQ(cost.string_crossing_cost, 0.0f);
  EXPECT_FLOAT_EQ(cost.shift_cost, 0.0f);
}

TEST(CelloModelTest, AdjacentStringTransitionModestCost) {
  CelloModel cello;
  auto state = cello.createInitialState();
  cello.updateState(state, 36);  // C2 on C string

  auto cost = cello.calculateTransitionCost(36, 43, state);  // To G2 (open G string)
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.string_crossing_cost, 0.0f);
}

TEST(CelloModelTest, LargePositionShiftHighCost) {
  CelloModel cello;
  auto state = cello.createInitialState();
  cello.updateState(state, 38);  // D2 (1st position on C string)

  auto cost = cello.calculateTransitionCost(38, 75, state);  // Large shift up
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.shift_cost, 0.0f);
  EXPECT_GT(cost.total, 0.3f);
}

TEST(CelloModelTest, UnplayableTransitionHighCost) {
  CelloModel cello;
  auto state = cello.createInitialState();
  auto cost = cello.calculateTransitionCost(36, 127, state);
  EXPECT_FALSE(cost.is_playable);
  EXPECT_GT(cost.total, 1e5f);
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

TEST(CelloModelTest, InitialStateIsDownbowOnLowestString) {
  CelloModel cello;
  auto state = cello.createInitialState();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
  EXPECT_EQ(state.current_string, 0);
  EXPECT_EQ(state.current_position, 0);
  EXPECT_EQ(state.last_pitch, 0);
  EXPECT_FLOAT_EQ(state.fatigue, 0.0f);
}

TEST(CelloModelTest, UpdateStateAlternatesBowDirection) {
  CelloModel cello;
  auto state = cello.createInitialState();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);

  cello.updateState(state, 36);
  EXPECT_EQ(state.bow_direction, BowDirection::Up);

  cello.updateState(state, 38);
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
}

TEST(CelloModelTest, UpdateStateTracksString) {
  CelloModel cello;
  auto state = cello.createInitialState();

  cello.updateState(state, 57);  // A3 (open A string, idx 3)
  EXPECT_EQ(state.current_string, 3);

  cello.updateState(state, 36);  // C2 (open C string, idx 0)
  EXPECT_EQ(state.current_string, 0);
}

TEST(CelloModelTest, UpdateStateTracksPitch) {
  CelloModel cello;
  auto state = cello.createInitialState();

  cello.updateState(state, 60);
  EXPECT_EQ(state.last_pitch, 60);

  cello.updateState(state, 72);
  EXPECT_EQ(state.last_pitch, 72);
}

TEST(CelloModelTest, ResetStateClearsAll) {
  CelloModel cello;
  auto state = cello.createInitialState();

  // Modify state.
  cello.updateState(state, 60);
  cello.updateState(state, 72);
  EXPECT_NE(state.last_pitch, 0);

  // Reset.
  state.reset();
  EXPECT_EQ(state.bow_direction, BowDirection::Down);
  EXPECT_EQ(state.current_string, 0);
  EXPECT_EQ(state.current_position, 0);
  EXPECT_EQ(state.last_pitch, 0);
  EXPECT_FLOAT_EQ(state.fatigue, 0.0f);
}

// ---------------------------------------------------------------------------
// BowDirection string conversion
// ---------------------------------------------------------------------------

TEST(BowDirectionTest, ToStringReturnsCorrectNames) {
  EXPECT_STREQ(bowDirectionToString(BowDirection::Down), "Down");
  EXPECT_STREQ(bowDirectionToString(BowDirection::Up), "Up");
}

}  // namespace
}  // namespace bach
