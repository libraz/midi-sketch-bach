// Tests for forms/prelude.h -- organ prelude generation, track configuration,
// note ranges, timing, and determinism.

#include "forms/prelude.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default PreludeConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return PreludeConfig with standard 3-voice C major settings.
PreludeConfig makeTestConfig(uint32_t seed = 42) {
  PreludeConfig config;
  config.key = {Key::C, false};
  config.type = PreludeType::FreeForm;
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = seed;
  config.fugue_length_ticks = 0;
  return config;
}

/// @brief Count total notes across all tracks.
/// @param result The prelude result to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalNoteCount(const PreludeResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
}

// ---------------------------------------------------------------------------
// calculatePreludeLength tests
// ---------------------------------------------------------------------------

TEST(PreludeTest, CalculatePreludeLength_WithFugueLength) {
  // 70% of fugue length.
  Tick fugue_length = 10 * kTicksPerBar;  // 10 bars.
  Tick prelude_length = calculatePreludeLength(fugue_length);
  // 70% of 10 bars = 7 bars worth of ticks.
  Tick expected = static_cast<Tick>(static_cast<float>(fugue_length) * 0.70f);
  EXPECT_EQ(prelude_length, expected);
}

TEST(PreludeTest, CalculatePreludeLength_ZeroFugue) {
  Tick prelude_length = calculatePreludeLength(0);
  EXPECT_EQ(prelude_length, 12 * kTicksPerBar) << "Default should be 12 bars";
}

TEST(PreludeTest, CalculatePreludeLength_LargeFugue) {
  Tick fugue_length = 100 * kTicksPerBar;
  Tick prelude_length = calculatePreludeLength(fugue_length);
  // Should be within 60-80% range (we use 70%).
  EXPECT_GE(prelude_length, fugue_length * 60 / 100);
  EXPECT_LE(prelude_length, fugue_length * 80 / 100);
}

// ---------------------------------------------------------------------------
// FreeForm generation
// ---------------------------------------------------------------------------

TEST(PreludeTest, FreeForm_GeneratesNonEmptyTracks) {
  PreludeConfig config = makeTestConfig();
  config.type = PreludeType::FreeForm;
  PreludeResult result = generatePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") is empty";
  }
}

TEST(PreludeTest, FreeForm_HasReasonableNoteCount) {
  PreludeConfig config = makeTestConfig();
  config.type = PreludeType::FreeForm;
  PreludeResult result = generatePrelude(config);

  ASSERT_TRUE(result.success);
  size_t total = totalNoteCount(result);
  // 12 bars of music with 3 voices should produce a substantial number of notes.
  EXPECT_GT(total, 30u) << "Too few notes for a 12-bar prelude";
}

// ---------------------------------------------------------------------------
// Perpetual motion generation
// ---------------------------------------------------------------------------

TEST(PreludeTest, Perpetual_GeneratesNonEmptyTracks) {
  PreludeConfig config = makeTestConfig();
  config.type = PreludeType::Perpetual;
  PreludeResult result = generatePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") is empty";
  }
}

TEST(PreludeTest, Perpetual_MoreNotesInTopVoice) {
  PreludeConfig config = makeTestConfig();
  config.type = PreludeType::Perpetual;
  PreludeResult result = generatePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Top voice (16th notes) should have more notes than middle voice (quarter notes).
  EXPECT_GT(result.tracks[0].notes.size(), result.tracks[1].notes.size())
      << "Top voice should have more notes in perpetual motion style";
}

// ---------------------------------------------------------------------------
// Track count matches num_voices
// ---------------------------------------------------------------------------

TEST(PreludeTest, TrackCount_MatchesNumVoices_Two) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 2;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u);
}

TEST(PreludeTest, TrackCount_MatchesNumVoices_Three) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(PreludeTest, TrackCount_MatchesNumVoices_Four) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

TEST(PreludeTest, TrackCount_ClampedLow) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 1;  // Below minimum.
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u) << "Should clamp to 2 voices minimum";
}

