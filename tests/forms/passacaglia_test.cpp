// Tests for forms/passacaglia.h -- passacaglia generation, ground bass
// immutability, variation complexity, track configuration, pitch ranges,
// and determinism.

#include "forms/passacaglia.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default PassacagliaConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return PassacagliaConfig with standard C minor settings.
PassacagliaConfig makeTestConfig(uint32_t seed = 42) {
  PassacagliaConfig config;
  config.key = {Key::C, true};  // C minor (BWV 582 default).
  config.bpm = 60;
  config.seed = seed;
  config.num_voices = 4;
  config.num_variations = 12;
  config.ground_bass_bars = 8;
  config.append_fugue = false;  // No fugue section for unit tests.
  return config;
}

/// @brief Count total notes across all tracks.
/// @param result The passacaglia result to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalNoteCount(const PassacagliaResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GenerateSucceeds) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(PassacagliaTest, ProducesCorrectNumberOfTracks) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

TEST(PassacagliaTest, AllTracksHaveNotes) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") is empty";
  }
}

TEST(PassacagliaTest, ReasonableNoteCount) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  size_t total = totalNoteCount(result);
  // 12 variations x 8 bars x at least 2 notes (ground bass) = 192 minimum.
  // Upper voices add substantially more.
  EXPECT_GT(total, 200u) << "Too few notes for a passacaglia";
}

TEST(PassacagliaTest, InvalidConfigReturnsError) {
  PassacagliaConfig config = makeTestConfig();
  config.num_variations = 0;
  PassacagliaResult result = generatePassacaglia(config);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

// ---------------------------------------------------------------------------
// Ground bass properties
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GroundBassLength) {
  KeySignature key = {Key::C, true};
  int bars = 8;
  auto ground_bass = generatePassacagliaGroundBass(key, bars, 42);

  // 1 whole note per bar x 8 bars = 8 notes.
  EXPECT_EQ(ground_bass.size(), 8u);
}

TEST(PassacagliaTest, GroundBassWholeNoteDurations) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);

  constexpr Tick kExpectedDuration = kTicksPerBar;  // Whole note = 1920.
  for (const auto& note : ground_bass) {
    EXPECT_EQ(note.duration, kExpectedDuration)
        << "Ground bass note at tick " << note.start_tick
        << " should be a whole note (" << kExpectedDuration << " ticks)";
  }
}

TEST(PassacagliaTest, GroundBassSourceTag) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);

  for (const auto& note : ground_bass) {
    EXPECT_EQ(note.source, BachNoteSource::GroundBass)
        << "Ground bass note must have GroundBass source tag";
  }
}

TEST(PassacagliaTest, GroundBassIsImmutable) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  // Extract the pedal track (last track).
  const auto& pedal_track = result.tracks.back();

  // Collect ground bass notes (source == GroundBass) grouped by variation.
  int notes_per_variation = config.ground_bass_bars;
  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;

  // Extract just ground bass notes.
  std::vector<NoteEvent> ground_bass_notes;
  for (const auto& note : pedal_track.notes) {
    if (note.source == BachNoteSource::GroundBass) {
      ground_bass_notes.push_back(note);
    }
  }

  // Should have notes_per_variation * num_variations ground bass notes.
  ASSERT_EQ(ground_bass_notes.size(),
            static_cast<size_t>(notes_per_variation * config.num_variations));

  // Verify every variation has identical pitches and durations.
  for (int var_idx = 1; var_idx < config.num_variations; ++var_idx) {
    for (int note_idx = 0; note_idx < notes_per_variation; ++note_idx) {
      size_t first_idx = static_cast<size_t>(note_idx);
      size_t current_idx =
          static_cast<size_t>(var_idx * notes_per_variation + note_idx);

      EXPECT_EQ(ground_bass_notes[current_idx].pitch,
                ground_bass_notes[first_idx].pitch)
          << "Ground bass pitch differs in variation " << var_idx
          << " note " << note_idx;
      EXPECT_EQ(ground_bass_notes[current_idx].duration,
                ground_bass_notes[first_idx].duration)
          << "Ground bass duration differs in variation " << var_idx
          << " note " << note_idx;

      // Verify tick offset is correct for the variation.
      Tick expected_tick = ground_bass_notes[first_idx].start_tick +
                           static_cast<Tick>(var_idx) * variation_duration;
      EXPECT_EQ(ground_bass_notes[current_idx].start_tick, expected_tick)
          << "Ground bass tick offset wrong in variation " << var_idx;
    }
  }
}

