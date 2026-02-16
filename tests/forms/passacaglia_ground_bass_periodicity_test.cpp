// Tests for ground bass periodicity in passacaglia.
// Verifies: bass pitch class pattern repeats with regularity at bar
// boundaries across all variations.

#include "forms/passacaglia.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/// @brief Create a PassacagliaConfig for periodicity testing.
/// @param seed Random seed.
/// @return PassacagliaConfig with standard settings.
PassacagliaConfig makePeriodTestConfig(uint32_t seed = 42) {
  PassacagliaConfig config;
  config.key = {Key::C, true};
  config.bpm = 60;
  config.seed = seed;
  config.num_voices = 4;
  config.num_variations = 12;
  config.ground_bass_bars = 8;
  config.append_fugue = false;
  return config;
}

// ---------------------------------------------------------------------------
// Ground bass periodicity tests
// ---------------------------------------------------------------------------

TEST(PassacagliaPeriodicityTest, BassPitchClassRepeatsAcrossVariations) {
  PassacagliaConfig config = makePeriodTestConfig(42);
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success) << result.error_message;

  // Extract ground bass notes from the pedal track (last track).
  const auto& pedal_track = result.tracks.back();
  std::vector<NoteEvent> bass_notes;
  for (const auto& note : pedal_track.notes) {
    if (note.source == BachNoteSource::GroundBass) {
      bass_notes.push_back(note);
    }
  }

  // Expected: ground_bass_bars notes per variation.
  int notes_per_var = config.ground_bass_bars;
  ASSERT_EQ(bass_notes.size(),
            static_cast<size_t>(notes_per_var * config.num_variations))
      << "Expected " << notes_per_var * config.num_variations
      << " ground bass notes";

  // Extract pitch class sequence from the first variation.
  std::vector<int> first_var_pcs;
  for (int i = 0; i < notes_per_var; ++i) {
    first_var_pcs.push_back(bass_notes[i].pitch % 12);
  }

  // Verify every subsequent variation has the same pitch class sequence.
  int mismatches = 0;
  for (int var_idx = 1; var_idx < config.num_variations; ++var_idx) {
    for (int note_idx = 0; note_idx < notes_per_var; ++note_idx) {
      size_t idx = static_cast<size_t>(var_idx * notes_per_var + note_idx);
      int pc = bass_notes[idx].pitch % 12;
      if (pc != first_var_pcs[note_idx]) {
        ++mismatches;
      }
    }
  }

  EXPECT_EQ(mismatches, 0)
      << "Ground bass pitch class pattern should repeat identically across "
      << "all " << config.num_variations << " variations, found "
      << mismatches << " mismatches";
}

TEST(PassacagliaPeriodicityTest, BassPitchRepeatsExactly) {
  // Not just pitch classes -- actual MIDI pitches should repeat.
  PassacagliaConfig config = makePeriodTestConfig(42);
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  const auto& pedal_track = result.tracks.back();
  std::vector<uint8_t> bass_pitches;
  for (const auto& note : pedal_track.notes) {
    if (note.source == BachNoteSource::GroundBass) {
      bass_pitches.push_back(note.pitch);
    }
  }

  int notes_per_var = config.ground_bass_bars;
  ASSERT_EQ(bass_pitches.size(),
            static_cast<size_t>(notes_per_var * config.num_variations));

  // Extract first variation pattern.
  std::vector<uint8_t> pattern(bass_pitches.begin(),
                                bass_pitches.begin() + notes_per_var);

  // Verify all subsequent variations match exactly.
  for (int var_idx = 1; var_idx < config.num_variations; ++var_idx) {
    for (int i = 0; i < notes_per_var; ++i) {
      size_t idx = static_cast<size_t>(var_idx * notes_per_var + i);
      EXPECT_EQ(bass_pitches[idx], pattern[i])
          << "Variation " << var_idx << " note " << i
          << ": expected pitch " << static_cast<int>(pattern[i])
          << " got " << static_cast<int>(bass_pitches[idx]);
    }
  }
}

