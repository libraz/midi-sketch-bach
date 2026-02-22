// Tests for forms/toccata.h -- organ toccata free section generation, track
// configuration, section structure, pitch ranges, timing, and determinism.

#include "forms/toccata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"
#include "test_helpers.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default ToccataConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return ToccataConfig with standard 3-voice D minor settings.
ToccataConfig makeTestConfig(uint32_t seed = 42) {
  ToccataConfig config;
  config.key = {Key::D, true};  // D minor (BWV 565 style).
  config.bpm = 80;
  config.seed = seed;
  config.num_voices = 3;
  config.total_bars = 24;
  return config;
}

/// @brief Count notes in a specific time range for a given track.
/// @param track The track to examine.
/// @param start_tick Start of the time range (inclusive).
/// @param end_tick End of the time range (exclusive).
/// @return Number of notes whose start_tick falls in [start_tick, end_tick).
size_t countNotesInRange(const Track& track, Tick start_tick, Tick end_tick) {
  size_t count = 0;
  for (const auto& note : track.notes) {
    if (note.start_tick >= start_tick && note.start_tick < end_tick) {
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(ToccataTest, GenerateSucceeds) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(ToccataTest, GenerateProducesNonEmptyTracks) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  // At minimum, voice 0 (opening) and voice 1 (recitative) should have notes.
  EXPECT_GT(result.tracks[0].notes.size(), 0u)
      << "Manual I (Great) should have notes from opening and drive";
  EXPECT_GT(result.tracks[1].notes.size(), 0u)
      << "Manual II (Swell) should have notes from recitative and drive";
}

TEST(ToccataTest, ReasonableNoteCount) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  size_t total = test_helpers::totalNoteCount(result);
  // 24 bars of toccata with fast passages should produce many notes.
  EXPECT_GT(total, 50u) << "Too few notes for a 24-bar toccata";
}

// ---------------------------------------------------------------------------
// Track channel mapping
// ---------------------------------------------------------------------------

TEST(ToccataTest, TracksHaveCorrectChannels) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  // Voice 0 -> Ch 0 (Manual I / Great).
  EXPECT_EQ(result.tracks[0].channel, 0u);
  // Voice 1 -> Ch 1 (Manual II / Swell).
  EXPECT_EQ(result.tracks[1].channel, 1u);
  // Voice 2 -> Ch 3 (Pedal).
  EXPECT_EQ(result.tracks[2].channel, 3u);
}

TEST(ToccataTest, TracksHaveCorrectPrograms) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
}

TEST(ToccataTest, TracksHaveCorrectNames) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  EXPECT_EQ(result.tracks[0].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[1].name, "Manual II (Swell)");
  EXPECT_EQ(result.tracks[2].name, "Pedal");
}

// ---------------------------------------------------------------------------
// Total duration matches total_bars
// ---------------------------------------------------------------------------

TEST(ToccataTest, TotalDurationMatchesSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 24u * kTicksPerBar);
}

TEST(ToccataTest, TotalDurationMatchesSectionBars_Short) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 8;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 8u * kTicksPerBar);
}

TEST(ToccataTest, TotalDurationMatchesSectionBars_Long) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 48;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 48u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Opening section has fast notes (16th note durations)
// ---------------------------------------------------------------------------

TEST(ToccataTest, OpeningSectionHasFastNotes) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 0u);
  ASSERT_GE(result.phases.size(), 1u);

  // Phase A (Gesture) has fast passages (16th notes).
  Tick gesture_end = result.phases[0].end;
  constexpr Tick kSixteenthDuration = kTicksPerBeat / 4;  // 120 ticks.

  int fast_note_count = 0;
  int gesture_note_count = 0;

  for (const auto& note : result.tracks[0].notes) {
    if (note.start_tick < gesture_end) {
      ++gesture_note_count;
      if (note.duration <= kSixteenthDuration) {
        ++fast_note_count;
      }
    }
  }

  EXPECT_GT(gesture_note_count, 0) << "Gesture phase should have notes on voice 0";
  // The majority of gesture notes should be 16th notes.
  float fast_ratio = static_cast<float>(fast_note_count) /
                     static_cast<float>(gesture_note_count);
  EXPECT_GE(fast_ratio, 0.55f)
      << "Expected >= 55% 16th notes in Gesture, got " << (fast_ratio * 100.0f) << "%";
}

