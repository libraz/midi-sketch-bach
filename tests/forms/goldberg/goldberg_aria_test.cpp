// Tests for Goldberg Aria (Sarabande) generator.

#include "forms/goldberg/goldberg_aria.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;
constexpr int kGridBars = 32;

class AriaGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
    result_ = generator_.generate(grid_, kGMajor, kThreeFour, kTestSeed);
  }

  /// @brief Helper to compute average pitch of a note vector.
  static float averagePitch(const std::vector<NoteEvent>& notes) {
    if (notes.empty()) return 0.0f;
    float sum = 0.0f;
    int count = 0;
    for (const auto& note : notes) {
      if (note.pitch > 0) {
        sum += static_cast<float>(note.pitch);
        ++count;
      }
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
  }

  GoldbergStructuralGrid grid_;
  AriaGenerator generator_;
  AriaResult result_;
};

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesResult
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, GenerateProducesResult) {
  EXPECT_TRUE(result_.success) << "generate() should return success=true";
}

// ---------------------------------------------------------------------------
// Test 2: MelodyNotesNonEmpty
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, MelodyNotesNonEmpty) {
  EXPECT_FALSE(result_.melody_notes.empty())
      << "Aria melody notes should be non-empty";
}

// ---------------------------------------------------------------------------
// Test 3: BassNotesNonEmpty
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, BassNotesNonEmpty) {
  EXPECT_FALSE(result_.bass_notes.empty())
      << "Aria bass notes should be non-empty";
}

// ---------------------------------------------------------------------------
// Test 4: MelodySpans32Bars
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, MelodySpans32Bars) {
  ASSERT_FALSE(result_.melody_notes.empty());

  Tick ticks_per_bar = kThreeFour.ticksPerBar();  // 3 * 480 = 1440
  Tick expected_total = static_cast<Tick>(kGridBars) * ticks_per_bar;

  Tick max_end = 0;
  for (const auto& note : result_.melody_notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) max_end = note_end;
  }

  // Last note should end at or near the 32-bar boundary.
  EXPECT_GE(max_end, expected_total - ticks_per_bar)
      << "Melody notes should span close to 32 bars";
  EXPECT_LE(max_end, expected_total + ticks_per_bar)
      << "Melody notes should not extend far beyond 32 bars";
}

// ---------------------------------------------------------------------------
// Test 5: BassSpans32Bars
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, BassSpans32Bars) {
  ASSERT_FALSE(result_.bass_notes.empty());

  Tick ticks_per_bar = kThreeFour.ticksPerBar();
  Tick expected_total = static_cast<Tick>(kGridBars) * ticks_per_bar;

  Tick max_end = 0;
  for (const auto& note : result_.bass_notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) max_end = note_end;
  }

  EXPECT_GE(max_end, expected_total - ticks_per_bar)
      << "Bass notes should span close to 32 bars";
  EXPECT_LE(max_end, expected_total + ticks_per_bar)
      << "Bass notes should not extend far beyond 32 bars";
}

// ---------------------------------------------------------------------------
// Test 6: MelodyHasAriaSource
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, MelodyHasAriaSource) {
  ASSERT_FALSE(result_.melody_notes.empty());

  for (const auto& note : result_.melody_notes) {
    // Melody notes should be either GoldbergAria or Ornament (from ornament engine).
    bool valid_source = (note.source == BachNoteSource::GoldbergAria ||
                         note.source == BachNoteSource::Ornament);
    EXPECT_TRUE(valid_source)
        << "Melody note at tick " << note.start_tick
        << " has unexpected source: "
        << static_cast<int>(note.source);
  }
}

// ---------------------------------------------------------------------------
// Test 7: BassHasGoldbergBassSource
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, BassHasGoldbergBassSource) {
  ASSERT_FALSE(result_.bass_notes.empty());

  for (const auto& note : result_.bass_notes) {
    EXPECT_EQ(note.source, BachNoteSource::GoldbergBass)
        << "Bass note at tick " << note.start_tick
        << " should have GoldbergBass source";
  }
}