TEST(PassacagliaTest, GroundBassPitchInPedalRange) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);

  for (const auto& note : ground_bass) {
    EXPECT_GE(note.pitch, organ_range::kPedalLow)
        << "Ground bass pitch " << static_cast<int>(note.pitch)
        << " below pedal range";
    EXPECT_LE(note.pitch, organ_range::kPedalHigh)
        << "Ground bass pitch " << static_cast<int>(note.pitch)
        << " above pedal range";
  }
}

TEST(PassacagliaTest, GroundBassEmptyForZeroBars) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 0, 42);

  EXPECT_TRUE(ground_bass.empty());
}

// ---------------------------------------------------------------------------
// Variation complexity increases
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, VariationComplexityIncreases) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  // Analyze the top voice (track 0 / Manual I) which has the primary figuration.
  const auto& top_voice = result.tracks[0];
  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;

  // Collect average durations for each variation stage.
  // Stage 0 (Establish, vars 0-2): quarter notes
  // Stage 1 (Develop early, vars 3-5): eighth notes
  // Stage 2 (Develop late, vars 6-8): eighth note arpeggios
  // Stage 3 (Accumulate, vars 9-11): sixteenth notes

  auto avgDurationInRange = [&](Tick range_start, Tick range_end) -> double {
    Tick total_dur = 0;
    int count = 0;
    for (const auto& note : top_voice.notes) {
      if (note.start_tick >= range_start && note.start_tick < range_end) {
        total_dur += note.duration;
        ++count;
      }
    }
    return count > 0 ? static_cast<double>(total_dur) / count : 0.0;
  };

  // Establish stage: variations 0-2.
  double establish_avg = avgDurationInRange(0, 3 * variation_duration);
  // Develop early: variations 3-5.
  double develop_early_avg =
      avgDurationInRange(3 * variation_duration, 6 * variation_duration);
  // Accumulate/Resolve: variations 9-11.
  double accumulate_avg =
      avgDurationInRange(9 * variation_duration, 12 * variation_duration);

  // Earlier stages should have longer average note durations (less complex).
  EXPECT_GT(establish_avg, develop_early_avg)
      << "Establish stage should have longer notes than Develop";
  EXPECT_GT(develop_early_avg, accumulate_avg)
      << "Develop stage should have longer notes than Accumulate";
}

// ---------------------------------------------------------------------------
// Track channel and program mapping
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, TracksHaveCorrectChannels) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  // Voice 0 -> Ch 0 (Manual I / Great).
  EXPECT_EQ(result.tracks[0].channel, 0u);
  // Voice 1 -> Ch 1 (Manual II / Swell).
  EXPECT_EQ(result.tracks[1].channel, 1u);
  // Voice 2 -> Ch 2 (Manual III / Positiv).
  EXPECT_EQ(result.tracks[2].channel, 2u);
  // Voice 3 -> Ch 3 (Pedal).
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

TEST(PassacagliaTest, TracksHaveCorrectPrograms) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[3].program, GmProgram::kChurchOrgan);
}

TEST(PassacagliaTest, TracksHaveNames) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    EXPECT_FALSE(track.name.empty()) << "Track should have a name";
  }
}

// ---------------------------------------------------------------------------
// Total duration
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, CorrectTotalDuration) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  Tick expected = static_cast<Tick>(config.num_variations) *
                  static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;
  EXPECT_EQ(result.total_duration_ticks, expected);
}