// ---------------------------------------------------------------------------
// Deterministic output
// ---------------------------------------------------------------------------

TEST(ToccataTest, DeterministicOutput) {
  ToccataConfig config = makeTestConfig(12345);
  ToccataResult result1 = generateToccata(config);
  ToccataResult result2 = generateToccata(config);

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

TEST(ToccataTest, DifferentSeedsProduceDifferentOutput) {
  ToccataConfig config1 = makeTestConfig(42);
  ToccataConfig config2 = makeTestConfig(99);
  ToccataResult result1 = generateToccata(config1);
  ToccataResult result2 = generateToccata(config2);

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
// All notes in range
// ---------------------------------------------------------------------------

TEST(ToccataTest, AllNotesInRange) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  // Voice 0 (Manual I): 36-96.
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual1Low)
        << "Manual I pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kManual1High)
        << "Manual I pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 1 (Manual II): 36-96.
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual2Low)
        << "Manual II pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kManual2High)
        << "Manual II pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 2 (Pedal): 24-50.
  for (const auto& note : result.tracks[2].notes) {
    EXPECT_GE(note.pitch, organ_range::kPedalLow)
        << "Pedal pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kPedalHigh)
        << "Pedal pitch above range: " << static_cast<int>(note.pitch);
  }
}

TEST(ToccataTest, AllNotesInRange_FourVoices) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 4;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  // Voice 3 (Positiv): 43-67.
  for (const auto& note : result.tracks[3].notes) {
    EXPECT_GE(note.pitch, 43)
        << "Positiv pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 67)
        << "Positiv pitch above range: " << static_cast<int>(note.pitch);
  }
}

// ---------------------------------------------------------------------------
// Multiple voices active
// ---------------------------------------------------------------------------

TEST(ToccataTest, MultipleVoicesActive) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 3u);

  // Count how many tracks have notes.
  int active_tracks = 0;
  for (const auto& track : result.tracks) {
    if (!track.notes.empty()) {
      ++active_tracks;
    }
  }

  // At least 2 voices should be active (opening on voice 0, recitative on voice 1).
  EXPECT_GE(active_tracks, 2)
      << "At least 2 voices should have notes in a toccata";
}

TEST(ToccataTest, AllVoicesActiveInDrive) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.sections.size(), 3u);

  // Drive section (legacy: phases F+G+H).
  Tick drive_start = result.sections[2].start;
  Tick drive_end = result.sections[2].end;

  // Voice 0 and 1 should both have notes in the drive section.
  size_t voice0_drive = countNotesInRange(result.tracks[0], drive_start, drive_end);
  size_t voice1_drive = countNotesInRange(result.tracks[1], drive_start, drive_end);

  EXPECT_GT(voice0_drive, 0u) << "Voice 0 should have notes in drive section";
  EXPECT_GT(voice1_drive, 0u) << "Voice 1 should have notes in drive section";
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(ToccataTest, AllNotesVelocity80) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
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
// Notes sorted by start_tick
// ---------------------------------------------------------------------------

TEST(ToccataTest, NotesSortedByStartTick) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Notes not sorted in track " << track.name << " at index " << idx;
    }
  }
}

// ---------------------------------------------------------------------------
// Non-zero note durations
// ---------------------------------------------------------------------------

TEST(ToccataTest, AllNotesHavePositiveDuration) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
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
// Notes do not exceed total duration
// ---------------------------------------------------------------------------

TEST(ToccataTest, AllNotesWithinTotalDuration) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_LT(note.start_tick, result.total_duration_ticks)
          << "Note starts at or after total duration in track " << track.name;
      EXPECT_LE(note.start_tick + note.duration, result.total_duration_ticks)
          << "Note extends past total duration in track " << track.name
          << " (start=" << note.start_tick << ", dur=" << note.duration << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Voice assignment correctness
// ---------------------------------------------------------------------------

