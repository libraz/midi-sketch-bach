// Tests for core/markov_tables.h -- Markov transition tables and scoring functions.

#include "core/markov_tables.h"

#include <cmath>
#include <cstdint>

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: all four models for parameterized tests
// ---------------------------------------------------------------------------

struct ModelParam {
  const MarkovModel* model;
  const char* label;
};

class MarkovModelTest : public ::testing::TestWithParam<ModelParam> {};

static const ModelParam kAllModels[] = {
    {&kFugueUpperMarkov, "FugueUpper"},
    {&kFuguePedalMarkov, "FuguePedal"},
    {&kCelloMarkov, "Cello"},
    {&kViolinMarkov, "Violin"},
};

INSTANTIATE_TEST_SUITE_P(AllModels, MarkovModelTest,
                         ::testing::ValuesIn(kAllModels),
                         [](const auto& info) { return info.param.label; });

// ---------------------------------------------------------------------------
// Row sum validity -- pitch tables
// ---------------------------------------------------------------------------

TEST_P(MarkovModelTest, PitchRowSumsApprox10000) {
  const auto& model = *GetParam().model;
  constexpr int kNumRows =
      kDegreeStepCount * kDegreeClassCount * kBeatPosCount;  // 228

  int non_zero_rows = 0;
  for (int row = 0; row < kNumRows; ++row) {
    uint32_t row_sum = 0;
    for (int col = 0; col < kDegreeStepCount; ++col) {
      row_sum += model.pitch.prob[row][col];
    }
    // Some rows may be all-zero if no data existed for that context.
    // Non-zero rows must sum to approximately 10000.
    if (row_sum > 0) {
      EXPECT_GE(row_sum, 9900u)
          << "Pitch row " << row << " sum too low: " << row_sum;
      EXPECT_LE(row_sum, 10100u)
          << "Pitch row " << row << " sum too high: " << row_sum;
      ++non_zero_rows;
    }
  }
  // At least half the rows should have data.
  EXPECT_GT(non_zero_rows, kNumRows / 2)
      << "Too few non-zero pitch rows in model " << model.name;
}

// ---------------------------------------------------------------------------
// Row sum validity -- duration tables
// ---------------------------------------------------------------------------

TEST_P(MarkovModelTest, DurationRowSumsApprox10000) {
  const auto& model = *GetParam().model;
  constexpr int kNumRows = kDurCatCount * kDirIvlClassCount;  // 30

  int non_zero_rows = 0;
  for (int row = 0; row < kNumRows; ++row) {
    uint32_t row_sum = 0;
    for (int col = 0; col < kDurCatCount; ++col) {
      row_sum += model.duration.prob[row][col];
    }
    if (row_sum > 0) {
      EXPECT_GE(row_sum, 9900u)
          << "Duration row " << row << " sum too low: " << row_sum;
      EXPECT_LE(row_sum, 10100u)
          << "Duration row " << row << " sum too high: " << row_sum;
      ++non_zero_rows;
    }
  }
  // Duration tables should be well-populated.
  EXPECT_GT(non_zero_rows, kNumRows / 2)
      << "Too few non-zero duration rows in model " << model.name;
}

// ---------------------------------------------------------------------------
// Score range -- pitch scoring
// ---------------------------------------------------------------------------

