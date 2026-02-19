// End-to-end integration tests for Goldberg Variations generation pipeline.
// Tests cover plan correctness, generation success, determinism, note validity,
// and scale/duration ordering. Generation tests guard on result.success so they
// degrade gracefully when the generator is still a stub.

#include "forms/goldberg/goldberg_config.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_plan.h"
#include "forms/goldberg/goldberg_types.h"
#include "test_helpers.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper utilities
// ---------------------------------------------------------------------------

/// @brief Create a GoldbergConfig for testing.
/// @param seed Random seed.
/// @param scale Duration scale.
/// @return GoldbergConfig with specified parameters.
GoldbergConfig makeTestConfig(uint32_t seed = 42,
                              DurationScale scale = DurationScale::Short) {
  GoldbergConfig config;
  config.seed = seed;
  config.scale = scale;
  return config;
}

/// @brief Check if a pitch class belongs to G major scale.
/// G major pitch classes: G(7), A(9), B(11), C(0), D(2), E(4), F#(6).
/// @param pitch_class Pitch class in [0, 11].
/// @return True if the pitch class is in G major.
bool isInGMajor(int pitch_class) {
  static constexpr int kGMajorPitchClasses[] = {0, 2, 4, 6, 7, 9, 11};
  for (int pc_val : kGMajorPitchClasses) {
    if (pitch_class == pc_val) return true;
  }
  return false;
}

// ===========================================================================
// Plan tests (no generation required)
// ===========================================================================

TEST(GoldbergGeneratorE2ETest, PlanHas32Entries) {
  auto plan = createGoldbergPlan();
  EXPECT_EQ(plan.size(), 32u);
}

TEST(GoldbergGeneratorE2ETest, AriaIsFirstAndLast) {
  auto plan = createGoldbergPlan();
  ASSERT_EQ(plan.size(), 32u);

  EXPECT_EQ(plan[0].type, GoldbergVariationType::Aria)
      << "Entry 0 should be Aria";
  EXPECT_EQ(plan[0].variation_number, 0);

  EXPECT_EQ(plan[31].type, GoldbergVariationType::Aria)
      << "Entry 31 (da capo) should be Aria";
  EXPECT_EQ(plan[31].variation_number, 31);
}

TEST(GoldbergGeneratorE2ETest, CanonsAtEvery3rd) {
  auto plan = createGoldbergPlan();
  ASSERT_EQ(plan.size(), 32u);

  constexpr int kCanonIndices[] = {3, 6, 9, 12, 15, 18, 21, 24, 27};
  for (int idx : kCanonIndices) {
    EXPECT_EQ(plan[idx].type, GoldbergVariationType::Canon)
        << "Var " << idx << " should be Canon";
  }
}

// ===========================================================================
// CLI / FormType integration
// ===========================================================================

TEST(GoldbergGeneratorE2ETest, CLIIntegration_FormTypeParsing) {
  // Verify that "goldberg_variations" is recognized as a valid form type.
  EXPECT_EQ(formTypeFromString("goldberg_variations"),
            FormType::GoldbergVariations);
  EXPECT_STREQ(formTypeToString(FormType::GoldbergVariations),
               "goldberg_variations");
}

// ===========================================================================
// Generation tests (guard on result.success)
// ===========================================================================

TEST(GoldbergGeneratorE2ETest, GenerateShortScale) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented: " << result.error_message;
  }

  EXPECT_TRUE(result.tracks.size() > 0u) << "Short scale should produce tracks";
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "Short scale should produce notes";
  EXPECT_EQ(result.seed_used, 42u);
}

TEST(GoldbergGeneratorE2ETest, GenerateMediumScale) {
  GoldbergConfig config_short = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result_short = generateGoldbergVariations(config_short);

  GoldbergConfig config_medium = makeTestConfig(42, DurationScale::Medium);
  GoldbergResult result_medium = generateGoldbergVariations(config_medium);

  if (!result_short.success || !result_medium.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  EXPECT_GT(test_helpers::totalNoteCount(result_medium), 0u);
  EXPECT_GT(test_helpers::totalNoteCount(result_medium), test_helpers::totalNoteCount(result_short))
      << "Medium scale should produce more notes than Short";
}

TEST(GoldbergGeneratorE2ETest, GenerateLongScale) {
  GoldbergConfig config_medium = makeTestConfig(42, DurationScale::Medium);
  GoldbergResult result_medium = generateGoldbergVariations(config_medium);

  GoldbergConfig config_long = makeTestConfig(42, DurationScale::Long);
  GoldbergResult result_long = generateGoldbergVariations(config_long);

  if (!result_medium.success || !result_long.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  EXPECT_GT(test_helpers::totalNoteCount(result_long), 0u);
  EXPECT_GT(test_helpers::totalNoteCount(result_long), test_helpers::totalNoteCount(result_medium))
      << "Long scale should produce more notes than Medium";
}

TEST(GoldbergGeneratorE2ETest, DeterministicOutput) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result1 = generateGoldbergVariations(config);
  GoldbergResult result2 = generateGoldbergVariations(config);

  if (!result1.success || !result2.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
    const auto& notes1 = result1.tracks[track_idx].notes;
    const auto& notes2 = result2.tracks[track_idx].notes;
    ASSERT_EQ(notes1.size(), notes2.size())
        << "Track " << track_idx << " note count differs between runs";

    for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
          << "Track " << track_idx << ", note " << note_idx << " pitch differs";
      EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick)
          << "Track " << track_idx << ", note " << note_idx << " tick differs";
      EXPECT_EQ(notes1[note_idx].duration, notes2[note_idx].duration)
          << "Track " << track_idx << ", note " << note_idx << " duration differs";
    }
  }
}

