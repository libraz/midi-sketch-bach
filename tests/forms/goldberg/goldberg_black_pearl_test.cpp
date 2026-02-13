// Tests for BlackPearl (Var 25) generator: G minor Adagio with suspension chains.

#include "forms/goldberg/variations/goldberg_black_pearl.h"

#include <algorithm>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMinor = {Key::G, true};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;
constexpr int kGridBars = 32;

class BlackPearlGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMinor(MinorModeProfile::MixedBaroqueMinor);
  }

  BlackPearlResult generateVariation(uint32_t seed = kTestSeed) {
    return generator_.generate(grid_, kGMinor, kThreeFour, seed);
  }

  /// @brief Find the latest note end tick in a note vector.
  static Tick maxEndTick(const std::vector<NoteEvent>& notes) {
    Tick max_end = 0;
    for (const auto& note : notes) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end > max_end) max_end = note_end;
    }
    return max_end;
  }

  GoldbergStructuralGrid grid_;
  BlackPearlGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesResult
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, GenerateProducesResult) {
  auto result = generateVariation();
  EXPECT_TRUE(result.success) << "BlackPearl should generate successfully";
  EXPECT_FALSE(result.notes.empty()) << "BlackPearl should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: IsInMinorKey
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, IsInMinorKey) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);

  // G minor scale pitch classes: G(7), A(9), Bb(10), C(0), D(2), Eb(3), F(5), F#(6)
  // Including F# for harmonic minor leading tone.
  std::set<int> g_minor_pcs = {0, 2, 3, 5, 6, 7, 9, 10};

  int in_scale = 0;
  int total = 0;
  for (const auto& note : result.notes) {
    if (note.pitch == 0) continue;
    int pitch_class = getPitchClass(note.pitch);
    if (g_minor_pcs.count(pitch_class) > 0) {
      ++in_scale;
    }
    ++total;
  }

  ASSERT_GT(total, 0) << "Should have non-zero pitched notes";
  float ratio = static_cast<float>(in_scale) / static_cast<float>(total);

  // At least 70% of pitches should be in G minor scale (allowing chromatic passing tones).
  EXPECT_GE(ratio, 0.7f)
      << "At least 70% of pitches should belong to G minor scale, got "
      << (ratio * 100.0f) << "%";
}

// ---------------------------------------------------------------------------
// Test 3: HasSuspensions
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, HasSuspensions) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);
  EXPECT_GT(result.suspension_count, 0)
      << "BlackPearl should contain at least one suspension event";
}

// ---------------------------------------------------------------------------
// Test 4: LamentoBassPresent
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, LamentoBassPresent) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);

  // The lamento bass contains a chromatic descent from G3 through D3.
  // Check that at least some of the characteristic lamento pitches appear
  // in the bass voice.
  std::set<uint8_t> lamento_pitches = {55, 54, 53, 52, 51, 50};  // G3 to D3

  int lamento_count = 0;
  for (const auto& note : result.notes) {
    if (note.voice == 1 && lamento_pitches.count(note.pitch) > 0) {
      ++lamento_count;
    }
  }

  // We expect at least 4 distinct lamento pitches across 4 lamento bass statements.
  EXPECT_GE(lamento_count, 4)
      << "Bass line should contain at least 4 lamento bass notes (chromatic descent)";
}

// ---------------------------------------------------------------------------
// Test 5: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, NotesSpan32Bars) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  Tick ticks_per_bar = kThreeFour.ticksPerBar();

  // After binary repeats, total duration = 4 * 16 bars = 64 bars.
  Tick section_ticks = static_cast<Tick>(kGridBars / 2) * ticks_per_bar;
  Tick expected_total = 4 * section_ticks;

  Tick max_end = maxEndTick(result.notes);

  EXPECT_GE(max_end, expected_total - 2 * ticks_per_bar)
      << "Notes should span close to the full repeated duration";
  // Suspension chains starting near bar boundaries may extend slightly beyond.
  // Allow up to 4 bars of overshoot for suspension chain tail.
  EXPECT_LE(max_end, expected_total + 4 * ticks_per_bar)
      << "Notes should not extend far beyond the expected duration";
}