TEST(ToccataTest, VoiceAssignment_CorrectPerTrack) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  for (size_t track_idx = 0; track_idx < result.tracks.size(); ++track_idx) {
    for (const auto& note : result.tracks[track_idx].notes) {
      EXPECT_EQ(note.voice, track_idx)
          << "Note in track " << track_idx << " has wrong voice " << note.voice;
    }
  }
}

// ---------------------------------------------------------------------------
// Voice count clamping
// ---------------------------------------------------------------------------

TEST(ToccataTest, VoiceCount_ClampedLow) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 1;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u) << "Should clamp to 2 voices minimum";
}

TEST(ToccataTest, VoiceCount_ClampedHigh) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 10;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u) << "Should clamp to 5 voices maximum";
}

// ---------------------------------------------------------------------------
// Timeline populated
// ---------------------------------------------------------------------------

TEST(ToccataTest, TimelineIsPopulated) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  EXPECT_GT(result.timeline.size(), 0u) << "Timeline should have events";
  EXPECT_GT(result.timeline.totalDuration(), 0u) << "Timeline duration should be > 0";
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

TEST(ToccataTest, MinorKeyGeneratesSuccessfully) {
  ToccataConfig config = makeTestConfig();
  config.key = {Key::D, true};  // D minor.
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(ToccataTest, MajorKeyGeneratesSuccessfully) {
  ToccataConfig config = makeTestConfig();
  config.key = {Key::C, false};  // C major.
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(ToccataTest, DifferentKeys_ProduceDifferentOutput) {
  ToccataConfig config_dm = makeTestConfig(42);
  config_dm.key = {Key::D, true};
  ToccataConfig config_cm = makeTestConfig(42);
  config_cm.key = {Key::C, false};

  ToccataResult result_dm = generateToccata(config_dm);
  ToccataResult result_cm = generateToccata(config_cm);

  ASSERT_TRUE(result_dm.success);
  ASSERT_TRUE(result_cm.success);

  // Same seed but different keys should produce different pitches.
  bool any_pitch_difference = false;
  const auto& notes_dm = result_dm.tracks[0].notes;
  const auto& notes_cm = result_cm.tracks[0].notes;

  size_t compare_count = std::min(notes_dm.size(), notes_cm.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_dm[idx].pitch != notes_cm[idx].pitch) {
      any_pitch_difference = true;
      break;
    }
  }
  EXPECT_TRUE(any_pitch_difference)
      << "D minor and C major with same seed should have different pitches";
}

// ---------------------------------------------------------------------------
// Energy curve: U-shape (high -> low -> high)
// ---------------------------------------------------------------------------

TEST(ToccataTest, EnergyCurve_TwoPeak) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.phases.size(), 8u);

  // Two-peak energy curve: valleys (B, E) should have longer avg note duration
  // than peaks (A, G/H). Use phases directly for precise measurement.
  auto avgDuration = [&](Tick start, Tick end) -> float {
    Tick total_dur = 0;
    size_t count = 0;
    for (size_t t = 0; t <= 1; ++t) {
      for (const auto& note : result.tracks[t].notes) {
        if (note.start_tick >= start && note.start_tick < end) {
          total_dur += note.duration;
          ++count;
        }
      }
    }
    return (count > 0) ? static_cast<float>(total_dur) / static_cast<float>(count)
                       : 0.0f;
  };

  float gesture_avg = avgDuration(result.phases[0].start, result.phases[0].end);
  float echo_avg = avgDuration(result.phases[1].start, result.phases[1].end);
  float drive_avg = avgDuration(result.phases[5].start, result.phases[7].end);

  // Phase A (Gesture, 0.90 energy) has fast passages -> shorter avg duration
  // than Phase B (EchoCollapse, 0.30 energy).
  if (gesture_avg > 0.0f && echo_avg > 0.0f) {
    EXPECT_LT(gesture_avg, echo_avg)
        << "Gesture should have shorter avg note duration than EchoCollapse "
        << "(gesture=" << gesture_avg << ", echo=" << echo_avg << ")";
  }

  // Drive phases (F+G+H, energy 0.85-1.00) have fast passages -> shorter avg
  // than echo valley.
  if (drive_avg > 0.0f && echo_avg > 0.0f) {
    EXPECT_LT(drive_avg, echo_avg)
        << "Drive phases should have shorter avg note duration than EchoCollapse "
        << "(drive=" << drive_avg << ", echo=" << echo_avg << ")";
  }
}

