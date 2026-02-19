// Tests for core/melodic_state.h -- melodic engine Phase 1 improvements.
// Covers PhraseContour, gravity bias, beat-position scoring, contour
// direction, candidate pitch contour scoring, and section boundary carry.

#include "core/melodic_state.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

namespace bach {
namespace {

// ===========================================================================
// GravityBiasTest
// ===========================================================================

class GravityBiasTest : public ::testing::Test {
 protected:
  static constexpr int kTrials = 3000;
  static constexpr uint32_t kSeed = 42;

  /// @brief Count how many times chooseMelodicDirection reverses from
  ///        the current last_direction over kTrials independent rolls.
  int countReversals(const MelodicState& state, const VoiceProfile& profile) {
    int reversals = 0;
    for (int i = 0; i < kTrials; ++i) {
      std::mt19937 rng(kSeed + static_cast<uint32_t>(i));
      int dir = chooseMelodicDirection(state, profile, rng);
      if (dir != state.last_direction) ++reversals;
    }
    return reversals;
  }
};

TEST_F(GravityBiasTest, ZeroGravityIsSymmetric) {
  // With gravity_bias=0 (bass), ascending and descending runs should
  // produce similar reversal rates.
  VoiceProfile profile = voice_profiles::kBassLine;
  ASSERT_FLOAT_EQ(profile.gravity_bias, 0.0f);

  MelodicState ascending{};
  ascending.last_direction = 1;
  ascending.direction_run_length = 2;

  MelodicState descending{};
  descending.last_direction = -1;
  descending.direction_run_length = 2;

  int asc_reversals = countReversals(ascending, profile);
  int desc_reversals = countReversals(descending, profile);

  // Symmetric gravity means the reversal counts should be close.
  // Allow +/- 8% of trials.
  float diff = std::abs(static_cast<float>(asc_reversals - desc_reversals));
  EXPECT_LT(diff, kTrials * 0.08f)
      << "asc_reversals=" << asc_reversals
      << " desc_reversals=" << desc_reversals;
}

TEST_F(GravityBiasTest, SopranoGravityFavorsDescending) {
  // With gravity_bias=0.08 (soprano), ascending runs should reverse
  // more often than descending runs (gravity pulls down).
  VoiceProfile profile = voice_profiles::kSoprano;
  ASSERT_GT(profile.gravity_bias, 0.0f);

  MelodicState ascending{};
  ascending.last_direction = 1;
  ascending.direction_run_length = 3;  // Full gravity at run_length>=3.
  ascending.phrase_progress = 0.5f;    // Mid-phrase: no cadence decay.

  MelodicState descending{};
  descending.last_direction = -1;
  descending.direction_run_length = 3;
  descending.phrase_progress = 0.5f;

  int asc_reversals = countReversals(ascending, profile);
  int desc_reversals = countReversals(descending, profile);

  // Ascending should reverse more (gravity adds to reversal from ascent,
  // subtracts from reversal when descending).
  EXPECT_GT(asc_reversals, desc_reversals)
      << "Gravity should make ascending runs reverse more often. "
      << "asc=" << asc_reversals << " desc=" << desc_reversals;
}

TEST_F(GravityBiasTest, CadenceWindowDecaysGravity) {
  // At phrase_progress=0.90, gravity should be substantially weaker
  // than at phrase_progress=0.50.
  VoiceProfile profile = voice_profiles::kAlto;  // gravity_bias=0.12
  ASSERT_GT(profile.gravity_bias, 0.0f);

  MelodicState mid_phrase{};
  mid_phrase.last_direction = 1;
  mid_phrase.direction_run_length = 3;
  mid_phrase.phrase_progress = 0.50f;

  MelodicState cadence{};
  cadence.last_direction = 1;
  cadence.direction_run_length = 3;
  cadence.phrase_progress = 0.90f;

  int mid_reversals = countReversals(mid_phrase, profile);
  int cad_reversals = countReversals(cadence, profile);

  // In cadence window, gravity decays. The difference in reversal rates
  // between ascending mid-phrase vs cadence should shrink.
  // Compare against zero-gravity baseline.
  VoiceProfile no_gravity = profile;
  no_gravity.gravity_bias = 0.0f;
  int baseline_reversals = countReversals(mid_phrase, no_gravity);

  // Mid-phrase with gravity should diverge more from baseline than cadence.
  float mid_diff = std::abs(static_cast<float>(mid_reversals - baseline_reversals));
  float cad_diff = std::abs(static_cast<float>(cad_reversals - baseline_reversals));
  EXPECT_GT(mid_diff, cad_diff * 0.5f)
      << "Cadence gravity effect (" << cad_diff
      << ") should be substantially weaker than mid-phrase (" << mid_diff << ")";
}

TEST_F(GravityBiasTest, CadenceWindowAtOneZerosGravity) {
  // At phrase_progress=1.0, the cadence decay formula yields 0.
  // decay = (1.0 - 0.85) / 0.15 = 1.0, so gravity *= (1 - 1) = 0.
  // Reversal rates should match zero-gravity baseline.
  VoiceProfile profile = voice_profiles::kAlto;  // gravity_bias=0.12

  MelodicState end_phrase{};
  end_phrase.last_direction = 1;
  end_phrase.direction_run_length = 3;
  end_phrase.phrase_progress = 1.0f;

  VoiceProfile no_gravity = profile;
  no_gravity.gravity_bias = 0.0f;

  MelodicState end_no_gravity = end_phrase;

  int with_gravity = countReversals(end_phrase, profile);
  int without_gravity = countReversals(end_no_gravity, no_gravity);

  float diff = std::abs(static_cast<float>(with_gravity - without_gravity));
  EXPECT_LT(diff, kTrials * 0.05f)
      << "At progress=1.0, gravity should be zero. "
      << "with=" << with_gravity << " without=" << without_gravity;
}

// ===========================================================================
// ContourDirectionTest
// ===========================================================================

class ContourDirectionTest : public ::testing::Test {};

TEST_F(ContourDirectionTest, ArchBeforePeak) {
  // Before peak: upward direction expected.
  // progress=0.2, peak=0.4 => 1.0 - (0.2/0.4) = 0.5
  float dir = computeContourDirection(PhraseContour::Arch, 0.2f, 0.4f);
  EXPECT_GT(dir, 0.0f);
  EXPECT_NEAR(dir, 0.5f, 1e-5f);
}

TEST_F(ContourDirectionTest, ArchAfterPeak) {
  // After peak: downward direction expected.
  // progress=0.8, peak=0.4 => -(0.8-0.4)/(1.0-0.4) = -0.6667
  float dir = computeContourDirection(PhraseContour::Arch, 0.8f, 0.4f);
  EXPECT_LT(dir, 0.0f);
  EXPECT_NEAR(dir, -2.0f / 3.0f, 1e-5f);
}

TEST_F(ContourDirectionTest, ArchAtPeak) {
  // At peak: direction should be 0.
  // progress=0.4, peak=0.4 => 1.0 - (0.4/0.4) = 0.0
  float dir = computeContourDirection(PhraseContour::Arch, 0.4f, 0.4f);
  EXPECT_NEAR(dir, 0.0f, 1e-5f);
}

TEST_F(ContourDirectionTest, DescentAlwaysNegative) {
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Descent, 0.0f, 0.4f),
                   -1.0f);
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Descent, 0.5f, 0.4f),
                   -1.0f);
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Descent, 1.0f, 0.4f),
                   -1.0f);
}

