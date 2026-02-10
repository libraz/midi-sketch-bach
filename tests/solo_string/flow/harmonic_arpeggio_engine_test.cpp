// Tests for solo_string/flow/harmonic_arpeggio_engine.h -- integration tests for the
// BWV1007-style harmonic arpeggio engine.

#include "solo_string/flow/harmonic_arpeggio_engine.h"

#include <algorithm>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "solo_string/flow/arpeggio_flow_config.h"

namespace bach {
namespace {

// ===========================================================================
// Test helpers
// ===========================================================================

/// @brief Create a minimal valid ArpeggioFlowConfig for engine tests.
/// @param num_sections Number of sections (>= 3).
/// @param bars_per_section Bars per section.
/// @param instrument Instrument type.
/// @param seed Deterministic seed (0 = auto).
/// @return Configured ArpeggioFlowConfig with default arc.
ArpeggioFlowConfig makeEngineConfig(int num_sections = 6,
                                     int bars_per_section = 4,
                                     InstrumentType instrument = InstrumentType::Cello,
                                     uint32_t seed = 42) {
  ArpeggioFlowConfig config;
  config.num_sections = num_sections;
  config.bars_per_section = bars_per_section;
  config.instrument = instrument;
  config.seed = seed;
  config.key = {Key::G, true};  // G minor
  config.bpm = 66;
  config.arc = createDefaultArcConfig(num_sections);
  config.cadence.cadence_bars = bars_per_section;
  return config;
}

// ===========================================================================
// Basic generation tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, DefaultConfigSucceeds) {
  auto config = makeEngineConfig();
  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << "Error: " << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

TEST(HarmonicArpeggioEngineTest, ProducesSingleTrack) {
  auto config = makeEngineConfig();
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_EQ(result.tracks.size(), 1u);
}

// ===========================================================================
// Note range tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, AllNotesWithinCelloRange) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  constexpr uint8_t kCelloLow = 36;   // C2
  constexpr uint8_t kCelloHigh = 81;  // A5

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, kCelloLow)
        << "Pitch " << static_cast<int>(note.pitch)
        << " below cello range at tick " << note.start_tick;
    EXPECT_LE(note.pitch, kCelloHigh)
        << "Pitch " << static_cast<int>(note.pitch)
        << " above cello range at tick " << note.start_tick;
  }
}

TEST(HarmonicArpeggioEngineTest, AllNotesWithinViolinRange) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Violin, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  constexpr uint8_t kViolinLow = 55;   // G3
  constexpr uint8_t kViolinHigh = 96;  // C7

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, kViolinLow)
        << "Pitch " << static_cast<int>(note.pitch)
        << " below violin range at tick " << note.start_tick;
    EXPECT_LE(note.pitch, kViolinHigh)
        << "Pitch " << static_cast<int>(note.pitch)
        << " above violin range at tick " << note.start_tick;
  }
}

TEST(HarmonicArpeggioEngineTest, AllNotesWithinGuitarRange) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Guitar, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  constexpr uint8_t kGuitarLow = 40;   // E2
  constexpr uint8_t kGuitarHigh = 83;  // B5

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, kGuitarLow)
        << "Pitch " << static_cast<int>(note.pitch)
        << " below guitar range at tick " << note.start_tick;
    EXPECT_LE(note.pitch, kGuitarHigh)
        << "Pitch " << static_cast<int>(note.pitch)
        << " above guitar range at tick " << note.start_tick;
  }
}

