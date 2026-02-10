// Tests for fugue/subject_validator.h -- individual dimension scorers,
// composite scoring, and known good/bad subjects.

#include "fugue/subject_validator.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// Helper to build a simple subject from pitch and duration arrays.
Subject makeSubject(const std::vector<uint8_t>& pitches,
                    const std::vector<Tick>& durations,
                    Key key = Key::C) {
  Subject subject;
  subject.key = key;
  Tick current_tick = 0;
  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    Tick dur = (idx < durations.size()) ? durations[idx] : kTicksPerBeat;
    subject.notes.push_back({current_tick, dur, pitches[idx], 80, 0});
    current_tick += dur;
  }
  subject.length_ticks = current_tick;
  return subject;
}

// Simpler helper: all quarter notes.
Subject makeSubjectQuarters(const std::vector<uint8_t>& pitches,
                            Key key = Key::C) {
  std::vector<Tick> durations(pitches.size(), kTicksPerBeat);
  return makeSubject(pitches, durations, key);
}

// ---------------------------------------------------------------------------
// intervalVariety
// ---------------------------------------------------------------------------

class SubjectValidatorTest : public ::testing::Test {
 protected:
  SubjectValidator validator;
};

TEST_F(SubjectValidatorTest, IntervalVarietyEmptySubject) {
  Subject empty;
  EXPECT_FLOAT_EQ(validator.scoreIntervalVariety(empty), 0.0f);
}

TEST_F(SubjectValidatorTest, IntervalVarietyMonotone) {
  // All same pitch: unison only.
  Subject subject = makeSubjectQuarters({60, 60, 60, 60});
  float score = validator.scoreIntervalVariety(subject);
  EXPECT_GT(score, 0.0f);   // One unique interval (unison).
  EXPECT_LT(score, 0.5f);   // But very low variety.
}

TEST_F(SubjectValidatorTest, IntervalVarietyDiverse) {
  // C E G A F D C: many different intervals.
  Subject subject = makeSubjectQuarters({60, 64, 67, 69, 65, 62, 60});
  float score = validator.scoreIntervalVariety(subject);
  EXPECT_GT(score, 0.5f);
}

// ---------------------------------------------------------------------------
// rhythmDiversity
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, RhythmDiversityAllSameDuration) {
  // All quarter notes: one duration dominates at 100%.
  Subject subject = makeSubjectQuarters({60, 62, 64, 62});
  float score = validator.scoreRhythmDiversity(subject);
  EXPECT_FLOAT_EQ(score, 0.0f);
}

TEST_F(SubjectValidatorTest, RhythmDiversityMixed) {
  // Mixed durations: quarter, eighth, half, quarter.
  Subject subject = makeSubject(
      {60, 62, 64, 62},
      {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2, kTicksPerBeat});
  float score = validator.scoreRhythmDiversity(subject);
  EXPECT_GT(score, 0.5f);
}

TEST_F(SubjectValidatorTest, RhythmDiversityPerfect) {
  // 4 notes, all different durations: max_count = 1, ratio = 25%.
  Subject subject = makeSubject(
      {60, 62, 64, 65},
      {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2,
       kTicksPerBeat + kTicksPerBeat / 2});
  float score = validator.scoreRhythmDiversity(subject);
  EXPECT_FLOAT_EQ(score, 1.0f);
}

// ---------------------------------------------------------------------------
// contourBalance
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, ContourBalanceSinglePeak) {
  // C D E F E D C -- single peak at F.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 64, 62, 60});
  float score = validator.scoreContourBalance(subject);
  EXPECT_GT(score, 0.7f);
}

TEST_F(SubjectValidatorTest, ContourBalanceMultiplePeaks) {
  // C F C F C F -- peak F appears 3 times.
  Subject subject = makeSubjectQuarters({60, 65, 60, 65, 60, 65});
  float score = validator.scoreContourBalance(subject);
  EXPECT_LT(score, 0.7f);
}

// ---------------------------------------------------------------------------
// rangeScore
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, RangeScoreNarrow) {
  // Range = 7 semitones (within 12).
  Subject subject = makeSubjectQuarters({60, 62, 64, 67, 64, 60});
  float score = validator.scoreRange(subject);
  EXPECT_FLOAT_EQ(score, 1.0f);
}

TEST_F(SubjectValidatorTest, RangeScoreTooWide) {
  // Range = 24 semitones (2 octaves) -- heavily penalized.
  Subject subject = makeSubjectQuarters({48, 60, 72, 60, 48});
  float score = validator.scoreRange(subject);
  EXPECT_LT(score, 0.5f);
}

TEST_F(SubjectValidatorTest, RangeScoreTooSmall) {
  // Range = 1 semitone -- too static.
  Subject subject = makeSubjectQuarters({60, 61, 60, 61});
  float score = validator.scoreRange(subject);
  EXPECT_LT(score, 0.7f);
}

// ---------------------------------------------------------------------------
// stepMotionRatio
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, StepMotionRatioAllSteps) {
  // C D E F G A: all steps (2 semitones each).
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 69});
  float score = validator.scoreStepMotionRatio(subject);
  EXPECT_GT(score, 0.5f);
}

