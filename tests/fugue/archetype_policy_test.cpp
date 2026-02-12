#include "fugue/archetype_policy.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(ArchetypePolicyTest, CompactPolicy) {
  const auto& pol = getArchetypePolicy(FugueArchetype::Compact);
  EXPECT_EQ(pol.min_range_degrees, 3);
  EXPECT_EQ(pol.max_range_degrees, 7);
  EXPECT_EQ(pol.min_subject_bars, 1);
  EXPECT_EQ(pol.max_subject_bars, 2);
  EXPECT_TRUE(pol.require_fragmentable);
  EXPECT_GT(pol.fragment_reusability_weight, 0.0f);
  EXPECT_GT(pol.sequence_potential_weight, 0.0f);
  EXPECT_FALSE(pol.require_invertible);
  EXPECT_FALSE(pol.require_functional_resolution);
}

TEST(ArchetypePolicyTest, CantabilePolicy) {
  const auto& pol = getArchetypePolicy(FugueArchetype::Cantabile);
  EXPECT_EQ(pol.min_range_degrees, 5);
  EXPECT_EQ(pol.max_range_degrees, 9);
  EXPECT_LE(pol.max_sixteenth_density, 0.15f);
  EXPECT_GE(pol.min_step_ratio, 0.6f);
  EXPECT_FALSE(pol.require_fragmentable);
}

TEST(ArchetypePolicyTest, InvertiblePolicy) {
  const auto& pol = getArchetypePolicy(FugueArchetype::Invertible);
  EXPECT_TRUE(pol.require_invertible);
  EXPECT_TRUE(pol.require_contour_symmetry);
  EXPECT_TRUE(pol.require_axis_stability);
  EXPECT_GT(pol.symmetry_score_weight, 0.0f);
  EXPECT_EQ(pol.preferred_answer, AnswerType::Real);
}

TEST(ArchetypePolicyTest, ChromaticPolicy) {
  const auto& pol = getArchetypePolicy(FugueArchetype::Chromatic);
  EXPECT_EQ(pol.max_consecutive_chromatic, 4);
  EXPECT_TRUE(pol.require_functional_resolution);
  EXPECT_EQ(pol.preferred_answer, AnswerType::Real);
}

TEST(ArchetypePolicyTest, AllArchetypesHaveValidRanges) {
  FugueArchetype types[] = {
      FugueArchetype::Compact, FugueArchetype::Cantabile,
      FugueArchetype::Invertible, FugueArchetype::Chromatic};
  for (auto type : types) {
    const auto& pol = getArchetypePolicy(type);
    EXPECT_LE(pol.min_range_degrees, pol.max_range_degrees);
    EXPECT_LE(pol.min_subject_bars, pol.max_subject_bars);
    EXPECT_LE(pol.min_climax_position, pol.max_climax_position);
    EXPECT_LE(pol.min_climax_pitch, pol.max_climax_pitch);
    EXPECT_GE(pol.dominant_ending_prob, 0.0f);
    EXPECT_LE(pol.dominant_ending_prob, 1.0f);
    EXPECT_LE(pol.min_anacrusis_prob, pol.max_anacrusis_prob);
    EXPECT_GE(pol.max_sixteenth_density, 0.0f);
    EXPECT_LE(pol.max_sixteenth_density, 1.0f);
    EXPECT_GE(pol.min_step_ratio, 0.0f);
    EXPECT_LE(pol.min_step_ratio, 1.0f);
    EXPECT_GE(pol.max_consecutive_chromatic, 1);
  }
}

TEST(ArchetypePolicyTest, FugueArchetypeToString) {
  EXPECT_STREQ(fugueArchetypeToString(FugueArchetype::Compact), "Compact");
  EXPECT_STREQ(fugueArchetypeToString(FugueArchetype::Cantabile), "Cantabile");
  EXPECT_STREQ(fugueArchetypeToString(FugueArchetype::Invertible), "Invertible");
  EXPECT_STREQ(fugueArchetypeToString(FugueArchetype::Chromatic), "Chromatic");
}

}  // namespace
}  // namespace bach
