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
  // All 4 character templates x 4 indices should have matching sizes.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    for (uint32_t idx = 0; idx < 4; ++idx) {
      auto [mot_a, mot_b] = motifTemplatesForCharacter(chr, idx);
      EXPECT_EQ(mot_a.degree_offsets.size(), mot_a.durations.size())
          << "Motif A mismatch for character " << static_cast<int>(chr)
          << " idx " << idx;
      EXPECT_EQ(mot_b.degree_offsets.size(), mot_b.durations.size())
          << "Motif B mismatch for character " << static_cast<int>(chr)
          << " idx " << idx;
    }
  }
}

TEST(MotifTemplateTest, AllDurationsPositive) {
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    for (uint32_t idx = 0; idx < 4; ++idx) {
      auto [mot_a, mot_b] = motifTemplatesForCharacter(chr, idx);
      for (Tick dur : mot_a.durations) {
        EXPECT_GT(dur, 0u) << "Zero duration in motif A, character "
                           << static_cast<int>(chr) << " idx " << idx;
      }
      for (Tick dur : mot_b.durations) {
        EXPECT_GT(dur, 0u) << "Zero duration in motif B, character "
                           << static_cast<int>(chr) << " idx " << idx;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Expanded template selection (4 pairs per character)
// ---------------------------------------------------------------------------

TEST(MotifTemplateTest, FourTemplatesPerCharacter) {
  // Each character should return 4 distinct template pairs.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    for (uint32_t idx = 0; idx < 4; ++idx) {
      auto [mot_a, mot_b] = motifTemplatesForCharacter(chr, idx);
      EXPECT_FALSE(mot_a.degree_offsets.empty())
          << "Motif A empty for character " << static_cast<int>(chr) << " idx " << idx;
      EXPECT_FALSE(mot_b.degree_offsets.empty())
          << "Motif B empty for character " << static_cast<int>(chr) << " idx " << idx;
    }
  }
}

TEST(MotifTemplateTest, TemplateIdxWrapsModulo4) {
  // template_idx values 0 and 4 should return the same template.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    auto [mot_a0, mot_b0] = motifTemplatesForCharacter(chr, 0);
    auto [mot_a4, mot_b4] = motifTemplatesForCharacter(chr, 4);
    EXPECT_EQ(mot_a0.degree_offsets, mot_a4.degree_offsets);
    EXPECT_EQ(mot_b0.degree_offsets, mot_b4.degree_offsets);
    EXPECT_EQ(mot_a0.durations, mot_a4.durations);
    EXPECT_EQ(mot_b0.durations, mot_b4.durations);
  }
}

TEST(MotifTemplateTest, DifferentIndicesProduceDifferentTemplates) {
  // At least some template indices should produce different motif types or offsets.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    bool found_difference = false;
    auto [base_a, base_b] = motifTemplatesForCharacter(chr, 0);
    for (uint32_t idx = 1; idx < 4; ++idx) {
      auto [mot_a, mot_b] = motifTemplatesForCharacter(chr, idx);
      if (mot_a.degree_offsets != base_a.degree_offsets ||
          mot_b.degree_offsets != base_b.degree_offsets) {
        found_difference = true;
        break;
      }
    }
    EXPECT_TRUE(found_difference)
        << "All 4 templates identical for character " << static_cast<int>(chr);
  }
}

TEST(MotifTemplateTest, SevereIdx0IsScaleAndLeap) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Severe, 0);
  EXPECT_EQ(mot_a.type, MotifType::Scale);
  EXPECT_EQ(mot_b.type, MotifType::Leap);
}

TEST(MotifTemplateTest, SevereIdx2IsScaleAndSustain) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Severe, 2);
  EXPECT_EQ(mot_a.type, MotifType::Scale);
  EXPECT_EQ(mot_b.type, MotifType::Sustain);
}

TEST(MotifTemplateTest, SevereIdx3IsLeapAndScale) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Severe, 3);
  EXPECT_EQ(mot_a.type, MotifType::Leap);
  EXPECT_EQ(mot_b.type, MotifType::Scale);
}

TEST(MotifTemplateTest, RestlessIdx1IsLeapAndChromatic) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Restless, 1);
  EXPECT_EQ(mot_a.type, MotifType::Leap);
  EXPECT_EQ(mot_b.type, MotifType::Chromatic);
}

TEST(MotifTemplateTest, RestlessIdx3IsRhythmicAndScale) {
  auto [mot_a, mot_b] = motifTemplatesForCharacter(SubjectCharacter::Restless, 3);
  EXPECT_EQ(mot_a.type, MotifType::Rhythmic);
  EXPECT_EQ(mot_b.type, MotifType::Scale);
}

TEST(MotifTemplateTest, DefaultIndexIsZero) {
  // Calling without template_idx (default=0) should match explicit idx=0.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    auto [def_a, def_b] = motifTemplatesForCharacter(chr);
    auto [exp_a, exp_b] = motifTemplatesForCharacter(chr, 0);
    EXPECT_EQ(def_a.degree_offsets, exp_a.degree_offsets);
    EXPECT_EQ(def_b.degree_offsets, exp_b.degree_offsets);
  }
}

}  // namespace bach
