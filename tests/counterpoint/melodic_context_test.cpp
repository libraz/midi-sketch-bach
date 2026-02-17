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
  // Gets stepwise bonus (+0.1) and resolution bonus (+0.1).
  float score = MelodicContext::scoreMelodicQuality(ctx, 72);
  EXPECT_FLOAT_EQ(score, 0.5f + 0.1f + 0.1f);
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

// ---------------------------------------------------------------------------
// Rule 8: Unresolved leap penalty / resolution bonus
// ---------------------------------------------------------------------------

TEST(MelodicContextTest, Rule8_CorrectResolution_Bonus) {
  // Leap up (C4->G4 = 7 semitones), then step down (resolution).
  MelodicContext ctx;
  ctx.prev_pitches[0] = 67;  // G4 (most recent)
  ctx.prev_pitches[1] = 60;  // C4 (before that)
  ctx.prev_count = 2;
  ctx.prev_direction = 1;    // Was ascending
  ctx.leap_needs_resolution = true;

  // F4 (step down = correct resolution)
  float resolved = MelodicContext::scoreMelodicQuality(ctx, 65);
  // D5 (leap up = wrong, consecutive leaps)
  float unresolved = MelodicContext::scoreMelodicQuality(ctx, 74);

  EXPECT_GT(resolved, unresolved);
  // Resolution should get strong bonus: base 0.5 + step_after_leap 0.3 + stepwise 0.2
  // + resolution 0.4 = 1.4 -> clamped to 1.0
  EXPECT_FLOAT_EQ(resolved, 1.0f);
}

TEST(MelodicContextTest, Rule8_ConsecutiveLeaps_Penalty) {
  // Leap up (C4->G4), then another leap up (should be penalized).
  MelodicContext ctx;
  ctx.prev_pitches[0] = 67;  // G4
  ctx.prev_pitches[1] = 60;  // C4
  ctx.prev_count = 2;
  ctx.prev_direction = 1;
  ctx.leap_needs_resolution = true;

  // C5 (another leap up = consecutive leaps)
  float score = MelodicContext::scoreMelodicQuality(ctx, 72);
  // Should be penalized: base 0.5 - 0.4 (consecutive leap) = 0.1
  EXPECT_LT(score, 0.5f);
}

TEST(MelodicContextTest, Rule8_NoLeapNoEffect) {
  // No leap pending, Rule 8 should not affect score.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 62;  // D4
  ctx.prev_pitches[1] = 60;  // C4
  ctx.prev_count = 2;
  ctx.prev_direction = 1;
  ctx.leap_needs_resolution = false;  // No leap to resolve

  float score_step = MelodicContext::scoreMelodicQuality(ctx, 64);  // E4 (step up)
  // Should get normal stepwise bonus but no Rule 8 bonus.
  EXPECT_GT(score_step, 0.5f);
  EXPECT_LT(score_step, 1.0f);
}

TEST(MelodicContextTest, Rule8_SameDirectionStep_MildPenalty) {
  // Leap down (G3->C3), then step down (same direction = mild penalty).
  MelodicContext ctx;
  ctx.prev_pitches[0] = 48;  // C3
  ctx.prev_pitches[1] = 55;  // G3
  ctx.prev_count = 2;
  ctx.prev_direction = -1;   // Was descending
  ctx.leap_needs_resolution = true;

  // Step down (same direction as leap)
  float same_dir = MelodicContext::scoreMelodicQuality(ctx, 46);  // Bb2
  // Step up (opposite direction = correct resolution)
  float opposite_dir = MelodicContext::scoreMelodicQuality(ctx, 50);  // D3

  EXPECT_GT(opposite_dir, same_dir);
}

TEST(MelodicContextTest, LeapNeedsResolution_DefaultFalse) {
  MelodicContext ctx;
  EXPECT_FALSE(ctx.leap_needs_resolution);
}

// ---------------------------------------------------------------------------
// Rule 4: Graduated same-pitch repetition penalty
// ---------------------------------------------------------------------------

