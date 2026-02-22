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
  ASSERT_EQ(result.tracks.size(), 2u);

  // Check overlaps in each track independently.
  for (size_t t = 0; t < result.tracks.size(); ++t) {
    int overlaps = countSamePitchOverlaps(result.tracks[t].notes);
    EXPECT_LE(overlaps, 50)
        << "Seed 1, track " << t << ": " << overlaps
        << " same-pitch overlaps exceeds regression threshold (max 50)";
  }
}

TEST(ChaconneOverlapTest, WithinVoiceOverlapBounded_Seed42) {
  auto config = createOverlapTestConfig(42);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 2u);

  for (size_t t = 0; t < result.tracks.size(); ++t) {
    int overlaps = countSamePitchOverlaps(result.tracks[t].notes);
    EXPECT_LE(overlaps, 50)
        << "Seed 42, track " << t << ": " << overlaps
        << " same-pitch overlaps exceeds regression threshold (max 50)";
  }
}

// ===========================================================================
// Chords/bariolage preserved: near-simultaneous pitches in texture track
// After overlap cleanup, chord notes are staggered by kChordStagger (60 ticks)
// into micro-arpeggios. Check that groups of distinct pitches within 60 ticks
// exist, indicating preserved chord/bariolage structure.
// ===========================================================================

/// @brief Count groups of near-simultaneous distinct pitches in a sorted note vector.
/// @param notes Notes sorted by start_tick.
/// @param tolerance Maximum tick gap within a group.
/// @return Number of groups with 2+ distinct pitches.
int countNearSimultaneousGroups(const std::vector<NoteEvent>& notes, Tick tolerance) {
  int groups = 0;
  for (size_t i = 0; i < notes.size(); /* advanced inside */) {
    Tick group_start = notes[i].start_tick;
    std::set<uint8_t> pitches;
    pitches.insert(notes[i].pitch);
    size_t j = i + 1;
    while (j < notes.size() && notes[j].start_tick - group_start <= tolerance) {
      pitches.insert(notes[j].pitch);
      ++j;
    }
    if (pitches.size() > 1) ++groups;
    i = j;
  }
  return groups;
}

TEST(ChaconneOverlapTest, SimultaneousDifferentPitchesPreserved_Seed1) {
  auto config = createOverlapTestConfig(1);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 2u);

  constexpr Tick kStaggerTolerance = 60;
  int groups = countNearSimultaneousGroups(result.tracks[1].notes, kStaggerTolerance);

  EXPECT_GT(groups, 0)
      << "Seed 1: no near-simultaneous different-pitch groups found "
      << "(chords/bariolage should be preserved as micro-arpeggios)";
}

TEST(ChaconneOverlapTest, SimultaneousDifferentPitchesPreserved_Seed42) {
  auto config = createOverlapTestConfig(42);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 2u);

  constexpr Tick kStaggerTolerance = 60;
  int groups = countNearSimultaneousGroups(result.tracks[1].notes, kStaggerTolerance);

  EXPECT_GT(groups, 0)
      << "Seed 42: no near-simultaneous different-pitch groups found";
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
    ASSERT_EQ(result.tracks.size(), 2u);

    for (size_t t = 0; t < result.tracks.size(); ++t) {
      int overlaps = countSamePitchOverlaps(result.tracks[t].notes);
      total_overlaps += overlaps;
      total_notes += static_cast<int>(result.tracks[t].notes.size());

      EXPECT_LE(overlaps, 50)
          << "Seed " << seed << ", track " << t << ": " << overlaps
          << " same-pitch overlaps exceeds per-seed threshold";
    }
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
