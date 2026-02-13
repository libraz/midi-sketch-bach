// Tests for Goldberg Variations 3-voice invention (Sinfonia-style) generator.

#include "forms/goldberg/variations/goldberg_invention.h"

#include <algorithm>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kTriple = {3, 4};  // 3/4 time (Var 2 standard).
constexpr uint32_t kTestSeed = 42;

class InventionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
  InventionGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesResult
// ---------------------------------------------------------------------------

TEST_F(InventionTest, GenerateProducesResult) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  EXPECT_TRUE(result.success) << "Invention generation should succeed";
  EXPECT_FALSE(result.notes.empty())
      << "Invention should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: Has3Voices
// ---------------------------------------------------------------------------

TEST_F(InventionTest, Has3Voices) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  std::set<VoiceId> voices;
  for (const auto& note : result.notes) {
    voices.insert(note.voice);
  }
  EXPECT_EQ(voices.size(), 3u)
      << "Invention output should contain notes for 3 different voice indices";
}

// ---------------------------------------------------------------------------
// Test 3: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(InventionTest, NotesSpan32Bars) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  Tick max_tick = 0;
  for (const auto& note : result.notes) {
    Tick end_tick = note.start_tick + note.duration;
    if (end_tick > max_tick) max_tick = end_tick;
  }

  Tick ticks_per_bar = kTriple.ticksPerBar();

  // Notes should span at least 24 bars (development + recap).
  Tick expected_min = 24 * ticks_per_bar;
  EXPECT_GE(max_tick, expected_min)
      << "Notes should span at least 24 bars, got "
      << (max_tick / ticks_per_bar) << " bars";

  // Notes should not exceed 32 bars.
  Tick expected_max = 32 * ticks_per_bar;
  EXPECT_LE(max_tick, expected_max)
      << "Notes should not exceed 32 bars, got "
      << (max_tick / ticks_per_bar) << " bars";
}

// ---------------------------------------------------------------------------
// Test 4: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(InventionTest, DifferentSeedsDifferent) {
  bool found_difference = false;
  for (uint32_t base = 1000; base < 1020 && !found_difference; base += 2) {
    auto result1 = generator_.generate(grid_, kGMajor, kTriple, base);
    auto result2 = generator_.generate(grid_, kGMajor, kTriple, base + 7777);

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
// Test 5: SuccessAcrossSeeds
// ---------------------------------------------------------------------------

TEST_F(InventionTest, SuccessAcrossSeeds) {
  int success_count = 0;
  constexpr int kNumSeeds = 10;

  for (uint32_t seed = 100; seed < 100 + kNumSeeds; ++seed) {
    auto result = generator_.generate(grid_, kGMajor, kTriple, seed);
    if (result.success && !result.notes.empty()) {
      ++success_count;
    }
  }

  EXPECT_EQ(success_count, kNumSeeds)
      << "All " << kNumSeeds << " seeds should produce successful output";
}

// ---------------------------------------------------------------------------
// Additional: ExpositionHasStaggeredEntries
// ---------------------------------------------------------------------------

TEST_F(InventionTest, ExpositionHasStaggeredEntries) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  Tick ticks_per_bar = kTriple.ticksPerBar();
  Tick exposition_end = 4 * ticks_per_bar;

  // Find the earliest note for each voice within the exposition.
  std::map<VoiceId, Tick> first_entry;
  for (const auto& note : result.notes) {
    if (note.start_tick >= exposition_end) continue;
    auto iter = first_entry.find(note.voice);
    if (iter == first_entry.end() || note.start_tick < iter->second) {
      first_entry[note.voice] = note.start_tick;
    }
  }

  ASSERT_EQ(first_entry.size(), 3u) << "All 3 voices should appear in exposition";

  // Entries should be staggered (not all at the same tick).
  std::set<Tick> unique_starts;
  for (const auto& pair : first_entry) {
    unique_starts.insert(pair.second);
  }

  EXPECT_GT(unique_starts.size(), 1u)
      << "Voice entries should be staggered in the exposition";
}

// ---------------------------------------------------------------------------
// Additional: AllNotesWithinMidiRange
// ---------------------------------------------------------------------------

TEST_F(InventionTest, AllNotesWithinMidiRange) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.notes) {
    EXPECT_GE(note.pitch, 36)
        << "All pitches should be >= C2 (36), got " << static_cast<int>(note.pitch)
        << " in voice " << static_cast<int>(note.voice);
    EXPECT_LE(note.pitch, 96)
        << "All pitches should be <= C6 (96), got " << static_cast<int>(note.pitch)
        << " in voice " << static_cast<int>(note.voice);
  }
}

// ---------------------------------------------------------------------------
// Additional: NotesHaveValidSource
// ---------------------------------------------------------------------------

TEST_F(InventionTest, NotesHaveValidSource) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.notes) {
    // Notes should be either GoldbergSoggetto (subject entries) or
    // GoldbergInvention (free counterpoint, inversions, sequences).
    bool valid_source = note.source == BachNoteSource::GoldbergSoggetto ||
                        note.source == BachNoteSource::GoldbergInvention;
    EXPECT_TRUE(valid_source)
        << "Note source should be GoldbergSoggetto or GoldbergInvention, got "
        << bachNoteSourceToString(note.source);
  }
}

// ---------------------------------------------------------------------------
// Additional: EachVoiceHasSufficientNotes
// ---------------------------------------------------------------------------

TEST_F(InventionTest, EachVoiceHasSufficientNotes) {
  auto result = generator_.generate(grid_, kGMajor, kTriple, kTestSeed);
  ASSERT_TRUE(result.success);

  std::map<VoiceId, size_t> voice_counts;
  for (const auto& note : result.notes) {
    voice_counts[note.voice]++;
  }

  for (uint8_t voice = 0; voice < 3; ++voice) {
    EXPECT_GT(voice_counts[voice], 10u)
        << "Voice " << static_cast<int>(voice)
        << " should have substantial note content (>10 notes)";
  }
}

}  // namespace
}  // namespace bach