TEST(PassacagliaTest, CorrectTotalDurationCustomConfig) {
  PassacagliaConfig config = makeTestConfig();
  config.num_variations = 6;
  config.ground_bass_bars = 4;
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  Tick expected = 6u * 4u * kTicksPerBar;
  EXPECT_EQ(result.total_duration_ticks, expected);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, DeterministicOutput) {
  PassacagliaConfig config = makeTestConfig(12345);
  PassacagliaResult result1 = generatePassacaglia(config);
  PassacagliaResult result2 = generatePassacaglia(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
    const auto& notes1 = result1.tracks[track_idx].notes;
    const auto& notes2 = result2.tracks[track_idx].notes;
    ASSERT_EQ(notes1.size(), notes2.size())
        << "Track " << track_idx << " note count differs";

    for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
      EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick)
          << "Tick mismatch in track " << track_idx << " note " << note_idx;
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
          << "Pitch mismatch in track " << track_idx << " note " << note_idx;
      EXPECT_EQ(notes1[note_idx].duration, notes2[note_idx].duration)
          << "Duration mismatch in track " << track_idx << " note " << note_idx;
    }
  }
}

TEST(PassacagliaTest, DifferentSeedsProduceDifferentOutput) {
  PassacagliaConfig config1 = makeTestConfig(42);
  PassacagliaConfig config2 = makeTestConfig(99);
  PassacagliaResult result1 = generatePassacaglia(config1);
  PassacagliaResult result2 = generatePassacaglia(config2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Upper voices should differ with different seeds.
  bool any_difference = false;
  const auto& notes1 = result1.tracks[0].notes;
  const auto& notes2 = result2.tracks[0].notes;

  if (notes1.size() != notes2.size()) {
    any_difference = true;
  } else {
    for (size_t idx = 0; idx < notes1.size(); ++idx) {
      if (notes1[idx].pitch != notes2[idx].pitch ||
          notes1[idx].start_tick != notes2[idx].start_tick) {
        any_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(any_difference) << "Different seeds should produce different output";
}

// ---------------------------------------------------------------------------
// All notes in range
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, AllNotesInRange) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  // Track 0: Manual I (Great) — soprano: C4-E6 (60-88).
  // Tightened voice ranges for minimal overlap.
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, static_cast<uint8_t>(60))
        << "Manual I pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, static_cast<uint8_t>(88))
        << "Manual I pitch above range: " << static_cast<int>(note.pitch);
  }

  // Track 1: Manual II (Swell) — alto: G3-E5 (55-76).
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, static_cast<uint8_t>(55))
        << "Manual II pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, static_cast<uint8_t>(76))
        << "Manual II pitch above range: " << static_cast<int>(note.pitch);
  }

  // Track 2: Manual III (Positiv) — tenor: C3-A4 (48-69).
  for (const auto& note : result.tracks[2].notes) {
    EXPECT_GE(note.pitch, static_cast<uint8_t>(48))
        << "Manual III pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, static_cast<uint8_t>(69))
        << "Manual III pitch above range: " << static_cast<int>(note.pitch);
  }

  // Track 3: Pedal -- C1-D3 (24-50).
  for (const auto& note : result.tracks[3].notes) {
    EXPECT_GE(note.pitch, organ_range::kPedalLow)
        << "Pedal pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kPedalHigh)
        << "Pedal pitch above range: " << static_cast<int>(note.pitch);
  }
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, AllNotesVelocity80) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u)
          << "Organ velocity must be 80, found "
          << static_cast<int>(note.velocity) << " in track " << track.name;
    }
  }
}

// ---------------------------------------------------------------------------
// Notes sorted
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, NotesSortedByStartTick) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Notes not sorted in track " << track.name << " at index " << idx;
    }
  }
}

// ---------------------------------------------------------------------------
// Non-zero durations
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, AllNotesHavePositiveDuration) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note at tick " << note.start_tick << " has zero duration in track "
          << track.name;
    }
  }
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, MajorKeyGeneratesSuccessfully) {
  PassacagliaConfig config = makeTestConfig();
  config.key = {Key::D, false};  // D major.
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(PassacagliaTest, MinorKeyGeneratesSuccessfully) {
  PassacagliaConfig config = makeTestConfig();
  config.key = {Key::G, true};  // G minor.
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// Ground bass standalone API
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GroundBassStartsOnTick0) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);

  ASSERT_FALSE(ground_bass.empty());
  EXPECT_EQ(ground_bass[0].start_tick, 0u);
}

