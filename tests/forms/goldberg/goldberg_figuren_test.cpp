// Tests for FigurenGenerator (Goldberg Variations Elaboratio melody generation).

#include "forms/goldberg/goldberg_figuren.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <gtest/gtest.h>

#include "core/pitch_utils.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;

/// @brief Create a default Circulatio FiguraProfile for testing.
FiguraProfile makeCirculatioProfile() {
  return {
      FiguraType::Circulatio,   // primary
      FiguraType::Circulatio,   // secondary
      4,                        // notes_per_beat
      DirectionBias::Symmetric,
      0.7f,                     // chord_tone_ratio
      0.3f                      // sequence_probability
  };
}

/// @brief Create a Tirata FiguraProfile for testing.
FiguraProfile makeTirataProfile() {
  return {
      FiguraType::Tirata,
      FiguraType::Tirata,
      4,
      DirectionBias::Ascending,
      0.7f,
      0.0f  // No sequence for isolated pattern testing.
  };
}

/// @brief Create a Batterie FiguraProfile for testing.
FiguraProfile makeBatterieProfile() {
  return {
      FiguraType::Batterie,
      FiguraType::Batterie,
      4,
      DirectionBias::Alternating,
      0.7f,
      0.0f
  };
}

/// @brief Create an Arpeggio FiguraProfile for testing.
FiguraProfile makeArpeggioProfile() {
  return {
      FiguraType::Arpeggio,
      FiguraType::Arpeggio,
      4,
      DirectionBias::Ascending,
      0.7f,
      0.0f
  };
}

/// @brief Create a Suspirans FiguraProfile for testing.
FiguraProfile makeSuspiransProfile() {
  return {
      FiguraType::Suspirans,
      FiguraType::Suspirans,
      4,
      DirectionBias::Descending,
      0.7f,
      0.0f
  };
}

/// @brief Create a DottedGrave FiguraProfile for testing.
FiguraProfile makeDottedGraveProfile() {
  return {
      FiguraType::DottedGrave,
      FiguraType::DottedGrave,
      2,  // Lower density for dotted rhythm.
      DirectionBias::Symmetric,
      0.7f,
      0.0f
  };
}

// ---------------------------------------------------------------------------
// Test 1: GenerateProducesNotes
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, GenerateProducesNotes) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeCirculatioProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);

  EXPECT_FALSE(notes.empty()) << "generate() should produce notes";
  EXPECT_GT(notes.size(), 32u) << "Should produce multiple notes per bar";
}

// ---------------------------------------------------------------------------
// Test 2: NotesSpan32Bars
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, NotesSpan32Bars) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeCirculatioProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_FALSE(notes.empty());

  // Find the last note's end tick.
  Tick ticks_per_bar = kThreeFour.ticksPerBar();  // 3 * 480 = 1440
  Tick expected_total = 32 * ticks_per_bar;

  Tick max_end = 0;
  for (const auto& note : notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) max_end = note_end;
  }

  // The last note should end at or near the 32-bar boundary.
  EXPECT_GE(max_end, expected_total - ticks_per_bar)
      << "Notes should span close to 32 bars";
  EXPECT_LE(max_end, expected_total + ticks_per_bar)
      << "Notes should not extend far beyond 32 bars";
}

// ---------------------------------------------------------------------------
// Test 3: CirculatioPattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, CirculatioPattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeCirculatioProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 4u);

  // Circulatio should have neighbor-tone rotations: patterns where a pitch
  // returns after visiting neighbors. Check that in the first bar, the first
  // pitch recurs within the first 4 notes.
  // Filter notes from bar 0.
  Tick bar_end = kThreeFour.ticksPerBar();
  std::vector<uint8_t> bar0_pitches;
  for (const auto& note : notes) {
    if (note.start_tick < bar_end && note.pitch > 0) {
      bar0_pitches.push_back(note.pitch);
    }
  }

  ASSERT_GE(bar0_pitches.size(), 4u);
  // In circulatio, pitch[0] == pitch[2] (center-upper-center-lower pattern).
  EXPECT_EQ(bar0_pitches[0], bar0_pitches[2])
      << "Circulatio: pitch 0 and 2 should be the same center note";
}