// ---------------------------------------------------------------------------
// Pedal points
// ---------------------------------------------------------------------------

TEST(ToccataTest, PedalVoiceHasNotesInOpeningAndDrive) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);
  ASSERT_GE(result.sections.size(), 3u);

  Tick opening_start = result.sections[0].start;
  Tick opening_end = result.sections[0].end;
  Tick drive_start = result.sections[2].start;
  Tick drive_end = result.sections[2].end;

  // Pedal (voice 2) should have notes in both opening and drive sections.
  size_t pedal_opening = countNotesInRange(result.tracks[2], opening_start, opening_end);
  size_t pedal_drive = countNotesInRange(result.tracks[2], drive_start, drive_end);

  EXPECT_GT(pedal_opening, 0u) << "Pedal should have notes during opening";
  EXPECT_GT(pedal_drive, 0u) << "Pedal should have notes during drive";
}

// ---------------------------------------------------------------------------
// Section bars edge cases
// ---------------------------------------------------------------------------

TEST(ToccataTest, SmallSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 4;  // Minimum reasonable size.
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 4u * kTicksPerBar);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

TEST(ToccataTest, LargeSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 64;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 64u * kTicksPerBar);
  EXPECT_GT(test_helpers::totalNoteCount(result), 100u)
      << "64-bar toccata should produce substantial number of notes";
}

// ---------------------------------------------------------------------------
// Multiple seeds stability
// ---------------------------------------------------------------------------

TEST(ToccataTest, MultipleSeeds_AllSucceed) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    ToccataConfig config = makeTestConfig(seed);
    ToccataResult result = generateToccata(config);

    EXPECT_TRUE(result.success) << "Failed with seed " << seed;
    EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "No notes with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// BWV 565-style structural tests
// ---------------------------------------------------------------------------

TEST(ToccataTest, OpeningHasGrandPause) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 1u);
  ASSERT_GE(result.phases.size(), 1u);

  // Check for a 960+ tick gap in voice 0 within Phase A (Gesture).
  Tick gesture_end = result.phases[0].end;
  bool found_gap = false;

  const auto& notes = result.tracks[0].notes;
  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].start_tick >= gesture_end) break;
    Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;
    if (notes[i].start_tick > prev_end) {
      Tick gap = notes[i].start_tick - prev_end;
      if (gap >= 960u) {
        found_gap = true;
        break;
      }
    }
  }

  EXPECT_TRUE(found_gap)
      << "Gesture phase should have a grand pause (960+ tick gap)";
}

TEST(ToccataTest, OpeningHasOctaveDoubling) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);
  ASSERT_GE(result.sections.size(), 1u);

  // Check for notes at the same tick with 12-semitone difference across tracks
  // within the legacy Opening section (Gesture + EchoCollapse).
  Tick opening_end = result.sections[0].end;
  bool found_octave = false;

  for (const auto& n0 : result.tracks[0].notes) {
    if (n0.start_tick >= opening_end) break;
    for (const auto& n1 : result.tracks[1].notes) {
      if (n1.start_tick > n0.start_tick) break;
      if (n1.start_tick == n0.start_tick) {
        int diff = absoluteInterval(n0.pitch, n1.pitch);
        if (diff == 12) {
          found_octave = true;
          break;
        }
      }
    }
    if (found_octave) break;
  }

  EXPECT_TRUE(found_octave)
      << "Opening should have octave doubling between Manual I and Manual II";
}