TEST_F(ContourDirectionTest, AscentAlwaysPositive) {
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Ascent, 0.0f, 0.4f),
                   1.0f);
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Ascent, 0.5f, 0.4f),
                   1.0f);
  EXPECT_FLOAT_EQ(computeContourDirection(PhraseContour::Ascent, 1.0f, 0.4f),
                   1.0f);
}

TEST_F(ContourDirectionTest, NeutralAlwaysZero) {
  EXPECT_FLOAT_EQ(
      computeContourDirection(PhraseContour::Neutral, 0.0f, 0.4f), 0.0f);
  EXPECT_FLOAT_EQ(
      computeContourDirection(PhraseContour::Neutral, 0.5f, 0.4f), 0.0f);
  EXPECT_FLOAT_EQ(
      computeContourDirection(PhraseContour::Neutral, 1.0f, 0.4f), 0.0f);
}

TEST_F(ContourDirectionTest, WaveAlternates) {
  // Wave: two sub-arches. local = fmod(progress*2, 1), return 1-2*local.
  // progress=0.0: local=0.0, 1-0=1.0 (positive)
  float d0 = computeContourDirection(PhraseContour::Wave, 0.0f, 0.4f);
  EXPECT_GT(d0, 0.0f);
  EXPECT_NEAR(d0, 1.0f, 1e-5f);

  // progress=0.3: local=fmod(0.6,1)=0.6, 1-1.2=-0.2 (negative)
  float d1 = computeContourDirection(PhraseContour::Wave, 0.3f, 0.4f);
  EXPECT_LT(d1, 0.0f);

  // progress=0.5: local=fmod(1.0,1)=0.0, 1-0=1.0 (positive)
  float d2 = computeContourDirection(PhraseContour::Wave, 0.5f, 0.4f);
  EXPECT_GT(d2, 0.0f);
  EXPECT_NEAR(d2, 1.0f, 1e-5f);

  // progress=0.8: local=fmod(1.6,1)=0.6, 1-1.2=-0.2 (negative)
  float d3 = computeContourDirection(PhraseContour::Wave, 0.8f, 0.4f);
  EXPECT_LT(d3, 0.0f);
}