TEST_F(SubjectValidatorTest, StepMotionRatioAllLeaps) {
  // C E G C E G: all leaps (3-5 semitones).
  Subject subject = makeSubjectQuarters({60, 64, 67, 72, 76, 79});
  float score = validator.scoreStepMotionRatio(subject);
  EXPECT_LT(score, 0.5f);
}

TEST_F(SubjectValidatorTest, StepMotionRatioIdealMix) {
  // Mix of steps and leaps: ~60-80% steps.
  // C D E G F E D C: 6/7 steps = 86% -- slightly above ideal.
  Subject subject = makeSubjectQuarters({60, 62, 64, 67, 65, 64, 62, 60});
  float score = validator.scoreStepMotionRatio(subject);
  EXPECT_GT(score, 0.5f);
}

// ---------------------------------------------------------------------------
// tonalStability
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, TonalStabilityStartAndEndOnTonic) {
  // C major: starts on C, ends on C, all diatonic.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 64, 62, 60});
  subject.key = Key::C;
  float score = validator.scoreTonalStability(subject);
  EXPECT_GT(score, 0.8f);
}

TEST_F(SubjectValidatorTest, TonalStabilityChromatic) {
  // All chromatic: C C# D D# E
  Subject subject = makeSubjectQuarters({60, 61, 62, 63, 64});
  subject.key = Key::C;
  float score = validator.scoreTonalStability(subject);
  // Still starts/ends on tonic area, but non-diatonic notes hurt.
  EXPECT_LT(score, 1.0f);
}

// ---------------------------------------------------------------------------
// answerCompatibility
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, AnswerCompatibilityDiatonic) {
  // All diatonic notes in C major.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 65, 64, 60});
  subject.key = Key::C;
  float score = validator.scoreAnswerCompatibility(subject);
  EXPECT_FLOAT_EQ(score, 1.0f);
}

TEST_F(SubjectValidatorTest, AnswerCompatibilityChromatic) {
  // Many chromatic notes.
  Subject subject = makeSubjectQuarters({60, 61, 63, 66, 68, 70, 60});
  subject.key = Key::C;
  float score = validator.scoreAnswerCompatibility(subject);
  EXPECT_LT(score, 1.0f);
}

// ---------------------------------------------------------------------------
// Composite and isAcceptable
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, CompositeWeightsSumToOne) {
  // Verify weights by setting all dimensions to 1.0.
  SubjectScore score;
  score.interval_variety = 1.0f;
  score.rhythm_diversity = 1.0f;
  score.contour_balance = 1.0f;
  score.range_score = 1.0f;
  score.step_motion_ratio = 1.0f;
  score.tonal_stability = 1.0f;
  score.answer_compatibility = 1.0f;

  EXPECT_FLOAT_EQ(score.composite(), 1.0f);
}

TEST_F(SubjectValidatorTest, IsAcceptableThreshold) {
  SubjectScore score;
  // All zeros: composite = 0.0.
  EXPECT_FALSE(score.isAcceptable());

  // All ones: composite = 1.0.
  score.interval_variety = 1.0f;
  score.rhythm_diversity = 1.0f;
  score.contour_balance = 1.0f;
  score.range_score = 1.0f;
  score.step_motion_ratio = 1.0f;
  score.tonal_stability = 1.0f;
  score.answer_compatibility = 1.0f;
  EXPECT_TRUE(score.isAcceptable());
}

TEST_F(SubjectValidatorTest, GoodSubjectIsAcceptable) {
  // Well-crafted subject: C D E F G F E D C (stepwise, single peak,
  // starts/ends tonic, good range, all diatonic).
  Subject subject = makeSubject(
      {60, 62, 64, 65, 67, 65, 64, 62, 60},
      {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat,
       kTicksPerBeat / 2, kTicksPerBeat * 2,
       kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat,
       kTicksPerBeat});
  subject.key = Key::C;

  SubjectScore score = validator.evaluate(subject);
  EXPECT_TRUE(score.isAcceptable())
      << "Composite = " << score.composite()
      << " (interval=" << score.interval_variety
      << " rhythm=" << score.rhythm_diversity
      << " contour=" << score.contour_balance
      << " range=" << score.range_score
      << " step=" << score.step_motion_ratio
      << " tonal=" << score.tonal_stability
      << " answer=" << score.answer_compatibility << ")";
}

// ---------------------------------------------------------------------------
// toJson
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, ToJsonContainsComposite) {
  SubjectScore score;
  score.interval_variety = 0.8f;
  score.tonal_stability = 0.9f;
  std::string json = score.toJson();
  EXPECT_NE(json.find("composite"), std::string::npos);
  EXPECT_NE(json.find("acceptable"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Generated subject evaluation
// ---------------------------------------------------------------------------

TEST_F(SubjectValidatorTest, GeneratedSevereSubjectScoresReasonably) {
  SubjectGenerator gen;
  FugueConfig config;
  config.character = SubjectCharacter::Severe;
  config.key = Key::C;
  config.subject_bars = 2;

  // Test across multiple seeds.
  int acceptable_count = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = gen.generate(config, seed);
    SubjectScore score = validator.evaluate(subject);
    if (score.isAcceptable()) acceptable_count++;
  }
  // At least some should pass.
  EXPECT_GT(acceptable_count, 0)
      << "No Severe subjects were acceptable across 10 seeds";
}

}  // namespace
}  // namespace bach
