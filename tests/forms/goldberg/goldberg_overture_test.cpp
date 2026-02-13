// Tests for Goldberg Variations French Overture generator (Var 16).

#include "forms/goldberg/variations/goldberg_overture.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kAllaBreve = {2, 2};  // Var 16: alla breve 2/2.
constexpr uint32_t kTestSeed = 42;

/// Total bars in the variation (before repeats).
constexpr int kTotalBars = 32;

/// Bars per section (Grave = first 16, Fugato = last 16).
constexpr int kSectionBars = 16;

class OvertureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
  OvertureGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesResult
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, GenerateProducesResult) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  EXPECT_TRUE(result.success) << "French Overture should succeed";
  EXPECT_FALSE(result.notes.empty())
      << "French Overture should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, NotesSpan32Bars) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  // With binary repeats, notes span at least 32 bars.
  Tick max_tick = 0;
  for (const auto& note : result.notes) {
    Tick end_tick = note.start_tick + note.duration;
    if (end_tick > max_tick) max_tick = end_tick;
  }

  Tick ticks_per_bar = kAllaBreve.ticksPerBar();
  Tick expected_min = static_cast<Tick>(kTotalBars) * ticks_per_bar;
  EXPECT_GE(max_tick, expected_min)
      << "Notes should span at least 32 bars (with binary repeats)";
}

// ---------------------------------------------------------------------------
// Test 3: GraveSectionHasLongerNotes
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, GraveSectionHasLongerNotes) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  Tick ticks_per_bar = kAllaBreve.ticksPerBar();
  Tick section_ticks = static_cast<Tick>(kSectionBars) * ticks_per_bar;

  // After binary repeats, layout is:
  //   A: [0, section_ticks)
  //   A repeat: [section_ticks, 2*section_ticks)
  //   B: [2*section_ticks, 3*section_ticks)
  //   B repeat: [3*section_ticks, 4*section_ticks)
  // Use the first A for Grave and first B for Fugato.
  Tick fugato_start = 2 * section_ticks;
  Tick fugato_end = 3 * section_ticks;

  double grave_total_dur = 0;
  int grave_count = 0;
  double fugato_total_dur = 0;
  int fugato_count = 0;

  for (const auto& note : result.notes) {
    if (note.start_tick < section_ticks) {
      grave_total_dur += static_cast<double>(note.duration);
      ++grave_count;
    } else if (note.start_tick >= fugato_start && note.start_tick < fugato_end) {
      fugato_total_dur += static_cast<double>(note.duration);
      ++fugato_count;
    }
  }

  ASSERT_GT(grave_count, 0) << "Grave section should have notes";
  ASSERT_GT(fugato_count, 0) << "Fugato section should have notes";

  double avg_grave = grave_total_dur / grave_count;
  double avg_fugato = fugato_total_dur / fugato_count;

  EXPECT_GT(avg_grave, avg_fugato)
      << "Grave section avg duration (" << avg_grave
      << ") should be longer than Fugato avg duration (" << avg_fugato << ")";
}

// ---------------------------------------------------------------------------
// Test 4: FugatoSectionHasMoreNotes
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, FugatoSectionHasMoreNotes) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  Tick ticks_per_bar = kAllaBreve.ticksPerBar();
  Tick section_ticks = static_cast<Tick>(kSectionBars) * ticks_per_bar;

  // After binary repeats, layout is A-A-B-B:
  //   A: [0, section_ticks)
  //   B: [2*section_ticks, 3*section_ticks)
  Tick fugato_start = 2 * section_ticks;
  Tick fugato_end = 3 * section_ticks;

  int grave_count = 0;
  int fugato_count = 0;

  for (const auto& note : result.notes) {
    if (note.start_tick < section_ticks) {
      ++grave_count;
    } else if (note.start_tick >= fugato_start && note.start_tick < fugato_end) {
      ++fugato_count;
    }
  }

  ASSERT_GT(grave_count, 0) << "Grave section should have notes";
  ASSERT_GT(fugato_count, 0) << "Fugato section should have notes";

  EXPECT_GT(fugato_count, grave_count)
      << "Fugato section (" << fugato_count
      << " notes) should have more notes than Grave section (" << grave_count << ")";
}

// ---------------------------------------------------------------------------
// Test 5: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, DifferentSeedsDifferent) {
  // Try multiple seed pairs; at least one pair should produce different output.
  bool found_difference = false;
  for (uint32_t base = 1000; base < 1020 && !found_difference; base += 2) {
    auto result1 = generator_.generate(grid_, kGMajor, kAllaBreve, base);
    auto result2 = generator_.generate(grid_, kGMajor, kAllaBreve, base + 7777);

    if (!result1.success || !result2.success) continue;
    if (result1.notes.empty() || result2.notes.empty()) continue;

    size_t compare_count = std::min(result1.notes.size(), result2.notes.size());
    for (size_t idx = 0; idx < compare_count; ++idx) {
      if (result1.notes[idx].pitch != result2.notes[idx].pitch) {
        found_difference = true;
        break;
      }
    }
    if (!found_difference && result1.notes.size() != result2.notes.size()) {
      found_difference = true;
    }
  }

  EXPECT_TRUE(found_difference)
      << "At least one seed pair should produce different pitch sequences";
}

// ---------------------------------------------------------------------------
// Test 6: SuccessAcrossSeeds
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, SuccessAcrossSeeds) {
  constexpr int kNumSeeds = 10;
  int success_count = 0;

  for (uint32_t seed = 100; seed < 100 + kNumSeeds; ++seed) {
    auto result = generator_.generate(grid_, kGMajor, kAllaBreve, seed);
    if (result.success && !result.notes.empty()) ++success_count;
  }

  EXPECT_EQ(success_count, kNumSeeds)
      << "All seeds should produce successful output";
}

// ---------------------------------------------------------------------------
// Test 7: BothSectionsHaveNotes
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, BothSectionsHaveNotes) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  Tick ticks_per_bar = kAllaBreve.ticksPerBar();
  Tick section_boundary = static_cast<Tick>(kSectionBars) * ticks_per_bar;

  bool has_grave = false;
  bool has_fugato = false;

  for (const auto& note : result.notes) {
    if (note.start_tick < section_boundary) {
      has_grave = true;
    } else if (note.start_tick >= section_boundary) {
      has_fugato = true;
    }
    if (has_grave && has_fugato) break;
  }

  EXPECT_TRUE(has_grave) << "Grave section (first 16 bars) should have notes";
  EXPECT_TRUE(has_fugato) << "Fugato section (last 16 bars) should have notes";
}

// ---------------------------------------------------------------------------
// Additional: AllNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(OvertureTest, AllNotesHaveCorrectSource) {
  auto result = generator_.generate(grid_, kGMajor, kAllaBreve, kTestSeed);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.notes) {
    EXPECT_EQ(note.source, BachNoteSource::GoldbergOverture)
        << "All overture notes should have GoldbergOverture source";
  }
}

}  // namespace
}  // namespace bach
