// Tests for Goldberg Variations fughetta and alla breve fugal generator.

#include "forms/goldberg/variations/goldberg_fughetta.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <set>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kTriple = {3, 4};      // Var 10: 3/4
constexpr TimeSignature kAllaBrève = {2, 2};    // Var 22: 2/2
constexpr uint32_t kTestSeed = 42;

class FughettaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
  FughettaGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: Var10FughettaGenerate
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, Var10FughettaGenerate) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  EXPECT_TRUE(result.success) << "Var 10 fughetta should succeed";
  EXPECT_FALSE(result.notes.empty())
      << "Var 10 fughetta should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: Var22AllaBreveFugalGenerate
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, Var22AllaBreveFugalGenerate) {
  auto result = generator_.generate(22, grid_, kGMajor, kAllaBrève, kTestSeed);
  EXPECT_TRUE(result.success) << "Var 22 alla breve fugal should succeed";
  EXPECT_FALSE(result.notes.empty())
      << "Var 22 alla breve fugal should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 3: FughettaHas4Voices
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, FughettaHas4Voices) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  std::set<VoiceId> voices;
  for (const auto& note : result.notes) {
    voices.insert(note.voice);
  }
  EXPECT_EQ(voices.size(), 4u)
      << "Fughetta output should contain notes for 4 different voice indices";
}

// ---------------------------------------------------------------------------
// Test 4: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, NotesSpan32Bars) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  // With binary repeats, notes span 64 bars (32 unique * 2 repeats).
  // Find the maximum tick position.
  Tick max_tick = 0;
  for (const auto& note : result.notes) {
    Tick end_tick = note.start_tick + note.duration;
    if (end_tick > max_tick) max_tick = end_tick;
  }

  // Binary repeats: AABB = 4 * 16 bars = 64 bars total.
  Tick ticks_per_bar = kTriple.ticksPerBar();
  Tick expected_min = 32 * ticks_per_bar;  // At minimum 32 bars with repeats.
  EXPECT_GE(max_tick, expected_min)
      << "Notes should span at least 32 bars (with binary repeats)";
}

// ---------------------------------------------------------------------------
// Test 5: Var22HasLongerNotes
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, Var22HasLongerNotes) {
  auto result10 = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  auto result22 = generator_.generate(22, grid_, kGMajor, kAllaBrève, kTestSeed);
  ASSERT_TRUE(result10.success);
  ASSERT_TRUE(result22.success);
  ASSERT_FALSE(result10.notes.empty());
  ASSERT_FALSE(result22.notes.empty());

  // Calculate average note duration for each variation.
  auto avg_duration = [](const std::vector<NoteEvent>& notes) -> double {
    double total = 0;
    for (const auto& note : notes) {
      total += static_cast<double>(note.duration);
    }
    return total / static_cast<double>(notes.size());
  };

  double avg10 = avg_duration(result10.notes);
  double avg22 = avg_duration(result22.notes);

  EXPECT_GT(avg22, avg10)
      << "Var 22 (alla breve) average note duration (" << avg22
      << ") should be longer than Var 10 (" << avg10 << ")";
}

// ---------------------------------------------------------------------------
// Test 6: ExpositionHasEntries
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, ExpositionHasEntries) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  Tick ticks_per_bar = kTriple.ticksPerBar();
  Tick exposition_end = 8 * ticks_per_bar;  // First 8 bars.

  // Check that each voice has at least one note in the first 8 bars.
  std::set<VoiceId> voices_with_entries;
  for (const auto& note : result.notes) {
    if (note.start_tick < exposition_end) {
      voices_with_entries.insert(note.voice);
    }
  }

  EXPECT_EQ(voices_with_entries.size(), 4u)
      << "First 8 bars (exposition) should have entries in all 4 voices";
}

// ---------------------------------------------------------------------------
// Test 7: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, DifferentSeedsDifferent) {
  // Try multiple seed pairs; at least one pair should produce different output.
  bool found_difference = false;
  for (uint32_t base = 1000; base < 1020 && !found_difference; base += 2) {
    auto result1 = generator_.generate(10, grid_, kGMajor, kTriple, base);
    auto result2 = generator_.generate(10, grid_, kGMajor, kTriple, base + 7777);

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
// Test 8: BothVariationsSucceed
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, BothVariationsSucceed) {
  // Test across multiple seeds to verify robustness.
  int success_count_10 = 0;
  int success_count_22 = 0;
  constexpr int kNumSeeds = 10;

  for (uint32_t seed = 100; seed < 100 + kNumSeeds; ++seed) {
    auto result10 = generator_.generate(10, grid_, kGMajor, kTriple, seed);
    auto result22 = generator_.generate(22, grid_, kGMajor, kAllaBrève, seed);
    if (result10.success && !result10.notes.empty()) ++success_count_10;
    if (result22.success && !result22.notes.empty()) ++success_count_22;
  }

  EXPECT_EQ(success_count_10, kNumSeeds)
      << "All Var 10 seeds should succeed";
  EXPECT_EQ(success_count_22, kNumSeeds)
      << "All Var 22 seeds should succeed";
}

// ---------------------------------------------------------------------------
// Additional: AllNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, AllNotesHaveCorrectSource) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.notes) {
    EXPECT_EQ(note.source, BachNoteSource::GoldbergFughetta)
        << "All fughetta notes should have GoldbergFughetta source";
  }
}

// ---------------------------------------------------------------------------
// Additional: VoiceEntriesAreStaggered
// ---------------------------------------------------------------------------

TEST_F(FughettaTest, VoiceEntriesAreStaggered) {
  auto result = generator_.generate(10, grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  // Find the earliest note for each voice.
  std::map<VoiceId, Tick> first_entry;
  for (const auto& note : result.notes) {
    auto iter = first_entry.find(note.voice);
    if (iter == first_entry.end() || note.start_tick < iter->second) {
      first_entry[note.voice] = note.start_tick;
    }
  }

  ASSERT_EQ(first_entry.size(), 4u) << "Should have 4 voices";

  // Entries should not all start at the same tick.
  std::set<Tick> unique_starts;
  for (const auto& pair : first_entry) {
    unique_starts.insert(pair.second);
  }

  EXPECT_GT(unique_starts.size(), 1u)
      << "Voice entries should be staggered (not all starting at tick 0)";
}

}  // namespace
}  // namespace bach