TEST(PassacagliaTest, GroundBassNotesContiguous) {
  KeySignature key = {Key::C, true};
  auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);

  constexpr Tick kWhole = kTicksPerBar;
  for (size_t idx = 0; idx < ground_bass.size(); ++idx) {
    EXPECT_EQ(ground_bass[idx].start_tick, static_cast<Tick>(idx) * kWhole)
        << "Ground bass note " << idx << " is not contiguous";
  }
}

TEST(PassacagliaTest, GroundBassDifferentKeys) {
  auto bass_c = generatePassacagliaGroundBass({Key::C, true}, 8, 42);
  auto bass_g = generatePassacagliaGroundBass({Key::G, true}, 8, 42);

  ASSERT_EQ(bass_c.size(), bass_g.size());

  // Different keys should produce at least some different pitches.
  bool any_different = false;
  for (size_t idx = 0; idx < bass_c.size(); ++idx) {
    if (bass_c[idx].pitch != bass_g[idx].pitch) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different)
      << "Different keys should produce different ground bass pitches";
}

// ---------------------------------------------------------------------------
// Baroque-style ground bass properties
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GroundBassStartsOnTonic) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    ASSERT_FALSE(ground_bass.empty()) << "Empty for seed " << seed;

    int tonic_class = static_cast<int>(key.tonic);
    int first_class = ground_bass[0].pitch % 12;
    EXPECT_EQ(first_class, tonic_class)
        << "First note should be tonic (seed " << seed << ")";
  }
}

TEST(PassacagliaTest, GroundBassEndsOnTonic) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    ASSERT_FALSE(ground_bass.empty()) << "Empty for seed " << seed;

    int tonic_class = static_cast<int>(key.tonic);
    int last_class = ground_bass.back().pitch % 12;
    EXPECT_EQ(last_class, tonic_class)
        << "Last note should be tonic (seed " << seed << ")";
  }
}

TEST(PassacagliaTest, GroundBassMaxTwoConsecutiveSamePitch) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);

    for (size_t idx = 2; idx < ground_bass.size(); ++idx) {
      bool three_same = (ground_bass[idx].pitch == ground_bass[idx - 1].pitch) &&
                        (ground_bass[idx - 1].pitch == ground_bass[idx - 2].pitch);
      EXPECT_FALSE(three_same)
          << "Three consecutive same pitches at index " << idx
          << " (seed " << seed << ")";
    }
  }
}

TEST(PassacagliaTest, GroundBassNoLeapAboveMajor6th) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);

    for (size_t idx = 1; idx < ground_bass.size(); ++idx) {
      int interval = std::abs(static_cast<int>(ground_bass[idx].pitch) -
                              static_cast<int>(ground_bass[idx - 1].pitch));
      // Body leaps ≤ major 6th (9 semitones). Cadential tail (last 2 notes)
      // may have wider leaps for leading-tone resolution (up to 11 semitones).
      bool is_cadential_tail = (idx >= ground_bass.size() - 2);
      int max_leap = is_cadential_tail ? 12 : 9;
      EXPECT_LE(interval, max_leap)
          << "Leap of " << interval << " semitones between index "
          << (idx - 1) << " and " << idx << " (seed " << seed << ")";
    }
  }
}

TEST(PassacagliaTest, GroundBassStepwiseRatio) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    if (ground_bass.size() < 2) continue;

    int stepwise_count = 0;
    int total_intervals = static_cast<int>(ground_bass.size()) - 1;
    for (size_t idx = 1; idx < ground_bass.size(); ++idx) {
      int interval = std::abs(static_cast<int>(ground_bass[idx].pitch) -
                              static_cast<int>(ground_bass[idx - 1].pitch));
      if (interval <= 2) ++stepwise_count;
    }
    double ratio = static_cast<double>(stepwise_count) / total_intervals;
    EXPECT_GE(ratio, 0.5)
        << "Stepwise ratio " << ratio << " below 50% (seed " << seed << ")";
  }
}

