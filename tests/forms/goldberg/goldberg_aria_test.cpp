// Tests for Goldberg Aria (Sarabande) generator.

#include "forms/goldberg/goldberg_aria.h"

#include <algorithm>
#include <cstdint>
#include <numeric>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "forms/goldberg/goldberg_aria_theme.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// Common test fixtures.
constexpr KeySignature kGMajor = {Key::G, false};
constexpr TimeSignature kThreeFour = {3, 4};
constexpr uint32_t kTestSeed = 42;
constexpr int kGridBars = 32;

// Soprano range from the plan.
constexpr uint8_t kSopranoLow = 67;   // G4
constexpr uint8_t kSopranoHigh = 81;  // A5

class AriaGeneratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
    result_ = generator_.generate(grid_, kGMajor, kThreeFour, kTestSeed);
  }

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

  std::vector<int> structural_pcs;
  structural_pcs.reserve(kGridBars);
  for (int bar_idx = 0; bar_idx < kGridBars; ++bar_idx) {
    uint8_t pitch = grid_.getStructuralBassPitch(bar_idx);
    structural_pcs.push_back(getPitchClass(pitch));
  }

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

  for (size_t idx = 0; idx < result_.melody_notes.size(); ++idx) {
    EXPECT_EQ(da_capo.melody_notes[idx].start_tick,
              result_.melody_notes[idx].start_tick + kOffset)
        << "Melody note " << idx << " should be offset by " << kOffset;
  }

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
// Test 12: DifferentSeedsDifferentMelody
// ---------------------------------------------------------------------------

TEST_F(AriaGeneratorTest, DifferentSeedsDifferentMelody) {
  auto result_seed1 = generator_.generate(grid_, kGMajor, kThreeFour, 1);
  auto result_seed2 = generator_.generate(grid_, kGMajor, kThreeFour, 2);

  ASSERT_TRUE(result_seed1.success);
  ASSERT_TRUE(result_seed2.success);
  ASSERT_FALSE(result_seed1.melody_notes.empty());
  ASSERT_FALSE(result_seed2.melody_notes.empty());

  // With generative melody, different seeds should produce different notes.
  bool different = (result_seed1.melody_notes.size() !=
                    result_seed2.melody_notes.size());
  if (!different) {
    for (size_t idx = 0; idx < result_seed1.melody_notes.size(); ++idx) {
      if (result_seed1.melody_notes[idx].pitch !=
          result_seed2.melody_notes[idx].pitch) {
        different = true;
        break;
      }
    }
  }

  EXPECT_TRUE(different)
      << "Different seeds should produce different melody";
}

// ===========================================================================
// AriaTheme generation tests
// ===========================================================================

class AriaThemeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    grid_ = GoldbergStructuralGrid::createMajor();
    theme_ = generateAriaMelody(grid_, kGMajor, kTestSeed);
  }

  /// Helper: build chord from bar info with degree-derived root.
  static Chord chordFromBar(const StructuralBarInfo& bar) {
    Chord chord;
    chord.degree = bar.chord_degree;
    chord.quality = majorKeyQuality(bar.chord_degree);
    uint8_t semi = degreeSemitones(bar.chord_degree);
    chord.root_pitch = static_cast<uint8_t>((static_cast<int>(Key::G) + semi) % 12);
    chord.inversion = 0;
    return chord;
  }

  /// Helper: check if pitch class matches any chord tone.
  static bool isChordTone(uint8_t pitch, const std::vector<uint8_t>& tones) {
    for (uint8_t ct : tones) {
      if (getPitchClass(pitch) == getPitchClass(ct)) return true;
    }
    return false;
  }

  GoldbergStructuralGrid grid_;
  AriaTheme theme_;
};

