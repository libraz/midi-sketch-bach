// Tests for counterpoint/melodic_context.h -- melodic quality scoring for
// voice-leading improvement in the CollisionResolver.

#include "counterpoint/melodic_context.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(MelodicContextTest, NoContext_ReturnsNeutral) {
  MelodicContext ctx;
  float score = MelodicContext::scoreMelodicQuality(ctx, 60);
  EXPECT_FLOAT_EQ(score, 0.5f);
}

TEST(MelodicContextTest, StepwiseMotion_HigherScore) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_count = 1;

  // Major 2nd up (stepwise) should score higher than perfect 5th up (leap).
  float step_score = MelodicContext::scoreMelodicQuality(ctx, 62);  // Major 2nd up
  float leap_score = MelodicContext::scoreMelodicQuality(ctx, 67);  // Perfect 5th up

  EXPECT_GT(step_score, leap_score);
}

TEST(MelodicContextTest, StepAfterLeap_HighestBonus) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 67;  // G4 (arrived by leap from C4)
  ctx.prev_pitches[1] = 60;  // C4
  ctx.prev_count = 2;
  ctx.prev_direction = 1;    // Was ascending

  // Step down from G4 (opposite direction after leap) should get bonus.
  // Rule 1: step-after-leap in opposite direction (+0.3)
  // Rule 2: stepwise motion (+0.2)
  // Total: 0.5 + 0.3 + 0.2 = 1.0 (clamped)
  float score = MelodicContext::scoreMelodicQuality(ctx, 65);  // F4: step down
  EXPECT_GT(score, 0.7f);
}

TEST(MelodicContextTest, SamePitchRepetition_Penalty) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_pitches[1] = 60;
  ctx.prev_count = 2;

  // 3rd consecutive repetition should be penalized vs stepwise motion.
  float repeat_score = MelodicContext::scoreMelodicQuality(ctx, 60);  // 3rd repeat
  float step_score = MelodicContext::scoreMelodicQuality(ctx, 62);    // Step up

  EXPECT_LT(repeat_score, step_score);
}

TEST(MelodicContextTest, TritoneLeap_Penalty) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_count = 1;

  // Tritone (augmented 4th) should score much lower than a step.
  float tritone_score = MelodicContext::scoreMelodicQuality(ctx, 66);  // Tritone
  float step_score = MelodicContext::scoreMelodicQuality(ctx, 62);     // Major 2nd

  EXPECT_LT(tritone_score, step_score);
}

TEST(MelodicContextTest, LeadingToneNonResolution_Penalty) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 71;  // B4 (leading tone in C major)
  ctx.prev_count = 1;
  ctx.is_leading_tone = true;

  // Resolving down to A4 (wrong) vs up to C5 (correct resolution).
  float wrong = MelodicContext::scoreMelodicQuality(ctx, 69);  // Down to A4
  float right = MelodicContext::scoreMelodicQuality(ctx, 72);  // Up to C5

  EXPECT_LT(wrong, right);
}

TEST(MelodicContextTest, ScoreClampedToRange) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_pitches[1] = 60;
  ctx.prev_count = 2;
  ctx.is_leading_tone = true;

  // Multiple penalties (tritone + repetition + leading tone non-resolution)
  // should not push the score below 0.0.
  float score = MelodicContext::scoreMelodicQuality(ctx, 66);  // Tritone
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

TEST(MelodicContextTest, ImperfectConsonance_SmallBonus) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_count = 1;

  // Minor 3rd (interval 3) should get imperfect consonance bonus.
  // Perfect 5th (interval 7) is not an imperfect consonance, gets no bonus.
  float minor_third = MelodicContext::scoreMelodicQuality(ctx, 63);
  float perfect_fifth = MelodicContext::scoreMelodicQuality(ctx, 67);

  EXPECT_GT(minor_third, perfect_fifth);
}

TEST(MelodicContextTest, LeadingToneCorrectResolution_Bonus) {
  MelodicContext ctx;
  ctx.prev_pitches[0] = 71;  // B4
  ctx.prev_count = 1;
  ctx.is_leading_tone = true;

  // Correct resolution: semitone up (interval=1, directed=+1).
  // Gets stepwise bonus (+0.2) and resolution bonus (+0.1).
  float score = MelodicContext::scoreMelodicQuality(ctx, 72);
  EXPECT_FLOAT_EQ(score, 0.5f + 0.2f + 0.1f);
}

TEST(MelodicContextTest, DefaultConstructor_AllZero) {
  MelodicContext ctx;
  EXPECT_EQ(ctx.prev_count, 0);
  EXPECT_EQ(ctx.prev_direction, 0);
  EXPECT_FALSE(ctx.is_leading_tone);
  EXPECT_EQ(ctx.prev_pitches[0], 0);
  EXPECT_EQ(ctx.prev_pitches[1], 0);
  EXPECT_EQ(ctx.prev_pitches[2], 0);
}

// ---------------------------------------------------------------------------
// PhraseGoal tests
// ---------------------------------------------------------------------------

TEST(MelodicContextTest, PhraseGoalDefaultBonus) {
  // Default PhraseGoal bonus weight should be 0.3f.
  PhraseGoal goal;
  EXPECT_FLOAT_EQ(goal.bonus, 0.3f);
}

