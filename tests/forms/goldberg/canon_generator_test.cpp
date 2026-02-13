// Tests for CanonGenerator: 3-voice canon generation for Goldberg Variations.

#include "forms/goldberg/canon/canon_generator.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr KeySignature kGMinor = {Key::G, true};
constexpr TimeSignature kTriple = {3, 4};  // 3/4 time (BWV 988 standard).

/// @brief Helper: create a CanonSpec for testing.
CanonSpec makeCanonSpec(int interval,
                        CanonTransform transform = CanonTransform::Regular,
                        KeySignature key = kGMajor,
                        MinorModeProfile minor_profile = MinorModeProfile::NaturalMinor,
                        int delay_bars = 1) {
  CanonSpec spec;
  spec.canon_interval = interval;
  spec.transform = transform;
  spec.key = key;
  spec.minor_profile = minor_profile;
  spec.delay_bars = delay_bars;
  spec.rhythmic_mode = CanonRhythmicMode::StrictRhythm;
  return spec;
}

class CanonGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
  }

  GoldbergStructuralGrid grid_;
  CanonGenerator generator_;
};

// ---------------------------------------------------------------------------
// Test 1: Var3UnisonCanon
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, Var3UnisonCanon) {
  // Var 3: Canon at the unison (interval=0), G major. Simplest canon.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  EXPECT_TRUE(result.success) << "Unison canon generation should succeed";
  EXPECT_FALSE(result.dux_notes.empty()) << "Dux should have notes";
  EXPECT_FALSE(result.comes_notes.empty()) << "Comes should have notes";
  EXPECT_FALSE(result.bass_notes.empty()) << "Bass should have notes";
}

// ---------------------------------------------------------------------------
// Test 2: Var6SecondCanon
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, Var6SecondCanon) {
  // Var 6: Canon at the 2nd (interval=1), G major.
  CanonSpec spec = makeCanonSpec(1);
  auto result = generator_.generate(spec, grid_, kTriple, 123);

  EXPECT_TRUE(result.success) << "2nd canon generation should succeed";
  EXPECT_FALSE(result.dux_notes.empty());
  EXPECT_FALSE(result.comes_notes.empty());
}

// ---------------------------------------------------------------------------
// Test 3: Var9ThirdCanon
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, Var9ThirdCanon) {
  // Var 9: Canon at the 3rd (interval=2), G major.
  CanonSpec spec = makeCanonSpec(2);
  auto result = generator_.generate(spec, grid_, kTriple, 456);

  EXPECT_TRUE(result.success) << "3rd canon generation should succeed";
  EXPECT_FALSE(result.dux_notes.empty());
  EXPECT_FALSE(result.comes_notes.empty());
}

// ---------------------------------------------------------------------------
// Test 4: AllVoicesPresent
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, AllVoicesPresent) {
  // All three voice arrays (dux, comes, bass) must be non-empty.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 789);

  ASSERT_TRUE(result.success);
  EXPECT_GT(result.dux_notes.size(), 0u) << "Dux must have notes";
  EXPECT_GT(result.comes_notes.size(), 0u) << "Comes must have notes";
  EXPECT_GT(result.bass_notes.size(), 0u) << "Bass must have notes";
}

// ---------------------------------------------------------------------------
// Test 5: DuxComesRelationship
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, DuxComesRelationship) {
  // For unison canon (interval=0), dux and comes should share the same
  // pitch class at corresponding positions. The absolute degree system may
  // place comes in a different octave for non-C keys, so we compare pitch
  // classes rather than exact MIDI values.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.dux_notes.empty());
  ASSERT_FALSE(result.comes_notes.empty());

  // Check that the majority of dux-comes pairs share the same pitch class.
  int matching = 0;
  int total = 0;
  size_t comes_idx = 0;
  Tick delay_ticks = kTriple.ticksPerBar();  // 1 bar delay.

  for (const auto& dux : result.dux_notes) {
    // Find the comes note that corresponds to this dux note.
    Tick expected_comes_tick = dux.start_tick + delay_ticks;
    while (comes_idx < result.comes_notes.size() &&
           result.comes_notes[comes_idx].start_tick < expected_comes_tick) {
      comes_idx++;
    }
    if (comes_idx < result.comes_notes.size() &&
        result.comes_notes[comes_idx].start_tick == expected_comes_tick) {
      total++;
      int dux_pc = getPitchClass(dux.pitch);
      int comes_pc = getPitchClass(result.comes_notes[comes_idx].pitch);
      if (dux_pc == comes_pc) {
        matching++;
      }
    }
  }

  ASSERT_GT(total, 0) << "Should find at least some dux-comes pairs";
  float match_rate = static_cast<float>(matching) / static_cast<float>(total);
  EXPECT_GT(match_rate, 0.9f)
      << "Unison canon: >90% of dux-comes pairs should share pitch class, got "
      << (match_rate * 100.0f) << "% (" << matching << "/" << total << ")";
}

