// Tests for forms/toccata.h -- organ toccata free section generation, track
// configuration, section structure, pitch ranges, timing, and determinism.

#include "forms/toccata.h"

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

/// @brief Create a default ToccataConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return ToccataConfig with standard 3-voice D minor settings.
ToccataConfig makeTestConfig(uint32_t seed = 42) {
  ToccataConfig config;
  config.key = {Key::D, true};  // D minor (BWV 565 style).
  config.bpm = 80;
  config.seed = seed;
  config.num_voices = 3;
  config.section_bars = 24;
  return config;
}

/// @brief Count total notes across all tracks.
/// @param result The toccata result to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalNoteCount(const ToccataResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
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
  size_t total = totalNoteCount(result);
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
// Total duration matches section_bars
// ---------------------------------------------------------------------------

TEST(ToccataTest, TotalDurationMatchesSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 24u * kTicksPerBar);
}

TEST(ToccataTest, TotalDurationMatchesSectionBars_Short) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 8;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 8u * kTicksPerBar);
}

TEST(ToccataTest, TotalDurationMatchesSectionBars_Long) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 48;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 48u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Opening section has fast notes (16th note durations)
// ---------------------------------------------------------------------------

TEST(ToccataTest, OpeningSectionHasFastNotes) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 0u);

  // Opening is first 25% = 6 bars.
  Tick opening_end = 6 * kTicksPerBar;
  constexpr Tick kSixteenthDuration = kTicksPerBeat / 4;  // 120 ticks.

  int fast_note_count = 0;
  int opening_note_count = 0;

  for (const auto& note : result.tracks[0].notes) {
    if (note.start_tick < opening_end) {
      ++opening_note_count;
      if (note.duration <= kSixteenthDuration) {
        ++fast_note_count;
      }
    }
  }

  EXPECT_GT(opening_note_count, 0) << "Opening section should have notes on voice 0";
  // The majority of opening notes should be 16th notes.
  float fast_ratio = static_cast<float>(fast_note_count) /
                     static_cast<float>(opening_note_count);
  EXPECT_GE(fast_ratio, 0.55f)
      << "Expected >= 55% 16th notes in opening, got " << (fast_ratio * 100.0f) << "%";
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

  // Voice 3 (Manual III / Positiv): 48-96.
  for (const auto& note : result.tracks[3].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual3Low)
        << "Manual III pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kManual3High)
        << "Manual III pitch above range: " << static_cast<int>(note.pitch);
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
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);

  // Drive section is last 25% (bars 18-24 in a 24-bar piece).
  // Opening = 6 bars, Recitative = 12 bars, Drive = 6 bars.
  Tick drive_start = 18u * kTicksPerBar;
  Tick drive_end = 24u * kTicksPerBar;

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
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(ToccataTest, MajorKeyGeneratesSuccessfully) {
  ToccataConfig config = makeTestConfig();
  config.key = {Key::C, false};  // C major.
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
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

TEST(ToccataTest, EnergyCurve_UShape) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  // U-shape energy: opening and drive have shorter average note durations (high
  // energy) than the recitative (low energy, sustained chords + free melody).
  // Metric: average note duration across both manual tracks (0 and 1).
  Tick opening_end = 6u * kTicksPerBar;
  Tick recit_start = opening_end;
  Tick recit_end = recit_start + 12u * kTicksPerBar;
  Tick drive_start = recit_end;

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

  float opening_avg = avgDuration(0, opening_end);
  float recit_avg = avgDuration(recit_start, recit_end);
  float drive_avg = avgDuration(drive_start, result.total_duration_ticks);

  // Opening has fast passages (32nd/16th notes) -> shorter average duration.
  EXPECT_LT(opening_avg, recit_avg)
      << "Opening should have shorter avg note duration than recitative "
      << "(opening=" << opening_avg << ", recit=" << recit_avg << ")";

  // Drive has accelerating passages -> shorter average duration.
  if (drive_avg > 0.0f) {
    EXPECT_LT(drive_avg, recit_avg)
        << "Drive should have shorter avg note duration than recitative "
        << "(drive=" << drive_avg << ", recit=" << recit_avg << ")";
  }
}

// ---------------------------------------------------------------------------
// Pedal points
// ---------------------------------------------------------------------------

TEST(ToccataTest, PedalVoiceHasNotesInOpeningAndDrive) {
  ToccataConfig config = makeTestConfig();
  config.num_voices = 3;
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  Tick opening_end = 6u * kTicksPerBar;
  Tick drive_start = 18u * kTicksPerBar;
  Tick drive_end = 24u * kTicksPerBar;

  // Pedal (voice 2) should have notes in both opening and drive sections.
  size_t pedal_opening = countNotesInRange(result.tracks[2], 0, opening_end);
  size_t pedal_drive = countNotesInRange(result.tracks[2], drive_start, drive_end);

  EXPECT_GT(pedal_opening, 0u) << "Pedal should have notes during opening";
  EXPECT_GT(pedal_drive, 0u) << "Pedal should have notes during drive";
}

// ---------------------------------------------------------------------------
// Section bars edge cases
// ---------------------------------------------------------------------------

TEST(ToccataTest, SmallSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 4;  // Minimum reasonable size.
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 4u * kTicksPerBar);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(ToccataTest, LargeSectionBars) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 64;
  ToccataResult result = generateToccata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 64u * kTicksPerBar);
  EXPECT_GT(totalNoteCount(result), 100u)
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
    EXPECT_GT(totalNoteCount(result), 0u) << "No notes with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// BWV 565-style structural tests