TEST(PassacagliaPeriodicityTest, MultiSeed_BassPeriodicityPreserved) {
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    PassacagliaConfig config = makePeriodTestConfig(seed);
    PassacagliaResult result = generatePassacaglia(config);
    ASSERT_TRUE(result.success) << "Seed " << seed;

    const auto& pedal_track = result.tracks.back();
    std::vector<uint8_t> bass_pitches;
    for (const auto& note : pedal_track.notes) {
      if (note.source == BachNoteSource::GroundBass) {
        bass_pitches.push_back(note.pitch);
      }
    }

    int notes_per_var = config.ground_bass_bars;
    ASSERT_EQ(bass_pitches.size(),
              static_cast<size_t>(notes_per_var * config.num_variations))
        << "Seed " << seed;

    // Check all variations against first.
    for (int var_idx = 1; var_idx < config.num_variations; ++var_idx) {
      for (int i = 0; i < notes_per_var; ++i) {
        size_t first_idx = static_cast<size_t>(i);
        size_t curr_idx = static_cast<size_t>(var_idx * notes_per_var + i);
        EXPECT_EQ(bass_pitches[curr_idx], bass_pitches[first_idx])
            << "Seed " << seed << " var " << var_idx << " note " << i;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Period detection: bar-boundary bass pitch regularity
// ---------------------------------------------------------------------------

TEST(PassacagliaPeriodicityTest, BarBoundaryBassHasRegularPeriod) {
  PassacagliaConfig config = makePeriodTestConfig(42);
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;

  // Collect bass pitch at every bar boundary.
  const auto& pedal_track = result.tracks.back();
  std::map<Tick, uint8_t> bass_at_bar;
  for (const auto& note : pedal_track.notes) {
    if (note.source == BachNoteSource::GroundBass) {
      bass_at_bar[note.start_tick] = note.pitch;
    }
  }

  // For each variation pair (0,1), (1,2), ..., verify identical bass
  // pitches at corresponding bar boundaries.
  int matched_pairs = 0;
  int total_pairs = 0;

  for (int var_idx = 0; var_idx + 1 < config.num_variations; ++var_idx) {
    for (int bar = 0; bar < config.ground_bass_bars; ++bar) {
      Tick tick_a = static_cast<Tick>(var_idx) * variation_duration +
                    static_cast<Tick>(bar) * kTicksPerBar;
      Tick tick_b = static_cast<Tick>(var_idx + 1) * variation_duration +
                    static_cast<Tick>(bar) * kTicksPerBar;

      auto it_a = bass_at_bar.find(tick_a);
      auto it_b = bass_at_bar.find(tick_b);

      if (it_a != bass_at_bar.end() && it_b != bass_at_bar.end()) {
        ++total_pairs;
        if (it_a->second == it_b->second) {
          ++matched_pairs;
        }
      }
    }
  }

  ASSERT_GT(total_pairs, 0) << "No bar-boundary pairs to compare";

  // Ground bass is immutable, so all pairs should match.
  double match_ratio = static_cast<double>(matched_pairs) / total_pairs;
  EXPECT_GE(match_ratio, 1.0)
      << "Bass periodicity ratio " << match_ratio
      << " (expected 1.0 for immutable ground bass): "
      << matched_pairs << "/" << total_pairs << " matched";
}

// ---------------------------------------------------------------------------
// Ground bass starts and ends on tonic in every key
// ---------------------------------------------------------------------------

TEST(PassacagliaPeriodicityTest, GroundBassTonicFraming_MultiKey) {
  Key keys[] = {Key::C, Key::D, Key::G, Key::A, Key::E};
  for (Key k : keys) {
    KeySignature ks = {k, true};
    auto ground_bass = generatePassacagliaGroundBass(ks, 8, 42);
    ASSERT_FALSE(ground_bass.empty()) << "Key " << static_cast<int>(k);

    int tonic_pc = static_cast<int>(k);
    EXPECT_EQ(ground_bass.front().pitch % 12, tonic_pc)
        << "Key " << static_cast<int>(k) << ": first note not tonic";
    EXPECT_EQ(ground_bass.back().pitch % 12, tonic_pc)
        << "Key " << static_cast<int>(k) << ": last note not tonic";
  }
}

}  // namespace
}  // namespace bach