// ===========================================================================
// Note duration tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, MostNotesAreSixteenthDuration) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  constexpr Tick kSixteenthDuration = kTicksPerBeat / 4;  // 120

  int sixteenth_count = 0;
  int total_count = static_cast<int>(result.tracks[0].notes.size());
  ASSERT_GT(total_count, 0);

  for (const auto& note : result.tracks[0].notes) {
    if (note.duration == kSixteenthDuration) {
      ++sixteenth_count;
    }
  }

  // Most notes should be 16th notes. Allow some to be 8th notes (cadence
  // simplification) and quarter notes (final bar). At least 70% should be 16ths.
  float sixteenth_ratio = static_cast<float>(sixteenth_count) /
                          static_cast<float>(total_count);
  EXPECT_GE(sixteenth_ratio, 0.70f)
      << "Only " << sixteenth_count << " of " << total_count
      << " notes are 16th duration (" << (sixteenth_ratio * 100.0f) << "%)";
}

TEST(HarmonicArpeggioEngineTest, AllNotesHavePositiveDuration) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GT(note.duration, 0u)
        << "Zero-duration note at tick " << note.start_tick;
  }
}

// ===========================================================================
// Continuity tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, NotesAreContinuous) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 1u);

  const auto& notes = result.tracks[0].notes;
  ASSERT_GT(notes.size(), 1u);

  // Sort notes by start_tick for analysis.
  auto sorted_notes = notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Count gaps larger than a 16th note between consecutive notes.
  constexpr Tick kMaxAcceptableGap = kTicksPerBeat / 4 + 10;  // 130 ticks tolerance
  int gap_count = 0;

  for (size_t idx = 1; idx < sorted_notes.size(); ++idx) {
    Tick prev_end = sorted_notes[idx - 1].start_tick + sorted_notes[idx - 1].duration;
    Tick curr_start = sorted_notes[idx].start_tick;
    if (curr_start > prev_end + kMaxAcceptableGap) {
      ++gap_count;
    }
  }

  // Allow very few gaps (e.g., bar transitions, cadence simplification).
  float gap_ratio = static_cast<float>(gap_count) /
                    static_cast<float>(sorted_notes.size() - 1);
  EXPECT_LT(gap_ratio, 0.05f)
      << gap_count << " gaps found in " << sorted_notes.size() << " notes";
}

// ===========================================================================
// Multiple instruments test
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, MultipleInstrumentsWork) {
  InstrumentType instruments[] = {
      InstrumentType::Cello,
      InstrumentType::Violin,
      InstrumentType::Guitar
  };

  for (auto inst : instruments) {
    auto config = makeEngineConfig(4, 4, inst, 100);
    auto result = generateArpeggioFlow(config);
    EXPECT_TRUE(result.success)
        << "Failed for instrument " << instrumentTypeToString(inst)
        << ": " << result.error_message;
    EXPECT_FALSE(result.tracks.empty())
        << "No tracks for instrument " << instrumentTypeToString(inst);
    EXPECT_FALSE(result.tracks[0].notes.empty())
        << "No notes for instrument " << instrumentTypeToString(inst);
  }
}

// ===========================================================================
// Seed determinism tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, SameSeedProducesIdenticalOutput) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 12345);
  auto result_a = generateArpeggioFlow(config);
  auto result_b = generateArpeggioFlow(config);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);
  ASSERT_EQ(result_a.tracks.size(), result_b.tracks.size());

  const auto& notes_a = result_a.tracks[0].notes;
  const auto& notes_b = result_b.tracks[0].notes;
  ASSERT_EQ(notes_a.size(), notes_b.size());

  for (size_t idx = 0; idx < notes_a.size(); ++idx) {
    EXPECT_EQ(notes_a[idx].start_tick, notes_b[idx].start_tick)
        << "Mismatch at note index " << idx;
    EXPECT_EQ(notes_a[idx].pitch, notes_b[idx].pitch)
        << "Mismatch at note index " << idx;
    EXPECT_EQ(notes_a[idx].duration, notes_b[idx].duration)
        << "Mismatch at note index " << idx;
    EXPECT_EQ(notes_a[idx].velocity, notes_b[idx].velocity)
        << "Mismatch at note index " << idx;
  }
}

