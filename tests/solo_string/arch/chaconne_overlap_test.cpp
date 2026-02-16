// Tests for chaconne within-voice overlap detection.
// Deviation-detection tests: these detect same-pitch overlap counts in
// chaconne output and verify that chord/bariolage multi-pitch events
// are preserved.
//
// Current status: the generator produces some same-pitch overlaps
// (4-30 per seed). These tests track the deviation count to detect
// regressions and document the issue for future fixes.

#include "solo_string/arch/chaconne_engine.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "solo_string/arch/chaconne_config.h"

namespace bach {
namespace {

// ===========================================================================
// Helper: create a standard test config
// ===========================================================================

/// @brief Create a ChaconneConfig with deterministic seed for overlap testing.
/// @param seed RNG seed (non-zero for determinism).
/// @return Fully initialized config.
ChaconneConfig createOverlapTestConfig(uint32_t seed = 42) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.bpm = 60;
  config.seed = seed;
  config.instrument = InstrumentType::Violin;
  return config;
}

/// @brief Count within-voice same-pitch overlaps in a note list.
/// @param notes Sorted-by-start-tick note events.
/// @return Number of same-pitch overlapping note pairs.
int countSamePitchOverlaps(const std::vector<NoteEvent>& notes) {
  int overlap_count = 0;
  for (size_t i = 0; i < notes.size(); ++i) {
    Tick end_i = notes[i].start_tick + notes[i].duration;
    for (size_t j = i + 1; j < notes.size(); ++j) {
      if (notes[j].start_tick >= end_i) break;  // Sorted by start_tick.
      if (notes[i].pitch == notes[j].pitch &&
          notes[j].start_tick < end_i) {
        ++overlap_count;
      }
    }
  }
  return overlap_count;
}

// ===========================================================================
// Within-voice overlap deviation detection
// Tracks overlap count to detect regressions. Currently the generator
// produces some overlaps; threshold set to catch significant regressions.
// ===========================================================================

TEST(ChaconneOverlapTest, WithinVoiceOverlapBounded_Seed1) {
  auto config = createOverlapTestConfig(1);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  int overlaps = countSamePitchOverlaps(result.tracks[0].notes);

  // Deviation detection: current baseline is ~18 overlaps for seed 1.
  // Flag regression if significantly worse. Ideal target is 0.
  EXPECT_LE(overlaps, 50)
      << "Seed 1: " << overlaps << " same-pitch overlaps exceeds "
      << "regression threshold (max 50)";
}

TEST(ChaconneOverlapTest, WithinVoiceOverlapBounded_Seed42) {
  auto config = createOverlapTestConfig(42);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  int overlaps = countSamePitchOverlaps(result.tracks[0].notes);

  EXPECT_LE(overlaps, 50)
      << "Seed 42: " << overlaps << " same-pitch overlaps exceeds "
      << "regression threshold (max 50)";
}

// ===========================================================================
// Chords/bariolage preserved: different pitches at same tick
// ===========================================================================

TEST(ChaconneOverlapTest, SimultaneousDifferentPitchesPreserved_Seed1) {
  auto config = createOverlapTestConfig(1);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  // Group notes by start_tick.
  std::map<Tick, std::set<uint8_t>> tick_pitches;
  for (const auto& note : notes) {
    tick_pitches[note.start_tick].insert(note.pitch);
  }

  // Count ticks with multiple different pitches (chords/bariolage).
  int multi_pitch_ticks = 0;
  for (const auto& [tick, pitches] : tick_pitches) {
    if (pitches.size() > 1) {
      ++multi_pitch_ticks;
    }
  }

  // A chaconne should have some double stops / chords.
  EXPECT_GT(multi_pitch_ticks, 0)
      << "Seed 1: no simultaneous different-pitch events found "
      << "(chords/bariolage should be preserved)";
}

TEST(ChaconneOverlapTest, SimultaneousDifferentPitchesPreserved_Seed42) {
  auto config = createOverlapTestConfig(42);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;

  std::map<Tick, std::set<uint8_t>> tick_pitches;
  for (const auto& note : notes) {
    tick_pitches[note.start_tick].insert(note.pitch);
  }

  int multi_pitch_ticks = 0;
  for (const auto& [tick, pitches] : tick_pitches) {
    if (pitches.size() > 1) {
      ++multi_pitch_ticks;
    }
  }

  EXPECT_GT(multi_pitch_ticks, 0)
      << "Seed 42: no simultaneous different-pitch events found";
}

// ===========================================================================
// Multi-seed overlap regression check
// ===========================================================================

TEST(ChaconneOverlapTest, OverlapBoundedAcrossMultipleSeeds) {
  int total_overlaps = 0;
  int total_notes = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto config = createOverlapTestConfig(seed);
    auto result = generateChaconne(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << ": " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 1u);

    int overlaps = countSamePitchOverlaps(result.tracks[0].notes);
    total_overlaps += overlaps;
    total_notes += static_cast<int>(result.tracks[0].notes.size());

    // Per-seed regression threshold: no seed should have more than 50 overlaps.
    EXPECT_LE(overlaps, 50)
        << "Seed " << seed << ": " << overlaps
        << " same-pitch overlaps exceeds per-seed threshold";
  }

  // Overall overlap rate across all seeds should be under 5% of total notes.
  if (total_notes > 0) {
    double overlap_rate = static_cast<double>(total_overlaps) / total_notes;
    EXPECT_LE(overlap_rate, 0.05)
        << "Overall same-pitch overlap rate " << (overlap_rate * 100.0)
        << "% exceeds 5% threshold (" << total_overlaps << "/"
        << total_notes << " notes)";
  }
}

}  // namespace
}  // namespace bach