TEST(MelodicContextTest, Rule4_FirstRepetition_MildPenalty) {
  // First repetition (2 consecutive same pitches) gets only -0.1.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_count = 1;

  float repeat_score = MelodicContext::scoreMelodicQuality(ctx, 60);
  // Base 0.5 - 0.1 (any repetition) = 0.4
  EXPECT_NEAR(repeat_score, 0.4f, 0.01f);
}

TEST(MelodicContextTest, Rule4_ThreeConsecutive_StrongerPenalty) {
  // 3rd consecutive same pitch: -0.1 (base) + -0.2 (3 consecutive) = -0.3 total.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_pitches[1] = 60;
  ctx.prev_count = 2;

  float repeat_score = MelodicContext::scoreMelodicQuality(ctx, 60);
  // Base 0.5 - 0.3 = 0.2
  EXPECT_NEAR(repeat_score, 0.2f, 0.01f);
}

TEST(MelodicContextTest, Rule4_FourConsecutive_HeavyPenalty) {
  // 4th consecutive same pitch: -0.1 + -0.2 + -0.2 = -0.5 total.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 60;
  ctx.prev_pitches[1] = 60;
  ctx.prev_pitches[2] = 60;
  ctx.prev_count = 3;

  float repeat_score = MelodicContext::scoreMelodicQuality(ctx, 60);
  // Base 0.5 - 0.5 = 0.0
  EXPECT_NEAR(repeat_score, 0.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Rule 9: Run length control (consecutive same-direction stepwise motion)
// ---------------------------------------------------------------------------

TEST(MelodicContextTest, RunLengthPenalty) {
  // A long run of same-direction stepwise motion should be penalized.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 67;  // G4
  ctx.prev_pitches[1] = 65;  // F4
  ctx.prev_pitches[2] = 64;  // E4
  ctx.prev_count = 3;
  ctx.prev_direction = 1;  // ascending
  ctx.consecutive_same_dir = 5;  // 5 consecutive ascending steps

  // Candidate: continue ascending by step (A4 = 69)
  float score_continue = MelodicContext::scoreMelodicQuality(ctx, 69);

  // Same context but short run
  MelodicContext ctx_short = ctx;
  ctx_short.consecutive_same_dir = 2;
  float score_short_run = MelodicContext::scoreMelodicQuality(ctx_short, 69);

  // Long run should score lower
  EXPECT_LT(score_continue, score_short_run);
}

TEST(MelodicContextTest, RunLength_BelowThreshold_NoPenalty) {
  // A run of 4 (below threshold of 5) should NOT trigger penalty.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 65;  // F4
  ctx.prev_pitches[1] = 64;  // E4
  ctx.prev_count = 2;
  ctx.prev_direction = 1;
  ctx.consecutive_same_dir = 4;

  float score_4 = MelodicContext::scoreMelodicQuality(ctx, 67);  // Step up

  MelodicContext ctx_5 = ctx;
  ctx_5.consecutive_same_dir = 5;
  float score_5 = MelodicContext::scoreMelodicQuality(ctx_5, 67);

  // Threshold is >= 5, so 4 should score equal or higher than 5.
  EXPECT_GT(score_4, score_5);
}

TEST(MelodicContextTest, RunLength_DirectionChange_NoPenalty) {
  // Changing direction should NOT trigger the run length penalty,
  // even with a long consecutive_same_dir count.
  MelodicContext ctx;
  ctx.prev_pitches[0] = 67;  // G4
  ctx.prev_pitches[1] = 65;  // F4
  ctx.prev_count = 2;
  ctx.prev_direction = 1;  // Was ascending
  ctx.consecutive_same_dir = 6;

  // Step DOWN (direction change): should not be penalized by Rule 9.
  float score_reverse = MelodicContext::scoreMelodicQuality(ctx, 65);
  // Step UP (same direction): should be penalized.
  float score_continue = MelodicContext::scoreMelodicQuality(ctx, 69);

  EXPECT_GT(score_reverse, score_continue);
}

TEST(MelodicContextTest, ConsecutiveSameDir_DefaultZero) {
  MelodicContext ctx;
  EXPECT_EQ(ctx.consecutive_same_dir, 0);
}

}  // namespace
}  // namespace bach