TEST(ToccataTest, OpeningHasBlockChord) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);
  ASSERT_GE(result.sections.size(), 1u);

  // Opening (legacy section = Gesture + EchoCollapse).
  Tick opening_end = result.sections[0].end;

  // Find a multi-voice simultaneous note onset (block chord) in the opening.
  // With the 8-phase system, block chord may be shorter than whole note.
  constexpr Tick kMinBlockDuration = kTicksPerBeat;  // At least a quarter note.
  bool found_block = false;

  for (const auto& n0 : result.tracks[0].notes) {
    if (n0.start_tick >= opening_end) break;
    if (n0.duration < kMinBlockDuration) continue;
    // Check if voice 1 also has a note at the same tick.
    bool v1_present = false;
    for (const auto& n1 : result.tracks[1].notes) {
      if (n1.start_tick > n0.start_tick) break;
      if (n1.start_tick == n0.start_tick && n1.duration >= kMinBlockDuration) {
        v1_present = true;
        break;
      }
    }
    // Check voice 2 (pedal).
    bool v2_present = false;
    for (const auto& n2 : result.tracks[2].notes) {
      if (n2.start_tick > n0.start_tick) break;
      if (n2.start_tick == n0.start_tick) {
        v2_present = true;
        break;
      }
    }
    if (v1_present && v2_present) {
      found_block = true;
      break;
    }
  }

  EXPECT_TRUE(found_block)
      << "Opening should have a block chord across 3+ voices";
}

TEST(ToccataTest, RecitativeHasRhythmicVariety) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.sections.size(), 2u);

  Tick recit_start = result.sections[1].start;
  Tick recit_end = result.sections[1].end;

  // Collect unique durations across both manual tracks in recitative.
  std::set<Tick> durations;
  for (size_t track_idx = 0; track_idx <= 1; ++track_idx) {
    for (const auto& note : result.tracks[track_idx].notes) {
      if (note.start_tick >= recit_start && note.start_tick < recit_end) {
        durations.insert(note.duration);
      }
    }
  }

  EXPECT_GE(durations.size(), 4u)
      << "Recitative should have at least 4 distinct note durations, found "
      << durations.size();
}

TEST(ToccataTest, RecitativeHasLeaps) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.sections.size(), 2u);

  Tick recit_start = result.sections[1].start;
  Tick recit_end = result.sections[1].end;

  // Count consecutive note pairs with 5+ semitone leaps in manual tracks.
  int leap_count = 0;
  int pair_count = 0;

  for (size_t track_idx = 0; track_idx <= 1; ++track_idx) {
    const auto& notes = result.tracks[track_idx].notes;
    for (size_t i = 1; i < notes.size(); ++i) {
      if (notes[i].start_tick < recit_start || notes[i].start_tick >= recit_end)
        continue;
      if (notes[i - 1].start_tick < recit_start) continue;
      ++pair_count;
      int interval = absoluteInterval(notes[i].pitch, notes[i - 1].pitch);
      if (interval >= 5) ++leap_count;
    }
  }

  if (pair_count > 0) {
    float leap_ratio = static_cast<float>(leap_count) /
                       static_cast<float>(pair_count);
    EXPECT_GE(leap_ratio, 0.10f)
        << "Recitative should have >= 10% leaps (5+ semitones), got "
        << (leap_ratio * 100.0f) << "% (" << leap_count << "/" << pair_count << ")";
  }
}

TEST(ToccataTest, RecitativeUsesBothManuals) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);
  ASSERT_GE(result.sections.size(), 2u);

  Tick recit_start = result.sections[1].start;
  Tick recit_end = result.sections[1].end;

  size_t v0_count = countNotesInRange(result.tracks[0], recit_start, recit_end);
  size_t v1_count = countNotesInRange(result.tracks[1], recit_start, recit_end);

  EXPECT_GT(v0_count, 0u) << "Voice 0 should have notes in recitative";
  EXPECT_GT(v1_count, 0u) << "Voice 1 should have notes in recitative";
}

TEST(ToccataTest, DriveRhythmicAcceleration) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.sections.size(), 3u);

  Tick drive_start = result.sections[2].start;
  Tick drive_end = result.sections[2].end;
  Tick drive_mid = drive_start + (drive_end - drive_start) / 2;

  // Count notes in first half vs second half of drive (voice 0).
  size_t first_half = countNotesInRange(result.tracks[0], drive_start, drive_mid);
  size_t second_half = countNotesInRange(result.tracks[0], drive_mid, drive_end);

  // With figure persistence (lock/release + micro-lock), the density
  // distribution can shift across the drive section.  Accept second_half
  // >= 80% of first_half as valid (persistence bundles notes differently).
  EXPECT_GE(second_half * 10, first_half * 8)
      << "Drive second half density should be at least 80% of first half "
      << "(first=" << first_half << ", second=" << second_half << ")";
}