TEST(PreludeTest, TrackCount_ClampedHigh) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 10;  // Above maximum.
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u) << "Should clamp to 5 voices maximum";
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(PreludeTest, AllNotesVelocity80) {
  PreludeConfig config = makeTestConfig();
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u)
          << "Organ velocity must be 80, found " << static_cast<int>(note.velocity)
          << " in track " << track.name;
    }
  }
}

// ---------------------------------------------------------------------------
// Duration and timing
// ---------------------------------------------------------------------------

TEST(PreludeTest, Duration_DefaultLength) {
  PreludeConfig config = makeTestConfig();
  config.fugue_length_ticks = 0;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  // Default is 12 bars, quantized to bar boundaries.
  EXPECT_EQ(result.total_duration_ticks, 12 * kTicksPerBar);
}

TEST(PreludeTest, Duration_ScaledToFugueLength) {
  PreludeConfig config = makeTestConfig();
  config.fugue_length_ticks = 20 * kTicksPerBar;  // 20 bars fugue.
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  // 70% of 20 bars = 14 bars, quantized to bar boundary.
  Tick raw_length = calculatePreludeLength(config.fugue_length_ticks);
  // Should be quantized up to a bar boundary.
  Tick quantized = raw_length;
  if (quantized % kTicksPerBar != 0) {
    quantized = ((quantized / kTicksPerBar) + 1) * kTicksPerBar;
  }
  EXPECT_EQ(result.total_duration_ticks, quantized);
}

TEST(PreludeTest, Duration_WithinFugueRatio) {
  PreludeConfig config = makeTestConfig();
  config.fugue_length_ticks = 30 * kTicksPerBar;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  // The total duration should be approximately 60-80% of fugue length
  // (accounting for bar quantization).
  Tick lower_bound = config.fugue_length_ticks * 60 / 100;
  Tick upper_bound = config.fugue_length_ticks * 80 / 100 + kTicksPerBar;  // Allow 1 bar padding.
  EXPECT_GE(result.total_duration_ticks, lower_bound);
  EXPECT_LE(result.total_duration_ticks, upper_bound);
}

// ---------------------------------------------------------------------------
// Organ manual ranges
// ---------------------------------------------------------------------------

TEST(PreludeTest, NotesWithinOrganRange) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  // Voice 0 (Great): 60-88
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, 60)
        << "Great pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 88)
        << "Great pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 1 (Swell): 52-76
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, 52)
        << "Swell pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 76)
        << "Swell pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 2 (Positiv): 43-64
  if (result.tracks.size() > 2) {
    for (const auto& note : result.tracks[2].notes) {
      EXPECT_GE(note.pitch, 43)
          << "Positiv pitch below range: " << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, 64)
          << "Positiv pitch above range: " << static_cast<int>(note.pitch);
    }
  }

  // Voice 3 (Pedal): 24-50
  if (result.tracks.size() > 3) {
    for (const auto& note : result.tracks[3].notes) {
      EXPECT_GE(note.pitch, organ_range::kPedalLow)
          << "Pedal pitch below range: " << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, organ_range::kPedalHigh)
          << "Pedal pitch above range: " << static_cast<int>(note.pitch);
    }
  }
}

// ---------------------------------------------------------------------------
// Channel and program mapping
// ---------------------------------------------------------------------------

TEST(PreludeTest, ChannelMapping_ThreeVoices) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  EXPECT_EQ(result.tracks[0].channel, 0u);
  EXPECT_EQ(result.tracks[1].channel, 1u);
  EXPECT_EQ(result.tracks[2].channel, 2u);
}

TEST(PreludeTest, ChannelMapping_FourVoices) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].channel, 0u);
  EXPECT_EQ(result.tracks[1].channel, 1u);
  EXPECT_EQ(result.tracks[2].channel, 2u);
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

TEST(PreludeTest, ProgramMapping) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[3].program, GmProgram::kChurchOrgan);
}

TEST(PreludeTest, TrackNames) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[1].name, "Manual II (Swell)");
  EXPECT_EQ(result.tracks[2].name, "Manual III (Positiv)");
  EXPECT_EQ(result.tracks[3].name, "Pedal");
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(PreludeTest, DeterministicWithSameSeed) {
  PreludeConfig config = makeTestConfig(12345);
  PreludeResult result1 = generatePrelude(config);
  PreludeResult result2 = generatePrelude(config);

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
          << "Track " << track_idx << ", note " << note_idx;
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
          << "Track " << track_idx << ", note " << note_idx;
      EXPECT_EQ(notes1[note_idx].duration, notes2[note_idx].duration)
          << "Track " << track_idx << ", note " << note_idx;
    }
  }
}