// ---------------------------------------------------------------------------
// Test 6: SlowDensity
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, SlowDensity) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);

  // Count melody notes (voice 0) in the first 32 bars (before repeats).
  Tick ticks_per_bar = kThreeFour.ticksPerBar();
  Tick first_section_end = static_cast<Tick>(kGridBars) * ticks_per_bar;

  int melody_count = 0;
  for (const auto& note : result.notes) {
    if (note.voice == 0 && note.start_tick < first_section_end) {
      ++melody_count;
    }
  }

  // Adagio with 1 note per beat: expect roughly 32 bars * 3 beats = ~96 melody notes.
  // Allow some variation from suspension overlay, but should be slow density.
  float notes_per_bar = static_cast<float>(melody_count) / static_cast<float>(kGridBars);

  // BlackPearl (Adagio, 1 npb) should have lower density than ornamental (4 npb).
  // Expect fewer than 8 melody notes per bar on average (generous upper bound).
  EXPECT_LT(notes_per_bar, 8.0f)
      << "BlackPearl Adagio should have low note density, got "
      << notes_per_bar << " notes/bar";
  EXPECT_GT(notes_per_bar, 0.5f)
      << "BlackPearl should have at least some melody content";
}

// ---------------------------------------------------------------------------
// Test 7: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, DifferentSeedsDifferent) {
  auto result_a = generateVariation(100);
  auto result_b = generateVariation(200);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);
  ASSERT_FALSE(result_a.notes.empty());
  ASSERT_FALSE(result_b.notes.empty());

  // Collect unique pitch sets to check for meaningful difference.
  std::set<uint8_t> pitches_a;
  std::set<uint8_t> pitches_b;
  for (const auto& note : result_a.notes) {
    if (note.pitch > 0) pitches_a.insert(note.pitch);
  }
  for (const auto& note : result_b.notes) {
    if (note.pitch > 0) pitches_b.insert(note.pitch);
  }

  bool differ_in_size = result_a.notes.size() != result_b.notes.size();
  bool differ_in_pitches = pitches_a != pitches_b;
  bool differ_in_suspensions = result_a.suspension_count != result_b.suspension_count;

  EXPECT_TRUE(differ_in_size || differ_in_pitches || differ_in_suspensions)
      << "Different seeds should produce different output";
}

// ---------------------------------------------------------------------------
// Test 8: SuccessAcrossSeeds
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, SuccessAcrossSeeds) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto result = generateVariation(seed);
    EXPECT_TRUE(result.success)
        << "BlackPearl should succeed with seed " << seed;
    EXPECT_FALSE(result.notes.empty())
        << "BlackPearl should produce notes with seed " << seed;
    EXPECT_GT(result.suspension_count, 0)
        << "BlackPearl should have suspensions with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Test 9: SuspensionNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, SuspensionNotesHaveCorrectSource) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);

  int suspension_source_count = 0;
  for (const auto& note : result.notes) {
    if (note.source == BachNoteSource::GoldbergSuspension) {
      ++suspension_source_count;
    }
  }

  // Should have suspension-sourced notes if suspension_count > 0.
  if (result.suspension_count > 0) {
    EXPECT_GT(suspension_source_count, 0)
        << "Should have notes with GoldbergSuspension source when suspensions are generated";
  }
}

// ---------------------------------------------------------------------------
// Test 10: BassNotesHaveGoldbergBassSource
// ---------------------------------------------------------------------------

TEST_F(BlackPearlGeneratorTest, BassNotesHaveGoldbergBassSource) {
  auto result = generateVariation();
  ASSERT_TRUE(result.success);

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

}  // namespace
}  // namespace bach