// ---------------------------------------------------------------------------
// Test 6: ComesDelayedByOneBar
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, ComesDelayedByOneBar) {
  // Comes notes should start at tick = 1 bar's worth of ticks after dux.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.comes_notes.empty());

  Tick one_bar = kTriple.ticksPerBar();  // 3 * 480 = 1440.
  Tick first_comes_tick = result.comes_notes[0].start_tick;

  // First dux note is at tick 0, so first comes should be at tick 1440.
  EXPECT_EQ(first_comes_tick, one_bar)
      << "First comes note should start 1 bar after first dux note";
}

// ---------------------------------------------------------------------------
// Test 7: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, NotesSpan32Bars) {
  // All notes should fit within 32 bars of 3/4 time (plus comes tail).
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);

  Tick max_tick = 32 * kTriple.ticksPerBar();
  Tick max_tick_with_tail = max_tick + kTriple.ticksPerBar();  // +1 bar tail.

  for (const auto& note : result.dux_notes) {
    EXPECT_LT(note.start_tick, max_tick)
        << "Dux note should be within 32 bars";
  }

  for (const auto& note : result.comes_notes) {
    EXPECT_LT(note.start_tick, max_tick_with_tail)
        << "Comes note should be within 33 bars (32 + 1 bar tail)";
  }

  for (const auto& note : result.bass_notes) {
    EXPECT_LT(note.start_tick, max_tick)
        << "Bass note should be within 32 bars";
  }
}

// ---------------------------------------------------------------------------
// Test 8: StrongBeatConsonance
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, StrongBeatConsonance) {
  // On strong beats (beat 1 of each bar), the dux-comes interval should
  // be consonant for the majority of occurrences (>60%).
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);

  int consonant_count = 0;
  int strong_beat_count = 0;

  // Build a map of comes pitches by start_tick for quick lookup.
  std::vector<std::pair<Tick, uint8_t>> comes_by_tick;
  for (const auto& comes : result.comes_notes) {
    comes_by_tick.push_back({comes.start_tick, comes.pitch});
  }

  for (const auto& dux : result.dux_notes) {
    // Check if this dux note is on beat 1 (start of a bar).
    if (dux.start_tick % kTriple.ticksPerBar() != 0) continue;

    // Find a comes note at the same tick.
    for (const auto& [tick, pitch] : comes_by_tick) {
      if (tick == dux.start_tick) {
        strong_beat_count++;
        int interval = interval_util::compoundToSimple(
            std::abs(static_cast<int>(dux.pitch) - static_cast<int>(pitch)));
        if (interval_util::isConsonance(interval)) {
          consonant_count++;
        }
        break;
      }
    }
  }

  if (strong_beat_count > 0) {
    float consonance_rate = static_cast<float>(consonant_count) /
                            static_cast<float>(strong_beat_count);
    EXPECT_GT(consonance_rate, 0.6f)
        << "Strong beat consonance rate should be >60%, got "
        << (consonance_rate * 100.0f) << "% ("
        << consonant_count << "/" << strong_beat_count << ")";
  }
}

// ---------------------------------------------------------------------------
// Test 9: BassUsesStructuralPitch
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, BassUsesStructuralPitch) {
  // Bass notes should predominantly use structural bass pitches from the grid.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.bass_notes.empty());

  int structural_match = 0;
  int total_bass = 0;

  for (const auto& bass : result.bass_notes) {
    int bar = static_cast<int>(bass.start_tick / kTriple.ticksPerBar());
    if (bar >= 32) continue;

    uint8_t structural_pitch = grid_.getStructuralBassPitch(bar);
    // Check if bass pitch class matches structural bass pitch class.
    int bass_pc = getPitchClass(bass.pitch);
    int structural_pc = getPitchClass(structural_pitch);

    total_bass++;
    if (bass_pc == structural_pc) {
      structural_match++;
    }
  }

  if (total_bass > 0) {
    float match_rate = static_cast<float>(structural_match) /
                       static_cast<float>(total_bass);
    EXPECT_GT(match_rate, 0.3f)
        << "Bass should use structural pitch class >30% of the time, got "
        << (match_rate * 100.0f) << "%";
  }
}

// ---------------------------------------------------------------------------
// Test 10: DifferentSeedsDifferentOutput
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, DifferentSeedsDifferentOutput) {
  // Two different seeds should produce different dux melodies.
  CanonSpec spec = makeCanonSpec(0);
  auto result_a = generator_.generate(spec, grid_, kTriple, 42);
  auto result_b = generator_.generate(spec, grid_, kTriple, 9999);

  ASSERT_TRUE(result_a.success);
  ASSERT_TRUE(result_b.success);

  // Compare a few dux pitches -- at least one should differ.
  bool found_difference = false;
  size_t compare_count = std::min(result_a.dux_notes.size(),
                                  result_b.dux_notes.size());
  compare_count = std::min(compare_count, static_cast<size_t>(20));

  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (result_a.dux_notes[idx].pitch != result_b.dux_notes[idx].pitch) {
      found_difference = true;
      break;
    }
  }

  EXPECT_TRUE(found_difference)
      << "Different seeds should produce different dux melodies";
}

