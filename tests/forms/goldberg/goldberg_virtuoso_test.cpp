// Tests for VirtuosoGenerator (Goldberg Variations Var 11, 23, 29).

#include "forms/goldberg/variations/goldberg_virtuoso.h"

#include <algorithm>
#include <cstdint>

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

class VirtuosoGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  /// @brief Helper to generate a virtuoso variation.
  VirtuosoResult generateVariation(int var_num, uint32_t seed = kTestSeed) {
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

  /// @brief Count notes for a specific voice.
  static int noteCountForVoice(const std::vector<NoteEvent>& notes, VoiceId voice) {
    int count = 0;
    for (const auto& note : notes) {
      if (note.voice == voice && note.pitch > 0) {
        ++count;
      }
    }
    return count;
  }

  GoldbergStructuralGrid grid_;
  VirtuosoGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: Var11ToccataGenerate
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, Var11ToccataGenerate) {
  auto result = generateVariation(11);
  EXPECT_TRUE(result.success) << "Var 11 (Toccata) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 11 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 2: Var23ScalePassageGenerate
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, Var23ScalePassageGenerate) {
  auto result = generateVariation(23);
  EXPECT_TRUE(result.success) << "Var 23 (ScalePassage) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 23 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 3: Var29BravuraGenerate
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, Var29BravuraGenerate) {
  auto result = generateVariation(29);
  EXPECT_TRUE(result.success) << "Var 29 (BravuraChordal climax) should succeed";
  EXPECT_FALSE(result.notes.empty()) << "Var 29 should produce non-empty output";
}

// ---------------------------------------------------------------------------
// Test 4: ScalePassageHasStepwiseMotion
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, ScalePassageHasStepwiseMotion) {
  auto result = generateVariation(23);
  ASSERT_TRUE(result.success);

  // Extract melody pitches (voice 0) in time order.
  std::vector<uint8_t> melody_pitches;
  for (const auto& note : result.notes) {
    if (note.voice == 0 && note.pitch > 0) {
      melody_pitches.push_back(note.pitch);
    }
  }

  ASSERT_GT(melody_pitches.size(), 10u)
      << "ScalePassage should have a substantial number of melody notes";

  // Count stepwise intervals (1 or 2 semitones).
  int stepwise_count = 0;
  int total_intervals = 0;
  for (size_t idx = 1; idx < melody_pitches.size(); ++idx) {
    int interval = absoluteInterval(melody_pitches[idx], melody_pitches[idx - 1]);
    ++total_intervals;
    if (interval <= 2) {
      ++stepwise_count;
    }
  }

  // Var 23 (Tirata/scale passage) should have predominantly stepwise motion.
  // At least 30% stepwise intervals (conservative threshold given chord tone
  // snapping and phrase boundaries).
  float stepwise_ratio = static_cast<float>(stepwise_count) /
                         static_cast<float>(total_intervals);
  EXPECT_GE(stepwise_ratio, 0.30f)
      << "ScalePassage (Var 23) should have at least 30% stepwise intervals, "
      << "got " << stepwise_ratio * 100.0f << "% (" << stepwise_count
      << "/" << total_intervals << ")";
}

// ---------------------------------------------------------------------------
// Test 5: BravuraHasHighDensity
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, BravuraHasHighDensity) {
  auto result_var11 = generateVariation(11);
  auto result_var29 = generateVariation(29);

  ASSERT_TRUE(result_var11.success);
  ASSERT_TRUE(result_var29.success);

  // Var 29 (BravuraChordal climax, 4 voices) should have more notes than
  // Var 11 (Toccata, 2 voices) due to the additional inner voices.
  EXPECT_GT(result_var29.notes.size(), result_var11.notes.size())
      << "Var 29 (BravuraChordal, 4 voices) should have higher note density "
      << "than Var 11 (Toccata, 2 voices). "
      << "Var 29: " << result_var29.notes.size()
      << ", Var 11: " << result_var11.notes.size();
}

// ---------------------------------------------------------------------------
// Test 6: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, NotesSpan32Bars) {
  Tick ticks_per_bar = kThreeFour.ticksPerBar();
  // After binary repeats: A-A-B-B = 64 bars.
  Tick expected_span = 64u * ticks_per_bar;

  for (int var_num : {11, 23, 29}) {
    auto result = generateVariation(var_num);
    ASSERT_TRUE(result.success) << "Var " << var_num << " should succeed";
    ASSERT_FALSE(result.notes.empty()) << "Var " << var_num << " should be non-empty";

    Tick max_end = maxEndTick(result.notes);

    EXPECT_GE(max_end, expected_span - 2 * ticks_per_bar)
        << "Var " << var_num
        << " notes should span close to 64 bars after binary repeats";
    EXPECT_LE(max_end, expected_span + ticks_per_bar)
        << "Var " << var_num
        << " notes should not extend far beyond 64 bars";
  }
}