// ---------------------------------------------------------------------------
// Test 13: GeneratedThemeValid
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, GeneratedThemeValid) {
  int non_zero_count = 0;

  for (int idx = 0; idx < AriaTheme::kTotalBeats; ++idx) {
    const auto& beat = theme_.beats[static_cast<size_t>(idx)];

    if (beat.pitch > 0) {
      EXPECT_GE(beat.pitch, kSopranoLow)
          << "Beat " << idx << " pitch " << static_cast<int>(beat.pitch)
          << " below soprano range";
      EXPECT_LE(beat.pitch, kSopranoHigh)
          << "Beat " << idx << " pitch " << static_cast<int>(beat.pitch)
          << " above soprano range";
      ++non_zero_count;
    } else {
      EXPECT_EQ(beat.func, BeatFunction::Hold)
          << "Beat " << idx << " has pitch=0 but func is not Hold";
    }

    EXPECT_LE(static_cast<int>(beat.func), 4)
        << "Beat " << idx << " has invalid BeatFunction value";
  }

  float non_zero_ratio = static_cast<float>(non_zero_count) / 96.0f;
  EXPECT_GT(non_zero_ratio, 0.6f)
      << "Theme should have at least 60% explicit pitches, got "
      << non_zero_ratio;

  // Each bar's beat 1 should have a non-zero pitch.
  for (int bar = 0; bar < 32; ++bar) {
    EXPECT_GT(theme_.beats[static_cast<size_t>(bar * 3)].pitch, 0u)
        << "Bar " << bar << " beat 1 must have a non-zero pitch";
  }
}

// ---------------------------------------------------------------------------
// Test 14: GeneratedThemeResolvesHold
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, GeneratedThemeResolvesHold) {
  for (int bar = 0; bar < 32; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      uint8_t pitch = theme_.getPitch(bar, beat);
      EXPECT_GT(pitch, 0u)
          << "getPitch(" << bar << ", " << beat << ") should resolve to non-zero";
    }
  }
}

// ---------------------------------------------------------------------------
// Test 15: DifferentSeedsDifferentKern
// ---------------------------------------------------------------------------

TEST(AriaThemeGenTest, DifferentSeedsDifferentKern) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto theme1 = generateAriaMelody(grid, kGMajor, 1);
  auto theme2 = generateAriaMelody(grid, kGMajor, 2);

  int diff_count = 0;
  for (int bar = 0; bar < 32; ++bar) {
    if (theme1.getPitch(bar, 0) != theme2.getPitch(bar, 0)) {
      ++diff_count;
    }
  }

  EXPECT_GE(diff_count, 4)
      << "Seeds 1 and 2 should produce at least 4 different Beat 1 pitches, got "
      << diff_count;
}

// ---------------------------------------------------------------------------
// Test 16: SameSeedSameMelody
// ---------------------------------------------------------------------------

TEST(AriaThemeGenTest, SameSeedSameMelody) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto theme1 = generateAriaMelody(grid, kGMajor, 42);
  auto theme2 = generateAriaMelody(grid, kGMajor, 42);

  for (int idx = 0; idx < AriaTheme::kTotalBeats; ++idx) {
    EXPECT_EQ(theme1.beats[static_cast<size_t>(idx)].pitch,
              theme2.beats[static_cast<size_t>(idx)].pitch)
        << "Same seed should produce identical pitches at beat " << idx;
    EXPECT_EQ(theme1.beats[static_cast<size_t>(idx)].func,
              theme2.beats[static_cast<size_t>(idx)].func)
        << "Same seed should produce identical functions at beat " << idx;
  }
}

// ---------------------------------------------------------------------------
// Test 17: AllBeat1AreChordTones
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, AllBeat1AreChordTones) {
  for (int bar = 0; bar < 32; ++bar) {
    uint8_t pitch = theme_.beats[static_cast<size_t>(bar * 3)].pitch;
    ASSERT_GT(pitch, 0u) << "Bar " << bar << " beat 1 has no pitch";

    const auto& bar_info = grid_.getBar(bar);
    Chord chord = chordFromBar(bar_info);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);

    EXPECT_TRUE(isChordTone(pitch, chord_tones))
        << "Bar " << bar << " beat 1 pitch " << static_cast<int>(pitch)
        << " is not a chord tone";
  }
}

