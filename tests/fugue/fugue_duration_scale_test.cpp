// Tests for fugue duration scaling (develop_pairs and episode_bars).

#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"

#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ===========================================================================
// FugueConfig defaults
// ===========================================================================

TEST(FugueDurationScaleTest, DefaultDevelopPairsIsTwo) {
  FugueConfig config;
  EXPECT_EQ(config.develop_pairs, 2);
}

TEST(FugueDurationScaleTest, DefaultEpisodeBarsIsTwo) {
  FugueConfig config;
  EXPECT_EQ(config.episode_bars, 2);
}

// ===========================================================================
// Backward compatibility: Short scale (develop_pairs=1) produces valid fugue
// ===========================================================================

TEST(FugueDurationScaleTest, ShortScaleGeneratesSuccessfully) {
  FugueConfig config;
  config.seed = 42;
  config.key = Key::C;
  config.num_voices = 3;
  config.develop_pairs = 1;
  config.episode_bars = 2;

  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
}

// ===========================================================================
// Parametric test for all scale levels
// ===========================================================================

struct FugueScaleParam {
  int develop_pairs;
  int episode_bars;
  const char* label;
};

class FugueDurationScaleParamTest : public ::testing::TestWithParam<FugueScaleParam> {};

TEST_P(FugueDurationScaleParamTest, GeneratesSuccessfully) {
  FugueConfig config;
  config.seed = 42;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.develop_pairs = GetParam().develop_pairs;
  config.episode_bars = GetParam().episode_bars;

  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success) << "Failed for " << GetParam().label
                               << ": " << result.error_message;
}

TEST_P(FugueDurationScaleParamTest, HasNonEmptyTracks) {
  FugueConfig config;
  config.seed = 42;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.develop_pairs = GetParam().develop_pairs;
  config.episode_bars = GetParam().episode_bars;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }
  EXPECT_GT(total_notes, 0u) << "No notes generated for " << GetParam().label;
}

TEST_P(FugueDurationScaleParamTest, LargerScaleProducesMoreNotes) {
  // Generate both Short and the parameterized scale.
  FugueConfig short_config;
  short_config.seed = 42;
  short_config.key = Key::C;
  short_config.num_voices = 3;
  short_config.develop_pairs = 1;
  short_config.episode_bars = 2;

  FugueConfig scaled_config = short_config;
  scaled_config.develop_pairs = GetParam().develop_pairs;
  scaled_config.episode_bars = GetParam().episode_bars;

  FugueResult short_result = generateFugue(short_config);
  FugueResult scaled_result = generateFugue(scaled_config);

  ASSERT_TRUE(short_result.success);
  ASSERT_TRUE(scaled_result.success);

  // Count total duration.
  Tick short_max = 0;
  for (const auto& track : short_result.tracks) {
    for (const auto& note : track.notes) {
      Tick end = note.start_tick + note.duration;
      if (end > short_max) short_max = end;
    }
  }

  Tick scaled_max = 0;
  for (const auto& track : scaled_result.tracks) {
    for (const auto& note : track.notes) {
      Tick end = note.start_tick + note.duration;
      if (end > scaled_max) scaled_max = end;
    }
  }

  EXPECT_GE(scaled_max, short_max)
      << "Scaled fugue (" << GetParam().label << ") should be at least as long as Short";
}

TEST_P(FugueDurationScaleParamTest, StructureHasCorrectPhaseOrder) {
  FugueConfig config;
  config.seed = 42;
  config.key = Key::C;
  config.num_voices = 3;
  config.develop_pairs = GetParam().develop_pairs;
  config.episode_bars = GetParam().episode_bars;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Verify FuguePhase order is Establish → Develop → Resolve (monotonic).
  const auto& sections = result.structure.sections;
  ASSERT_FALSE(sections.empty());

  uint8_t last_phase = 0;
  for (const auto& section : sections) {
    uint8_t phase_val = static_cast<uint8_t>(section.phase);
    EXPECT_GE(phase_val, last_phase)
        << "FuguePhase order violated: "
        << fuguePhaseToString(section.phase) << " after phase " << last_phase;
    last_phase = phase_val;
  }
}

INSTANTIATE_TEST_SUITE_P(
    AllScales, FugueDurationScaleParamTest,
    ::testing::Values(
        FugueScaleParam{2, 2, "Short"},
        FugueScaleParam{3, 3, "Medium"},
        FugueScaleParam{5, 3, "Long"},
        FugueScaleParam{8, 4, "Full"}
    ),
    [](const ::testing::TestParamInfo<FugueScaleParam>& info) {
      return info.param.label;
    }
);

// ===========================================================================
// Seed determinism with different scales
// ===========================================================================

TEST(FugueDurationScaleTest, SameSeedProducesSameOutputForMediumScale) {
  FugueConfig config;
  config.seed = 123;
  config.key = Key::D;
  config.num_voices = 3;
  config.develop_pairs = 3;
  config.episode_bars = 3;

  FugueResult r1 = generateFugue(config);
  FugueResult r2 = generateFugue(config);

  ASSERT_TRUE(r1.success);
  ASSERT_TRUE(r2.success);
  ASSERT_EQ(r1.tracks.size(), r2.tracks.size());

  for (size_t t = 0; t < r1.tracks.size(); ++t) {
    ASSERT_EQ(r1.tracks[t].notes.size(), r2.tracks[t].notes.size())
        << "Note count mismatch in track " << t;
    for (size_t n = 0; n < r1.tracks[t].notes.size(); ++n) {
      EXPECT_EQ(r1.tracks[t].notes[n].start_tick, r2.tracks[t].notes[n].start_tick);
      EXPECT_EQ(r1.tracks[t].notes[n].pitch, r2.tracks[t].notes[n].pitch);
    }
  }
}

}  // namespace
}  // namespace bach
