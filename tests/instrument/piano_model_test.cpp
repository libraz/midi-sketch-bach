// Tests for PianoModel -- 88-key piano instrument model.

#include "instrument/keyboard/piano_model.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(PianoModelTest, DefaultConstructionUsesIntermediateConstraints) {
  PianoModel piano;
  auto expected_span = KeyboardSpanConstraints::intermediate();
  auto expected_physics = KeyboardHandPhysics::intermediate();

  const auto& span = piano.getSpanConstraints();
  EXPECT_EQ(span.normal_span, expected_span.normal_span);
  EXPECT_EQ(span.max_span, expected_span.max_span);
  EXPECT_EQ(span.max_notes, expected_span.max_notes);

  const auto& physics = piano.getHandPhysics();
  EXPECT_FLOAT_EQ(physics.jump_cost_per_semitone, expected_physics.jump_cost_per_semitone);
  EXPECT_FLOAT_EQ(physics.stretch_cost_per_semitone, expected_physics.stretch_cost_per_semitone);
  EXPECT_FLOAT_EQ(physics.cross_over_cost, expected_physics.cross_over_cost);
  EXPECT_FLOAT_EQ(physics.repeated_note_cost, expected_physics.repeated_note_cost);
  EXPECT_FLOAT_EQ(physics.fatigue_recovery_rate, expected_physics.fatigue_recovery_rate);
}

TEST(PianoModelTest, CustomConstraintsAreStored) {
  auto span = KeyboardSpanConstraints::virtuoso();
  auto physics = KeyboardHandPhysics::beginner();

  PianoModel piano(span, physics);

  EXPECT_EQ(piano.getSpanConstraints().normal_span, span.normal_span);
  EXPECT_EQ(piano.getSpanConstraints().max_span, span.max_span);
  EXPECT_EQ(piano.getSpanConstraints().max_notes, span.max_notes);
  EXPECT_FLOAT_EQ(piano.getHandPhysics().jump_cost_per_semitone,
                   physics.jump_cost_per_semitone);
}

TEST(PianoModelTest, ConstructWithAllSkillLevels) {
  // Verify that all preset combinations construct without error.
  PianoModel beginner(KeyboardSpanConstraints::beginner(), KeyboardHandPhysics::beginner());
  PianoModel intermediate(KeyboardSpanConstraints::intermediate(),
                          KeyboardHandPhysics::intermediate());
  PianoModel advanced(KeyboardSpanConstraints::advanced(), KeyboardHandPhysics::advanced());
  PianoModel virtuoso(KeyboardSpanConstraints::virtuoso(), KeyboardHandPhysics::virtuoso());

  EXPECT_EQ(beginner.getSpanConstraints().normal_span, 7);
  EXPECT_EQ(intermediate.getSpanConstraints().normal_span, 9);
  EXPECT_EQ(advanced.getSpanConstraints().normal_span, 10);
  EXPECT_EQ(virtuoso.getSpanConstraints().normal_span, 12);
}

// ---------------------------------------------------------------------------
// Pitch range
// ---------------------------------------------------------------------------

TEST(PianoModelTest, LowestPitchIsA0) {
  PianoModel piano;
  EXPECT_EQ(piano.getLowestPitch(), 21);  // A0 = MIDI 21
}

TEST(PianoModelTest, HighestPitchIsC8) {
  PianoModel piano;
  EXPECT_EQ(piano.getHighestPitch(), 108);  // C8 = MIDI 108
}

TEST(PianoModelTest, PitchInRangeBoundaries) {
  PianoModel piano;

  // Boundary: lowest and highest valid pitches.
  EXPECT_TRUE(piano.isPitchInRange(21));   // A0
  EXPECT_TRUE(piano.isPitchInRange(108));  // C8

  // Just inside the range.
  EXPECT_TRUE(piano.isPitchInRange(22));   // A#0
  EXPECT_TRUE(piano.isPitchInRange(107));  // B7

  // Just outside the range.
  EXPECT_FALSE(piano.isPitchInRange(20));  // Ab0 (below A0)
  EXPECT_FALSE(piano.isPitchInRange(109)); // C#8 (above C8)
}

TEST(PianoModelTest, PitchInRangeExtremes) {
  PianoModel piano;

  EXPECT_FALSE(piano.isPitchInRange(0));    // Far below
  EXPECT_FALSE(piano.isPitchInRange(127));  // Far above
}

TEST(PianoModelTest, PitchInRangeMiddleC) {
  PianoModel piano;
  EXPECT_TRUE(piano.isPitchInRange(60));  // C4 (middle C)
}

// ---------------------------------------------------------------------------
// Velocity sensitivity -- piano IS velocity-sensitive (unlike organ)
// ---------------------------------------------------------------------------

// Note: PianoModel does not expose isVelocitySensitive() directly,
// but we can verify it is NOT an organ (which has velocity fixed at 80).
// Instead we test that the model's range differs from organ defaults.