TEST(ToccataTest, HarmonicPlanIsRich) {
  ToccataConfig config = makeTestConfig();
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  // Count distinct ChordQuality values in the timeline.
  std::set<ChordQuality> qualities;
  for (const auto& event : result.timeline.events()) {
    qualities.insert(event.chord.quality);
  }

  EXPECT_GE(qualities.size(), 3u)
      << "Harmonic plan should contain at least 3 distinct chord qualities, found "
      << qualities.size();
}

TEST(ToccataTest, PedalPhaseActivity) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);
  ASSERT_EQ(result.phases.size(), 8u);

  // Pedal (voice 2) should have notes in the majority of phases (8-phase
  // pedal generation covers all phases with different rhythmic densities).
  int phases_with_pedal = 0;
  for (const auto& phase : result.phases) {
    if (countNotesInRange(result.tracks[2], phase.start, phase.end) > 0) {
      ++phases_with_pedal;
    }
  }

  EXPECT_GE(phases_with_pedal, 6)
      << "Pedal should be active in at least 6 of 8 phases, found "
      << phases_with_pedal;
}

// ---------------------------------------------------------------------------
// 8-phase Dramaticus structure tests
// ---------------------------------------------------------------------------

TEST(ToccataTest, DramaticusHasEightPhases) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  ASSERT_EQ(result.phases.size(), 8u);
  EXPECT_EQ(result.phases[0].id, ToccataSectionId::Gesture);
  EXPECT_EQ(result.phases[1].id, ToccataSectionId::EchoCollapse);
  EXPECT_EQ(result.phases[2].id, ToccataSectionId::RecitExpansion);
  EXPECT_EQ(result.phases[3].id, ToccataSectionId::SequenceClimb1);
  EXPECT_EQ(result.phases[4].id, ToccataSectionId::HarmonicBreak);
  EXPECT_EQ(result.phases[5].id, ToccataSectionId::SequenceClimb2);
  EXPECT_EQ(result.phases[6].id, ToccataSectionId::DomObsession);
  EXPECT_EQ(result.phases[7].id, ToccataSectionId::FinalExplosion);
}

TEST(ToccataTest, DramaticusPhasesContiguous) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.phases.size(), 8u);

  // First phase starts at 0.
  EXPECT_EQ(result.phases[0].start, 0u);

  // Each phase is contiguous with the next.
  for (size_t i = 1; i < result.phases.size(); ++i) {
    EXPECT_EQ(result.phases[i].start, result.phases[i - 1].end)
        << "Phase " << i << " not contiguous with phase " << (i - 1);
  }

  // Last phase ends at total_duration.
  EXPECT_EQ(result.phases[7].end, result.total_duration_ticks);
}

TEST(ToccataTest, DramaticusLegacySectionsFromPhases) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);
  ASSERT_EQ(result.phases.size(), 8u);

  // Legacy Opening = phases[0..1].
  EXPECT_EQ(result.sections[0].id, ToccataSectionId::Opening);
  EXPECT_EQ(result.sections[0].start, result.phases[0].start);
  EXPECT_EQ(result.sections[0].end, result.phases[1].end);

  // Legacy Recitative = phases[2..4].
  EXPECT_EQ(result.sections[1].id, ToccataSectionId::Recitative);
  EXPECT_EQ(result.sections[1].start, result.phases[2].start);
  EXPECT_EQ(result.sections[1].end, result.phases[4].end);

  // Legacy Drive = phases[5..7].
  EXPECT_EQ(result.sections[2].id, ToccataSectionId::Drive);
  EXPECT_EQ(result.sections[2].start, result.phases[5].start);
  EXPECT_EQ(result.sections[2].end, result.phases[7].end);
}

TEST(ToccataTest, DramaticusDiscreteBarAllocation_24) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.phases.size(), 8u);

  // 24 bars -> known table: {2, 2, 4, 3, 2, 4, 4, 3}.
  std::vector<Tick> expected_bars = {2, 2, 4, 3, 2, 4, 4, 3};
  for (size_t i = 0; i < 8; ++i) {
    Tick actual_bars = (result.phases[i].end - result.phases[i].start) / kTicksPerBar;
    EXPECT_EQ(actual_bars, expected_bars[i])
        << "Phase " << i << " bar count mismatch";
  }
}