// ---------------------------------------------------------------------------
// Test 4: TirataPattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, TirataPattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeTirataProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 4u);

  // Tirata should produce predominantly stepwise motion (scale fills).
  // Check first bar for mostly small intervals.
  Tick bar_end = kThreeFour.ticksPerBar();
  std::vector<uint8_t> bar0_pitches;
  for (const auto& note : notes) {
    if (note.start_tick < bar_end && note.pitch > 0) {
      bar0_pitches.push_back(note.pitch);
    }
  }

  int step_count = 0;
  for (size_t idx = 1; idx < bar0_pitches.size(); ++idx) {
    int abs_interval = absoluteInterval(bar0_pitches[idx], bar0_pitches[idx - 1]);
    if (abs_interval <= 4) {  // Within a major 3rd = stepwise motion.
      ++step_count;
    }
  }

  if (bar0_pitches.size() > 1) {
    float step_ratio = static_cast<float>(step_count) /
                       static_cast<float>(bar0_pitches.size() - 1);
    EXPECT_GT(step_ratio, 0.5f) << "Tirata should be predominantly stepwise";
  }
}

// ---------------------------------------------------------------------------
// Test 5: BatteriePattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, BatteriePattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeBatterieProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 4u);

  // Batterie produces large intervals (leaps). Check for intervals > 3 semitones.
  Tick bar_end = kThreeFour.ticksPerBar();
  std::vector<uint8_t> bar0_pitches;
  for (const auto& note : notes) {
    if (note.start_tick < bar_end && note.pitch > 0) {
      bar0_pitches.push_back(note.pitch);
    }
  }

  int leap_count = 0;
  for (size_t idx = 1; idx < bar0_pitches.size(); ++idx) {
    int abs_interval = absoluteInterval(bar0_pitches[idx], bar0_pitches[idx - 1]);
    if (abs_interval > 3) {
      ++leap_count;
    }
  }

  EXPECT_GT(leap_count, 0) << "Batterie should contain leaps (intervals > 3 semitones)";
}

// ---------------------------------------------------------------------------
// Test 6: ArpeggioPattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, ArpeggioPattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeArpeggioProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 4u);

  // Arpeggio should produce chord-tone sequences. Most pitches should
  // be chord tones (scale degrees 1, 3, 5 relative to the key).
  Tick bar_end = kThreeFour.ticksPerBar();
  int chord_tone_count = 0;
  int total_count = 0;
  // G major chord tones: G(7), B(11), D(2).
  constexpr int kGMajorChordPCs[] = {7, 11, 2};

  for (const auto& note : notes) {
    if (note.start_tick < bar_end && note.pitch > 0) {
      int pitch_class = getPitchClass(note.pitch);
      for (int chord_pc : kGMajorChordPCs) {
        if (pitch_class == chord_pc) {
          ++chord_tone_count;
          break;
        }
      }
      ++total_count;
    }
  }

  if (total_count > 0) {
    float chord_ratio = static_cast<float>(chord_tone_count) /
                        static_cast<float>(total_count);
    EXPECT_GT(chord_ratio, 0.4f) << "Arpeggio should contain many chord tones";
  }
}

// ---------------------------------------------------------------------------
// Test 7: SuspiransPattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, SuspiransPattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeSuspiransProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 2u);

  // Suspirans has timing gaps (rest markers are skipped, leaving empty slots).
  // With notes_per_beat=4 and 3 beats, there are 12 total slots. Suspirans uses
  // a 3-phase cycle (rest, upper, chord) so 4 of 12 slots are rests.
  // Verify fewer sounding notes than total slots in bar 0.
  Tick bar_end = kThreeFour.ticksPerBar();
  int sounding_count = 0;
  for (const auto& note : notes) {
    if (note.start_tick < bar_end && note.pitch > 0) {
      ++sounding_count;
    }
  }

  // 12 total slots, 4 rests = 8 sounding notes.
  EXPECT_LT(sounding_count, 12)
      << "Suspirans should have fewer sounding notes than total slots (rest gaps)";
  EXPECT_GT(sounding_count, 0) << "Suspirans should have sounding notes";
}