TEST(PassacagliaTest, GroundBassVarietyAcrossSeeds) {
  std::set<std::vector<uint8_t>> unique_patterns;
  KeySignature key = {Key::C, true};
  for (uint32_t seed = 0; seed < 50; ++seed) {
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    std::vector<uint8_t> pattern;
    for (const auto& note : ground_bass) {
      pattern.push_back(note.pitch);
    }
    unique_patterns.insert(pattern);
  }
  EXPECT_GE(unique_patterns.size(), 3u)
      << "At least 3 distinct patterns expected across 50 seeds";
}

TEST(PassacagliaTest, GroundBassTwoConsecutiveOnlyAtOpening) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);

    for (size_t idx = 2; idx < ground_bass.size(); ++idx) {
      // Consecutive same pitch after bar 1 should not occur.
      EXPECT_NE(ground_bass[idx].pitch, ground_bass[idx - 1].pitch)
          << "Same pitch at index " << (idx - 1) << "-" << idx
          << " (only allowed at 0-1, seed " << seed << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Voice count handling
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, ThreeVoiceGeneratesSuccessfully) {
  PassacagliaConfig config = makeTestConfig();
  config.num_voices = 3;
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  // Note: generator.cpp enforces min 4 voices for passacaglia, but
  // clampVoiceCount inside generatePassacaglia still clamps to [3,5].
  // The actual voice count depends on which clamping takes precedence.
  EXPECT_GE(result.tracks.size(), 3u);
}

TEST(PassacagliaTest, FiveVoiceGeneratesSuccessfully) {
  PassacagliaConfig config = makeTestConfig();
  config.num_voices = 5;
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u);
}

TEST(PassacagliaTest, VoiceCountClampedToMinimum) {
  PassacagliaConfig config = makeTestConfig();
  config.num_voices = 1;  // Below minimum.
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.tracks.size(), 3u);
}

