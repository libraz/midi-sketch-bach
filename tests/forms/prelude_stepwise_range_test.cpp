// Tests for prelude figuration stepwise ratio deviation detection.
// Verifies: stepwise ratio (intervals <= 2 semitones / total intervals)
// falls within Bach-reference calibrated ranges across multiple seeds.
//
// Bach reference data (CLAUDE.md Section 2b):
//   Organ fugue upper voices: stepwise 59-67%, leap 32-35%
//   WTC1 (avg): stepwise 49%, leap 47%
//   Perpetual motion top voice is naturally high stepwise (continuous runs).
//   FreeForm has varied passage work with more leaps.

#include "forms/prelude.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/// @brief Calculate stepwise ratio for a track's notes.
/// @param notes The notes to analyze.
/// @return Ratio of stepwise intervals (interval <= 2 semitones) to total.
double calculateStepwiseRatio(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.0;

  int stepwise_count = 0;
  int total_intervals = 0;

  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(static_cast<int>(notes[i].pitch) -
                            static_cast<int>(notes[i - 1].pitch));
    ++total_intervals;
    if (interval <= 2) {
      ++stepwise_count;
    }
  }

  return total_intervals > 0
             ? static_cast<double>(stepwise_count) / total_intervals
             : 0.0;
}

// ---------------------------------------------------------------------------
// Perpetual prelude stepwise ratio range tests
// Perpetual motion preludes use continuous 16th notes -- naturally high
// stepwise ratio. Reference: organ upper voices 59-67% stepwise.
// Observed range: 0.42-0.89 for Perpetual top voice.
// Range: [0.30, 0.95]
// ---------------------------------------------------------------------------

TEST(PreludeStepwiseRangeTest, Perpetual_Seed1_TopVoiceStepwiseInRange) {
  PreludeConfig config;
  config.key = {Key::C, false};
  config.type = PreludeType::Perpetual;
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = 1;
  config.fugue_length_ticks = 0;

  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  double ratio = calculateStepwiseRatio(result.tracks[0].notes);
  EXPECT_GE(ratio, 0.30)
      << "Perpetual seed 1: stepwise ratio " << ratio << " below 0.30";
  EXPECT_LE(ratio, 0.95)
      << "Perpetual seed 1: stepwise ratio " << ratio << " above 0.95";
}

TEST(PreludeStepwiseRangeTest, Perpetual_Seed7_TopVoiceStepwiseInRange) {
  PreludeConfig config;
  config.key = {Key::C, false};
  config.type = PreludeType::Perpetual;
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = 7;
  config.fugue_length_ticks = 0;

  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  double ratio = calculateStepwiseRatio(result.tracks[0].notes);
  EXPECT_GE(ratio, 0.30)
      << "Perpetual seed 7: stepwise ratio " << ratio << " below 0.30";
  EXPECT_LE(ratio, 0.95)
      << "Perpetual seed 7: stepwise ratio " << ratio << " above 0.95";
}

TEST(PreludeStepwiseRangeTest, Perpetual_Seed42_TopVoiceStepwiseInRange) {
  PreludeConfig config;
  config.key = {Key::C, false};
  config.type = PreludeType::Perpetual;
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = 42;
  config.fugue_length_ticks = 0;

  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  double ratio = calculateStepwiseRatio(result.tracks[0].notes);
  EXPECT_GE(ratio, 0.30)
      << "Perpetual seed 42: stepwise ratio " << ratio << " below 0.30";
  EXPECT_LE(ratio, 0.95)
      << "Perpetual seed 42: stepwise ratio " << ratio << " above 0.95";
}

// ---------------------------------------------------------------------------
// FreeForm prelude stepwise ratio
// FreeForm uses varied passage work with arpeggios and scales.
// Observed range: 0.29-0.69 for FreeForm top voice.
// Range: [0.20, 0.75]
// ---------------------------------------------------------------------------