// ---------------------------------------------------------------------------

TEST(ToccataTest, OpeningHasGrandPause) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 1u);

  // Check for a 960+ tick gap in voice 0 within the first 4 bars.
  Tick four_bars = 4u * kTicksPerBar;
  bool found_gap = false;

  const auto& notes = result.tracks[0].notes;
  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].start_tick >= four_bars) break;
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
      << "Opening should have a grand pause (960+ tick gap) in the first 4 bars";
}

TEST(ToccataTest, OpeningHasOctaveDoubling) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Check for notes at the same tick with 12-semitone difference across tracks.
  Tick opening_end = 6u * kTicksPerBar;
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
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  Tick opening_end = 6u * kTicksPerBar;
  constexpr Tick kWholeNoteDur = kTicksPerBeat * 4;

  // Find a whole-note-or-longer note in voice 0 during opening.
  bool found_block = false;
  for (const auto& n0 : result.tracks[0].notes) {
    if (n0.start_tick >= opening_end) break;
    if (n0.duration >= kWholeNoteDur) {
      // Check if voices 1 and 2 also have notes at the same tick.
      bool v1_present = false, v2_present = false;
      for (const auto& n1 : result.tracks[1].notes) {
        if (n1.start_tick == n0.start_tick && n1.duration >= kWholeNoteDur) {
          v1_present = true;
          break;
        }
      }
      for (const auto& n2 : result.tracks[2].notes) {
        if (n2.start_tick == n0.start_tick && n2.duration >= kWholeNoteDur / 2) {
          v2_present = true;
          break;
        }
      }
      if (v1_present && v2_present) {
        found_block = true;
        break;
      }
    }
  }

  EXPECT_TRUE(found_block)
      << "Opening should have a block chord (whole note) across 3+ voices";
}

TEST(ToccataTest, RecitativeHasRhythmicVariety) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  Tick recit_start = 6u * kTicksPerBar;
  Tick recit_end = 18u * kTicksPerBar;

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
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  Tick recit_start = 6u * kTicksPerBar;
  Tick recit_end = 18u * kTicksPerBar;

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
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  Tick recit_start = 6u * kTicksPerBar;
  Tick recit_end = 18u * kTicksPerBar;

  size_t v0_count = countNotesInRange(result.tracks[0], recit_start, recit_end);
  size_t v1_count = countNotesInRange(result.tracks[1], recit_start, recit_end);

  EXPECT_GT(v0_count, 0u) << "Voice 0 should have notes in recitative";
  EXPECT_GT(v1_count, 0u) << "Voice 1 should have notes in recitative";
}

TEST(ToccataTest, DriveRhythmicAcceleration) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);

  Tick drive_start = 18u * kTicksPerBar;
  Tick drive_end = 24u * kTicksPerBar;
  Tick drive_mid = drive_start + (drive_end - drive_start) / 2;

  // Count notes in first half vs second half of drive (voice 0).
  size_t first_half = countNotesInRange(result.tracks[0], drive_start, drive_mid);
  size_t second_half = countNotesInRange(result.tracks[0], drive_mid, drive_end);

  EXPECT_GT(second_half, first_half)
      << "Drive second half should have higher note density than first half "
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

TEST(ToccataTest, PedalSoloPresent) {
  ToccataConfig config = makeTestConfig();
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  // Check for 8th-note-or-shorter pedal activity near opening/recit boundary.
  Tick opening_end = 6u * kTicksPerBar;
  Tick search_start = opening_end - 2u * kTicksPerBar;
  Tick search_end = opening_end;

  int short_pedal_count = 0;
  for (const auto& note : result.tracks[2].notes) {
    if (note.start_tick >= search_start && note.start_tick < search_end) {
      if (note.duration <= kTicksPerBeat / 2) {  // 8th note or shorter
        ++short_pedal_count;
      }
    }
  }

  EXPECT_GT(short_pedal_count, 0)
      << "Pedal solo should have 8th-note-or-shorter activity near opening end";
}

TEST(ToccataTest, FinalChordIsPicardy) {
  ToccataConfig config = makeTestConfig();
  config.key = {Key::D, true};  // D minor
  config.section_bars = 24;
  ToccataResult result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 0u);

  // Find the last note in track 0 in the final bar.
  Tick final_bar_start = (24u - 1) * kTicksPerBar;
  uint8_t last_pitch = 0;
  bool found_final = false;

  for (auto it = result.tracks[0].notes.rbegin();
       it != result.tracks[0].notes.rend(); ++it) {
    if (it->start_tick >= final_bar_start) {
      last_pitch = it->pitch;
      found_final = true;
      break;
    }
  }

  ASSERT_TRUE(found_final) << "Should have notes in the final bar on track 0";

  // For D minor, the Picardy 3rd means F# (pitch class 6 = D+4).
  uint8_t tonic_pc = static_cast<uint8_t>(Key::D);
  uint8_t major_third_pc = (tonic_pc + 4) % 12;  // F# = 6
  uint8_t actual_pc = last_pitch % 12;

  EXPECT_EQ(actual_pc, major_third_pc)
      << "Final chord should contain Picardy third (F# for D minor), got pitch class "
      << static_cast<int>(actual_pc) << " expected " << static_cast<int>(major_third_pc);
}

}  // namespace
}  // namespace bach