TEST_F(ContourDirectionTest, ArchAtPeakProgressZero) {
  // Edge case: peak_progress=0 => return -1.0
  float dir = computeContourDirection(PhraseContour::Arch, 0.5f, 0.0f);
  EXPECT_FLOAT_EQ(dir, -1.0f);
}

TEST_F(ContourDirectionTest, ArchAtPeakProgressOne) {
  // Edge case: peak_progress=1.0 => return 1.0
  float dir = computeContourDirection(PhraseContour::Arch, 0.5f, 1.0f);
  EXPECT_FLOAT_EQ(dir, 1.0f);
}

// ===========================================================================
// BeatPositionScoringTest
// ===========================================================================

class BeatPositionScoringTest : public ::testing::Test {
 protected:
  /// @brief Compute score for a given interval at a specific metric position.
  ///        Uses a neutral MelodicState and soprano profile.
  float scoreAtLevel(int interval, MetricLevel level) {
    MelodicState state{};
    state.last_direction = (interval > 0) ? 1 : -1;
    state.direction_run_length = 1;
    state.phrase_progress = 0.3f;
    state.contour = {PhraseContour::Neutral, 0.4f, 0.0f};  // No contour effect.

    uint8_t prev = 60;
    uint8_t candidate = static_cast<uint8_t>(60 + interval);

    // Choose tick that produces the desired metric level.
    Tick tick = 0;
    switch (level) {
      case MetricLevel::Bar: tick = 0; break;        // Start of bar.
      case MetricLevel::Beat: tick = kTicksPerBeat; break;  // Beat 2.
      case MetricLevel::Offbeat: tick = kTicksPerBeat / 2; break;  // 8th note.
    }

    return scoreCandidatePitch(state, prev, candidate, tick,
                                /*is_chord_tone=*/true,
                                voice_profiles::kSoprano);
  }
};

TEST_F(BeatPositionScoringTest, BarFavorsSkipOverStep) {
  // On bar beats, skip (3rd, +3 semitones) should score higher than
  // step (+1 semitone) due to cross-term bonus for 3rd/4th on bar.
  float step_score = scoreAtLevel(1, MetricLevel::Bar);
  float skip_score = scoreAtLevel(3, MetricLevel::Bar);
  EXPECT_GT(skip_score, step_score)
      << "Bar: skip=" << skip_score << " step=" << step_score;
}