// ---------------------------------------------------------------------------
// Test 18: MaxLeapWithinBounds
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, MaxLeapWithinBounds) {
  uint8_t prev = 0;
  for (int idx = 0; idx < AriaTheme::kTotalBeats; ++idx) {
    uint8_t pitch = theme_.beats[static_cast<size_t>(idx)].pitch;
    if (pitch == 0) continue;

    if (prev > 0) {
      int interval = absoluteInterval(pitch, prev);
      EXPECT_LE(interval, 7)
          << "Leap from " << static_cast<int>(prev)
          << " to " << static_cast<int>(pitch)
          << " at beat " << idx << " exceeds P5 (7 semitones)";
    }
    prev = pitch;
  }
}

// ---------------------------------------------------------------------------
// Test 19: CadenceBarsResolve
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, CadenceBarsResolve) {
  // Cadence bars: Beat 1 must be a chord tone of the cadence bar's harmony.
  // Tonic/dominant landing depends on whether the bar's chord contains those PCs.
  // For bars with standard diatonic chords (I, V), this enforces tonic/dominant.
  // For applied chords (V/vi, V/V), it's correct to land on their chord tones.
  int cadence_count = 0;
  int chord_tone_match = 0;

  for (int bar = 0; bar < 32; ++bar) {
    const auto& info = grid_.getBar(bar);
    if (!info.cadence.has_value()) continue;
    ++cadence_count;

    uint8_t beat1 = theme_.getPitch(bar, 0);
    Chord chord = chordFromBar(info);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);

    // Beat 1 must be a chord tone (already tested in AllBeat1AreChordTones).
    if (isChordTone(beat1, chord_tones)) {
      ++chord_tone_match;
    }
  }

  EXPECT_GT(cadence_count, 0) << "Should have at least one cadence bar";
  EXPECT_EQ(chord_tone_match, cadence_count)
      << "All cadence bars should have Beat 1 as chord tone";
}

// ---------------------------------------------------------------------------
// Test 20: SuspensionAlwaysResolves
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, SuspensionAlwaysResolves) {
  for (int bar = 0; bar < 32; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      if (theme_.getFunction(bar, beat) != BeatFunction::Suspension43) continue;

      // The next sounding beat should be stepwise resolution (downward).
      int next_beat = beat + 1;
      int next_bar = bar;
      if (next_beat >= 3) {
        next_beat = 0;
        next_bar = bar + 1;
      }
      if (next_bar >= 32) continue;

      uint8_t susp_pitch = theme_.getPitch(bar, beat);
      uint8_t res_pitch = theme_.getPitch(next_bar, next_beat);
      int interval = absoluteInterval(susp_pitch, res_pitch);

      EXPECT_LE(interval, 3)
          << "Suspension at bar " << bar << " beat " << beat
          << " (pitch " << static_cast<int>(susp_pitch)
          << ") resolution interval=" << interval << " exceeds stepwise (3)";
    }
  }
}

// ---------------------------------------------------------------------------
// Test 21: AppogiaturaAlwaysResolves
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, AppogiaturaAlwaysResolves) {
  for (int bar = 0; bar < 32; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      if (theme_.getFunction(bar, beat) != BeatFunction::Appoggiatura) continue;

      int next_beat = beat + 1;
      int next_bar = bar;
      if (next_beat >= 3) {
        next_beat = 0;
        next_bar = bar + 1;
      }
      if (next_bar >= 32) continue;

      uint8_t app_pitch = theme_.getPitch(bar, beat);
      uint8_t res_pitch = theme_.getPitch(next_bar, next_beat);
      int interval = absoluteInterval(app_pitch, res_pitch);

      EXPECT_LE(interval, 3)
          << "Appoggiatura at bar " << bar << " beat " << beat
          << " (pitch " << static_cast<int>(app_pitch)
          << ") resolution interval=" << interval << " exceeds stepwise (3)";
    }
  }
}