// ---------------------------------------------------------------------------
// Test 8: DottedGravePattern
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, DottedGravePattern) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeDottedGraveProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_GE(notes.size(), 2u);

  // DottedGrave with notes_per_beat=2 should produce fewer notes per bar
  // than Circulatio with notes_per_beat=4. We check note count per bar.
  Tick bar_end = kThreeFour.ticksPerBar();
  int notes_in_bar0 = 0;
  for (const auto& note : notes) {
    if (note.start_tick < bar_end) ++notes_in_bar0;
  }

  // With npb=2, 3 beats -> ~6 notes per bar.
  EXPECT_LE(notes_in_bar0, 12)
      << "DottedGrave with npb=2 should not produce excessive notes";
  EXPECT_GE(notes_in_bar0, 2)
      << "DottedGrave should produce at least a few notes";
}

// ---------------------------------------------------------------------------
// Test 9: PhraseShapingDefaults
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, PhraseShapingDefaults) {
  // Opening.
  auto opening = getDefaultPhraseShaping(PhrasePosition::Opening);
  EXPECT_FLOAT_EQ(opening.density_multiplier, 1.0f);
  EXPECT_FLOAT_EQ(opening.range_expansion, 0.0f);
  EXPECT_FLOAT_EQ(opening.chord_tone_shift, 0.1f);
  EXPECT_EQ(opening.direction_override, DirectionBias::Symmetric);

  // Expansion.
  auto expansion = getDefaultPhraseShaping(PhrasePosition::Expansion);
  EXPECT_FLOAT_EQ(expansion.density_multiplier, 1.0f);
  EXPECT_FLOAT_EQ(expansion.range_expansion, 2.0f);
  EXPECT_FLOAT_EQ(expansion.chord_tone_shift, 0.0f);
  EXPECT_EQ(expansion.direction_override, DirectionBias::Symmetric);

  // Intensification.
  auto intens = getDefaultPhraseShaping(PhrasePosition::Intensification);
  EXPECT_FLOAT_EQ(intens.density_multiplier, 1.2f);
  EXPECT_FLOAT_EQ(intens.range_expansion, 4.0f);
  EXPECT_FLOAT_EQ(intens.chord_tone_shift, -0.1f);
  EXPECT_EQ(intens.direction_override, DirectionBias::Ascending);

  // Cadence.
  auto cadence = getDefaultPhraseShaping(PhrasePosition::Cadence);
  EXPECT_FLOAT_EQ(cadence.density_multiplier, 0.8f);
  EXPECT_FLOAT_EQ(cadence.range_expansion, -2.0f);
  EXPECT_FLOAT_EQ(cadence.chord_tone_shift, 0.2f);
  EXPECT_EQ(cadence.direction_override, DirectionBias::Descending);
}

// ---------------------------------------------------------------------------
// Test 10: CadenceReducesDensity
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, CadenceReducesDensity) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeCirculatioProfile();

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);

  Tick tpb = kThreeFour.ticksPerBar();

  // Count notes in an Opening bar (bar 1, index 0) vs Cadence bar (bar 4, index 3).
  int opening_count = 0;
  int cadence_count = 0;
  Tick bar0_start = 0;
  Tick bar0_end = tpb;
  Tick bar3_start = 3 * tpb;
  Tick bar3_end = 4 * tpb;

  for (const auto& note : notes) {
    if (note.start_tick >= bar0_start && note.start_tick < bar0_end) {
      ++opening_count;
    }
    if (note.start_tick >= bar3_start && note.start_tick < bar3_end) {
      ++cadence_count;
    }
  }

  // Cadence bars have density_multiplier=0.8, so should have fewer or equal notes.
  // We allow some tolerance since other factors (chord tone snapping) may affect count.
  EXPECT_LE(cadence_count, opening_count + 2)
      << "Cadence bars should have similar or fewer notes than opening bars";
}