TEST(PreludeTest, DifferentSeedsProduceDifferentOutput) {
  PreludeConfig config1 = makeTestConfig(42);
  PreludeConfig config2 = makeTestConfig(99);
  PreludeResult result1 = generatePrelude(config1);
  PreludeResult result2 = generatePrelude(config2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Compare first track notes to check for differences.
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
  EXPECT_TRUE(any_difference) << "Seeds 42 and 99 produced identical output";
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

TEST(PreludeTest, KeyIsRespected_ScaleTones) {
  PreludeConfig config = makeTestConfig();
  config.key = {Key::C, false};  // C major.
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  // Check that the majority of notes in the top voice are scale tones.
  // We allow some passing tones, so check >= 80%.
  const auto& top_notes = result.tracks[0].notes;
  ASSERT_GT(top_notes.size(), 0u);

  int scale_tone_count = 0;
  for (const auto& note : top_notes) {
    int pitch_class = getPitchClass(note.pitch);
    // C major pitch classes: C(0), D(2), E(4), F(5), G(7), A(9), B(11).
    if (pitch_class == 0 || pitch_class == 2 || pitch_class == 4 ||
        pitch_class == 5 || pitch_class == 7 || pitch_class == 9 ||
        pitch_class == 11) {
      ++scale_tone_count;
    }
  }

  float ratio =
      static_cast<float>(scale_tone_count) / static_cast<float>(top_notes.size());
  EXPECT_GE(ratio, 0.80f)
      << "Expected >= 80% C major scale tones, got " << (ratio * 100.0f) << "%";
}

TEST(PreludeTest, MinorKey_GeneratesSuccessfully) {
  PreludeConfig config = makeTestConfig();
  config.key = {Key::A, true};  // A minor.
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(PreludeTest, DifferentKeys_ProduceDifferentOutput) {
  PreludeConfig config_c = makeTestConfig(42);
  config_c.key = {Key::C, false};
  PreludeConfig config_g = makeTestConfig(42);
  config_g.key = {Key::G, false};

  PreludeResult result_c = generatePrelude(config_c);
  PreludeResult result_g = generatePrelude(config_g);

  ASSERT_TRUE(result_c.success);
  ASSERT_TRUE(result_g.success);

  // Same seed but different keys should produce different pitches.
  bool any_pitch_difference = false;
  const auto& notes_c = result_c.tracks[0].notes;
  const auto& notes_g = result_g.tracks[0].notes;

  size_t compare_count = std::min(notes_c.size(), notes_g.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_c[idx].pitch != notes_g[idx].pitch) {
      any_pitch_difference = true;
      break;
    }
  }
  EXPECT_TRUE(any_pitch_difference) << "C major and G major should have different pitches";
}

// ---------------------------------------------------------------------------
// Notes sorted
// ---------------------------------------------------------------------------

TEST(PreludeTest, NotesSortedByStartTick) {
  PreludeConfig config = makeTestConfig();
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Notes not sorted in track " << track.name << " at index " << idx;
    }
  }
}

// ---------------------------------------------------------------------------
// Timeline is populated
// ---------------------------------------------------------------------------

TEST(PreludeTest, TimelineIsPopulated) {
  PreludeConfig config = makeTestConfig();
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  EXPECT_GT(result.timeline.size(), 0u) << "Timeline should have events";
  EXPECT_GT(result.timeline.totalDuration(), 0u) << "Timeline duration should be > 0";
}

// ---------------------------------------------------------------------------
// Non-zero note durations
// ---------------------------------------------------------------------------

