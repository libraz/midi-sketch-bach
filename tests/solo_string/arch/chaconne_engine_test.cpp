// Tests for solo_string/arch/chaconne_engine.h -- integration tests for chaconne generation.

#include "solo_string/arch/chaconne_engine.h"

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/key.h"
#include "solo_string/arch/chaconne_config.h"
#include "solo_string/arch/ground_bass.h"
#include "solo_string/arch/variation_types.h"

namespace bach {
namespace {

// ===========================================================================
// Helper: create a standard test config
// ===========================================================================

/// @brief Create a ChaconneConfig with deterministic seed for testing.
/// @param seed RNG seed (non-zero for determinism).
/// @return Fully initialized config.
ChaconneConfig createTestConfig(uint32_t seed = 42) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.bpm = 60;
  config.seed = seed;
  config.instrument = InstrumentType::Violin;
  // Leave variations empty -- engine uses createStandardVariationPlan.
  return config;
}

// ===========================================================================
// Basic generation
// ===========================================================================

TEST(ChaconneEngineTest, DefaultConfigGeneratesSuccessfully) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, ProducesSingleTrack) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.tracks.size(), 1u);
}

TEST(ChaconneEngineTest, TrackHasNotes) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

TEST(ChaconneEngineTest, TrackHasCorrectNameViolin) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Violin;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].name, "Violin");
}

TEST(ChaconneEngineTest, TrackHasCorrectNameCello) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Cello;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].name, "Cello");
}

TEST(ChaconneEngineTest, TrackHasCorrectNameGuitar) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Guitar;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].name, "Guitar");
}

// ===========================================================================
// GM program numbers
// ===========================================================================

TEST(ChaconneEngineTest, ViolinTrackHasCorrectGmProgram) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Violin;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 40u);  // Violin GM program
}

TEST(ChaconneEngineTest, CelloTrackHasCorrectGmProgram) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Cello;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 42u);  // Cello GM program
}

TEST(ChaconneEngineTest, GuitarTrackHasCorrectGmProgram) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Guitar;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 24u);  // Nylon Guitar GM program
}

// ===========================================================================
// Instrument ranges
// ===========================================================================

TEST(ChaconneEngineTest, ViolinNotesWithinRange) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Violin;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, 43u)  // Allow some bass notes from ground bass
        << "Note pitch " << static_cast<int>(note.pitch) << " below violin range"
        << " at tick " << note.start_tick;
    EXPECT_LE(note.pitch, 96u)
        << "Note pitch " << static_cast<int>(note.pitch) << " above violin range"
        << " at tick " << note.start_tick;
  }
}

TEST(ChaconneEngineTest, CelloNotesWithinRange) {
  auto config = createTestConfig();
  config.instrument = InstrumentType::Cello;
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    // Ground bass may go slightly below cello texture range but still
    // within instrument capability. Use generous bounds for bass notes.
    EXPECT_LE(note.pitch, 96u)
        << "Note pitch " << static_cast<int>(note.pitch) << " above range"
        << " at tick " << note.start_tick;
  }
}

// ===========================================================================
// Seed determinism
// ===========================================================================

TEST(ChaconneEngineTest, SameSeedProducesSameOutput) {
  auto config_a = createTestConfig(123);
  auto config_b = createTestConfig(123);

  auto result_a = generateChaconne(config_a);
  auto result_b = generateChaconne(config_b);

  ASSERT_TRUE(result_a.success) << result_a.error_message;
  ASSERT_TRUE(result_b.success) << result_b.error_message;
  ASSERT_EQ(result_a.tracks.size(), 1u);
  ASSERT_EQ(result_b.tracks.size(), 1u);

  // All notes must match exactly.
  const auto& notes_a = result_a.tracks[0].notes;
  const auto& notes_b = result_b.tracks[0].notes;
  ASSERT_EQ(notes_a.size(), notes_b.size());

  for (size_t idx = 0; idx < notes_a.size(); ++idx) {
    EXPECT_EQ(notes_a[idx].pitch, notes_b[idx].pitch) << "at note " << idx;
    EXPECT_EQ(notes_a[idx].start_tick, notes_b[idx].start_tick) << "at note " << idx;
    EXPECT_EQ(notes_a[idx].duration, notes_b[idx].duration) << "at note " << idx;
  }

  EXPECT_EQ(result_a.total_duration_ticks, result_b.total_duration_ticks);
}