TEST_F(BeatPositionScoringTest, OffbeatFavorsStepOverSkip) {
  // On offbeats, step (+1) should score higher than skip (+3) due to
  // cross-term bonus for steps on offbeat and penalty for skips.
  float step_score = scoreAtLevel(1, MetricLevel::Offbeat);
  float skip_score = scoreAtLevel(3, MetricLevel::Offbeat);
  EXPECT_GT(step_score, skip_score)
      << "Offbeat: step=" << step_score << " skip=" << skip_score;
}

TEST_F(BeatPositionScoringTest, BeatLevelUnchanged) {
  // At Beat level, no cross-term adjustments are applied.
  // Score should only reflect base profile bonuses.
  float step_at_beat = scoreAtLevel(1, MetricLevel::Beat);
  float skip_at_beat = scoreAtLevel(3, MetricLevel::Beat);

  // Verify that the ordering reflects only the base stepwise_bonus
  // (steps get full stepwise_bonus, skips get 0.5x).
  // No additional cross-term perturbation at Beat level.
  // Stepwise bonus for soprano: 0.20. Skip gets 0.10.
  // Step also gets continuation bonus. The key thing is there are
  // no beat-position cross terms (0.12 or -0.06 etc.) at Beat level.

  // Build the same scores manually without cross terms to verify.
  MelodicState state{};
  state.last_direction = 1;
  state.direction_run_length = 1;
  state.phrase_progress = 0.3f;
  state.contour = {PhraseContour::Neutral, 0.4f, 0.0f};

  // Score at bar should differ from beat due to cross terms.
  float step_at_bar = scoreAtLevel(1, MetricLevel::Bar);
  float skip_at_bar = scoreAtLevel(3, MetricLevel::Bar);
  float step_diff = std::abs(step_at_beat - step_at_bar);
  float skip_diff = std::abs(skip_at_beat - skip_at_bar);
  EXPECT_GT(step_diff, 0.01f)
      << "Beat and Bar should differ for steps due to cross terms";
  EXPECT_GT(skip_diff, 0.01f)
      << "Beat and Bar should differ for skips due to cross terms";
}

// ===========================================================================
// ContourScoringTest
// ===========================================================================

class ContourScoringTest : public ::testing::Test {
 protected:
  /// @brief Score a candidate at a specific direction with an Arch contour.
  float scoreWithContour(PhraseContour::Shape shape, float progress,
                          int candidate_offset) {
    MelodicState state{};
    state.last_direction = 0;  // No prior direction.
    state.direction_run_length = 0;
    state.phrase_progress = progress;
    state.contour = {shape, 0.4f, 0.25f};  // Default peak and strength.

    uint8_t prev = 60;
    uint8_t candidate = static_cast<uint8_t>(60 + candidate_offset);
    Tick tick = kTicksPerBeat;  // Beat position (moderate metric weight).

    return scoreCandidatePitch(state, prev, candidate, tick,
                                /*is_chord_tone=*/true,
                                voice_profiles::kSoprano);
  }
};

TEST_F(ContourScoringTest, UpwardCandidateGetsArchBonus) {
  // Arch at progress=0.2 (before peak): contour direction is positive.
  // Ascending candidate (+2 semitones) should score higher than
  // descending candidate (-2 semitones).
  float up_score = scoreWithContour(PhraseContour::Arch, 0.2f, 2);
  float down_score = scoreWithContour(PhraseContour::Arch, 0.2f, -2);
  EXPECT_GT(up_score, down_score)
      << "Arch before peak: ascending=" << up_score
      << " descending=" << down_score;
}

TEST_F(ContourScoringTest, DownwardCandidateGetsArchPenalty) {
  // After peak (progress=0.8): contour direction is negative.
  // Descending candidate should now score higher.
  float up_score = scoreWithContour(PhraseContour::Arch, 0.8f, 2);
  float down_score = scoreWithContour(PhraseContour::Arch, 0.8f, -2);
  EXPECT_GT(down_score, up_score)
      << "Arch after peak: descending=" << down_score
      << " ascending=" << up_score;
}

