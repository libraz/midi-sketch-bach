// Tests for Goldberg ornamental variation and trill etude generator.

#include "forms/goldberg/variations/goldberg_ornamental.h"

#include <algorithm>
#include <cstdint>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;
constexpr int kGridBars = 32;

class OrnamentalGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  /// @brief Helper to compute total note count for a given variation.
  OrnamentalResult generateVariation(int var_num, uint32_t seed = kTestSeed) {
    return generator_.generate(var_num, grid_, kGMajor, kThreeFour, seed);
  }

  /// @brief Helper to find the latest note end tick.
  static Tick maxEndTick(const std::vector<NoteEvent>& notes) {
    Tick max_end = 0;
    for (const auto& note : notes) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end > max_end) max_end = note_end;
    }
    return max_end;
  }

  GoldbergStructuralGrid grid_;
  OrnamentalGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: Var1CirculatioGenerate
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, Var1CirculatioGenerate) {
  auto result = generateVariation(1);
  EXPECT_TRUE(result.success) << "Var 1 (Circulatio) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 1 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: Var5CirculatioGenerate
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, Var5CirculatioGenerate) {
  auto result = generateVariation(5);
  EXPECT_TRUE(result.success) << "Var 5 (Circulatio ascending) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 5 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 3: Var13SarabandeGenerate
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, Var13SarabandeGenerate) {
  auto result = generateVariation(13);
  EXPECT_TRUE(result.success) << "Var 13 (Sarabande) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 13 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 4: Var14TrilloGenerate
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, Var14TrilloGenerate) {
  auto result = generateVariation(14);
  EXPECT_TRUE(result.success) << "Var 14 (Trillo) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 14 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 5: Var28TrilloGenerate
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, Var28TrilloGenerate) {
  auto result = generateVariation(28);
  EXPECT_TRUE(result.success) << "Var 28 (Trillo) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 28 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 6: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, NotesSpan32Bars) {
  Tick ticks_per_bar = kThreeFour.ticksPerBar();
  Tick expected_total = static_cast<Tick>(kGridBars) * ticks_per_bar;

  // Check all supported variations span 32 bars.
  for (int var_num : {1, 5, 13, 14, 28}) {
    auto result = generateVariation(var_num);
    ASSERT_TRUE(result.success) << "Var " << var_num << " should succeed";
    ASSERT_FALSE(result.notes.empty()) << "Var " << var_num << " should be non-empty";

    Tick max_end = maxEndTick(result.notes);

    EXPECT_GE(max_end, expected_total - ticks_per_bar)
        << "Var " << var_num << " notes should span close to 32 bars";
    EXPECT_LE(max_end, expected_total + ticks_per_bar)
        << "Var " << var_num << " notes should not extend far beyond 32 bars";
  }
}

// ---------------------------------------------------------------------------
// Test 7: TrillEtudeHighDensity
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, TrillEtudeHighDensity) {
  auto result_var13 = generateVariation(13);
  auto result_var14 = generateVariation(14);

  ASSERT_TRUE(result_var13.success);
  ASSERT_TRUE(result_var14.success);

  // Var 14 (Trillo, 4 notes/beat) should have higher note density than
  // Var 13 (Sarabande, 1 note/beat).
  EXPECT_GT(result_var14.notes.size(), result_var13.notes.size())
      << "Var 14 (trill etude, 4 notes/beat) should have more notes than "
         "Var 13 (sarabande, 1 note/beat)";
}

// ---------------------------------------------------------------------------
// Test 8: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, DifferentSeedsDifferent) {
  auto result_seed1 = generateVariation(1, 1);
  auto result_seed2 = generateVariation(1, 2);

  ASSERT_TRUE(result_seed1.success);
  ASSERT_TRUE(result_seed2.success);
  ASSERT_FALSE(result_seed1.notes.empty());
  ASSERT_FALSE(result_seed2.notes.empty());

  // Compare pitches -- different seeds should produce different results.
  size_t compare_len = std::min(result_seed1.notes.size(),
                                result_seed2.notes.size());
  int diff_count = 0;
  for (size_t idx = 0; idx < compare_len; ++idx) {
    if (result_seed1.notes[idx].pitch != result_seed2.notes[idx].pitch) {
      ++diff_count;
    }
  }

  EXPECT_GT(diff_count, 0)
      << "Different seeds should produce different note pitches";
}

// ---------------------------------------------------------------------------
// Test 9: AllVariationsSucceed
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, AllVariationsSucceed) {
  for (int var_num : {1, 5, 13, 14, 28}) {
    auto result = generateVariation(var_num);
    EXPECT_TRUE(result.success)
        << "Variation " << var_num << " should succeed";
    EXPECT_FALSE(result.notes.empty())
        << "Variation " << var_num << " should produce notes";
  }
}

// ---------------------------------------------------------------------------
// Test 10: UnsupportedVariationFails
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, UnsupportedVariationFails) {
  auto result = generateVariation(99);
  EXPECT_FALSE(result.success)
      << "Unsupported variation number should return success=false";
  EXPECT_TRUE(result.notes.empty())
      << "Unsupported variation should produce no notes";
}

// ---------------------------------------------------------------------------
// Test 11: BassNotesHaveGoldbergBassSource
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, BassNotesHaveGoldbergBassSource) {
  auto result = generateVariation(1);
  ASSERT_TRUE(result.success);

  // Bass notes (voice=1) should have GoldbergBass source.
  int bass_count = 0;
  for (const auto& note : result.notes) {
    if (note.voice == 1) {
      EXPECT_EQ(note.source, BachNoteSource::GoldbergBass)
          << "Bass note at tick " << note.start_tick
          << " should have GoldbergBass source";
      ++bass_count;
    }
  }
  EXPECT_GT(bass_count, 0) << "Should have at least some bass notes";
}

// ---------------------------------------------------------------------------
// Test 12: MelodyNotesHaveProperSource
// ---------------------------------------------------------------------------

TEST_F(OrnamentalGeneratorTest, MelodyNotesHaveProperSource) {
  auto result = generateVariation(1);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.notes) {
    if (note.voice == 0) {
      // Melody notes should be either GoldbergFigura or Ornament.
      bool valid_source = (note.source == BachNoteSource::GoldbergFigura ||
                           note.source == BachNoteSource::Ornament);
      EXPECT_TRUE(valid_source)
          << "Melody note at tick " << note.start_tick
          << " has unexpected source: "
          << static_cast<int>(note.source);
    }
  }
}

}  // namespace
}  // namespace bach