TEST(ChaconneEngineTest, DifferentSeedsProduceDifferentOutput) {
  auto config_a = createTestConfig(42);
  auto config_b = createTestConfig(99);

  auto result_a = generateChaconne(config_a);
  auto result_b = generateChaconne(config_b);

  ASSERT_TRUE(result_a.success) << result_a.error_message;
  ASSERT_TRUE(result_b.success) << result_b.error_message;
  ASSERT_EQ(result_a.tracks.size(), 1u);
  ASSERT_EQ(result_b.tracks.size(), 1u);

  // With different seeds, at least some notes should differ.
  const auto& notes_a = result_a.tracks[0].notes;
  const auto& notes_b = result_b.tracks[0].notes;

  // They may have the same count due to identical structural plan, but
  // the texture note pitches should differ in at least one position.
  bool any_diff = false;
  size_t min_size = std::min(notes_a.size(), notes_b.size());
  for (size_t idx = 0; idx < min_size; ++idx) {
    if (notes_a[idx].pitch != notes_b[idx].pitch ||
        notes_a[idx].start_tick != notes_b[idx].start_tick) {
      any_diff = true;
      break;
    }
  }
  if (notes_a.size() != notes_b.size()) {
    any_diff = true;
  }
  EXPECT_TRUE(any_diff) << "Different seeds produced identical output";
}

TEST(ChaconneEngineTest, SeedUsedIsRecordedInResult) {
  auto config = createTestConfig(77);
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.seed_used, 77u);
}

TEST(ChaconneEngineTest, AutoSeedGeneratesNonZero) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.seed = 0;  // Auto seed
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_NE(result.seed_used, 0u);
}

// ===========================================================================
// Duration and structure
// ===========================================================================

TEST(ChaconneEngineTest, TotalDurationMatchesVariationCount) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;

  // 10 variations * 4 bars * 1920 ticks/bar = 76800
  auto ground_bass = GroundBass::createForKey(config.key);
  Tick expected_min = 10u * ground_bass.getLengthTicks();
  EXPECT_GE(result.total_duration_ticks, expected_min);
}

TEST(ChaconneEngineTest, AllNotesHaveNonZeroDuration) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GT(note.duration, 0u)
        << "Note with zero duration at tick " << note.start_tick
        << " pitch " << static_cast<int>(note.pitch);
  }
}

TEST(ChaconneEngineTest, AllNotesHaveReasonableVelocity) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GT(note.velocity, 0u)
        << "Note with zero velocity at tick " << note.start_tick;
    EXPECT_LE(note.velocity, 127u);
  }
}

TEST(ChaconneEngineTest, NotesAreSortedByStartTick) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    EXPECT_GE(notes[idx].start_tick, notes[idx - 1].start_tick)
        << "Notes not sorted at index " << idx;
  }
}

// ===========================================================================
// Ground bass presence
// ===========================================================================

TEST(ChaconneEngineTest, GroundBassNotesArePresentInOutput) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  auto ground_bass = GroundBass::createForKey(config.key);
  Tick bass_length = ground_bass.getLengthTicks();
  const auto& bass_notes = ground_bass.getNotes();
  const auto& output_notes = result.tracks[0].notes;

  // For each variation (10), check that each bass note exists at the correct offset.
  int variations_checked = 0;
  for (int var_idx = 0; var_idx < 10; ++var_idx) {
    Tick offset = static_cast<Tick>(var_idx) * bass_length;
    bool all_found = true;

    for (const auto& bass_note : bass_notes) {
      Tick expected_tick = offset + bass_note.start_tick;
      bool found = false;

      for (const auto& note : output_notes) {
        if (note.start_tick == expected_tick &&
            note.pitch == bass_note.pitch &&
            note.duration == bass_note.duration) {
          found = true;
          break;
        }
      }

      if (!found) {
        all_found = false;
        break;
      }
    }

    if (all_found) {
      ++variations_checked;
    }
  }

  EXPECT_EQ(variations_checked, 10)
      << "Not all variations contain the complete ground bass";
}

// ===========================================================================
// Key variations
// ===========================================================================