// ---------------------------------------------------------------------------
// Test 8: BassUsesStructuralPitches
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, BassUsesStructuralPitches) {
  ASSERT_FALSE(result_.bass_notes.empty());

  // Collect structural pitch classes from the grid.
  std::vector<int> structural_pcs;
  structural_pcs.reserve(kGridBars);
  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    uint8_t pitch = grid_.getStructuralBassPitch(bar_idx);
    structural_pcs.push_back(getPitchClass(pitch));
  }

  // Check that bass note pitch classes predominantly match structural pitches.
  int match_count = 0;
  int total_count = 0;
  for (const auto& note : result_.bass_notes) {
    if (note.pitch == 0) continue;
    int note_pc = getPitchClass(note.pitch);
    for (int structural_pc : structural_pcs) {
      if (note_pc == structural_pc) {
        ++match_count;
        break;
      }
    }
    ++total_count;
  }

  if (total_count > 0) {
    float match_ratio = static_cast<float>(match_count) /
                        static_cast<float>(total_count);
    EXPECT_GT(match_ratio, 0.7f)
        << "Bass pitches should predominantly match structural grid pitch classes";
  }
}

// ---------------------------------------------------------------------------
// Test 9: MelodyInUpperRegister
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, MelodyInUpperRegister) {
  ASSERT_FALSE(result_.melody_notes.empty());
  ASSERT_FALSE(result_.bass_notes.empty());

  float melody_avg = averagePitch(result_.melody_notes);
  float bass_avg = averagePitch(result_.bass_notes);

  EXPECT_GT(melody_avg, bass_avg)
      << "Melody average pitch (" << melody_avg
      << ") should be higher than bass average pitch (" << bass_avg << ")";
}

// ---------------------------------------------------------------------------
// Test 10: DaCapoOffsetsCorrectly
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, DaCapoOffsetsCorrectly) {
  constexpr Tick kOffset = 100000;
  auto da_capo = AriaGenerator::createDaCapo(result_, kOffset);

  EXPECT_TRUE(da_capo.success);
  ASSERT_EQ(da_capo.melody_notes.size(), result_.melody_notes.size());
  ASSERT_EQ(da_capo.bass_notes.size(), result_.bass_notes.size());

  // Verify all melody notes are offset by kOffset.
  for (size_t idx = 0; idx < result_.melody_notes.size(); ++idx) {
    EXPECT_EQ(da_capo.melody_notes[idx].start_tick,
              result_.melody_notes[idx].start_tick + kOffset)
        << "Melody note " << idx << " should be offset by " << kOffset;
  }

  // Verify all bass notes are offset by kOffset.
  for (size_t idx = 0; idx < result_.bass_notes.size(); ++idx) {
    EXPECT_EQ(da_capo.bass_notes[idx].start_tick,
              result_.bass_notes[idx].start_tick + kOffset)
        << "Bass note " << idx << " should be offset by " << kOffset;
  }
}

// ---------------------------------------------------------------------------
// Test 11: DaCapoPreservesPitches
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, DaCapoPreservesPitches) {
  constexpr Tick kOffset = 50000;
  auto da_capo = AriaGenerator::createDaCapo(result_, kOffset);

  ASSERT_EQ(da_capo.melody_notes.size(), result_.melody_notes.size());

  for (size_t idx = 0; idx < result_.melody_notes.size(); ++idx) {
    EXPECT_EQ(da_capo.melody_notes[idx].pitch,
              result_.melody_notes[idx].pitch)
        << "Da capo melody note " << idx << " should preserve original pitch";
  }

  for (size_t idx = 0; idx < result_.bass_notes.size(); ++idx) {
    EXPECT_EQ(da_capo.bass_notes[idx].pitch,
              result_.bass_notes[idx].pitch)
        << "Da capo bass note " << idx << " should preserve original pitch";
  }
}

// ---------------------------------------------------------------------------
// Test 12: DifferentSeedsDifferentMelodies
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, DifferentSeedsDifferentMelodies) {
  auto result_seed1 = generator_.generate(grid_, kGMajor, kThreeFour, 1);
  auto result_seed2 = generator_.generate(grid_, kGMajor, kThreeFour, 2);

  ASSERT_TRUE(result_seed1.success);
  ASSERT_TRUE(result_seed2.success);
  ASSERT_FALSE(result_seed1.melody_notes.empty());
  ASSERT_FALSE(result_seed2.melody_notes.empty());

  // Compare melody pitches -- different seeds should produce different melodies.
  size_t compare_len = std::min(result_seed1.melody_notes.size(),
                                result_seed2.melody_notes.size());
  int diff_count = 0;
  for (size_t idx = 0; idx < compare_len; ++idx) {
    if (result_seed1.melody_notes[idx].pitch !=
        result_seed2.melody_notes[idx].pitch) {
      ++diff_count;
    }
  }

  EXPECT_GT(diff_count, 0)
      << "Different seeds should produce different melody pitches";
}

}  // namespace
}  // namespace bach