TEST(MelodicContextTest, PhraseGoalBonusNearTarget) {
  // When the candidate pitch is close to the goal pitch and the tick is near
  // the target tick, the bonus should be high (close to goal.bonus).
  PhraseGoal goal;
  goal.target_pitch = 60;   // C4
  goal.target_tick = 1920;  // End of bar 1
  goal.bonus = 0.3f;

  // Pitch distance = 1 semitone, temporal factor = 1920/1920 = 1.0
  // pitch_factor = 1.0 - 1/12 = 11/12 ~ 0.9167
  // bonus = 0.3 * (11/12) * 1.0 ~ 0.275
  float bonus = computeGoalApproachBonus(61, 1920, goal);
  EXPECT_GT(bonus, 0.2f);
  EXPECT_LE(bonus, 0.3f);

  // Exact pitch match at target tick should yield max bonus.
  float exact_bonus = computeGoalApproachBonus(60, 1920, goal);
  EXPECT_FLOAT_EQ(exact_bonus, 0.3f);
}

TEST(MelodicContextTest, PhraseGoalBonusFarFromTarget) {
  // When the candidate pitch is far from the goal pitch (> octave),
  // the bonus should be zero regardless of timing.
  PhraseGoal goal;
  goal.target_pitch = 60;   // C4
  goal.target_tick = 1920;
  goal.bonus = 0.3f;

  // 13 semitones away (> octave) -- pitch_factor = 0.
  float far_bonus = computeGoalApproachBonus(73, 1920, goal);
  EXPECT_FLOAT_EQ(far_bonus, 0.0f);

  // Exactly 12 semitones away -- pitch_factor = 0 (at boundary).
  float octave_bonus = computeGoalApproachBonus(72, 1920, goal);
  EXPECT_FLOAT_EQ(octave_bonus, 0.0f);
}

TEST(MelodicContextTest, PhraseGoalBonusTemporalDecay) {
  // The bonus should increase as the tick approaches the target_tick.
  PhraseGoal goal;
  goal.target_pitch = 60;   // C4
  goal.target_tick = 1920;
  goal.bonus = 0.3f;

  // Same pitch at various times.
  float early_bonus = computeGoalApproachBonus(60, 480, goal);   // 25% through
  float mid_bonus = computeGoalApproachBonus(60, 960, goal);     // 50% through
  float late_bonus = computeGoalApproachBonus(60, 1440, goal);   // 75% through
  float at_target = computeGoalApproachBonus(60, 1920, goal);    // 100%

  // Temporal factor ramps linearly, so bonus should increase monotonically.
  EXPECT_LT(early_bonus, mid_bonus);
  EXPECT_LT(mid_bonus, late_bonus);
  EXPECT_LT(late_bonus, at_target);
  EXPECT_FLOAT_EQ(at_target, 0.3f);
}

TEST(MelodicContextTest, PhraseGoalBonusIntegrationWithScoring) {
  // The goal bonus should be additive to the existing melodic quality score.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 62;  // D4
  ctx.prev_count = 1;

  PhraseGoal goal;
  goal.target_pitch = 60;   // C4
  goal.target_tick = 1920;
  goal.bonus = 0.3f;

  // Score without goal.
  float base_score = MelodicContext::scoreMelodicQuality(ctx, 60);

  // Score with goal at the target tick (full temporal factor).
  float goal_score = MelodicContext::scoreMelodicQuality(ctx, 60, &goal, 1920);

  // The goal score should be higher by the full bonus amount (pitch distance = 0).
  EXPECT_GT(goal_score, base_score);
  // The difference should be approximately 0.3 (may be clamped at 1.0).
  float diff = goal_score - base_score;
  if (goal_score < 1.0f) {
    EXPECT_NEAR(diff, 0.3f, 0.01f);
  }
}

TEST(MelodicContextTest, PhraseGoalNullptrDoesNotAffectScore) {
  // Passing nullptr for goal should produce the same score as no goal.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_count = 1;

  float without_goal = MelodicContext::scoreMelodicQuality(ctx, 62);
  float with_nullptr = MelodicContext::scoreMelodicQuality(ctx, 62, nullptr, 960);

  EXPECT_FLOAT_EQ(without_goal, with_nullptr);
}

TEST(MelodicContextTest, PhraseGoalZeroTargetPitchNoBonus) {
  // A PhraseGoal with target_pitch=0 should return zero bonus (invalid target).
  PhraseGoal goal;
  goal.target_pitch = 0;
  goal.target_tick = 1920;
  goal.bonus = 0.3f;

  float bonus = computeGoalApproachBonus(60, 1920, goal);
  EXPECT_FLOAT_EQ(bonus, 0.0f);
}

TEST(MelodicContextTest, PhraseGoalZeroTargetTickNoBonus) {
  // A PhraseGoal with target_tick=0 should return zero bonus.
  PhraseGoal goal;
  goal.target_pitch = 60;
  goal.target_tick = 0;
  goal.bonus = 0.3f;

  float bonus = computeGoalApproachBonus(60, 1920, goal);
  EXPECT_FLOAT_EQ(bonus, 0.0f);
}

TEST(MelodicContextTest, PhraseGoalNoContextWithGoal) {
  // With no melodic context (prev_count=0) but with a goal, the base should
  // be 0.5 plus the goal bonus, clamped to [0.0, 1.0].
  MelodicContext ctx;  // prev_count = 0

  PhraseGoal goal;
  goal.target_pitch = 60;
  goal.target_tick = 1920;
  goal.bonus = 0.3f;

  // Exact match at target tick: base 0.5 + bonus 0.3 = 0.8
  float score = MelodicContext::scoreMelodicQuality(ctx, 60, &goal, 1920);
  EXPECT_FLOAT_EQ(score, 0.8f);
}

}  // namespace
}  // namespace bach
