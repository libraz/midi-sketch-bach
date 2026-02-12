#include "fugue/subject_params.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "fugue/archetype_policy.h"

namespace bach {
namespace {

TEST(SubjectParamsTest, DurationConstants) {
  EXPECT_EQ(kQuarterNote, kTicksPerBeat);
  EXPECT_EQ(kHalfNote, kTicksPerBeat * 2);
  EXPECT_EQ(kEighthNote, kTicksPerBeat / 2);
  EXPECT_EQ(kSixteenthNote, kTicksPerBeat / 4);
  EXPECT_EQ(kDottedQuarter, kQuarterNote + kEighthNote);
  EXPECT_EQ(kDottedEighth, kEighthNote + kEighthNote / 2);
  EXPECT_EQ(kDottedHalf, kHalfNote + kQuarterNote);
}

TEST(SubjectParamsTest, GetCharacterParamsSevere) {
  std::mt19937 gen(42);
  CharacterParams params = getCharacterParams(SubjectCharacter::Severe, gen);
  EXPECT_GE(params.leap_prob, 0.10f);
  EXPECT_LE(params.leap_prob, 0.20f);
  EXPECT_EQ(params.max_range_degrees, 7);
  EXPECT_EQ(params.dur_count, kSevereDurCount);
  // Compare duration values (not pointers, since constexpr arrays may have
  // different addresses across translation units).
  for (int idx = 0; idx < params.dur_count; ++idx) {
    EXPECT_EQ(params.durations[idx], kSevereDurations[idx]);
  }
}

TEST(SubjectParamsTest, GetCharacterParamsPlayful) {
  std::mt19937 gen(42);
  CharacterParams params = getCharacterParams(SubjectCharacter::Playful, gen);
  EXPECT_GE(params.leap_prob, 0.35f);
  EXPECT_LE(params.leap_prob, 0.50f);
  EXPECT_EQ(params.max_range_degrees, 7);
  EXPECT_EQ(params.dur_count, kPlayfulDurCount);
  for (int idx = 0; idx < params.dur_count; ++idx) {
    EXPECT_EQ(params.durations[idx], kPlayfulDurations[idx]);
  }
}

TEST(SubjectParamsTest, GetCharacterParamsNoble) {
  std::mt19937 gen(42);
  CharacterParams params = getCharacterParams(SubjectCharacter::Noble, gen);
  EXPECT_GE(params.leap_prob, 0.20f);
  EXPECT_LE(params.leap_prob, 0.30f);
  EXPECT_EQ(params.max_range_degrees, 8);
}

TEST(SubjectParamsTest, GetCharacterParamsRestless) {
  std::mt19937 gen(42);
  CharacterParams params = getCharacterParams(SubjectCharacter::Restless, gen);
  EXPECT_GE(params.leap_prob, 0.30f);
  EXPECT_LE(params.leap_prob, 0.45f);
  EXPECT_EQ(params.max_range_degrees, 9);
}

TEST(SubjectParamsTest, SnapToScaleClampsBounds) {
  int result = snapToScale(120, Key::C, ScaleType::Major, 48, 84);
  EXPECT_GE(result, 48);
  EXPECT_LE(result, 84);
}

TEST(SubjectParamsTest, SnapToScaleReturnsScaleTone) {
  int result = snapToScale(61, Key::C, ScaleType::Major, 48, 84);
  // C major scale: C D E F G A B. 61 = C#, should snap to C(60) or D(62).
  EXPECT_TRUE(result == 60 || result == 62);
}

TEST(SubjectParamsTest, AvoidUnisonChangesPitch) {
  int result = avoidUnison(60, 60, Key::C, ScaleType::Major, 48, 84);
  EXPECT_NE(result, 60);
  EXPECT_GE(result, 48);
  EXPECT_LE(result, 84);
}

TEST(SubjectParamsTest, AvoidUnisonNoChangeWhenDifferent) {
  int result = avoidUnison(62, 60, Key::C, ScaleType::Major, 48, 84);
  EXPECT_EQ(result, 62);
}

TEST(SubjectParamsTest, AvoidUnisonNoPreviousNote) {
  int result = avoidUnison(60, -1, Key::C, ScaleType::Major, 48, 84);
  EXPECT_EQ(result, 60);
}

TEST(SubjectParamsTest, QuantizeToStrongBeatReturnsStrongBeat) {
  // Tick 500 should quantize to beat 1 (0) or beat 3 (960).
  Tick result = quantizeToStrongBeat(500, SubjectCharacter::Severe,
                                      kTicksPerBar * 4);
  EXPECT_TRUE(result % (kTicksPerBeat * 2) == 0 || result == 0);
}

TEST(SubjectParamsTest, QuantizeToStrongBeatNobleAlwaysBeat1) {
  Tick result = quantizeToStrongBeat(kTicksPerBeat * 2 + 100,
                                      SubjectCharacter::Noble,
                                      kTicksPerBar * 4);
  // Noble: always beat 1.
  EXPECT_EQ(result % kTicksPerBar, 0u);
}

TEST(SubjectParamsTest, QuantizeMinimumOneBar) {
  // Climax too early should be pushed to at least bar 1.
  Tick result = quantizeToStrongBeat(100, SubjectCharacter::Severe,
                                      kTicksPerBar * 4);
  EXPECT_GE(result, kTicksPerBar);
}

TEST(SubjectParamsTest, VaryDurationPairPreservesSum) {
  std::mt19937 gen(42);
  Tick out_a, out_b;
  for (int idx = 0; idx < 20; ++idx) {
    Tick dur_a = kQuarterNote, dur_b = kQuarterNote;
    varyDurationPair(dur_a, dur_b, SubjectCharacter::Playful, gen, out_a, out_b);
    EXPECT_EQ(out_a + out_b, dur_a + dur_b);
  }
}

TEST(SubjectParamsTest, ClampLeapWithinBounds) {
  std::mt19937 gen(42);
  int large_count = 0;
  // Leap of 20 semitones should be clamped.
  int result = clampLeap(80, 60, SubjectCharacter::Severe,
                          Key::C, ScaleType::Major, 48, 84,
                          gen, &large_count);
  EXPECT_LE(std::abs(result - 60), 7);  // Severe max = 7
}

TEST(SubjectParamsTest, ClampLeapNoPreviousNote) {
  std::mt19937 gen(42);
  int result = clampLeap(70, -1, SubjectCharacter::Severe,
                          Key::C, ScaleType::Major, 48, 84, gen);
  EXPECT_EQ(result, 70);
}

TEST(SubjectParamsTest, GetCadentialFormulaSevere) {
  CadentialFormula formula = getCadentialFormula(SubjectCharacter::Severe);
  EXPECT_EQ(formula.count, 3);
  EXPECT_EQ(formula.degrees[0], 2);
  EXPECT_EQ(formula.degrees[2], 0);  // Ends on tonic.
}

TEST(SubjectParamsTest, GetCadentialFormulaPlayful) {
  CadentialFormula formula = getCadentialFormula(SubjectCharacter::Playful);
  EXPECT_EQ(formula.count, 5);
  EXPECT_EQ(formula.degrees[formula.count - 1], 0);  // Ends on tonic.
}

TEST(SubjectParamsTest, ApplyArchetypeConstraintsClamps) {
  CharacterParams params;
  params.leap_prob = 0.3f;
  params.max_range_degrees = 10;
  params.durations = kSevereDurations;
  params.dur_count = kSevereDurCount;

  ArchetypePolicy policy{};
  policy.min_range_degrees = 3;
  policy.max_range_degrees = 7;
  // (other fields default to zero/false)

  applyArchetypeConstraints(params, policy);
  EXPECT_LE(params.max_range_degrees, 7);
  EXPECT_GE(params.max_range_degrees, 3);
}

}  // namespace
}  // namespace bach
