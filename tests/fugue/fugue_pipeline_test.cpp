// Tests for the constraint-driven fugue pipeline.

#include "fugue/fugue_pipeline.h"

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "fugue/fugue_config.h"
#include "fugue/fugue_structure.h"

namespace bach {
namespace {

/// @brief Create a minimal FugueConfig for testing.
FugueConfig makeTestConfig(uint32_t seed = 42, uint8_t num_voices = 3) {
  FugueConfig config;
  config.seed = seed;
  config.num_voices = num_voices;
  config.key = Key::C;
  config.is_minor = false;
  config.archetype = FugueArchetype::Compact;
  return config;
}

TEST(FuguePipelineTest, BasicPipelineReturnsResult) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFuguePipeline(config);

  // Pipeline is fully implemented; expect success.
  EXPECT_TRUE(result.success) << "error: " << result.error_message;
}

TEST(FuguePipelineTest, SubjectGenerationSucceeds) {
  FugueConfig config = makeTestConfig(12345);
  FugueResult result = generateFuguePipeline(config);

  // Subject generation should succeed for a reasonable seed.
  EXPECT_TRUE(result.success);
  EXPECT_GE(result.attempts, 1);
}

TEST(FuguePipelineTest, TracksCreatedForVoiceCount) {
  for (uint8_t voices = 2; voices <= 5; ++voices) {
    FugueConfig config = makeTestConfig(42, voices);
    FugueResult result = generateFuguePipeline(config);

    if (result.success) {
      EXPECT_EQ(result.tracks.size(), voices)
          << "Expected " << static_cast<int>(voices) << " tracks for "
          << static_cast<int>(voices) << " voices";
    }
  }
}

TEST(FuguePipelineTest, DifferentSeedsProduceResults) {
  for (uint32_t seed = 0; seed < 5; ++seed) {
    FugueConfig config = makeTestConfig(seed * 1000 + 100);
    FugueResult result = generateFuguePipeline(config);

    // Every seed should produce a valid result (subject generation).
    EXPECT_TRUE(result.success)
        << "Failed for seed " << (seed * 1000 + 100);
  }
}

TEST(FuguePipelineTest, MinorKeySupported) {
  FugueConfig config = makeTestConfig();
  config.is_minor = true;
  config.key = Key::D;

  FugueResult result = generateFuguePipeline(config);
  EXPECT_TRUE(result.success);
}

// ---------------------------------------------------------------------------
// StabilityMultiVoiceMultiSeed: 3 voice counts x 5 seeds = 15 configs.
// All must succeed without crashing.
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, StabilityMultiVoiceMultiSeed) {
  const uint8_t voice_counts[] = {3, 4, 5};
  const uint32_t seeds[] = {42, 123, 789, 2024, 5555};

  for (uint8_t voices : voice_counts) {
    for (uint32_t seed : seeds) {
      FugueConfig config = makeTestConfig(seed, voices);
      FugueResult result = generateFuguePipeline(config);

      EXPECT_TRUE(result.success)
          << "Failed for voices=" << static_cast<int>(voices)
          << " seed=" << seed << ": " << result.error_message;

      if (result.success) {
        EXPECT_EQ(result.tracks.size(), voices)
            << "Track count mismatch for voices=" << static_cast<int>(voices)
            << " seed=" << seed;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// MinorKeyStability: D, G, A, E minor with 4 voices and seed 42.
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, MinorKeyStability) {
  const Key minor_keys[] = {Key::D, Key::G, Key::A, Key::E};

  for (Key k : minor_keys) {
    FugueConfig config = makeTestConfig(42, 4);
    config.is_minor = true;
    config.key = k;

    FugueResult result = generateFuguePipeline(config);

    EXPECT_TRUE(result.success)
        << "Failed for minor key " << keyToString(k) << ": "
        << result.error_message;
  }
}

// ---------------------------------------------------------------------------
// StructureHasAllSections: a successful 4-voice fugue must contain at least
// one Exposition section and at least one Develop-phase section (Episode or
// MiddleEntry).
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, StructureHasAllSections) {
  FugueConfig config = makeTestConfig(42, 4);
  FugueResult result = generateFuguePipeline(config);
  ASSERT_TRUE(result.success) << "Pipeline failed: " << result.error_message;

  const auto& sections = result.structure.sections;
  ASSERT_FALSE(sections.empty()) << "Structure has no sections";

  // Must have at least one Exposition.
  bool has_exposition = std::any_of(
      sections.begin(), sections.end(),
      [](const FugueSection& s) { return s.type == SectionType::Exposition; });
  EXPECT_TRUE(has_exposition) << "No Exposition section found in structure";

  // Must have at least one section in the Develop phase.
  bool has_develop = std::any_of(
      sections.begin(), sections.end(), [](const FugueSection& s) {
        return s.phase == FuguePhase::Develop &&
               (s.type == SectionType::Episode ||
                s.type == SectionType::MiddleEntry);
      });
  EXPECT_TRUE(has_develop) << "No Develop-phase section (Episode/MiddleEntry) "
                              "found in structure";
}

// ---------------------------------------------------------------------------
// AllTracksHaveNotes: for a successful result, every track should contain at
// least one note.
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, AllTracksHaveNotes) {
  FugueConfig config = makeTestConfig(42, 4);
  FugueResult result = generateFuguePipeline(config);
  ASSERT_TRUE(result.success) << "Pipeline failed: " << result.error_message;
  ASSERT_EQ(result.tracks.size(), 4u);

  for (size_t i = 0; i < result.tracks.size(); ++i) {
    EXPECT_FALSE(result.tracks[i].notes.empty())
        << "Track " << i << " has no notes";
  }
}

// ---------------------------------------------------------------------------
// NoWithinVoiceOverlaps: for each track, sort notes by start_tick and verify
// no note begins before the previous note ends. Allows 1-tick tolerance for
// rounding.
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, NoWithinVoiceOverlaps) {
  FugueConfig config = makeTestConfig(42, 4);
  FugueResult result = generateFuguePipeline(config);
  ASSERT_TRUE(result.success) << "Pipeline failed: " << result.error_message;

  constexpr Tick kOverlapTolerance = 1;  // 1-tick rounding tolerance.

  for (size_t t = 0; t < result.tracks.size(); ++t) {
    std::vector<NoteEvent> notes = result.tracks[t].notes;
    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    for (size_t i = 1; i < notes.size(); ++i) {
      Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;
      if (notes[i].start_tick + kOverlapTolerance < prev_end) {
        ADD_FAILURE()
            << "Within-voice overlap in track " << t << ": note at tick "
            << notes[i - 1].start_tick << " (dur " << notes[i - 1].duration
            << ", end " << prev_end << ") overlaps note at tick "
            << notes[i].start_tick << " (pitch " << static_cast<int>(notes[i].pitch)
            << ")";
        break;  // Report only the first overlap per track.
      }
    }
  }
}

// ---------------------------------------------------------------------------
// NoteCountReasonable: for a successful 4-voice fugue, the total note count
// should be > 50.
// ---------------------------------------------------------------------------
TEST(FuguePipelineTest, NoteCountReasonable) {
  FugueConfig config = makeTestConfig(42, 4);
  FugueResult result = generateFuguePipeline(config);
  ASSERT_TRUE(result.success) << "Pipeline failed: " << result.error_message;

  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }

  EXPECT_GT(total_notes, 50u)
      << "Total note count " << total_notes
      << " is unreasonably low for a 4-voice fugue";
}

}  // namespace
}  // namespace bach