TEST_P(MarkovModelTest, PitchScoreWithinRange) {
  const auto& model = *GetParam().model;
  // Sweep all degree steps and beat positions for a representative context.
  for (int prev = -9; prev <= 9; ++prev) {
    for (int next = -9; next <= 9; ++next) {
      for (int beat = 0; beat < kBeatPosCount; ++beat) {
        float score = scoreMarkovPitch(
            model, static_cast<DegreeStep>(prev), DegreeClass::Stable,
            static_cast<BeatPos>(beat), static_cast<DegreeStep>(next));
        // tanh(x * 0.5) is bounded by [-1, +1]; with real data, expect
        // approximately [-0.5, +0.5].
        EXPECT_GE(score, -1.0f);
        EXPECT_LE(score, 1.0f);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Score range -- duration scoring
// ---------------------------------------------------------------------------

TEST_P(MarkovModelTest, DurationScoreWithinRange) {
  const auto& model = *GetParam().model;
  for (int prev_dur = 0; prev_dur < kDurCatCount; ++prev_dur) {
    for (int dir = 0; dir < kDirIvlClassCount; ++dir) {
      for (int next_dur = 0; next_dur < kDurCatCount; ++next_dur) {
        float score = scoreMarkovDuration(
            model, static_cast<DurCategory>(prev_dur),
            static_cast<DirIntervalClass>(dir),
            static_cast<DurCategory>(next_dur));
        EXPECT_GE(score, -1.0f);
        EXPECT_LE(score, 1.0f);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Zero probability handling -- no crash on extreme inputs
// ---------------------------------------------------------------------------

TEST_P(MarkovModelTest, NoCrashOnExtremeInputs) {
  const auto& model = *GetParam().model;

  // Large degree steps (clamped to +/-9 internally).
  float score_large_up = scoreMarkovPitch(model, 9, DegreeClass::Motion,
                                          BeatPos::Off16, 9);
  EXPECT_FALSE(std::isnan(score_large_up));
  EXPECT_FALSE(std::isinf(score_large_up));

  float score_large_down = scoreMarkovPitch(model, -9, DegreeClass::Motion,
                                            BeatPos::Off16, -9);
  EXPECT_FALSE(std::isnan(score_large_down));
  EXPECT_FALSE(std::isinf(score_large_down));

  // Duration edge: HalfPlus -> HalfPlus with LeapDown.
  float dur_score = scoreMarkovDuration(model, DurCategory::HalfPlus,
                                        DirIntervalClass::LeapDown,
                                        DurCategory::HalfPlus);
  EXPECT_FALSE(std::isnan(dur_score));
  EXPECT_FALSE(std::isinf(dur_score));
}

// ---------------------------------------------------------------------------
// Asymmetry -- up vs down transitions differ
// ---------------------------------------------------------------------------

TEST(MarkovAsymmetryTest, UpDownStepsDifferInFugueUpper) {
  // For various contexts, step +1 and step -1 should not always produce
  // identical scores -- the corpus distinguishes ascending from descending.
  int differ_count = 0;
  constexpr int kContextsToTest = kDegreeClassCount * kBeatPosCount;  // 12

  for (int deg_cls = 0; deg_cls < kDegreeClassCount; ++deg_cls) {
    for (int beat = 0; beat < kBeatPosCount; ++beat) {
      float score_up = scoreMarkovPitch(
          kFugueUpperMarkov, 0, static_cast<DegreeClass>(deg_cls),
          static_cast<BeatPos>(beat), 1);
      float score_down = scoreMarkovPitch(
          kFugueUpperMarkov, 0, static_cast<DegreeClass>(deg_cls),
          static_cast<BeatPos>(beat), -1);
      if (std::abs(score_up - score_down) > 1e-6f) {
        ++differ_count;
      }
    }
  }
  // At least some contexts should show asymmetry.
  EXPECT_GT(differ_count, kContextsToTest / 2)
      << "Expected majority of contexts to show up/down asymmetry";
}

// ---------------------------------------------------------------------------
// Model differentiation -- different models yield different scores
// ---------------------------------------------------------------------------

TEST(MarkovModelDifferentiationTest, FugueUpperVsCelloDiffer) {
  // The same input context should produce different scores in most cases.
  int differ_count = 0;
  constexpr int kStepsToTest = 5;  // -2, -1, 0, +1, +2

  for (int next = -2; next <= 2; ++next) {
    for (int beat = 0; beat < kBeatPosCount; ++beat) {
      float fugue_score = scoreMarkovPitch(
          kFugueUpperMarkov, 1, DegreeClass::Stable,
          static_cast<BeatPos>(beat), static_cast<DegreeStep>(next));
      float cello_score = scoreMarkovPitch(
          kCelloMarkov, 1, DegreeClass::Stable,
          static_cast<BeatPos>(beat), static_cast<DegreeStep>(next));
      if (std::abs(fugue_score - cello_score) > 1e-6f) {
        ++differ_count;
      }
    }
  }
  int total = kStepsToTest * kBeatPosCount;  // 20
  EXPECT_GT(differ_count, total / 2)
      << "FugueUpper and Cello should differ in most contexts";
}

TEST(MarkovModelDifferentiationTest, FugueUpperVsPedalDiffer) {
  // Upper voices and pedal have very different melodic profiles.
  int differ_count = 0;
  for (int next = -4; next <= 4; ++next) {
    float upper_score = scoreMarkovPitch(
        kFugueUpperMarkov, 1, DegreeClass::Dominant, BeatPos::Beat,
        static_cast<DegreeStep>(next));
    float pedal_score = scoreMarkovPitch(
        kFuguePedalMarkov, 1, DegreeClass::Dominant, BeatPos::Beat,
        static_cast<DegreeStep>(next));
    if (std::abs(upper_score - pedal_score) > 1e-6f) {
      ++differ_count;
    }
  }
  EXPECT_GT(differ_count, 4)
      << "FugueUpper and FuguePedal should differ for most step sizes";
}

// ---------------------------------------------------------------------------
// Leading tone resolution -- Dominant degree, step +1 should be favored
// ---------------------------------------------------------------------------

TEST(MarkovLeadingToneTest, DominantDegreeStepUpFavored) {
  // In the Dominant degree class (degrees 4, 6 -- includes leading tone),
  // a step +1 from the previous note should be among the higher-probability
  // transitions, reflecting the tendency for leading tone -> tonic resolution.
  //
  // Test across multiple models and beat positions. We compare step +1
  // against step -1: in Dominant context, ascending resolution should
  // generally score higher than descending.
  int resolution_favored = 0;
  int total_tests = 0;

  const MarkovModel* models[] = {&kFugueUpperMarkov, &kCelloMarkov,
                                 &kViolinMarkov};
  for (const auto* mdl : models) {
    for (int beat = 0; beat < kBeatPosCount; ++beat) {
      // Previous step was +1 (approaching from below -- classic leading tone).
      float score_resolve = scoreMarkovPitch(
          *mdl, 1, DegreeClass::Dominant, static_cast<BeatPos>(beat), 1);
      float score_retreat = scoreMarkovPitch(
          *mdl, 1, DegreeClass::Dominant, static_cast<BeatPos>(beat), -1);

      ++total_tests;
      if (score_resolve > score_retreat) {
        ++resolution_favored;
      }
    }
  }
  // Leading tone resolution should be favored in the majority of contexts.
  EXPECT_GT(resolution_favored, total_tests / 2)
      << "Expected step +1 (resolution) to outscore step -1 (retreat) "
      << "in Dominant degree class for most model/beat combinations";
}

// ---------------------------------------------------------------------------
// degreeStepToIndex
// ---------------------------------------------------------------------------

TEST(DegreeStepToIndexTest, CenterAndBounds) {
  // Step 0 maps to index 9 (the center).
  EXPECT_EQ(degreeStepToIndex(0), 9);
  // Step -9 maps to index 0.
  EXPECT_EQ(degreeStepToIndex(-9), 0);
  // Step +9 maps to index 18.
  EXPECT_EQ(degreeStepToIndex(9), 18);
}

TEST(DegreeStepToIndexTest, ClampsBeyondRange) {
  // Values beyond [-9, +9] are clamped.
  EXPECT_EQ(degreeStepToIndex(-15), 0);
  EXPECT_EQ(degreeStepToIndex(15), kDegreeStepCount - 1);
  // Just outside range.
  EXPECT_EQ(degreeStepToIndex(-10), 0);
  EXPECT_EQ(degreeStepToIndex(10), kDegreeStepCount - 1);
}

TEST(DegreeStepToIndexTest, NegativeSteps) {
  EXPECT_EQ(degreeStepToIndex(-1), 8);
  EXPECT_EQ(degreeStepToIndex(-5), 4);
}

TEST(DegreeStepToIndexTest, PositiveSteps) {
  EXPECT_EQ(degreeStepToIndex(1), 10);
  EXPECT_EQ(degreeStepToIndex(5), 14);
}

// ---------------------------------------------------------------------------
// tickToBeatPos
// ---------------------------------------------------------------------------

TEST(TickToBeatPosTest, BarStart) {
  EXPECT_EQ(tickToBeatPos(0), BeatPos::Bar);
  EXPECT_EQ(tickToBeatPos(kTicksPerBar), BeatPos::Bar);
  EXPECT_EQ(tickToBeatPos(kTicksPerBar * 5), BeatPos::Bar);
}

TEST(TickToBeatPosTest, MainBeat) {
  // Beats 1, 2, 3 in 4/4 (tick offsets 480, 960, 1440 within a bar).
  EXPECT_EQ(tickToBeatPos(kTicksPerBeat), BeatPos::Beat);
  EXPECT_EQ(tickToBeatPos(kTicksPerBeat * 2), BeatPos::Beat);
  EXPECT_EQ(tickToBeatPos(kTicksPerBeat * 3), BeatPos::Beat);
  // Same in second bar.
  EXPECT_EQ(tickToBeatPos(kTicksPerBar + kTicksPerBeat), BeatPos::Beat);
}

TEST(TickToBeatPosTest, Off8) {
  // 8th-note offbeats: halfway between beats (240 ticks from beat).
  Tick half_beat = kTicksPerBeat / 2;  // 240
  EXPECT_EQ(tickToBeatPos(half_beat), BeatPos::Off8);
  EXPECT_EQ(tickToBeatPos(kTicksPerBeat + half_beat), BeatPos::Off8);
}

TEST(TickToBeatPosTest, Off16) {
  // 16th-note offbeats: quarter of a beat (120 ticks from beat).
  Tick quarter_beat = kTicksPerBeat / 4;  // 120
  EXPECT_EQ(tickToBeatPos(quarter_beat), BeatPos::Off16);
  // 3/4 beat (360 ticks from beat start) is also Off16.
  EXPECT_EQ(tickToBeatPos(quarter_beat * 3), BeatPos::Off16);
  // Arbitrary non-aligned tick.
  EXPECT_EQ(tickToBeatPos(100), BeatPos::Off16);
}

// ---------------------------------------------------------------------------
// ticksToDurCategory
// ---------------------------------------------------------------------------

TEST(TicksToDurCategoryTest, SixteenthNote) {
  // < 180 ticks = S16.
  EXPECT_EQ(ticksToDurCategory(120), DurCategory::S16);  // Exact 16th
  EXPECT_EQ(ticksToDurCategory(60), DurCategory::S16);   // 32nd
  EXPECT_EQ(ticksToDurCategory(179), DurCategory::S16);  // Just below boundary
}

TEST(TicksToDurCategoryTest, EighthNote) {
  // 180-299 ticks = S8.
  EXPECT_EQ(ticksToDurCategory(180), DurCategory::S8);
  EXPECT_EQ(ticksToDurCategory(240), DurCategory::S8);  // Exact 8th
  EXPECT_EQ(ticksToDurCategory(299), DurCategory::S8);
}

TEST(TicksToDurCategoryTest, DottedEighth) {
  // 300-479 ticks = Dot8.
  EXPECT_EQ(ticksToDurCategory(300), DurCategory::Dot8);
  EXPECT_EQ(ticksToDurCategory(360), DurCategory::Dot8);  // Exact dotted 8th
  EXPECT_EQ(ticksToDurCategory(479), DurCategory::Dot8);
}

TEST(TicksToDurCategoryTest, QuarterNote) {
  // 480-959 ticks = Qtr.
  EXPECT_EQ(ticksToDurCategory(480), DurCategory::Qtr);  // Exact quarter
  EXPECT_EQ(ticksToDurCategory(720), DurCategory::Qtr);  // Dotted quarter
  EXPECT_EQ(ticksToDurCategory(959), DurCategory::Qtr);
}

TEST(TicksToDurCategoryTest, HalfPlus) {
  // >= 960 ticks = HalfPlus.
  EXPECT_EQ(ticksToDurCategory(960), DurCategory::HalfPlus);   // Half note
  EXPECT_EQ(ticksToDurCategory(1920), DurCategory::HalfPlus);  // Whole note
  EXPECT_EQ(ticksToDurCategory(5000), DurCategory::HalfPlus);
}

// ---------------------------------------------------------------------------
// scaleDegreeToClass
// ---------------------------------------------------------------------------

TEST(ScaleDegreeToClassTest, StableDegrees) {
  // Degrees 0 (tonic) and 2 (mediant) are Stable.
  EXPECT_EQ(scaleDegreeToClass(0), DegreeClass::Stable);
  EXPECT_EQ(scaleDegreeToClass(2), DegreeClass::Stable);
}

TEST(ScaleDegreeToClassTest, DominantDegrees) {
  // Degrees 4 (dominant) and 6 (leading tone) are Dominant.
  EXPECT_EQ(scaleDegreeToClass(4), DegreeClass::Dominant);
  EXPECT_EQ(scaleDegreeToClass(6), DegreeClass::Dominant);
}

TEST(ScaleDegreeToClassTest, MotionDegrees) {
  // Degrees 1, 3, 5 are Motion.
  EXPECT_EQ(scaleDegreeToClass(1), DegreeClass::Motion);
  EXPECT_EQ(scaleDegreeToClass(3), DegreeClass::Motion);
  EXPECT_EQ(scaleDegreeToClass(5), DegreeClass::Motion);
}

TEST(ScaleDegreeToClassTest, NormalizesNegativeAndLarge) {
  // Negative degrees and degrees > 6 are normalized via modulo.
  EXPECT_EQ(scaleDegreeToClass(7), DegreeClass::Stable);   // 7 % 7 = 0
  EXPECT_EQ(scaleDegreeToClass(14), DegreeClass::Stable);  // 14 % 7 = 0
  EXPECT_EQ(scaleDegreeToClass(-1), DegreeClass::Dominant);  // (-1%7+7)%7 = 6
  EXPECT_EQ(scaleDegreeToClass(-7), DegreeClass::Stable);    // (-7%7+7)%7 = 0
}

// ---------------------------------------------------------------------------
// toDirIvlClass
// ---------------------------------------------------------------------------

TEST(ToDirIvlClassTest, StepUp) {
  EXPECT_EQ(toDirIvlClass(1), DirIntervalClass::StepUp);
  EXPECT_EQ(toDirIvlClass(2), DirIntervalClass::StepUp);
}

TEST(ToDirIvlClassTest, StepDown) {
  EXPECT_EQ(toDirIvlClass(-1), DirIntervalClass::StepDown);
  EXPECT_EQ(toDirIvlClass(-2), DirIntervalClass::StepDown);
}

TEST(ToDirIvlClassTest, SkipUp) {
  EXPECT_EQ(toDirIvlClass(3), DirIntervalClass::SkipUp);
  EXPECT_EQ(toDirIvlClass(4), DirIntervalClass::SkipUp);
}

TEST(ToDirIvlClassTest, SkipDown) {
  EXPECT_EQ(toDirIvlClass(-3), DirIntervalClass::SkipDown);
  EXPECT_EQ(toDirIvlClass(-4), DirIntervalClass::SkipDown);
}

TEST(ToDirIvlClassTest, LeapUp) {
  EXPECT_EQ(toDirIvlClass(5), DirIntervalClass::LeapUp);
  EXPECT_EQ(toDirIvlClass(9), DirIntervalClass::LeapUp);
}

TEST(ToDirIvlClassTest, LeapDown) {
  EXPECT_EQ(toDirIvlClass(-5), DirIntervalClass::LeapDown);
  EXPECT_EQ(toDirIvlClass(-9), DirIntervalClass::LeapDown);
}

TEST(ToDirIvlClassTest, UnisonIsStepUp) {
  // Step 0 is treated as StepUp (unison, rare case).
  EXPECT_EQ(toDirIvlClass(0), DirIntervalClass::StepUp);
}

// ---------------------------------------------------------------------------
// computeDegreeStep
// ---------------------------------------------------------------------------

TEST(ComputeDegreeStepTest, UnisonIsZero) {
  // C4 -> C4 = step 0.
  EXPECT_EQ(computeDegreeStep(60, 60, Key::C, ScaleType::Major), 0);
}

TEST(ComputeDegreeStepTest, StepUpInCMajor) {
  // C4 (60) -> D4 (62) = +1 degree step.
  EXPECT_EQ(computeDegreeStep(60, 62, Key::C, ScaleType::Major), 1);
  // C4 (60) -> E4 (64) = +2 degree steps.
  EXPECT_EQ(computeDegreeStep(60, 64, Key::C, ScaleType::Major), 2);
}

TEST(ComputeDegreeStepTest, StepDownInCMajor) {
  // E4 (64) -> C4 (60) = -2 degree steps.
  EXPECT_EQ(computeDegreeStep(64, 60, Key::C, ScaleType::Major), -2);
}

TEST(ComputeDegreeStepTest, OctaveIsSevenSteps) {
  // C4 (60) -> C5 (72) = +7 degree steps (one octave in diatonic scale).
  EXPECT_EQ(computeDegreeStep(60, 72, Key::C, ScaleType::Major), 7);
}

TEST(ComputeDegreeStepTest, LargeLeapClampedToNine) {
  // Very large leap: C2 (36) -> C5 (72) = +21 degree steps -> clamped to +9.
  DegreeStep step = computeDegreeStep(36, 72, Key::C, ScaleType::Major);
  EXPECT_EQ(step, 9);
  // Downward: clamped to -9.
  DegreeStep step_down = computeDegreeStep(72, 36, Key::C, ScaleType::Major);
  EXPECT_EQ(step_down, -9);
}

TEST(ComputeDegreeStepTest, WorksInGMajor) {
  // G4 (67) -> A4 (69) = +1 degree step in G major.
  EXPECT_EQ(computeDegreeStep(67, 69, Key::G, ScaleType::Major), 1);
  // G4 (67) -> B4 (71) = +2 degree steps in G major.
  EXPECT_EQ(computeDegreeStep(67, 71, Key::G, ScaleType::Major), 2);
}

// ---------------------------------------------------------------------------
// Score functions -- specific score value sanity checks
// ---------------------------------------------------------------------------

TEST(MarkovPitchScoreTest, CommonTransitionsScorePositive) {
  // In Stable degree class at Bar position, stepwise motion (step +1 or -1)
  // should be relatively common and score near-zero or positive.
  float step_up = scoreMarkovPitch(kFugueUpperMarkov, 1, DegreeClass::Stable,
                                   BeatPos::Bar, 1);
  float step_down = scoreMarkovPitch(kFugueUpperMarkov, 1, DegreeClass::Stable,
                                     BeatPos::Bar, -1);
  // At least one of step up or step down should be above uniform (score > 0).
  EXPECT_TRUE(step_up > 0.0f || step_down > 0.0f)
      << "At least one stepwise direction should be above uniform probability "
      << "(step_up=" << step_up << ", step_down=" << step_down << ")";
}

TEST(MarkovPitchScoreTest, StepwiseOutscoresLargeLeap) {
  // Stepwise motion (step +1 or -1) should score higher than large leaps
  // (step +/-8) in most contexts. Step +/-9 is a catch-all bin that
  // aggregates all leaps >= octave, so we use +/-8 to test true large leaps.
  float step = scoreMarkovPitch(kFugueUpperMarkov, 1, DegreeClass::Stable,
                                BeatPos::Beat, 1);
  float large_leap = scoreMarkovPitch(kFugueUpperMarkov, 1, DegreeClass::Stable,
                                      BeatPos::Beat, 8);
  EXPECT_GT(step, large_leap)
      << "Stepwise motion should outscore a large leap (octave minus one)";
}

TEST(MarkovDurationScoreTest, SixteenthAfterSixteenthCommon) {
  // Runs of sixteenth notes are very common in fugue upper voices.
  float score = scoreMarkovDuration(kFugueUpperMarkov, DurCategory::S16,
                                    DirIntervalClass::StepUp, DurCategory::S16);
  // The data shows kFugueUpperDur[0] = {8057, 856, 488, 438, 161} for S16/StepUp,
  // so S16 -> S16 has probability ~80% which is far above uniform (20%).
  EXPECT_GT(score, 0.0f)
      << "S16 -> S16 in FugueUpper should score above uniform";
}

TEST(MarkovDurationScoreTest, HalfPlusAfterSixteenthRare) {
  // Jumping from sixteenth note to half-plus is rare.
  float score = scoreMarkovDuration(kFugueUpperMarkov, DurCategory::S16,
                                    DirIntervalClass::StepUp,
                                    DurCategory::HalfPlus);
  EXPECT_LT(score, 0.0f)
      << "S16 -> HalfPlus in FugueUpper should score below uniform";
}

// ---------------------------------------------------------------------------
// Model names are set correctly
// ---------------------------------------------------------------------------

TEST(MarkovModelNameTest, ModelsHaveCorrectNames) {
  EXPECT_STREQ(kFugueUpperMarkov.name, "FugueUpper");
  EXPECT_STREQ(kFuguePedalMarkov.name, "FuguePedal");
  EXPECT_STREQ(kCelloMarkov.name, "Cello");
  EXPECT_STREQ(kViolinMarkov.name, "Violin");
}

// ---------------------------------------------------------------------------
// Vertical interval table -- row sum validity
// ---------------------------------------------------------------------------

TEST(VerticalTableTest, VerticalRowSumsApprox10000) {
  for (int row = 0; row < kVerticalRows; ++row) {
    uint32_t row_sum = 0;
    for (int col = 0; col < kPcOffsetCount; ++col) {
      row_sum += kFugueVerticalTable.prob[row][col];
    }
    EXPECT_GE(row_sum, 9900u) << "Vertical row " << row << " sum too low: " << row_sum;
    EXPECT_LE(row_sum, 10100u) << "Vertical row " << row << " sum too high: " << row_sum;
  }
}

TEST(VerticalTableTest, VerticalScoreWithinRange) {
  for (int bd = 0; bd < kBassDegreeCount; ++bd) {
    for (int bp = 0; bp < kBeatPosCount; ++bp) {
      for (int vb = 0; vb < kVoiceBinCount; ++vb) {
        for (int hf = 0; hf < kHarmFuncCount; ++hf) {
          for (int pc = 0; pc < kPcOffsetCount; ++pc) {
            float score = scoreVerticalInterval(
                kFugueVerticalTable, bd, static_cast<BeatPos>(bp),
                vb, static_cast<HarmFunc>(hf), pc);
            EXPECT_GE(score, -1.0f);
            EXPECT_LE(score, 1.0f);
          }
        }
      }
    }
  }
}

TEST(VerticalTableTest, OctaveP5FavoredOnTonic) {
  // On tonic (bass degree 0), harmonic function Tonic, beat 1 (Bar),
  // voice_bin 0 (2-voice context where real data exists),
  // the unison (offset 0) and P5 (offset 7) should score higher than
  // the tritone (offset 6).
  float unison = scoreVerticalInterval(kFugueVerticalTable, 0, BeatPos::Bar,
                                        0, HarmFunc::Tonic, 0);
  float p5 = scoreVerticalInterval(kFugueVerticalTable, 0, BeatPos::Bar,
                                    0, HarmFunc::Tonic, 7);
  float tritone = scoreVerticalInterval(kFugueVerticalTable, 0, BeatPos::Bar,
                                         0, HarmFunc::Tonic, 6);
  EXPECT_GT(unison, tritone)
      << "Unison should outscore tritone on tonic";
  EXPECT_GT(p5, tritone)
      << "P5 should outscore tritone on tonic";
}

TEST(VerticalTableTest, DominantFunctionDistinct) {
  // The minor 7th (offset 10) should score higher on a dominant-function
  // context (bd=4, hf=Dominant) than on a tonic-function context
  // (bd=0, hf=Tonic). Both rows use voice_bin 0 (2v) where real data exists.
  float m7_dom = scoreVerticalInterval(kFugueVerticalTable, 4, BeatPos::Beat,
                                        0, HarmFunc::Dominant, 10);
  float m7_ton = scoreVerticalInterval(kFugueVerticalTable, 0, BeatPos::Bar,
                                        0, HarmFunc::Tonic, 10);
  EXPECT_GT(m7_dom, m7_ton)
      << "Minor 7th should score higher on Dominant than Tonic function";
}

// ---------------------------------------------------------------------------
// Oracle functions -- horizontal and vertical candidates
// ---------------------------------------------------------------------------

TEST(OracleFunctionTest, HorizontalOracleValidPitches) {
  OracleCandidate out[8];
  int count = getTopMelodicCandidates(
      kFugueUpperMarkov, 1, DegreeClass::Stable, BeatPos::Beat,
      60, Key::C, ScaleType::Major, 48, 84, out, 8);
  EXPECT_GT(count, 0) << "Should return at least one candidate";
  // All pitches within range and probability descending.
  for (int i = 0; i < count; ++i) {
    EXPECT_GE(out[i].pitch, 48);
    EXPECT_LE(out[i].pitch, 84);
    EXPECT_GT(out[i].prob, 0.0f);
    if (i > 0) {
      EXPECT_LE(out[i].prob, out[i - 1].prob)
          << "Candidates should be in descending probability order";
    }
  }
}

TEST(OracleFunctionTest, VerticalOracleReturnsPitchClasses) {
  OracleCandidate out[6];
  int count = getTopVerticalCandidates(
      kFugueVerticalTable, 0, BeatPos::Bar, 1, HarmFunc::Tonic,
      out, 6);
  EXPECT_GT(count, 0) << "Should return at least one candidate";
  // All pitch classes within 0-11 and probability descending.
  for (int i = 0; i < count; ++i) {
    EXPECT_GE(out[i].pitch, 0);
    EXPECT_LE(out[i].pitch, 11);
    EXPECT_GT(out[i].prob, 0.0f);
    if (i > 0) {
      EXPECT_LE(out[i].prob, out[i - 1].prob)
          << "Candidates should be in descending probability order";
    }
  }
}

// ---------------------------------------------------------------------------
// degreeToHarmFunc
// ---------------------------------------------------------------------------

TEST(DegreeToHarmFuncTest, TonicDegrees) {
  EXPECT_EQ(degreeToHarmFunc(0), HarmFunc::Tonic);    // I
  EXPECT_EQ(degreeToHarmFunc(5), HarmFunc::Tonic);    // vi
  EXPECT_EQ(degreeToHarmFunc(2), HarmFunc::Tonic);    // iii
}

TEST(DegreeToHarmFuncTest, SubdominantDegrees) {
  EXPECT_EQ(degreeToHarmFunc(3), HarmFunc::Subdominant);  // IV
  EXPECT_EQ(degreeToHarmFunc(1), HarmFunc::Subdominant);  // ii
}

TEST(DegreeToHarmFuncTest, DominantDegrees) {
  EXPECT_EQ(degreeToHarmFunc(4), HarmFunc::Dominant);  // V
  EXPECT_EQ(degreeToHarmFunc(6), HarmFunc::Dominant);  // vii
}

TEST(DegreeToHarmFuncTest, NormalizesOutOfRange) {
  EXPECT_EQ(degreeToHarmFunc(7), HarmFunc::Tonic);   // 7 % 7 = 0
  EXPECT_EQ(degreeToHarmFunc(-1), HarmFunc::Dominant); // (-1+7)%7 = 6
}

// ---------------------------------------------------------------------------
// voiceCountToBin
// ---------------------------------------------------------------------------

TEST(VoiceCountToBinTest, ClassifiesCorrectly) {
  EXPECT_EQ(voiceCountToBin(1), 0);
  EXPECT_EQ(voiceCountToBin(2), 0);
  EXPECT_EQ(voiceCountToBin(3), 1);
  EXPECT_EQ(voiceCountToBin(4), 2);
  EXPECT_EQ(voiceCountToBin(5), 2);
}

// ---------------------------------------------------------------------------
// verticalRowIndex
// ---------------------------------------------------------------------------

TEST(VerticalRowIndexTest, FirstRow) {
  EXPECT_EQ(verticalRowIndex(0, BeatPos::Bar, 0, HarmFunc::Tonic), 0);
}

TEST(VerticalRowIndexTest, LastRow) {
  // bd=6, bp=Off16(3), vb=2, hf=Dominant(2) = 6*4*3*3 + 3*3*3 + 2*3 + 2 = 216+27+6+2 = 251
  EXPECT_EQ(verticalRowIndex(6, BeatPos::Off16, 2, HarmFunc::Dominant), 251);
}

TEST(VerticalRowIndexTest, BoundsCheck) {
  // Total rows = 252, so last valid index = 251
  int max_row = verticalRowIndex(6, BeatPos::Off16, 2, HarmFunc::Dominant);
  EXPECT_EQ(max_row, kVerticalRows - 1);
}

}  // namespace
}  // namespace bach