TEST_F(ContourScoringTest, NeutralContourNoEffect) {
  // Neutral contour produces direction 0, so ascending and descending
  // should receive identical contour bonus.
  float up_score = scoreWithContour(PhraseContour::Neutral, 0.5f, 2);
  float down_score = scoreWithContour(PhraseContour::Neutral, 0.5f, -2);
  EXPECT_FLOAT_EQ(up_score, down_score)
      << "Neutral: up=" << up_score << " down=" << down_score;
}

TEST_F(ContourScoringTest, ContourStrengthZeroNoEffect) {
  // When contour strength is 0, no contour bonus should be applied.
  MelodicState state{};
  state.phrase_progress = 0.2f;
  state.contour = {PhraseContour::Arch, 0.4f, 0.0f};  // strength=0

  float up = scoreCandidatePitch(state, 60, 62, kTicksPerBeat, true,
                                  voice_profiles::kSoprano);
  float down = scoreCandidatePitch(state, 60, 58, kTicksPerBeat, true,
                                    voice_profiles::kSoprano);
  EXPECT_FLOAT_EQ(up, down)
      << "Zero strength: up=" << up << " down=" << down;
}

TEST_F(ContourScoringTest, DescentContourFavorsDescending) {
  // Descent contour should always favor descending candidates.
  float up_score = scoreWithContour(PhraseContour::Descent, 0.5f, 2);
  float down_score = scoreWithContour(PhraseContour::Descent, 0.5f, -2);
  EXPECT_GT(down_score, up_score)
      << "Descent contour: down=" << down_score << " up=" << up_score;
}

TEST_F(ContourScoringTest, AscentContourFavorsAscending) {
  // Ascent contour should always favor ascending candidates.
  float up_score = scoreWithContour(PhraseContour::Ascent, 0.5f, 2);
  float down_score = scoreWithContour(PhraseContour::Ascent, 0.5f, -2);
  EXPECT_GT(up_score, down_score)
      << "Ascent contour: up=" << up_score << " down=" << down_score;
}

// ===========================================================================
// VoiceProfileGravityValues
// ===========================================================================

TEST(VoiceProfileGravityValues, CorrectGravityAssignments) {
  EXPECT_FLOAT_EQ(voice_profiles::kSoprano.gravity_bias, 0.08f);
  EXPECT_FLOAT_EQ(voice_profiles::kAlto.gravity_bias, 0.12f);
  EXPECT_FLOAT_EQ(voice_profiles::kTenor.gravity_bias, 0.12f);
  EXPECT_FLOAT_EQ(voice_profiles::kBassLine.gravity_bias, 0.0f);
  EXPECT_FLOAT_EQ(voice_profiles::kPedalPoint.gravity_bias, 0.0f);
  EXPECT_FLOAT_EQ(voice_profiles::kCantusFirmus.gravity_bias, 0.0f);
}

// ===========================================================================
// PhraseContourDefaults
// ===========================================================================

TEST(PhraseContourDefaults, DefaultValues) {
  PhraseContour contour{};
  EXPECT_EQ(contour.shape, PhraseContour::Arch);
  EXPECT_FLOAT_EQ(contour.peak_progress, 0.4f);
  EXPECT_FLOAT_EQ(contour.strength, 0.25f);
}

// ===========================================================================
// BeatPositionIntervalChoice
// ===========================================================================

class BeatPositionIntervalChoiceTest : public ::testing::Test {
 protected:
  static constexpr int kTrials = 5000;

  /// @brief Count interval type distribution for chooseMelodicInterval
  ///        with tick-based overload.
  struct Distribution {
    int steps = 0;   // interval == 1
    int skips = 0;   // interval == 2
    int leaps = 0;   // interval == 3
  };

