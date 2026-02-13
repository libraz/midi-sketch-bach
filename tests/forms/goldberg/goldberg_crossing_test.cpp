// Tests for CrossingGenerator (Goldberg Variations hand-crossing, Var 8, 17, 20).

#include "forms/goldberg/variations/goldberg_crossing.h"

#include <algorithm>
#include <numeric>

#include <gtest/gtest.h>

#include "core/pitch_utils.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;

/// @brief Helper: compute average pitch of sounding notes for a given voice.
float averagePitchForVoice(const std::vector<NoteEvent>& notes, VoiceId voice) {
  float sum = 0.0f;
  int count = 0;
  for (const auto& note : notes) {
    if (note.voice == voice && note.pitch > 0) {
      sum += static_cast<float>(note.pitch);
      ++count;
    }
  }
  return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

/// @brief Helper: count notes for a given voice.
int noteCountForVoice(const std::vector<NoteEvent>& notes, VoiceId voice) {
  int count = 0;
  for (const auto& note : notes) {
    if (note.voice == voice && note.pitch > 0) {
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Test 1: Var8Generate
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, Var8Generate) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(8, grid, kGMajor, kThreeFour, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 8 generation should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 8 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 2: Var17Generate
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, Var17Generate) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(17, grid, kGMajor, kThreeFour, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 17 generation should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 17 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 3: Var20Generate
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, Var20Generate) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(20, grid, kGMajor, kThreeFour, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 20 generation should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 20 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 4: RegisterSeparation
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, RegisterSeparation) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(8, grid, kGMajor, kThreeFour, kTestSeed);
  ASSERT_TRUE(result.success);

  float upper_avg = averagePitchForVoice(result.notes, 0);
  float lower_avg = averagePitchForVoice(result.notes, 1);

  // Upper voice should have a higher average pitch than lower voice,
  // even with crossing effects applied at some positions.
  EXPECT_GT(upper_avg, lower_avg)
      << "Upper voice average pitch (" << upper_avg
      << ") should be higher than lower voice (" << lower_avg << ")";
}

// ---------------------------------------------------------------------------
// Test 5: BatterieHasLeaps
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, BatterieHasLeaps) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(8, grid, kGMajor, kThreeFour, kTestSeed);
  ASSERT_TRUE(result.success);

  // Check upper voice (voice 0) for characteristic Batterie leaps.
  std::vector<uint8_t> upper_pitches;
  for (const auto& note : result.notes) {
    if (note.voice == 0 && note.pitch > 0) {
      upper_pitches.push_back(note.pitch);
    }
  }

  int leap_count = 0;
  for (size_t idx = 1; idx < upper_pitches.size(); ++idx) {
    int interval = absoluteInterval(upper_pitches[idx], upper_pitches[idx - 1]);
    if (interval > 5) {  // More than a perfect 4th = characteristic leap.
      ++leap_count;
    }
  }

  EXPECT_GT(leap_count, 0)
      << "Batterie pattern should contain intervals > 5 semitones";
}

// ---------------------------------------------------------------------------
// Test 6: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, NotesSpan32Bars) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(8, grid, kGMajor, kThreeFour, kTestSeed);
  ASSERT_TRUE(result.success);

  // After binary repeats, the variation spans 64 bars worth of ticks
  // (32 unique bars, each section repeated). But the unique notes should
  // cover 32 bars before repeats. Check that the output covers the full
  // repeated span.
  Tick ticks_per_bar = kThreeFour.ticksPerBar();
  Tick expected_span = 64u * ticks_per_bar;  // After binary repeats: A-A-B-B.

  Tick max_end = 0;
  for (const auto& note : result.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) max_end = note_end;
  }

  // The last note should end near the 64-bar boundary.
  EXPECT_GE(max_end, expected_span - 2 * ticks_per_bar)
      << "Notes should span close to 64 bars after binary repeats";
  EXPECT_LE(max_end, expected_span + ticks_per_bar)
      << "Notes should not extend far beyond 64 bars";
}

// ---------------------------------------------------------------------------
// Test 7: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, DifferentSeedsDifferent) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  // Use seeds that are far apart in the seed space to maximize divergence.
  auto result_a = gen.generate(8, grid, kGMajor, kThreeFour, 42);
  auto result_b = gen.generate(8, grid, kGMajor, kThreeFour, 123456);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);

  // Different seeds should produce different note sequences.
  // Compare combined pitch sequences from both voices, since individual
  // voices may converge due to register clamping and chord-tone snapping.
  auto extractAllPitches = [](const std::vector<NoteEvent>& notes) {
    std::vector<uint8_t> pitches;
    for (const auto& note : notes) {
      if (note.pitch > 0) {
        pitches.push_back(note.pitch);
      }
    }
    return pitches;
  };

  auto pitches_a = extractAllPitches(result_a.notes);
  auto pitches_b = extractAllPitches(result_b.notes);

  // At least some pitches should differ between the two seeds.
  bool all_same = (pitches_a.size() == pitches_b.size());
  if (all_same) {
    for (size_t idx = 0; idx < pitches_a.size(); ++idx) {
      if (pitches_a[idx] != pitches_b[idx]) {
        all_same = false;
        break;
      }
    }
  }

  EXPECT_FALSE(all_same)
      << "Different seeds should produce different pitch sequences";
}

// ---------------------------------------------------------------------------
// Test 8: AllVariationsSucceed
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, AllVariationsSucceed) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  for (int var_num : {8, 17, 20}) {
    auto result = gen.generate(var_num, grid, kGMajor, kThreeFour, kTestSeed);
    EXPECT_TRUE(result.success)
        << "Variation " << var_num << " should succeed";
    EXPECT_FALSE(result.notes.empty())
        << "Variation " << var_num << " should produce notes";

    // Verify both voices are present.
    int upper_count = noteCountForVoice(result.notes, 0);
    int lower_count = noteCountForVoice(result.notes, 1);
    EXPECT_GT(upper_count, 0)
        << "Variation " << var_num << " should have upper voice notes";
    EXPECT_GT(lower_count, 0)
        << "Variation " << var_num << " should have lower voice notes";
  }
}

// ---------------------------------------------------------------------------
// Test 9: UnsupportedVariationFails
// ---------------------------------------------------------------------------

TEST(CrossingGeneratorTest, UnsupportedVariationFails) {
  CrossingGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(5, grid, kGMajor, kThreeFour, kTestSeed);
  EXPECT_FALSE(result.success)
      << "Unsupported variation number should return success=false";
  EXPECT_TRUE(result.notes.empty())
      << "Unsupported variation should produce no notes";
}

}  // namespace
}  // namespace bach