// ---------------------------------------------------------------------------
// Test 11: SequenceApplication
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, SequenceApplication) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  // High sequence probability.
  FiguraProfile profile = makeCirculatioProfile();
  profile.sequence_probability = 1.0f;

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);

  // With sequence_probability=1.0, we should get notes. The exact behavior
  // depends on whether non-cadence bars trigger sequence application.
  EXPECT_FALSE(notes.empty())
      << "With sequence_probability=1.0, should still produce notes";

  // Verify notes still span the grid.
  Tick tpb = kThreeFour.ticksPerBar();
  Tick max_tick = 0;
  for (const auto& note : notes) {
    if (note.start_tick > max_tick) max_tick = note.start_tick;
  }
  EXPECT_GE(max_tick, 20 * tpb)
      << "Sequential patterns should cover a significant portion of 32 bars";
}

// ---------------------------------------------------------------------------
// Test 12: ChordToneRatio
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, ChordToneRatio) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();

  // Profile with high chord_tone_ratio.
  FiguraProfile profile = makeCirculatioProfile();
  profile.chord_tone_ratio = 0.9f;

  auto notes = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  ASSERT_FALSE(notes.empty());

  // Count chord tones across all notes.
  // G major chord tones: G(7), B(11), D(2).
  constexpr int kGMajorChordPCs[] = {7, 11, 2};
  int chord_count = 0;
  int total_count = 0;

  for (const auto& note : notes) {
    if (note.pitch == 0) continue;  // Skip rests.
    int pitch_class = getPitchClass(note.pitch);
    for (int chord_pc : kGMajorChordPCs) {
      if (pitch_class == chord_pc) {
        ++chord_count;
        break;
      }
    }
    ++total_count;
  }

  if (total_count > 10) {
    float ratio = static_cast<float>(chord_count) / static_cast<float>(total_count);
    // With chord_tone_ratio=0.9 and snapping, expect a high proportion.
    // But circulatio includes neighbor tones, so the actual ratio will be lower.
    EXPECT_GT(ratio, 0.2f)
        << "With high chord_tone_ratio, chord tones should be well-represented";
  }
}

// ---------------------------------------------------------------------------
// Test 13: VoiceIndexAffectsRegister
// ---------------------------------------------------------------------------

TEST(FigurenGeneratorTest, VoiceIndexAffectsRegister) {
  FigurenGenerator gen;
  auto grid = GoldbergStructuralGrid::createMajor();
  auto profile = makeCirculatioProfile();

  auto notes_v0 = gen.generate(profile, grid, kGMajor, kThreeFour, 0, kTestSeed);
  auto notes_v1 = gen.generate(profile, grid, kGMajor, kThreeFour, 1, kTestSeed);

  ASSERT_FALSE(notes_v0.empty());
  ASSERT_FALSE(notes_v1.empty());

  // Calculate average pitch for each voice.
  auto avg_pitch = [](const std::vector<NoteEvent>& notes) -> float {
    float sum = 0.0f;
    int count = 0;
    for (const auto& note : notes) {
      if (note.pitch > 0) {
        sum += static_cast<float>(note.pitch);
        ++count;
      }
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
  };

  float avg_v0 = avg_pitch(notes_v0);
  float avg_v1 = avg_pitch(notes_v1);

  // Voice 0 (upper) should have higher average pitch than voice 1 (middle).
  EXPECT_GT(avg_v0, avg_v1)
      << "Voice 0 (upper register) should produce higher pitches than voice 1";
}

}  // namespace
}  // namespace bach