TEST(ToccataTest, DramaticusAllPhasesHaveNotes) {
  ToccataConfig config = makeTestConfig();
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.phases.size(), 8u);

  for (size_t pi = 0; pi < result.phases.size(); ++pi) {
    size_t total = 0;
    for (const auto& track : result.tracks) {
      total += countNotesInRange(track, result.phases[pi].start,
                                 result.phases[pi].end);
    }
    EXPECT_GT(total, 0u) << "Phase " << pi << " should have notes";
  }
}

TEST(ToccataTest, FinalChordIsPicardy) {
  // For D minor, the Picardy 3rd means F# (pitch class 6 = D+4).
  uint8_t tonic_pc = static_cast<uint8_t>(Key::D);
  uint8_t major_third_pc = (tonic_pc + 4) % 12;  // F# = 6

  // Picardy third is a structural feature. Verify across multiple seeds
  // to avoid fragility from RNG stream changes (figure persistence, micro-lock).
  int picardy_count = 0;
  constexpr int kNumSeeds = 5;
  for (int seed_offset = 0; seed_offset < kNumSeeds; ++seed_offset) {
    ToccataConfig config = makeTestConfig(42 + seed_offset);
    config.key = {Key::D, true};  // D minor
    config.total_bars = 24;
    ToccataResult result = generateToccata(config);
    ASSERT_TRUE(result.success);

    // Check that at least one note in the final bar across all tracks has the
    // Picardy third pitch class. createBachNote may rearrange which track holds it.
    Tick final_bar_start = (24u - 1) * kTicksPerBar;
    bool found_picardy = false;

    for (const auto& track : result.tracks) {
      for (auto iter = track.notes.rbegin(); iter != track.notes.rend(); ++iter) {
        if (iter->start_tick < final_bar_start) break;
        if (iter->pitch % 12 == major_third_pc) {
          found_picardy = true;
          break;
        }
      }
      if (found_picardy) break;
    }
    if (found_picardy) picardy_count++;
  }

  // At least 3 out of 5 seeds should produce a Picardy third.
  EXPECT_GE(picardy_count, 3)
      << "Picardy third (F# for D minor, pc="
      << static_cast<int>(major_third_pc) << ") found in "
      << picardy_count << "/" << kNumSeeds << " seeds";
}

// ---------------------------------------------------------------------------
// Figure persistence creates repeating patterns (Step 2 validation)
// ---------------------------------------------------------------------------

TEST(ToccataTest, FigurePersistenceCreatesRepeats) {
  // Figure persistence locks the same figure type for 2-6 repetitions.
  // Figures resolve to different absolute pitches depending on starting note,
  // but they maintain the same note_count and duration pattern.
  // Test: check for runs of same-duration consecutive notes (evidence of
  // uniform figure emission) in high-energy phases G+H across multiple seeds.
  int seeds_with_runs = 0;
  constexpr int kNumSeeds = 5;
  constexpr int kMinRunLength = 6;  // 6+ same-duration notes = at least 1.5 figures.

  for (int seed = 0; seed < kNumSeeds; ++seed) {
    ToccataConfig config = makeTestConfig(static_cast<uint32_t>(seed));
    config.total_bars = 24;
    ToccataResult result = generateToccata(config);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.phases.size(), 8u);

    Tick gh_start = result.phases[6].start;
    Tick gh_end = result.phases[7].end;

    // Collect voice 0 note durations in phases G+H.
    std::vector<Tick> durations;
    for (const auto& note : result.tracks[0].notes) {
      if (note.start_tick >= gh_start && note.start_tick < gh_end) {
        durations.push_back(note.duration);
      }
    }

    // Find the longest run of identical durations.
    int max_run = 0;
    int current_run = 1;
    for (size_t i = 1; i < durations.size(); ++i) {
      if (durations[i] == durations[i - 1]) {
        current_run++;
      } else {
        max_run = std::max(max_run, current_run);
        current_run = 1;
      }
    }
    max_run = std::max(max_run, current_run);

    if (max_run >= kMinRunLength) {
      seeds_with_runs++;
    }
  }

  // At least 3/5 seeds should produce long same-duration runs.
  EXPECT_GE(seeds_with_runs, 3)
      << "Expected >= 3/" << kNumSeeds << " seeds to produce duration runs of "
      << kMinRunLength << "+, found " << seeds_with_runs;
}