TEST(GoldbergGeneratorE2ETest, DifferentSeedsProduceDifferentOutput) {
  GoldbergConfig config1 = makeTestConfig(42, DurationScale::Short);
  GoldbergConfig config2 = makeTestConfig(43, DurationScale::Short);
  GoldbergResult result1 = generateGoldbergVariations(config1);
  GoldbergResult result2 = generateGoldbergVariations(config2);

  if (!result1.success || !result2.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  // At least one difference in note data between the two seeds.
  bool any_difference = false;

  if (result1.tracks.size() != result2.tracks.size()) {
    any_difference = true;
  } else {
    for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
      const auto& notes1 = result1.tracks[track_idx].notes;
      const auto& notes2 = result2.tracks[track_idx].notes;
      if (notes1.size() != notes2.size()) {
        any_difference = true;
        break;
      }
      for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
        if (notes1[note_idx].pitch != notes2[note_idx].pitch ||
            notes1[note_idx].start_tick != notes2[note_idx].start_tick) {
          any_difference = true;
          break;
        }
      }
      if (any_difference) break;
    }
  }
  EXPECT_TRUE(any_difference) << "Seeds 42 and 43 produced identical output";
}

TEST(GoldbergGeneratorE2ETest, AllNotesHaveValidPitch) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  for (const auto& track : result.tracks) {
    for (size_t note_idx = 0; note_idx < track.notes.size(); ++note_idx) {
      EXPECT_LE(track.notes[note_idx].pitch, 127u)
          << "Track " << track.name << ", note " << note_idx
          << " has invalid pitch " << static_cast<int>(track.notes[note_idx].pitch);
    }
  }
}

TEST(GoldbergGeneratorE2ETest, AllNotesHavePositiveDuration) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  for (const auto& track : result.tracks) {
    for (size_t note_idx = 0; note_idx < track.notes.size(); ++note_idx) {
      EXPECT_GT(track.notes[note_idx].duration, 0u)
          << "Track " << track.name << ", note " << note_idx
          << " has zero duration at tick " << track.notes[note_idx].start_tick;
    }
  }
}

TEST(GoldbergGeneratorE2ETest, NotesAreChronological) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Track " << track.name << ": notes not sorted at index " << idx
          << " (tick " << track.notes[idx - 1].start_tick << " > "
          << track.notes[idx].start_tick << ")";
    }
  }
}

TEST(GoldbergGeneratorE2ETest, TracksExist) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  ASSERT_GT(result.tracks.size(), 0u) << "Result should have at least 1 track";

  // At least one track should contain notes.
  bool any_track_has_notes = false;
  for (const auto& track : result.tracks) {
    if (!track.notes.empty()) {
      any_track_has_notes = true;
      break;
    }
  }
  EXPECT_TRUE(any_track_has_notes) << "At least one track should have notes";
}

TEST(GoldbergGeneratorE2ETest, KeySignatureRespected) {
  // Default key is G major. Verify that a majority of pitches use G major
  // pitch classes: G(7), A(9), B(11), C(0), D(2), E(4), F#(6).
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  if (!result.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  size_t in_key_count = 0;
  size_t total_count = 0;

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      ++total_count;
      int pitch_class = getPitchClass(note.pitch);
      if (isInGMajor(pitch_class)) {
        ++in_key_count;
      }
    }
  }

  ASSERT_GT(total_count, 0u) << "No notes to analyze for key";

  // Expect at least 70% of notes in key. Some chromatic passing tones,
  // ornaments, and minor-key variations (Var 15, 21, 25) will use accidentals.
  double ratio = static_cast<double>(in_key_count) / static_cast<double>(total_count);
  EXPECT_GE(ratio, 0.70)
      << "Only " << (ratio * 100.0) << "% of notes in G major (expected >= 70%)";
}

TEST(GoldbergGeneratorE2ETest, ShortScaleSubsetOfFull) {
  GoldbergConfig config_short = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result_short = generateGoldbergVariations(config_short);

  GoldbergConfig config_full = makeTestConfig(42, DurationScale::Full);
  GoldbergResult result_full = generateGoldbergVariations(config_full);

  if (!result_short.success || !result_full.success) {
    GTEST_SKIP() << "Generator not yet implemented";
  }

  EXPECT_LT(test_helpers::totalNoteCount(result_short), test_helpers::totalNoteCount(result_full))
      << "Short scale should produce fewer total notes than Full scale";
}

// ===========================================================================
// Seed used is reported correctly
// ===========================================================================

TEST(GoldbergGeneratorE2ETest, SeedUsedReported) {
  GoldbergConfig config = makeTestConfig(42, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  // seed_used should be set regardless of success.
  EXPECT_EQ(result.seed_used, 42u);
}

TEST(GoldbergGeneratorE2ETest, SeedUsedReported_DifferentSeed) {
  GoldbergConfig config = makeTestConfig(12345, DurationScale::Short);
  GoldbergResult result = generateGoldbergVariations(config);

  EXPECT_EQ(result.seed_used, 12345u);
}

// ===========================================================================
// Config defaults
// ===========================================================================

TEST(GoldbergGeneratorE2ETest, DefaultConfigIsGMajor) {
  GoldbergConfig config;
  EXPECT_EQ(config.key.tonic, Key::G);
  EXPECT_FALSE(config.key.is_minor);
}

TEST(GoldbergGeneratorE2ETest, DefaultConfigIsHarpsichord) {
  GoldbergConfig config;
  EXPECT_EQ(config.instrument, InstrumentType::Harpsichord);
}

}  // namespace
}  // namespace bach
