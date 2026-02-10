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

}  // namespace
}  // namespace bach