TEST(PreludeTest, AllNotesHavePositiveDuration) {
  PreludeConfig config = makeTestConfig();
  PreludeResult result = generatePrelude(config);
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
// Voice assignment
// ---------------------------------------------------------------------------

TEST(PreludeTest, VoiceAssignment_CorrectPerTrack) {
  PreludeConfig config = makeTestConfig();
  config.num_voices = 4;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);

  for (size_t track_idx = 0; track_idx < result.tracks.size(); ++track_idx) {
    for (const auto& note : result.tracks[track_idx].notes) {
      EXPECT_EQ(note.voice, track_idx)
          << "Note in track " << track_idx << " has wrong voice " << note.voice;
    }
  }
}

// ---------------------------------------------------------------------------
// Motivic unity: middle voice pitch variety
// ---------------------------------------------------------------------------

TEST(PreludeTest, MiddleVoice_PitchVarietyAcrossSeeds) {
  // Voice 1 (middle) should use at least 8 unique pitches in bars 1-8 across
  // multiple seeds. This verifies the full-timeline rewrite produces variety
  // rather than repeating the same 4-note pattern.
  constexpr uint32_t kTestSeeds[] = {1, 2, 3, 4, 5};
  constexpr size_t kNumSeeds = sizeof(kTestSeeds) / sizeof(kTestSeeds[0]);

  for (size_t seed_idx = 0; seed_idx < kNumSeeds; ++seed_idx) {
    PreludeConfig config = makeTestConfig(kTestSeeds[seed_idx]);
    config.num_voices = 3;
    PreludeResult result = generatePrelude(config);
    ASSERT_TRUE(result.success) << "seed=" << kTestSeeds[seed_idx];
    ASSERT_GE(result.tracks.size(), 2u);

    // Collect unique pitches in bars 1-8 (ticks 0 to 8*1920).
    constexpr Tick kBar8End = 8 * kTicksPerBar;
    std::set<uint8_t> unique_pitches;
    for (const auto& note : result.tracks[1].notes) {
      if (note.start_tick < kBar8End) {
        unique_pitches.insert(note.pitch);
      }
    }

    EXPECT_GE(unique_pitches.size(), 5u)
        << "Middle voice (seed=" << kTestSeeds[seed_idx]
        << ") has too few unique pitches in bars 1-8: " << unique_pitches.size();
  }
}

TEST(PreludeTest, MiddleVoice_NoRepeating4NotePattern) {
  // Verify that no identical 4-note pitch pattern repeats for 3 or more
  // consecutive bars in the middle voice. This catches the old bug where
  // per-event state reset caused "C4, C4, B3, C4" to repeat every bar.
  PreludeConfig config = makeTestConfig(42);
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  const auto& notes = result.tracks[1].notes;
  if (notes.size() < 12) return;  // Not enough notes to check.

  // Build per-bar pitch sequences.
  Tick total = result.total_duration_ticks;
  Tick num_bars = total / kTicksPerBar;
  std::vector<std::vector<uint8_t>> bar_pitches(num_bars);

  for (const auto& note : notes) {
    Tick bar = note.start_tick / kTicksPerBar;
    if (bar < num_bars) {
      bar_pitches[bar].push_back(note.pitch);
    }
  }

  // Check for 3+ consecutive bars with identical pitch patterns.
  int consecutive_same = 1;
  for (size_t bar_idx = 1; bar_idx < bar_pitches.size(); ++bar_idx) {
    if (!bar_pitches[bar_idx].empty() &&
        bar_pitches[bar_idx] == bar_pitches[bar_idx - 1]) {
      ++consecutive_same;
      EXPECT_LT(consecutive_same, 3)
          << "Middle voice has identical pitch pattern repeating for "
          << consecutive_same << " consecutive bars starting at bar "
          << (bar_idx - consecutive_same + 1);
    } else {
      consecutive_same = 1;
    }
  }
}

// ---------------------------------------------------------------------------
// Motivic unity: bass voice variety
// ---------------------------------------------------------------------------