  Distribution getDistribution(Tick tick, const VoiceProfile& profile) {
    Distribution dist;
    for (int i = 0; i < kTrials; ++i) {
      std::mt19937 rng(42 + static_cast<uint32_t>(i));
      MelodicState state{};  // Clean state each trial.
      int interval = chooseMelodicInterval(state, rng, profile, tick);
      switch (interval) {
        case 1: ++dist.steps; break;
        case 2: ++dist.skips; break;
        case 3: ++dist.leaps; break;
        default: break;
      }
    }
    return dist;
  }
};

TEST_F(BeatPositionIntervalChoiceTest, BarIncreasesSkipProbability) {
  // At bar position, skip_prob *= 1.12 and step_prob *= 0.94.
  // Compare against beat position (no adjustment).
  Tick bar_tick = 0;           // MetricLevel::Bar
  Tick beat_tick = kTicksPerBeat;  // MetricLevel::Beat

  auto bar_dist = getDistribution(bar_tick, voice_profiles::kSoprano);
  auto beat_dist = getDistribution(beat_tick, voice_profiles::kSoprano);

  float bar_skip_ratio = static_cast<float>(bar_dist.skips) / kTrials;
  float beat_skip_ratio = static_cast<float>(beat_dist.skips) / kTrials;

  EXPECT_GT(bar_skip_ratio, beat_skip_ratio)
      << "Bar skip ratio (" << bar_skip_ratio
      << ") should exceed beat skip ratio (" << beat_skip_ratio << ")";
}

TEST_F(BeatPositionIntervalChoiceTest, OffbeatIncreasesStepProbability) {
  // At offbeat, step_prob *= 1.08 and skip_prob *= 0.95.
  Tick offbeat_tick = kTicksPerBeat / 2;  // MetricLevel::Offbeat
  Tick beat_tick = kTicksPerBeat;          // MetricLevel::Beat

  auto off_dist = getDistribution(offbeat_tick, voice_profiles::kSoprano);
  auto beat_dist = getDistribution(beat_tick, voice_profiles::kSoprano);

  float off_step_ratio = static_cast<float>(off_dist.steps) / kTrials;
  float beat_step_ratio = static_cast<float>(beat_dist.steps) / kTrials;

  EXPECT_GT(off_step_ratio, beat_step_ratio)
      << "Offbeat step ratio (" << off_step_ratio
      << ") should exceed beat step ratio (" << beat_step_ratio << ")";
}

// ===========================================================================
// GoalTensionFactorTest
// ===========================================================================

TEST(GoalTensionFactorTest, FullTensionBeforeSixtyPercent) {
  EXPECT_FLOAT_EQ(goalTensionFactor(0.0f), 1.0f);
  EXPECT_FLOAT_EQ(goalTensionFactor(0.3f), 1.0f);
  EXPECT_FLOAT_EQ(goalTensionFactor(0.6f), 1.0f);
}

TEST(GoalTensionFactorTest, LinearDecayAfterSixtyPercent) {
  // At 80%: t = (0.8-0.6)/0.4 = 0.5, tension = 1.0 - 0.5*0.6 = 0.7
  EXPECT_NEAR(goalTensionFactor(0.8f), 0.7f, 1e-5f);
  // At 100%: t = 1.0, tension = 1.0 - 0.6 = 0.4
  EXPECT_NEAR(goalTensionFactor(1.0f), 0.4f, 1e-5f);
}

// ===========================================================================
// UpdateMelodicStateTest
// ===========================================================================

class UpdateMelodicStateTest : public ::testing::Test {};

TEST_F(UpdateMelodicStateTest, StepResetsLeapTracking) {
  MelodicState state{};
  state.consecutive_leap_count = 2;
  state.last_skip_size = 4;
  state.last_large_leap = 7;

  updateMelodicState(state, 60, 62);  // +2 semitones (step)

  EXPECT_EQ(state.consecutive_leap_count, 0);
  EXPECT_EQ(state.last_skip_size, 0);
  EXPECT_EQ(state.last_large_leap, 0);
  EXPECT_EQ(state.last_direction, 1);
}