TEST(PianoModelTest, RangeCoversFullPiano88Keys) {
  PianoModel piano;
  // 88 keys: A0 (21) through C8 (108), inclusive = 88 notes.
  int range = piano.getHighestPitch() - piano.getLowestPitch() + 1;
  EXPECT_EQ(range, 88);
}

// ---------------------------------------------------------------------------
// isPlayableByOneHand
// ---------------------------------------------------------------------------

TEST(PianoModelTest, EmptyPitchesPlayableByOneHand) {
  PianoModel piano;
  EXPECT_TRUE(piano.isPlayableByOneHand({}, Hand::Left));
  EXPECT_TRUE(piano.isPlayableByOneHand({}, Hand::Right));
}

TEST(PianoModelTest, SingleNotePlayableByOneHand) {
  PianoModel piano;
  EXPECT_TRUE(piano.isPlayableByOneHand({60}, Hand::Left));
  EXPECT_TRUE(piano.isPlayableByOneHand({60}, Hand::Right));
}

TEST(PianoModelTest, OctavePlayableByOneHandIntermediate) {
  // Intermediate max_span = 11 semitones, so a full octave (12) is NOT playable.
  PianoModel piano;
  EXPECT_FALSE(piano.isPlayableByOneHand({60, 72}, Hand::Right));
}

TEST(PianoModelTest, OctavePlayableByOneHandAdvanced) {
  // Advanced max_span = 12, so octave (12 semitones) IS playable.
  PianoModel piano(KeyboardSpanConstraints::advanced(), KeyboardHandPhysics::advanced());
  EXPECT_TRUE(piano.isPlayableByOneHand({60, 72}, Hand::Right));
}

TEST(PianoModelTest, TooManyNotesForOneHand) {
  // Default max_notes = 5 (intermediate), so 6 notes should fail.
  PianoModel piano;
  std::vector<uint8_t> six_notes = {60, 61, 62, 63, 64, 65};
  EXPECT_FALSE(piano.isPlayableByOneHand(six_notes, Hand::Right));
}

TEST(PianoModelTest, OutOfRangePitchNotPlayable) {
  PianoModel piano;
  // Pitch 10 is below A0 (21).
  EXPECT_FALSE(piano.isPlayableByOneHand({10}, Hand::Left));
  // Pitch 120 is above C8 (108).
  EXPECT_FALSE(piano.isPlayableByOneHand({120}, Hand::Right));
}

// ---------------------------------------------------------------------------
// isVoicingPlayable
// ---------------------------------------------------------------------------

TEST(PianoModelTest, EmptyVoicingIsPlayable) {
  PianoModel piano;
  EXPECT_TRUE(piano.isVoicingPlayable({}));
}

TEST(PianoModelTest, CMajorTriadPlayable) {
  PianoModel piano;
  // C4-E4-G4 (60-64-67): span = 7 semitones, easily playable by one hand.
  EXPECT_TRUE(piano.isVoicingPlayable({60, 64, 67}));
}

TEST(PianoModelTest, WideVoicingPlayableWithTwoHands) {
  PianoModel piano;
  // C3 and C5 (48, 72): each hand takes one note.
  EXPECT_TRUE(piano.isVoicingPlayable({48, 72}));
}

TEST(PianoModelTest, OutOfRangePitchInVoicingNotPlayable) {
  PianoModel piano;
  // MIDI 10 is below the piano range.
  EXPECT_FALSE(piano.isVoicingPlayable({10, 60}));
}

// ---------------------------------------------------------------------------
// assignHands
// ---------------------------------------------------------------------------

TEST(PianoModelTest, AssignHandsEmptyIsValid) {
  PianoModel piano;
  auto assignment = piano.assignHands({});
  EXPECT_TRUE(assignment.is_valid);
  EXPECT_TRUE(assignment.left_pitches.empty());
  EXPECT_TRUE(assignment.right_pitches.empty());
}

TEST(PianoModelTest, AssignHandsSingleLowNote) {
  PianoModel piano;
  // A single note below middle C should go to left hand.
  auto assignment = piano.assignHands({48});  // C3
  EXPECT_TRUE(assignment.is_valid);
  EXPECT_EQ(assignment.left_pitches.size(), 1u);
  EXPECT_TRUE(assignment.right_pitches.empty());
}

TEST(PianoModelTest, AssignHandsSingleHighNote) {
  PianoModel piano;
  // A single note at or above middle C should go to right hand.
  auto assignment = piano.assignHands({72});  // C5
  EXPECT_TRUE(assignment.is_valid);
  EXPECT_TRUE(assignment.left_pitches.empty());
  EXPECT_EQ(assignment.right_pitches.size(), 1u);
}

TEST(PianoModelTest, AssignHandsOutOfRangeIsInvalid) {
  PianoModel piano;
  auto assignment = piano.assignHands({10});  // Below piano range
  EXPECT_FALSE(assignment.is_valid);
}