// ---------------------------------------------------------------------------
// Test 22: ConsonantRatioPerBar
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, ConsonantRatioPerBar) {
  int bars_checked = 0;
  int bars_passing = 0;

  for (int bar = 0; bar < 32; ++bar) {
    // Skip bars that contain suspension or appoggiatura (valid non-chord tones).
    bool has_nct_function = false;
    for (int beat = 0; beat < 3; ++beat) {
      BeatFunction func = theme_.getFunction(bar, beat);
      if (func == BeatFunction::Suspension43 || func == BeatFunction::Appoggiatura) {
        has_nct_function = true;
        break;
      }
    }
    if (has_nct_function) continue;

    const auto& bar_info = grid_.getBar(bar);
    Chord chord = chordFromBar(bar_info);
    auto chord_tones = collectChordTonesInRange(chord, kSopranoLow, kSopranoHigh);
    if (chord_tones.empty()) continue;

    int ct_count = 0;
    int sounding_count = 0;
    for (int beat = 0; beat < 3; ++beat) {
      uint8_t pitch = theme_.beats[static_cast<size_t>(bar * 3 + beat)].pitch;
      if (pitch == 0) continue;
      ++sounding_count;
      if (isChordTone(pitch, chord_tones)) ++ct_count;
    }

    if (sounding_count >= 2) {
      ++bars_checked;
      if (ct_count >= 2) ++bars_passing;
    }
  }

  // At least 80% of checked bars should have consonant ratio >= 2/3.
  ASSERT_GT(bars_checked, 0);
  float ratio = static_cast<float>(bars_passing) /
                static_cast<float>(bars_checked);
  EXPECT_GE(ratio, 0.8f)
      << "At least 80% of bars (without suspension/appoggiatura) should have "
      << ">= 2/3 chord tones, got " << bars_passing << "/" << bars_checked;
}

// ---------------------------------------------------------------------------
// Test 23: NoTripleSamePitchDownbeat
// ---------------------------------------------------------------------------

TEST_F(AriaThemeTest, NoTripleSamePitchDownbeat) {
  for (int bar = 2; bar < 32; ++bar) {
    uint8_t p0 = theme_.getPitch(bar - 2, 0);
    uint8_t p1 = theme_.getPitch(bar - 1, 0);
    uint8_t p2 = theme_.getPitch(bar, 0);

    bool triple = (p0 == p1 && p1 == p2);
    EXPECT_FALSE(triple)
        << "Bars " << (bar - 2) << "-" << bar
        << " have triple same downbeat pitch " << static_cast<int>(p0);
  }
}

// ---------------------------------------------------------------------------
// Test 24: GridSeedDependency
// ---------------------------------------------------------------------------

TEST(AriaThemeGenTest, GridSeedDependency) {
  auto grid1 = GoldbergStructuralGrid::createMajor();
  auto grid2 = GoldbergStructuralGrid::createMajor();

  // Generate Aria and write back to grid.
  auto theme1 = generateAriaMelody(grid1, kGMajor, 1);
  auto theme2 = generateAriaMelody(grid2, kGMajor, 2);

  for (int bar = 0; bar < 32; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      grid1.setAriaMelody(bar, beat, theme1.getPitch(bar, beat));
      grid2.setAriaMelody(bar, beat, theme2.getPitch(bar, beat));
    }
  }

  // Grids should differ.
  int diff = 0;
  for (int bar = 0; bar < 32; ++bar) {
    for (int beat = 0; beat < 3; ++beat) {
      if (grid1.getBar(bar).aria_melody[static_cast<size_t>(beat)] !=
          grid2.getBar(bar).aria_melody[static_cast<size_t>(beat)]) {
        ++diff;
      }
    }
  }

  EXPECT_GT(diff, 0) << "Different seeds should produce different grid aria_melody";
}

}  // namespace
}  // namespace bach