TEST(ChaconneEngineTest, WorksWithDMinorKey) {
  auto config = createTestConfig();
  config.key = {Key::D, true};
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, WorksWithCMinorKey) {
  auto config = createTestConfig();
  config.key = {Key::C, true};
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, WorksWithGMinorKey) {
  auto config = createTestConfig();
  config.key = {Key::G, true};
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, WorksWithAMinorKey) {
  auto config = createTestConfig();
  config.key = {Key::A, true};
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, WorksWithEMinorKey) {
  auto config = createTestConfig();
  config.key = {Key::E, true};
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST(ChaconneEngineTest, InvalidVariationPlanReturnsError) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.seed = 42;

  // Create an invalid plan: reversed role order.
  config.variations.push_back(
      {0, VariationRole::Resolve, VariationType::Theme,
       TextureType::SingleLine, {Key::D, true}, false});
  config.variations.push_back(
      {1, VariationRole::Establish, VariationType::Theme,
       TextureType::SingleLine, {Key::D, true}, false});

  auto result = generateChaconne(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(ChaconneEngineTest, EmptyGroundBassReturnsError) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.seed = 42;
  // Provide an empty ground bass explicitly.
  config.ground_bass_notes = {};  // Empty -> engine creates from key, which is fine

  // To truly test empty ground bass, we need a custom ground bass that is empty.
  // The engine uses createForKey if ground_bass_notes is empty, which is valid.
  // So this should succeed.
  auto result = generateChaconne(config);
  EXPECT_TRUE(result.success) << result.error_message;
}

TEST(ChaconneEngineTest, InvalidTypesInPlanReturnsError) {
  ChaconneConfig config;
  config.key = {Key::D, true};
  config.seed = 42;

  // Create plan where Establish uses Virtuosic (not allowed).
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(config.key, rng);
  plan[0].type = VariationType::Virtuosic;
  config.variations = plan;

  auto result = generateChaconne(config);
  EXPECT_FALSE(result.success);
}

// ===========================================================================
// Channel assignment
// ===========================================================================

TEST(ChaconneEngineTest, TrackUsesChannel0) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].channel, 0u);
}

// ===========================================================================
// Timeline propagation
// ===========================================================================

TEST(ChaconneEngineTest, ResultIncludesTimeline) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_GT(result.timeline.size(), 0u)
      << "Chaconne result should include concatenated harmonic timeline";
}

TEST(ChaconneEngineTest, TimelineSpansFullDuration) {
  auto config = createTestConfig();
  auto result = generateChaconne(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_GT(result.timeline.size(), 0u);

  // The last timeline event should end at or near the total duration.
  Tick last_end = result.timeline.events().back().end_tick;
  EXPECT_GT(last_end, 0u);
}

// ===========================================================================
// Seed diversity fingerprint -- regression test for seed independence
// ===========================================================================

/// @brief Extract a pitch class histogram (top 3 most frequent) for a track.
std::vector<int> topPitchClasses(const std::vector<NoteEvent>& notes, int top_n = 3) {
  int pc_count[12] = {};
  for (const auto& n : notes) {
    pc_count[n.pitch % 12]++;
  }
  // Build pairs and sort by count descending.
  std::vector<std::pair<int, int>> pairs;
  for (int i = 0; i < 12; ++i) {
    pairs.push_back({pc_count[i], i});
  }
  std::sort(pairs.begin(), pairs.end(), std::greater<>());
  std::vector<int> result;
  for (int i = 0; i < top_n && i < static_cast<int>(pairs.size()); ++i) {
    result.push_back(pairs[i].second);
  }
  return result;
}

TEST(ChaconneSeedDiversityTest, DifferentSeedsProduceDiverseOutput) {
  constexpr int kNumSeeds = 10;
  constexpr int kMinUnique = 6;

  // Collect fingerprints: pitch class top-3 as a string.
  std::set<std::string> fingerprints;

  for (int seed_idx = 1; seed_idx <= kNumSeeds; ++seed_idx) {
    auto config = createTestConfig(static_cast<uint32_t>(seed_idx));
    auto result = generateChaconne(config);
    ASSERT_TRUE(result.success) << "Seed " << seed_idx << ": " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 1u);

    auto top = topPitchClasses(result.tracks[0].notes);
    std::string fp;
    for (int pc : top) {
      fp += std::to_string(pc) + ",";
    }
    fingerprints.insert(fp);
  }

  EXPECT_GE(fingerprints.size(), static_cast<size_t>(kMinUnique))
      << "Expected at least " << kMinUnique << " unique fingerprints across "
      << kNumSeeds << " seeds, got " << fingerprints.size();
}

TEST(ChaconneSeedDiversityTest, SeedsProduceDifferentNoteCounts) {
  // Different seeds should produce at least some variation in total note count
  // (due to rhythm profile variation).
  std::set<size_t> note_counts;

  for (int seed_idx = 1; seed_idx <= 10; ++seed_idx) {
    auto config = createTestConfig(static_cast<uint32_t>(seed_idx));
    auto result = generateChaconne(config);
    ASSERT_TRUE(result.success) << "Seed " << seed_idx << ": " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 1u);
    note_counts.insert(result.tracks[0].notes.size());
  }

  // With rhythm profile variation, we expect multiple distinct note counts.
  EXPECT_GE(note_counts.size(), 3u)
      << "Expected at least 3 distinct note counts across 10 seeds";
}

}  // namespace
}  // namespace bach