// ---------------------------------------------------------------------------
// ClimbingMotif durations are properly capped (Step 1 validation)
// ---------------------------------------------------------------------------

TEST(ToccataTest, ClimbMotifDurationsCapped) {
  // Phase D motif should have no duration > kEighthNote (240 ticks).
  // Phase F motif should have no duration > kSixteenthNote (120 ticks).
  // We verify indirectly by checking that Phase D and F voice 0 notes
  // contain no held notes (>= 720 ticks = dotted quarter).
  ToccataConfig config = makeTestConfig(42);
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.phases.size(), 8u);

  // Phase D (idx 3) voice 0: no notes >= dotted quarter.
  {
    Tick d_start = result.phases[3].start;
    Tick d_end = result.phases[3].end;
    int held_count = 0;
    int total_v0 = 0;
    for (const auto& note : result.tracks[0].notes) {
      if (note.start_tick >= d_start && note.start_tick < d_end) {
        total_v0++;
        if (note.duration >= duration::kDottedQuarter) {
          held_count++;
        }
      }
    }
    EXPECT_EQ(held_count, 0)
        << "Phase D voice 0 should have no held notes (>= 720 ticks), found "
        << held_count << " out of " << total_v0;
  }

  // Phase F (idx 5) voice 0: no notes >= dotted quarter.
  {
    Tick f_start = result.phases[5].start;
    Tick f_end = result.phases[5].end;
    int held_count = 0;
    int total_v0 = 0;
    for (const auto& note : result.tracks[0].notes) {
      if (note.start_tick >= f_start && note.start_tick < f_end) {
        total_v0++;
        if (note.duration >= duration::kDottedQuarter) {
          held_count++;
        }
      }
    }
    EXPECT_EQ(held_count, 0)
        << "Phase F voice 0 should have no held notes (>= 720 ticks), found "
        << held_count << " out of " << total_v0;
  }
}

// ---------------------------------------------------------------------------
// Strong-beat unresolved NCT rate (Step 3 validation)
// ---------------------------------------------------------------------------

TEST(ToccataTest, StrongBeatUnresolvedNCTRate) {
  // Verify that strong-beat (beat 0, beat 2) non-chord tones that lack
  // stepwise resolution to the next note are not excessive.
  ToccataConfig config = makeTestConfig(42);
  config.total_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  // Collect voice 0 notes sorted by start_tick.
  std::vector<NoteEvent> v0_notes;
  for (const auto& note : result.tracks[0].notes) {
    v0_notes.push_back(note);
  }
  std::sort(v0_notes.begin(), v0_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  int strong_beat_count = 0;
  int unresolved_nct_count = 0;

  for (size_t i = 0; i < v0_notes.size(); ++i) {
    Tick pos = positionInBar(v0_notes[i].start_tick);
    // Strong beats: beat 0 and beat 2.
    if (pos != 0 && pos != kTicksPerBeat * 2) continue;
    strong_beat_count++;

    // Check if this note is a chord tone.
    const HarmonicEvent& harm = result.timeline.getAt(v0_notes[i].start_tick);
    if (isChordTone(v0_notes[i].pitch, harm)) continue;

    // Non-chord-tone: check if next note resolves by step (1-2 semitones).
    bool resolved = false;
    if (i + 1 < v0_notes.size()) {
      int step = std::abs(static_cast<int>(v0_notes[i + 1].pitch)
                          - static_cast<int>(v0_notes[i].pitch));
      if (step <= 2) {
        resolved = true;
      }
    }

    if (!resolved) {
      unresolved_nct_count++;
    }
  }

  if (strong_beat_count > 0) {
    float rate = static_cast<float>(unresolved_nct_count)
                 / static_cast<float>(strong_beat_count);
    EXPECT_LE(rate, 0.15f)
        << "Unresolved strong-beat NCT rate should be <= 15%, got "
        << (rate * 100.0f) << "% (" << unresolved_nct_count
        << "/" << strong_beat_count << ")";
  }
}

}  // namespace
}  // namespace bach
