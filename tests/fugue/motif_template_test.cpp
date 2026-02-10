// Tests for motif template system and goal tone design values.

#include "fugue/motif_template.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {

TEST(GoalToneTest, SevereValues) {
  GoalTone goal = goalToneForCharacter(SubjectCharacter::Severe);
  EXPECT_FLOAT_EQ(goal.position_ratio, 0.65f);
  EXPECT_FLOAT_EQ(goal.pitch_ratio, 0.85f);
}

TEST(GoalToneTest, PlayfulValues) {
  GoalTone goal = goalToneForCharacter(SubjectCharacter::Playful);
  EXPECT_FLOAT_EQ(goal.position_ratio, 0.50f);
  EXPECT_FLOAT_EQ(goal.pitch_ratio, 0.90f);
}

TEST(GoalToneTest, NobleValues) {
  GoalTone goal = goalToneForCharacter(SubjectCharacter::Noble);
  EXPECT_FLOAT_EQ(goal.position_ratio, 0.70f);
  EXPECT_FLOAT_EQ(goal.pitch_ratio, 0.80f);
}

TEST(GoalToneTest, RestlessValues) {
  GoalTone goal = goalToneForCharacter(SubjectCharacter::Restless);
  EXPECT_FLOAT_EQ(goal.position_ratio, 0.60f);
  EXPECT_FLOAT_EQ(goal.pitch_ratio, 0.95f);
}

TEST(MotifTemplateTest, SevereHasTwoTemplates) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Severe);
  EXPECT_FALSE(mot_a.degree_offsets.empty());
  EXPECT_FALSE(mot_b.degree_offsets.empty());
  EXPECT_EQ(mot_a.degree_offsets.size(), mot_a.durations.size());
  EXPECT_EQ(mot_b.degree_offsets.size(), mot_b.durations.size());
}

TEST(MotifTemplateTest, PlayfulHasTwoTemplates) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Playful);
  EXPECT_FALSE(mot_a.degree_offsets.empty());
  EXPECT_FALSE(mot_b.degree_offsets.empty());
  EXPECT_EQ(mot_a.type, MotifType::Leap);
  EXPECT_EQ(mot_b.type, MotifType::Rhythmic);
}

TEST(MotifTemplateTest, NobleHasTwoTemplates) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Noble);
  EXPECT_FALSE(mot_a.degree_offsets.empty());
  EXPECT_FALSE(mot_b.degree_offsets.empty());
  EXPECT_EQ(mot_a.type, MotifType::Sustain);
  EXPECT_EQ(mot_b.type, MotifType::Scale);
}

TEST(MotifTemplateTest, RestlessHasTwoTemplates) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Restless);
  EXPECT_FALSE(mot_a.degree_offsets.empty());
  EXPECT_FALSE(mot_b.degree_offsets.empty());
  EXPECT_EQ(mot_a.type, MotifType::Chromatic);
  EXPECT_EQ(mot_b.type, MotifType::Leap);
}

TEST(MotifTemplateTest, DurationsMatchOffsets) {
  // All 4 character templates should have matching sizes.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    auto [mot_a, mot_b] = motifTemplatesForCharacter(chr);
    EXPECT_EQ(mot_a.degree_offsets.size(), mot_a.durations.size());
    EXPECT_EQ(mot_b.degree_offsets.size(), mot_b.durations.size());
  }
}

TEST(MotifTemplateTest, AllDurationsPositive) {
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    auto [mot_a, mot_b] = motifTemplatesForCharacter(chr);
    for (Tick dur : mot_a.durations) EXPECT_GT(dur, 0u);
    for (Tick dur : mot_b.durations) EXPECT_GT(dur, 0u);
  }
}

}  // namespace bach