// ---------------------------------------------------------------------------
// Multiple seeds stability
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, MultiSeedGenerationSucceeds) {
  for (uint32_t seed = 0; seed < 10; ++seed) {
    PassacagliaConfig config = makeTestConfig(seed);
    PassacagliaResult result = generatePassacaglia(config);

    EXPECT_TRUE(result.success) << "Failed for seed " << seed;
    EXPECT_GT(totalNoteCount(result), 0u) << "No notes for seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Baroque-style ground bass quality (fixes 1-4 verification)
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GroundBassEndingTonicInBaseRegister) {
  // Fix 1 verification: final note must be within 12 semitones of the first
  // note (no octave displacement from leap smoothing of cadential tail).
  for (uint32_t seed = 0; seed < 20; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    ASSERT_GE(ground_bass.size(), 2u) << "seed " << seed;

    int first_pitch = static_cast<int>(ground_bass.front().pitch);
    int last_pitch = static_cast<int>(ground_bass.back().pitch);
    EXPECT_LE(std::abs(last_pitch - first_pitch), 12)
        << "Final tonic displaced by " << std::abs(last_pitch - first_pitch)
        << " semitones from opening (seed " << seed << ")";
  }
}

TEST(PassacagliaTest, GroundBassCadentialTailNotSmoothed) {
  // Fix 1 verification: when cadential degree 6 produces a leading-tone
  // resolution (e.g. B->C in C minor = 11 semitones down), the final note
  // must NOT be brought up an octave by leap smoothing.
  KeySignature key = {Key::C, true};
  // Test across many seeds: at least one should use degree 6 cadence.
  int leading_tone_count = 0;
  for (uint32_t seed = 0; seed < 100; ++seed) {
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    ASSERT_GE(ground_bass.size(), 2u);
    size_t n = ground_bass.size();

    uint8_t penultimate = ground_bass[n - 2].pitch;
    uint8_t final_note = ground_bass[n - 1].pitch;

    // Check if penultimate is B (pitch class 11) = leading tone in C minor.
    if (penultimate % 12 == 11) {
      ++leading_tone_count;
      // Final note should be C in low register (C2=36 or C3=48), not C4=60.
      EXPECT_LE(final_note, 48u)
          << "Leading tone B(" << static_cast<int>(penultimate)
          << ")->C should resolve to low register, got "
          << static_cast<int>(final_note) << " (seed " << seed << ")";
    }
  }
  // Sanity: at least some seeds should produce leading-tone cadences.
  EXPECT_GT(leading_tone_count, 0)
      << "No leading-tone cadences found in 100 seeds";
}

TEST(PassacagliaTest, GroundBassTonicStartEndMultipleKeys) {
  // Verify tonic start/end for all 12 keys in both major and minor.
  Key all_keys[] = {Key::C,  Key::Cs, Key::D,  Key::Eb, Key::E,  Key::F,
                    Key::Fs, Key::G,  Key::Ab, Key::A,  Key::Bb, Key::B};
  for (Key k : all_keys) {
    for (bool is_minor : {true, false}) {
      KeySignature key = {k, is_minor};
      auto ground_bass = generatePassacagliaGroundBass(key, 8, 42);
      ASSERT_FALSE(ground_bass.empty())
          << "Empty for key " << static_cast<int>(k) << " minor=" << is_minor;

      int tonic_class = static_cast<int>(k);
      int first_class = ground_bass.front().pitch % 12;
      int last_class = ground_bass.back().pitch % 12;

      EXPECT_EQ(first_class, tonic_class)
          << "First note wrong for key " << static_cast<int>(k)
          << " minor=" << is_minor;
      EXPECT_EQ(last_class, tonic_class)
          << "Last note wrong for key " << static_cast<int>(k)
          << " minor=" << is_minor;
    }
  }
}

TEST(PassacagliaTest, GroundBassCadentialLeadingTone) {
  // Fix 3 verification: across 100 seeds, leading-tone (degree 6) should
  // appear in at least 10% of cadential penultimate notes, confirming that
  // the context-aware selection still produces variety.
  KeySignature key = {Key::C, true};
  int leading_tone_count = 0;
  for (uint32_t seed = 0; seed < 100; ++seed) {
    auto ground_bass = generatePassacagliaGroundBass(key, 8, seed);
    ASSERT_GE(ground_bass.size(), 2u);
    uint8_t penultimate = ground_bass[ground_bass.size() - 2].pitch;
    // In C minor, leading tone B natural = pitch class 11.
    if (penultimate % 12 == 11) {
      ++leading_tone_count;
    }
  }
  EXPECT_GE(leading_tone_count, 10)
      << "Leading tone appeared only " << leading_tone_count
      << "/100 times (expected >= 10%)";
}

TEST(PassacagliaTest, GroundBassExtendedLength) {
  // Fix 4 verification: 12-bar ground bass stays in pedal range,
  // has no excessive leaps, and ends on tonic.
  for (uint32_t seed = 0; seed < 10; ++seed) {
    KeySignature key = {Key::C, true};
    auto ground_bass = generatePassacagliaGroundBass(key, 12, seed);
    ASSERT_EQ(ground_bass.size(), 12u) << "seed " << seed;

    // All pitches in pedal range.
    for (size_t idx = 0; idx < ground_bass.size(); ++idx) {
      EXPECT_GE(ground_bass[idx].pitch, organ_range::kPedalLow)
          << "Below pedal range at index " << idx << " (seed " << seed << ")";
      EXPECT_LE(ground_bass[idx].pitch, organ_range::kPedalHigh)
          << "Above pedal range at index " << idx << " (seed " << seed << ")";
    }

    // Body leaps ≤ major 6th (9 semitones), cadential tail ≤ octave.
    for (size_t idx = 1; idx < ground_bass.size(); ++idx) {
      int interval = std::abs(static_cast<int>(ground_bass[idx].pitch) -
                              static_cast<int>(ground_bass[idx - 1].pitch));
      bool is_cadential_tail = (idx >= ground_bass.size() - 2);
      int max_leap = is_cadential_tail ? 12 : 9;
      EXPECT_LE(interval, max_leap)
          << "Leap of " << interval << " at index " << idx
          << " (seed " << seed << ")";
    }

    // Tonic ending.
    int tonic_class = static_cast<int>(key.tonic);
    EXPECT_EQ(ground_bass.back().pitch % 12, tonic_class)
        << "Not ending on tonic (seed " << seed << ")";

    // Tonic start.
    EXPECT_EQ(ground_bass.front().pitch % 12, tonic_class)
        << "Not starting on tonic (seed " << seed << ")";
  }
}

// ---------------------------------------------------------------------------
// Phase 5b: New tests for passacaglia overhaul verification
// ---------------------------------------------------------------------------

TEST(PassacagliaTest, GroundBassAudible) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  const auto& pedal_track = result.tracks.back();
  Tick variation_duration =
      static_cast<Tick>(config.ground_bass_bars) * kTicksPerBar;

  // Each variation must have ground bass notes in the pedal track.
  for (int var_idx = 0; var_idx < config.num_variations; ++var_idx) {
    Tick var_start = static_cast<Tick>(var_idx) * variation_duration;
    Tick var_end = var_start + variation_duration;

    int bass_count = 0;
    for (const auto& note : pedal_track.notes) {
      if (note.start_tick >= var_start && note.start_tick < var_end &&
          note.source == BachNoteSource::GroundBass) {
        ++bass_count;
      }
    }
    EXPECT_GT(bass_count, 0)
        << "No ground bass notes in variation " << var_idx;
  }
}