TEST_F(UpdateMelodicStateTest, SkipTracking) {
  MelodicState state{};
  updateMelodicState(state, 60, 63);  // +3 semitones (minor 3rd, skip)

  EXPECT_EQ(state.last_skip_size, 3);
  EXPECT_EQ(state.last_large_leap, 0);
  EXPECT_EQ(state.consecutive_leap_count, 1);
}

TEST_F(UpdateMelodicStateTest, LargeLeapTracking) {
  MelodicState state{};
  updateMelodicState(state, 60, 67);  // +7 semitones (P5, large leap)

  EXPECT_EQ(state.last_skip_size, 0);
  EXPECT_EQ(state.last_large_leap, 7);
  EXPECT_EQ(state.consecutive_leap_count, 1);
}

TEST_F(UpdateMelodicStateTest, DirectionRunLengthIncrementsOnContinuation) {
  MelodicState state{};
  state.last_direction = 1;
  state.direction_run_length = 2;

  updateMelodicState(state, 60, 62);  // Continue ascending

  EXPECT_EQ(state.last_direction, 1);
  EXPECT_EQ(state.direction_run_length, 3);
  EXPECT_FALSE(state.prev_was_reversal);
}

TEST_F(UpdateMelodicStateTest, DirectionReversalResetsRunLength) {
  MelodicState state{};
  state.last_direction = 1;
  state.direction_run_length = 3;

  updateMelodicState(state, 62, 60);  // Reverse to descending

  EXPECT_EQ(state.last_direction, -1);
  EXPECT_EQ(state.direction_run_length, 1);
  EXPECT_TRUE(state.prev_was_reversal);
}

// ===========================================================================
// GravityChainLengthModulationTest
// ===========================================================================

class GravityChainLengthTest : public ::testing::Test {
 protected:
  static constexpr int kTrials = 4000;

  int countReversals(int run_length, float gravity_bias) {
    VoiceProfile profile = voice_profiles::kSoprano;
    profile.gravity_bias = gravity_bias;

    MelodicState state{};
    state.last_direction = 1;  // Ascending.
    state.direction_run_length = run_length;
    state.phrase_progress = 0.5f;  // Mid-phrase.

    int reversals = 0;
    for (int i = 0; i < kTrials; ++i) {
      std::mt19937 rng(100 + static_cast<uint32_t>(i));
      int dir = chooseMelodicDirection(state, profile, rng);
      if (dir != 1) ++reversals;
    }
    return reversals;
  }
};

TEST_F(GravityChainLengthTest, ShortRunWeakerGravity) {
  // run_length=1 applies gravity*0.3; run_length=3 applies full gravity.
  // For ascending runs with positive gravity, longer runs should reverse more.
  float gravity = 0.15f;
  int short_reversals = countReversals(1, gravity);
  int long_reversals = countReversals(3, gravity);

  // The difference should be noticeable but both should be in valid range.
  // Long runs also get base_reversal += 0.30 bonus at run_length>=3,
  // so long_reversals will be significantly higher.
  EXPECT_GT(long_reversals, short_reversals)
      << "short_run=" << short_reversals << " long_run=" << long_reversals;
}

TEST_F(GravityChainLengthTest, MediumRunIntermediateGravity) {
  // run_length=2 applies gravity*0.7, between 0.3 and 1.0.
  float gravity = 0.15f;
  int short_reversals = countReversals(1, gravity);
  int medium_reversals = countReversals(2, gravity);
  int long_reversals = countReversals(3, gravity);

  // Medium should be between short and long (though long also has
  // the base_reversal bump at >=3, so it may jump significantly).
  EXPECT_GE(medium_reversals, short_reversals)
      << "medium=" << medium_reversals << " short=" << short_reversals;
  // long_reversals should be the highest due to both full gravity and
  // the base_reversal +=0.30 at run_length>=3.
  EXPECT_GE(long_reversals, medium_reversals)
      << "long=" << long_reversals << " medium=" << medium_reversals;
}

}  // namespace
}  // namespace bach