// ---------------------------------------------------------------------------
// Test 11: BacktrackCountReasonable
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, BacktrackCountReasonable) {
  // Backtrack count should be small for simple canons.
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  EXPECT_LT(result.backtrack_count, 20)
      << "Backtrack count should be reasonable (< 20), got "
      << result.backtrack_count;
}

// ---------------------------------------------------------------------------
// Test 12: Var15InvertedMinor
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, Var15InvertedMinor) {
  // Var 15: Canon at the 5th inverted (interval=4, inverted), G minor.
  CanonSpec spec = makeCanonSpec(4, CanonTransform::Inverted, kGMinor,
                                 MinorModeProfile::MixedBaroqueMinor);
  auto result = generator_.generate(spec, grid_, kTriple, 1234);

  EXPECT_TRUE(result.success) << "Inverted minor canon should succeed";
  EXPECT_FALSE(result.dux_notes.empty());
  EXPECT_FALSE(result.comes_notes.empty());
}

// ---------------------------------------------------------------------------
// Test 13: Var21MinorCanon
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, Var21MinorCanon) {
  // Var 21: Canon at the 7th (interval=6), G minor.
  CanonSpec spec = makeCanonSpec(6, CanonTransform::Regular, kGMinor,
                                 MinorModeProfile::HarmonicMinor);
  auto result = generator_.generate(spec, grid_, kTriple, 5678);

  EXPECT_TRUE(result.success) << "7th minor canon should succeed";
  EXPECT_FALSE(result.dux_notes.empty());
  EXPECT_FALSE(result.comes_notes.empty());
}

// ---------------------------------------------------------------------------
// Test 14: CanonSuccessRate
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, CanonSuccessRate) {
  // Generate unison canon with 10 seeds; all should succeed.
  CanonSpec spec = makeCanonSpec(0);
  int success_count = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    auto result = generator_.generate(spec, grid_, kTriple, seed * 137);
    if (result.success) success_count++;
  }

  EXPECT_EQ(success_count, 10)
      << "All 10 unison canon generations should succeed";
}

// ---------------------------------------------------------------------------
// Additional: DuxNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, DuxNotesHaveCorrectSource) {
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.dux_notes) {
    EXPECT_EQ(note.source, BachNoteSource::CanonDux)
        << "All dux notes should have CanonDux source";
  }
}

// ---------------------------------------------------------------------------
// Additional: ComesNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, ComesNotesHaveCorrectSource) {
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.comes_notes) {
    EXPECT_EQ(note.source, BachNoteSource::CanonComes)
        << "All comes notes should have CanonComes source";
  }
}

// ---------------------------------------------------------------------------
// Additional: BassNotesHaveCorrectSource
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, BassNotesHaveCorrectSource) {
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.bass_notes) {
    EXPECT_EQ(note.source, BachNoteSource::CanonFreeBass)
        << "All bass notes should have CanonFreeBass source";
  }
}

// ---------------------------------------------------------------------------
// Additional: DuxPitchesInRegister
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, DuxPitchesInRegister) {
  // Dux pitches should be within the expected register (C4-C6).
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.dux_notes) {
    EXPECT_GE(note.pitch, 48)  // Allow some margin below C4.
        << "Dux pitch should not be extremely low";
    EXPECT_LE(note.pitch, 96)  // Allow some margin above C6.
        << "Dux pitch should not be extremely high";
  }
}

// ---------------------------------------------------------------------------
// Additional: BassPitchesInRegister
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, BassPitchesInRegister) {
  // Bass pitches should be within the bass register (C2-C4).
  CanonSpec spec = makeCanonSpec(0);
  auto result = generator_.generate(spec, grid_, kTriple, 42);

  ASSERT_TRUE(result.success);
  for (const auto& note : result.bass_notes) {
    EXPECT_GE(note.pitch, 36)
        << "Bass pitch should be >= C2 (36)";
    EXPECT_LE(note.pitch, 60)
        << "Bass pitch should be <= C4 (60)";
  }
}

// ---------------------------------------------------------------------------
// Additional: MultipleIntervalsSucceed
// ---------------------------------------------------------------------------

TEST_F(CanonGeneratorTest, MultipleIntervalsSucceed) {
  // All 9 canon intervals used in BWV 988 should succeed.
  // Intervals: 0(unison), 1(2nd), 2(3rd), 3(4th), 4(5th),
  //            5(6th), 6(7th), 7(octave), 8(9th).
  for (int interval = 0; interval <= 8; ++interval) {
    CanonSpec spec = makeCanonSpec(interval);
    auto result = generator_.generate(spec, grid_, kTriple, 42 + interval);
    EXPECT_TRUE(result.success)
        << "Canon at interval " << interval << " should succeed";
    EXPECT_FALSE(result.dux_notes.empty())
        << "Canon at interval " << interval << " should produce dux notes";
  }
}

}  // namespace
}  // namespace bach