// ---------------------------------------------------------------------------
// Test 7: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, DifferentSeedsDifferent) {
  // Use seeds far apart in the seed space.
  auto result_a = generateVariation(11, 42);
  auto result_b = generateVariation(11, 123456);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);
  ASSERT_FALSE(result_a.notes.empty());
  ASSERT_FALSE(result_b.notes.empty());

  // Compare pitch sequences -- different seeds should produce different results.
  size_t compare_len = std::min(result_a.notes.size(), result_b.notes.size());
  int diff_count = 0;
  for (size_t idx = 0; idx < compare_len; ++idx) {
    if (result_a.notes[idx].pitch != result_b.notes[idx].pitch) {
      ++diff_count;
    }
  }

  EXPECT_GT(diff_count, 0)
      << "Different seeds should produce different note pitches";
}

// ---------------------------------------------------------------------------
// Test 8: AllVariationsSucceed
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, AllVariationsSucceed) {
  for (int var_num : {11, 23, 29}) {
    auto result = generateVariation(var_num);
    EXPECT_TRUE(result.success)
        << "Variation " << var_num << " should succeed";
    EXPECT_FALSE(result.notes.empty())
        << "Variation " << var_num << " should produce notes";
  }
}

// ---------------------------------------------------------------------------
// Test 9: UnsupportedVariationFails
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, UnsupportedVariationFails) {
  auto result = generateVariation(99);
  EXPECT_FALSE(result.success)
      << "Unsupported variation number should return success=false";
  EXPECT_TRUE(result.notes.empty())
      << "Unsupported variation should produce no notes";
}

// ---------------------------------------------------------------------------
// Test 10: BravuraHasMultipleVoices
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, BravuraHasMultipleVoices) {
  auto result = generateVariation(29);
  ASSERT_TRUE(result.success);

  // Var 29 should have notes on at least 3 voices (voices 0, 1, 2 + bass).
  int voice0_count = noteCountForVoice(result.notes, 0);
  int voice1_count = noteCountForVoice(result.notes, 1);
  int voice2_count = noteCountForVoice(result.notes, 2);

  EXPECT_GT(voice0_count, 0)
      << "Var 29 should have voice 0 (upper melody) notes";
  EXPECT_GT(voice1_count, 0)
      << "Var 29 should have voice 1 (inner voice) notes";
  EXPECT_GT(voice2_count, 0)
      << "Var 29 should have voice 2 (inner voice) notes";
}

// ---------------------------------------------------------------------------
// Test 11: BassNotesHaveGoldbergBassSource
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, BassNotesHaveGoldbergBassSource) {
  auto result = generateVariation(11);
  ASSERT_TRUE(result.success);

  // Bass notes (voice=1 for Var 11, which has 2 voices) should have
  // GoldbergBass source.
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
// Test 12: ClimaxVelocityBoost
// ---------------------------------------------------------------------------

TEST_F(VirtuosoGeneratorTest, ClimaxVelocityBoost) {
  auto result_var11 = generateVariation(11);
  auto result_var29 = generateVariation(29);

  ASSERT_TRUE(result_var11.success);
  ASSERT_TRUE(result_var29.success);

  // Compute average velocity for melody notes (voice 0) in each variation.
  auto avgVelocity = [](const std::vector<NoteEvent>& notes, VoiceId voice) {
    float sum = 0.0f;
    int count = 0;
    for (const auto& note : notes) {
      if (note.voice == voice && note.pitch > 0) {
        sum += static_cast<float>(note.velocity);
        ++count;
      }
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
  };

  float var11_avg = avgVelocity(result_var11.notes, 0);
  float var29_avg = avgVelocity(result_var29.notes, 0);

  // Var 29 climax should have higher average velocity than Var 11.
  EXPECT_GT(var29_avg, var11_avg)
      << "Var 29 (climax) should have higher average velocity ("
      << var29_avg << ") than Var 11 (" << var11_avg << ")";
}

}  // namespace
}  // namespace bach