TEST(PassacagliaTest, HarmonyMatchesBassDegree) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);

  const auto& pedal_track = result.tracks.back();
  int mismatches = 0;
  int total_checked = 0;

  for (const auto& note : pedal_track.notes) {
    if (note.source != BachNoteSource::GroundBass) continue;
    const auto& event = result.timeline.getAt(note.start_tick);

    // The timeline's bass_pitch should match the ground bass note pitch.
    if (event.bass_pitch != note.pitch) {
      ++mismatches;
    }
    ++total_checked;
  }

  EXPECT_GT(total_checked, 0) << "No ground bass notes to check";
  EXPECT_EQ(mismatches, 0)
      << mismatches << "/" << total_checked << " bass-harmony mismatches";
}

TEST(PassacagliaTest, StrongBeatConsonance) {
  PassacagliaConfig config = makeTestConfig();
  PassacagliaResult result = generatePassacaglia(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Collect bass pitches at each bar start.
  const auto& pedal_track = result.tracks.back();
  std::map<Tick, uint8_t> bass_at_bar;
  for (const auto& note : pedal_track.notes) {
    if (note.source == BachNoteSource::GroundBass) {
      bass_at_bar[note.start_tick] = note.pitch;
    }
  }

  int consonant_count = 0;
  int total_checked = 0;

  // Check upper voice notes at strong beats (bar starts).
  for (size_t track_idx = 0; track_idx < result.tracks.size() - 1; ++track_idx) {
    for (const auto& note : result.tracks[track_idx].notes) {
      auto it = bass_at_bar.find(note.start_tick);
      if (it == bass_at_bar.end()) continue;

      uint8_t bass_pitch = it->second;
      int interval = std::abs(static_cast<int>(note.pitch) -
                              static_cast<int>(bass_pitch)) % 12;

      // Consonant intervals: unison(0), m3(3), M3(4), P4(5), P5(7),
      // m6(8), M6(9), octave(0).
      bool consonant = (interval == 0 || interval == 3 || interval == 4 ||
                        interval == 5 || interval == 7 || interval == 8 ||
                        interval == 9);
      if (consonant) ++consonant_count;
      ++total_checked;
    }
  }

  if (total_checked > 0) {
    double ratio = static_cast<double>(consonant_count) / total_checked;
    EXPECT_GE(ratio, 0.5)
        << "Only " << (ratio * 100) << "% of strong-beat intervals are "
        << "consonant with bass (" << consonant_count << "/" << total_checked
        << ")";
  }
}

TEST(PassacagliaTest, MinVoicesEnforced) {
  // When requesting fewer than 4 voices via generator.cpp pathway,
  // passacaglia should still produce at least 4 tracks since generator
  // enforces num_voices >= 4. Here we test the direct API which uses
  // clampVoiceCount (min 3), showing the minimum boundary.
  PassacagliaConfig config = makeTestConfig();
  config.num_voices = 2;  // Below minimum.
  PassacagliaResult result = generatePassacaglia(config);

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.tracks.size(), 3u)
      << "Direct API should clamp to at least 3 voices";
}

}  // namespace
}  // namespace bach