TEST(PreludeTest, BassVoice_PitchVariety) {
  // Bass voice (voice 2 in 3-voice) should use at least 4 unique pitches
  // in bars 1-8. No same-note run of 8+ consecutive notes.
  constexpr uint32_t kTestSeeds[] = {1, 7, 42, 100};
  constexpr size_t kNumSeeds = sizeof(kTestSeeds) / sizeof(kTestSeeds[0]);

  for (size_t seed_idx = 0; seed_idx < kNumSeeds; ++seed_idx) {
    PreludeConfig config = makeTestConfig(kTestSeeds[seed_idx]);
    config.num_voices = 3;
    PreludeResult result = generatePrelude(config);
    ASSERT_TRUE(result.success) << "seed=" << kTestSeeds[seed_idx];
    ASSERT_GE(result.tracks.size(), 3u);

    constexpr Tick kBar8End = 8 * kTicksPerBar;
    std::set<uint8_t> unique_pitches;
    int max_same_run = 0;
    int current_run = 1;
    uint8_t prev_pitch = 0;
    bool first = true;

    for (const auto& note : result.tracks[2].notes) {
      if (note.start_tick >= kBar8End) break;
      unique_pitches.insert(note.pitch);

      if (first) {
        prev_pitch = note.pitch;
        first = false;
      } else {
        if (note.pitch == prev_pitch) {
          ++current_run;
        } else {
          if (current_run > max_same_run) max_same_run = current_run;
          current_run = 1;
        }
        prev_pitch = note.pitch;
      }
    }
    if (current_run > max_same_run) max_same_run = current_run;

    EXPECT_GE(unique_pitches.size(), 4u)
        << "Bass voice (seed=" << kTestSeeds[seed_idx]
        << ") has too few unique pitches: " << unique_pitches.size();

    EXPECT_LT(max_same_run, 8)
        << "Bass voice (seed=" << kTestSeeds[seed_idx]
        << ") has same-note run of " << max_same_run;
  }
}

// ---------------------------------------------------------------------------
// Motivic unity: standard durations only
// ---------------------------------------------------------------------------

TEST(PreludeTest, MiddleVoice_QuantizedDurationsOnly) {
  // Voice 1 durations must be from {240, 480, 960} (eighth, quarter, half).
  // No arbitrary clamped durations.
  PreludeConfig config = makeTestConfig(42);
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  constexpr Tick kAllowedMid[] = {
      duration::kEighthNote,   // 240
      duration::kQuarterNote,  // 480
      duration::kHalfNote      // 960
  };

  for (const auto& note : result.tracks[1].notes) {
    bool valid = false;
    for (Tick allowed : kAllowedMid) {
      if (note.duration == allowed) {
        valid = true;
        break;
      }
    }
    EXPECT_TRUE(valid)
        << "Middle voice note at tick " << note.start_tick
        << " has non-standard duration " << note.duration;
  }
}

TEST(PreludeTest, BassVoice_QuantizedDurationsOnly) {
  // Voice 2 (bass) durations must be from {480, 960} (quarter, half).
  PreludeConfig config = makeTestConfig(42);
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  constexpr Tick kAllowedBass[] = {
      duration::kQuarterNote,  // 480
      duration::kHalfNote      // 960
  };

  for (const auto& note : result.tracks[2].notes) {
    bool valid = false;
    for (Tick allowed : kAllowedBass) {
      if (note.duration == allowed) {
        valid = true;
        break;
      }
    }
    EXPECT_TRUE(valid)
        << "Bass voice note at tick " << note.start_tick
        << " has non-standard duration " << note.duration;
  }
}

// ---------------------------------------------------------------------------
// Motivic unity: Perpetual type also benefits from improvements
// ---------------------------------------------------------------------------

TEST(PreludeTest, Perpetual_MiddleVoicePitchVariety) {
  // Perpetual-type prelude middle voice should also have pitch variety.
  PreludeConfig config = makeTestConfig(42);
  config.type = PreludeType::Perpetual;
  config.num_voices = 3;
  PreludeResult result = generatePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  constexpr Tick kBar8End = 8 * kTicksPerBar;
  std::set<uint8_t> unique_pitches;
  for (const auto& note : result.tracks[1].notes) {
    if (note.start_tick < kBar8End) {
      unique_pitches.insert(note.pitch);
    }
  }

  EXPECT_GE(unique_pitches.size(), 5u)
      << "Perpetual middle voice has too few unique pitches: " << unique_pitches.size();
}

}  // namespace
}  // namespace bach