TEST(HarmonicArpeggioEngineTest, DifferentSeedsProduceDifferentOutput) {
  auto config_a = makeEngineConfig(6, 4, InstrumentType::Cello, 111);
  auto config_b = makeEngineConfig(6, 4, InstrumentType::Cello, 999);

  auto result_a = generateArpeggioFlow(config_a);
  auto result_b = generateArpeggioFlow(config_b);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);
  ASSERT_FALSE(result_a.tracks[0].notes.empty());
  ASSERT_FALSE(result_b.tracks[0].notes.empty());

  // With different seeds, at least some notes should differ.
  // Compare pitch sequences.
  const auto& notes_a = result_a.tracks[0].notes;
  const auto& notes_b = result_b.tracks[0].notes;

  int diff_count = 0;
  size_t compare_count = std::min(notes_a.size(), notes_b.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_a[idx].pitch != notes_b[idx].pitch) {
      ++diff_count;
    }
  }

  EXPECT_GT(diff_count, 0)
      << "Two different seeds produced identical pitch sequences";
}

TEST(HarmonicArpeggioEngineTest, SeedUsedFieldMatchesInput) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 54321);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.seed_used, 54321u);
}

TEST(HarmonicArpeggioEngineTest, AutoSeedResolvesToNonZero) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 0);  // auto seed
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  EXPECT_NE(result.seed_used, 0u);
}

// ===========================================================================
// Invalid configuration tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, TooFewSectionsReturnsError) {
  auto config = makeEngineConfig();
  config.num_sections = 2;  // Less than 3
  config.arc = {};  // Will be empty since createDefaultArcConfig(2) is empty

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(HarmonicArpeggioEngineTest, OneSectionReturnsError) {
  ArpeggioFlowConfig config;
  config.num_sections = 1;
  config.bars_per_section = 4;
  config.seed = 42;

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(HarmonicArpeggioEngineTest, ZeroBarsPerSectionReturnsError) {
  ArpeggioFlowConfig config;
  config.num_sections = 6;
  config.bars_per_section = 0;
  config.seed = 42;
  config.arc = createDefaultArcConfig(6);

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(HarmonicArpeggioEngineTest, InvalidArcConfigReturnsError) {
  ArpeggioFlowConfig config;
  config.num_sections = 4;
  config.bars_per_section = 4;
  config.seed = 42;

  // Create an invalid arc: reversed order.
  config.arc.phase_assignment = {
      {0, ArcPhase::Descent},
      {1, ArcPhase::Peak},
      {2, ArcPhase::Ascent},
      {3, ArcPhase::Ascent}
  };

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST(HarmonicArpeggioEngineTest, ArcSectionCountMismatchReturnsError) {
  ArpeggioFlowConfig config;
  config.num_sections = 6;
  config.bars_per_section = 4;
  config.seed = 42;

  // Arc has 4 sections but config says 6.
  config.arc = createDefaultArcConfig(4);

  auto result = generateArpeggioFlow(config);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

// ===========================================================================
// GM program mapping tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, CelloTrackHasCorrectGmProgram) {
  auto config = makeEngineConfig(4, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 42u);  // GM Cello
}

TEST(HarmonicArpeggioEngineTest, ViolinTrackHasCorrectGmProgram) {
  auto config = makeEngineConfig(4, 4, InstrumentType::Violin, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 40u);  // GM Violin
}

TEST(HarmonicArpeggioEngineTest, GuitarTrackHasCorrectGmProgram) {
  auto config = makeEngineConfig(4, 4, InstrumentType::Guitar, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].program, 24u);  // GM Nylon Guitar
}

// ===========================================================================
// Total duration tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, TotalDurationMatchesExpected) {
  constexpr int kNumSections = 6;
  constexpr int kBarsPerSection = 4;
  auto config = makeEngineConfig(kNumSections, kBarsPerSection, InstrumentType::Cello, 42);

  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);

  Tick expected_duration = static_cast<Tick>(kNumSections * kBarsPerSection) * kTicksPerBar;
  EXPECT_EQ(result.total_duration_ticks, expected_duration);
}

