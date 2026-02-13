// Tests for DanceGenerator (Goldberg Variations dance variations).

#include "forms/goldberg/variations/goldberg_dance.h"

#include <algorithm>
#include <set>

#include <gtest/gtest.h>

#include "forms/goldberg/goldberg_structural_grid.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr uint32_t kTestSeed = 42;

// ---------------------------------------------------------------------------
// Test 1: Var4PassepiedGenerate
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, Var4PassepiedGenerate) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(4, grid, kGMajor, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 4 Passepied should generate successfully";
  EXPECT_FALSE(result.notes.empty()) << "Var 4 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 2: Var7GigueGenerate
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, Var7GigueGenerate) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(7, grid, kGMajor, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 7 Gigue should generate successfully";
  EXPECT_FALSE(result.notes.empty()) << "Var 7 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 3: Var19PassepiedGenerate
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, Var19PassepiedGenerate) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(19, grid, kGMajor, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 19 Passepied should generate successfully";
  EXPECT_FALSE(result.notes.empty()) << "Var 19 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 4: Var26SarabandeGenerate
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, Var26SarabandeGenerate) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result = gen.generate(26, grid, kGMajor, kTestSeed);

  EXPECT_TRUE(result.success) << "Var 26 Sarabande should generate successfully";
  EXPECT_FALSE(result.notes.empty()) << "Var 26 should produce notes";
}

// ---------------------------------------------------------------------------
// Test 5: DanceProfileCorrect
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, DanceProfileCorrect) {
  // Var 4: Passepied, 3/8, 2 npb, Symmetric, 0.6, 0.3, 3 voices.
  auto var4 = getDanceProfile(4);
  EXPECT_EQ(var4.primary_figura, FiguraType::Passepied);
  EXPECT_EQ(var4.notes_per_beat, 2);
  EXPECT_EQ(var4.direction, DirectionBias::Symmetric);
  EXPECT_FLOAT_EQ(var4.chord_tone_ratio, 0.6f);
  EXPECT_FLOAT_EQ(var4.sequence_probability, 0.3f);
  EXPECT_EQ(var4.voice_count, 3);

  // Var 7: Gigue, 6/8, 2 npb, Ascending, 0.5, 0.4, 2 voices.
  auto var7 = getDanceProfile(7);
  EXPECT_EQ(var7.primary_figura, FiguraType::Gigue);
  EXPECT_EQ(var7.notes_per_beat, 2);
  EXPECT_EQ(var7.direction, DirectionBias::Ascending);
  EXPECT_FLOAT_EQ(var7.chord_tone_ratio, 0.5f);
  EXPECT_FLOAT_EQ(var7.sequence_probability, 0.4f);
  EXPECT_EQ(var7.voice_count, 2);

  // Var 19: Passepied, 3/8, 2 npb, Symmetric, 0.6, 0.3, 3 voices.
  auto var19 = getDanceProfile(19);
  EXPECT_EQ(var19.primary_figura, FiguraType::Passepied);
  EXPECT_EQ(var19.time_sig.numerator, 3);
  EXPECT_EQ(var19.time_sig.denominator, 8);
  EXPECT_EQ(var19.voice_count, 3);

  // Var 26: Sarabande, 3/4, 2 npb, Symmetric, 0.7, 0.2, 2 voices.
  auto var26 = getDanceProfile(26);
  EXPECT_EQ(var26.primary_figura, FiguraType::Sarabande);
  EXPECT_EQ(var26.notes_per_beat, 2);
  EXPECT_EQ(var26.direction, DirectionBias::Symmetric);
  EXPECT_FLOAT_EQ(var26.chord_tone_ratio, 0.7f);
  EXPECT_FLOAT_EQ(var26.sequence_probability, 0.2f);
  EXPECT_EQ(var26.voice_count, 2);
}

// ---------------------------------------------------------------------------
// Test 6: PassepiedHas3_8Time
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, PassepiedHas3_8Time) {
  auto var4 = getDanceProfile(4);
  EXPECT_EQ(var4.time_sig.numerator, 3);
  EXPECT_EQ(var4.time_sig.denominator, 8);

  // 3/8 time: ticksPerBar = 3 * (480 * 4 / 8) = 3 * 240 = 720
  EXPECT_EQ(var4.time_sig.ticksPerBar(), 720u);
}

// ---------------------------------------------------------------------------
// Test 7: GigueHas6_8Time
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, GigueHas6_8Time) {
  auto var7 = getDanceProfile(7);
  EXPECT_EQ(var7.time_sig.numerator, 6);
  EXPECT_EQ(var7.time_sig.denominator, 8);

  // 6/8 time: ticksPerBar = 6 * (480 * 4 / 8) = 6 * 240 = 1440
  EXPECT_EQ(var7.time_sig.ticksPerBar(), 1440u);

  // 6/8 should be compound time.
  EXPECT_TRUE(var7.time_sig.isCompound());
  EXPECT_EQ(var7.time_sig.pulsesPerBar(), 2);
}

// ---------------------------------------------------------------------------
// Test 8: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, NotesSpan32Bars) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  // Test with Var 4 (3/8 time).
  auto result = gen.generate(4, grid, kGMajor, kTestSeed);
  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.notes.empty());

  auto var4 = getDanceProfile(4);
  Tick ticks_per_bar = var4.time_sig.ticksPerBar();

  // After binary repeats, the total duration is 4 * 16 bars = 64 bars.
  // Original 32 bars: section_ticks = 16 * ticks_per_bar.
  // Binary repeats produce: A-A-B-B = 4 * section_ticks.
  Tick section_ticks = 16 * ticks_per_bar;
  Tick expected_total = 4 * section_ticks;

  Tick max_end = 0;
  for (const auto& note : result.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) max_end = note_end;
  }

  // The last note should end near the full repeated duration.
  EXPECT_GE(max_end, expected_total - 2 * ticks_per_bar)
      << "Notes should span close to the full repeated duration";
  EXPECT_LE(max_end, expected_total + ticks_per_bar)
      << "Notes should not extend far beyond the expected duration";
}

// ---------------------------------------------------------------------------
// Test 9: DifferentSeedsDifferent
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, DifferentSeedsDifferent) {
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  auto result_a = gen.generate(4, grid, kGMajor, 100);
  auto result_b = gen.generate(4, grid, kGMajor, 200);

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

  // With different seeds, pitch distributions or note counts should differ.
  bool differ_in_size = result_a.notes.size() != result_b.notes.size();
  bool differ_in_pitches = pitches_a != pitches_b;

  EXPECT_TRUE(differ_in_size || differ_in_pitches)
      << "Different seeds should produce different output";
}

// ---------------------------------------------------------------------------
// Test 10: InvalidVariationNumber
// ---------------------------------------------------------------------------

TEST(DanceGeneratorTest, InvalidVariationNumber) {
  // getDanceProfile should return a default for non-dance variation numbers.
  auto profile = getDanceProfile(1);  // Var 1 is not a dance variation.
  EXPECT_EQ(profile.primary_figura, FiguraType::Passepied)
      << "Non-dance variation numbers should return the default Passepied profile";

  // The generator should still produce output for any variation number.
  DanceGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto result = gen.generate(1, grid, kGMajor, kTestSeed);

  EXPECT_TRUE(result.success)
      << "Generator should handle non-dance variation numbers gracefully";
  EXPECT_FALSE(result.notes.empty());
}

}  // namespace
}  // namespace bach