// ---------------------------------------------------------------------------
// calculateTransitionCost
// ---------------------------------------------------------------------------

TEST(PianoModelTest, TransitionCostEmptyIsZero) {
  PianoModel piano;
  KeyboardState state;
  state.reset();

  auto cost = piano.calculateTransitionCost(state, {});
  EXPECT_TRUE(cost.is_playable);
  EXPECT_FLOAT_EQ(cost.total, 0.0f);
}

TEST(PianoModelTest, TransitionCostFromRestPosition) {
  PianoModel piano;
  KeyboardState state;
  state.reset();  // Left at C3 (48), Right at C5 (72)

  // Play middle C (60): assigned to right hand, moderate travel from C5.
  auto cost = piano.calculateTransitionCost(state, {60});
  EXPECT_TRUE(cost.is_playable);
  EXPECT_GT(cost.total, 0.0f);
}

TEST(PianoModelTest, TransitionCostOutOfRangeNotPlayable) {
  PianoModel piano;
  KeyboardState state;
  state.reset();

  auto cost = piano.calculateTransitionCost(state, {10});  // Below range
  EXPECT_FALSE(cost.is_playable);
}

// ---------------------------------------------------------------------------
// suggestPlayableVoicing
// ---------------------------------------------------------------------------

TEST(PianoModelTest, SuggestPlayableVoicingEmptyReturnsEmpty) {
  PianoModel piano;
  auto result = piano.suggestPlayableVoicing({});
  EXPECT_TRUE(result.empty());
}

TEST(PianoModelTest, SuggestPlayableVoicingAlreadyPlayable) {
  PianoModel piano;
  // A simple triad that is already playable.
  std::vector<uint8_t> triad = {60, 64, 67};
  auto result = piano.suggestPlayableVoicing(triad);
  EXPECT_FALSE(result.empty());
  EXPECT_TRUE(piano.isVoicingPlayable(result));
}

TEST(PianoModelTest, SuggestPlayableVoicingClampsOutOfRange) {
  PianoModel piano;
  // Pitch 5 is below A0 (21), pitch 125 is above C8 (108).
  // suggestPlayableVoicing should clamp and return something playable.
  auto result = piano.suggestPlayableVoicing({5, 125});
  EXPECT_FALSE(result.empty());
  for (uint8_t pitch : result) {
    EXPECT_TRUE(piano.isPitchInRange(pitch));
  }
}

// ---------------------------------------------------------------------------
// Default properties compared to other instruments
// ---------------------------------------------------------------------------

TEST(PianoModelTest, PianoRangeIsWiderThanOrganGreatManual) {
  PianoModel piano;
  // Organ Great: C2 (36) to C6 (96), Piano: A0 (21) to C8 (108).
  EXPECT_LT(piano.getLowestPitch(), 36);
  EXPECT_GT(piano.getHighestPitch(), 96);
}

TEST(PianoModelTest, SpanConstraintsSkillLevelsAreOrdered) {
  // Verify that higher skill levels have equal or larger spans.
  auto beginner = KeyboardSpanConstraints::beginner();
  auto intermediate = KeyboardSpanConstraints::intermediate();
  auto advanced = KeyboardSpanConstraints::advanced();
  auto virtuoso = KeyboardSpanConstraints::virtuoso();

  EXPECT_LE(beginner.normal_span, intermediate.normal_span);
  EXPECT_LE(intermediate.normal_span, advanced.normal_span);
  EXPECT_LE(advanced.normal_span, virtuoso.normal_span);

  EXPECT_LE(beginner.max_span, intermediate.max_span);
  EXPECT_LE(intermediate.max_span, advanced.max_span);
  EXPECT_LE(advanced.max_span, virtuoso.max_span);
}

TEST(PianoModelTest, HandPhysicsSkillLevelsCostsDecrease) {
  // Higher skill levels should have lower (or equal) movement costs.
  auto beginner = KeyboardHandPhysics::beginner();
  auto intermediate = KeyboardHandPhysics::intermediate();
  auto advanced = KeyboardHandPhysics::advanced();
  auto virtuoso = KeyboardHandPhysics::virtuoso();

  EXPECT_GE(beginner.jump_cost_per_semitone, intermediate.jump_cost_per_semitone);
  EXPECT_GE(intermediate.jump_cost_per_semitone, advanced.jump_cost_per_semitone);
  EXPECT_GE(advanced.jump_cost_per_semitone, virtuoso.jump_cost_per_semitone);

  EXPECT_GE(beginner.stretch_cost_per_semitone, intermediate.stretch_cost_per_semitone);
  EXPECT_GE(intermediate.stretch_cost_per_semitone, advanced.stretch_cost_per_semitone);
  EXPECT_GE(advanced.stretch_cost_per_semitone, virtuoso.stretch_cost_per_semitone);
}

}  // namespace
}  // namespace bach