TEST(HarmonicArpeggioEngineTest, TotalDurationMatchesDifferentConfigs) {
  // Test several section/bar combinations.
  struct TestCase {
    int num_sections;
    int bars_per_section;
  };

  TestCase cases[] = {
      {3, 2},
      {4, 4},
      {6, 4},
      {8, 3},
      {10, 2},
  };

  for (const auto& test_case : cases) {
    auto config = makeEngineConfig(test_case.num_sections, test_case.bars_per_section,
                                    InstrumentType::Cello, 42);
    auto result = generateArpeggioFlow(config);
    ASSERT_TRUE(result.success)
        << "Failed for sections=" << test_case.num_sections
        << " bars=" << test_case.bars_per_section;

    Tick expected = static_cast<Tick>(test_case.num_sections * test_case.bars_per_section)
                    * kTicksPerBar;
    EXPECT_EQ(result.total_duration_ticks, expected)
        << "Duration mismatch for sections=" << test_case.num_sections
        << " bars=" << test_case.bars_per_section;
  }
}

// ===========================================================================
// Velocity tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, VelocityIsReasonable) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.velocity, 60u)
        << "Velocity " << static_cast<int>(note.velocity)
        << " too low at tick " << note.start_tick;
    EXPECT_LE(note.velocity, 127u)
        << "Velocity " << static_cast<int>(note.velocity)
        << " exceeds MIDI max at tick " << note.start_tick;
  }
}

// ===========================================================================
// Track metadata tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, TrackChannelIsZero) {
  auto config = makeEngineConfig(4, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);
  EXPECT_EQ(result.tracks[0].channel, 0u);
}

TEST(HarmonicArpeggioEngineTest, TrackNameMatchesInstrument) {
  auto cello_config = makeEngineConfig(4, 4, InstrumentType::Cello, 42);
  auto cello_result = generateArpeggioFlow(cello_config);
  ASSERT_TRUE(cello_result.success);
  EXPECT_EQ(cello_result.tracks[0].name, "Cello");

  auto violin_config = makeEngineConfig(4, 4, InstrumentType::Violin, 42);
  auto violin_result = generateArpeggioFlow(violin_config);
  ASSERT_TRUE(violin_result.success);
  EXPECT_EQ(violin_result.tracks[0].name, "Violin");

  auto guitar_config = makeEngineConfig(4, 4, InstrumentType::Guitar, 42);
  auto guitar_result = generateArpeggioFlow(guitar_config);
  ASSERT_TRUE(guitar_result.success);
  EXPECT_EQ(guitar_result.tracks[0].name, "Guitar");
}

// ===========================================================================
// Key variation tests
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, WorksWithMajorKey) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  config.key = {Key::C, false};  // C major

  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

TEST(HarmonicArpeggioEngineTest, WorksWithMinorKey) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  config.key = {Key::D, true};  // D minor (default)

  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

// ===========================================================================
// Default arc usage test
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, EmptyArcUsesDefaultArc) {
  ArpeggioFlowConfig config;
  config.num_sections = 6;
  config.bars_per_section = 4;
  config.seed = 42;
  config.arc = {};  // Empty arc -- engine should use default

  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

// ===========================================================================
// Minimum viable config test
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, MinimumThreeSectionsOneBarsEach) {
  auto config = makeEngineConfig(3, 1, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  EXPECT_FALSE(result.tracks[0].notes.empty());
}

// ===========================================================================
// Voice field test
// ===========================================================================

TEST(HarmonicArpeggioEngineTest, AllNotesHaveVoiceZero) {
  auto config = makeEngineConfig(6, 4, InstrumentType::Cello, 42);
  auto result = generateArpeggioFlow(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 1u);

  for (const auto& note : result.tracks[0].notes) {
    EXPECT_EQ(note.voice, 0u)
        << "Solo instrument note should have voice=0 at tick " << note.start_tick;
  }
}

}  // namespace
}  // namespace bach