TEST(PreludeStepwiseRangeTest, FreeForm_MultiSeedStepwiseInRange) {
  constexpr uint32_t kSeeds[] = {1, 7, 42, 100, 256};
  for (uint32_t seed : kSeeds) {
    PreludeConfig config;
    config.key = {Key::C, false};
    config.type = PreludeType::FreeForm;
    config.num_voices = 3;
    config.bpm = 100;
    config.seed = seed;
    config.fugue_length_ticks = 0;

    PreludeResult result = generatePrelude(config);
    ASSERT_TRUE(result.success) << "seed=" << seed;
    ASSERT_GE(result.tracks.size(), 1u);

    double ratio = calculateStepwiseRatio(result.tracks[0].notes);
    EXPECT_GE(ratio, 0.20)
        << "FreeForm seed " << seed << ": stepwise ratio " << ratio
        << " below 0.20 (too many leaps)";
    EXPECT_LE(ratio, 0.75)
        << "FreeForm seed " << seed << ": stepwise ratio " << ratio
        << " above 0.75 (too few leaps for FreeForm)";
  }
}

// ---------------------------------------------------------------------------
// All voices stepwise ratio sanity check
// ---------------------------------------------------------------------------

TEST(PreludeStepwiseRangeTest, AllVoices_StepwiseReasonable) {
  PreludeConfig config;
  config.key = {Key::C, false};
  config.type = PreludeType::Perpetual;
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = 42;
  config.fugue_length_ticks = 0;

  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  for (size_t track_idx = 0; track_idx < result.tracks.size(); ++track_idx) {
    const auto& notes = result.tracks[track_idx].notes;
    if (notes.size() < 4) continue;  // Skip voices with too few notes.

    double ratio = calculateStepwiseRatio(notes);
    // All voices should have some stepwise motion (at least 15%).
    // Perpetual top voice can be up to 95% stepwise (continuous runs).
    // Middle/bass voices have more varied motion.
    EXPECT_GE(ratio, 0.15)
        << "Track " << track_idx << " stepwise ratio " << ratio
        << " is unreasonably low";
    EXPECT_LE(ratio, 0.95)
        << "Track " << track_idx << " stepwise ratio " << ratio
        << " is unreasonably high (potential stuck notes)";
  }
}

// ---------------------------------------------------------------------------
// Cross-type comparison: Perpetual should have higher stepwise than FreeForm
// ---------------------------------------------------------------------------

TEST(PreludeStepwiseRangeTest, PerpetualHigherStepwiseThanFreeForm) {
  // Over multiple seeds, Perpetual should have higher average stepwise ratio
  // in the top voice compared to FreeForm (because Perpetual uses continuous
  // 16th note figurations).
  double perp_total = 0.0;
  double free_total = 0.0;
  int count = 0;

  constexpr uint32_t kSeeds[] = {1, 7, 42, 100, 256};
  for (uint32_t seed : kSeeds) {
    PreludeConfig config_p;
    config_p.key = {Key::C, false};
    config_p.type = PreludeType::Perpetual;
    config_p.num_voices = 3;
    config_p.bpm = 100;
    config_p.seed = seed;
    config_p.fugue_length_ticks = 0;

    PreludeConfig config_f = config_p;
    config_f.type = PreludeType::FreeForm;

    PreludeResult result_p = generatePrelude(config_p);
    PreludeResult result_f = generatePrelude(config_f);

    if (!result_p.success || !result_f.success) continue;
    if (result_p.tracks.empty() || result_f.tracks.empty()) continue;

    perp_total += calculateStepwiseRatio(result_p.tracks[0].notes);
    free_total += calculateStepwiseRatio(result_f.tracks[0].notes);
    ++count;
  }

  if (count > 0) {
    double perp_avg = perp_total / count;
    double free_avg = free_total / count;
    EXPECT_GE(perp_avg, free_avg)
        << "Perpetual avg stepwise (" << perp_avg
        << ") should be >= FreeForm avg (" << free_avg << ")";
  }
}

}  // namespace
}  // namespace bach
