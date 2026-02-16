// Tests for forms/fantasia.h -- fantasia free section generation, track
// configuration, voice characteristics, pitch ranges, timing, and determinism.

#include "forms/fantasia.h"

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

/// @brief Create a default FantasiaConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return FantasiaConfig with standard 4-voice G minor settings.
FantasiaConfig makeTestConfig(uint32_t seed = 42) {
  FantasiaConfig config;
  config.key = {Key::G, true};  // G minor (BWV 542 default).
  config.bpm = 66;
  config.seed = seed;
  config.num_voices = 4;
  config.section_bars = 32;
  return config;
}

/// @brief Count total notes across all tracks.
/// @param result The fantasia result to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalNoteCount(const FantasiaResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(FantasiaTest, GenerateSucceeds) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(result.total_duration_ticks, 0u);
  EXPECT_TRUE(result.error_message.empty());
}

TEST(FantasiaTest, AllTracksHaveNotes) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") is empty";
  }
}

TEST(FantasiaTest, ReasonableNoteCount) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  size_t total = totalNoteCount(result);
  // 32 bars of 4-voice music should produce a substantial number of notes.
  EXPECT_GT(total, 100u) << "Too few notes for a 32-bar fantasia";
}

// ---------------------------------------------------------------------------
// Track count and default config
// ---------------------------------------------------------------------------

TEST(FantasiaTest, FourVoicesDefault) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u) << "Default config should produce 4 tracks";
}

TEST(FantasiaTest, TrackCount_MatchesNumVoices_Two) {
  FantasiaConfig config = makeTestConfig();
  config.num_voices = 2;
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u);
}

TEST(FantasiaTest, TrackCount_MatchesNumVoices_Three) {
  FantasiaConfig config = makeTestConfig();
  config.num_voices = 3;
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(FantasiaTest, TrackCount_ClampedLow) {
  FantasiaConfig config = makeTestConfig();
  config.num_voices = 1;  // Below minimum.
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u) << "Should clamp to 2 voices minimum";
}

TEST(FantasiaTest, TrackCount_ClampedHigh) {
  FantasiaConfig config = makeTestConfig();
  config.num_voices = 10;  // Above maximum.
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u) << "Should clamp to 5 voices maximum";
}

// ---------------------------------------------------------------------------
// Channel mapping
// ---------------------------------------------------------------------------

TEST(FantasiaTest, TracksHaveCorrectChannels) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  // Voice 0: Manual I (Great) -> Channel 0.
  EXPECT_EQ(result.tracks[0].channel, 0u);
  // Voice 1: Manual II (Swell) -> Channel 1.
  EXPECT_EQ(result.tracks[1].channel, 1u);
  // Voice 2: Manual III (Positiv) -> Channel 2.
  EXPECT_EQ(result.tracks[2].channel, 2u);
  // Voice 3: Pedal -> Channel 3.
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

TEST(FantasiaTest, ProgramMapping) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[3].program, GmProgram::kChurchOrgan);
}

TEST(FantasiaTest, TrackNames) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[1].name, "Manual II (Swell)");
  EXPECT_EQ(result.tracks[2].name, "Manual III (Positiv)");
  EXPECT_EQ(result.tracks[3].name, "Pedal");
}

// ---------------------------------------------------------------------------
// Duration
// ---------------------------------------------------------------------------

TEST(FantasiaTest, TotalDurationMatchesSectionBars) {
  FantasiaConfig config = makeTestConfig();
  config.section_bars = 32;
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 32u * kTicksPerBar)
      << "Total duration should be section_bars * kTicksPerBar";
}

TEST(FantasiaTest, TotalDurationMatchesSectionBars_Short) {
  FantasiaConfig config = makeTestConfig();
  config.section_bars = 8;
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.total_duration_ticks, 8u * kTicksPerBar);
}

TEST(FantasiaTest, ZeroSectionBars_ReturnsError) {
  FantasiaConfig config = makeTestConfig();
  config.section_bars = 0;
  FantasiaResult result = generateFantasia(config);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

// ---------------------------------------------------------------------------
// Voice characteristics: note durations
// ---------------------------------------------------------------------------

TEST(FantasiaTest, Voice0HasShorterNotes) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Voice 0 (ornamental melody) uses quarter/eighth notes.
  // It should have significantly more notes than Voice 1 (sustained chords).
  EXPECT_GT(result.tracks[0].notes.size(), result.tracks[1].notes.size())
      << "Ornamental melody (Voice 0) should have more notes than sustained "
         "chords (Voice 1)";

  // Verify that most Voice 0 notes are quarter or eighth note duration.
  constexpr Tick kMaxMelodyDuration = kTicksPerBeat;  // Quarter note.
  int short_note_count = 0;
  for (const auto& note : result.tracks[0].notes) {
    if (note.duration <= kMaxMelodyDuration) {
      ++short_note_count;
    }
  }
  float ratio = static_cast<float>(short_note_count) /
                static_cast<float>(result.tracks[0].notes.size());
  EXPECT_GE(ratio, 0.80f)
      << "Expected >= 80% of melody notes to be quarter or shorter, got "
      << (ratio * 100.0f) << "%";
}

TEST(FantasiaTest, Voice1HasLongerNotes) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Voice 1 (sustained chords) uses half and whole notes.
  // All notes should be at least a half note duration.
  constexpr Tick kMinChordDuration = kTicksPerBeat * 2;  // Half note = 960.
  const auto& chord_notes = result.tracks[1].notes;
  ASSERT_GT(chord_notes.size(), 0u);

  int long_note_count = 0;
  for (const auto& note : chord_notes) {
    if (note.duration >= kMinChordDuration) {
      ++long_note_count;
    }
  }
  // Allow some notes at event boundaries to be shorter.
  float ratio = static_cast<float>(long_note_count) /
                static_cast<float>(chord_notes.size());
  EXPECT_GE(ratio, 0.70f)
      << "Expected >= 70% of chord notes to be half note or longer, got "
      << (ratio * 100.0f) << "%";
}

TEST(FantasiaTest, PedalHasSectionAwareRhythm) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 4u);

  // Voice 3 (Pedal) uses section-texture-aware durations:
  // - Passage sections: half notes (960 ticks) for rhythmic motion
  // - Chordal/Cadential sections: whole notes (1920 ticks) for stability
  // Expect a mix of half and whole notes, with all notes at least half note.
  const auto& pedal_notes = result.tracks[3].notes;
  ASSERT_GT(pedal_notes.size(), 0u);

  constexpr Tick kHalfNoteDur = kTicksPerBeat * 2;   // 960
  constexpr Tick kWholeNoteDur = kTicksPerBeat * 4;   // 1920
  int half_note_count = 0;
  int whole_note_count = 0;
  int short_note_count = 0;
  for (const auto& note : pedal_notes) {
    if (note.duration >= kWholeNoteDur) {
      ++whole_note_count;
    } else if (note.duration >= kHalfNoteDur) {
      ++half_note_count;
    } else {
      ++short_note_count;
    }
  }

  int total = static_cast<int>(pedal_notes.size());

  // All notes should be at least half note duration (no 8th/quarter notes).
  float long_ratio = static_cast<float>(half_note_count + whole_note_count) /
                     static_cast<float>(total);
  EXPECT_GE(long_ratio, 0.80f)
      << "Expected >= 80% of pedal notes to be at least half notes, got "
      << (long_ratio * 100.0f) << "% (short: " << short_note_count << ")";

  // Expect both half and whole notes present for rhythm diversity.
  // Passage sections produce half notes, chordal/cadential produce whole.
  EXPECT_GT(half_note_count, 0)
      << "Expected some half notes in pedal (passage sections)";
  EXPECT_GT(whole_note_count, 0)
      << "Expected some whole notes in pedal (chordal/cadential sections)";
}

TEST(FantasiaTest, Voice2HasMixedRhythm) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  // Voice 2 (countermelody) uses section-texture-aware rhythm:
  // - Passage sections: eighth notes (active counterpoint).
  // - Chordal sections: quarter notes (calmer texture).
  // - Cadential sections: half notes (convergence).
  // With 4-bar alternation, expect a mix of durations.
  const auto& counter_notes = result.tracks[2].notes;
  ASSERT_GT(counter_notes.size(), 0u);

  constexpr Tick kEighthDuration = kTicksPerBeat / 2;  // 240.
  constexpr Tick kQuarterDuration = kTicksPerBeat;       // 480.
  int short_count = 0;   // Eighth or shorter.
  int medium_count = 0;  // Quarter.
  int long_count = 0;    // Half or longer.
  for (const auto& note : counter_notes) {
    if (note.duration <= kEighthDuration) {
      ++short_count;
    } else if (note.duration <= kQuarterDuration) {
      ++medium_count;
    } else {
      ++long_count;
    }
  }

  // Passage sections provide eighth notes; chordal/cadential provide quarter/half.
  // At least 30% should be short (passage sections).
  float short_ratio = static_cast<float>(short_count) /
                      static_cast<float>(counter_notes.size());
  EXPECT_GE(short_ratio, 0.30f)
      << "Expected >= 30% of countermelody notes to be eighth notes "
      << "(passage sections), got " << (short_ratio * 100.0f) << "%";

  // At least some longer notes should exist (chordal/cadential sections).
  int longer_count = medium_count + long_count;
  EXPECT_GT(longer_count, 0)
      << "Expected some quarter/half notes in countermelody "
      << "(chordal/cadential sections)";
}

// ---------------------------------------------------------------------------
// Pitch ranges
// ---------------------------------------------------------------------------

TEST(FantasiaTest, AllNotesInRange) {
  FantasiaConfig config = makeTestConfig();
  config.num_voices = 4;
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  // Voice 0 (Melody): 67-88.
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, 67)
        << "Melody pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 88)
        << "Melody pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 1 (Chords): 52-76.
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, 52)
        << "Chords pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 76)
        << "Chords pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 2 (Counter): 43-64.
  for (const auto& note : result.tracks[2].notes) {
    EXPECT_GE(note.pitch, 43)
        << "Counter pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 64)
        << "Counter pitch above range: " << static_cast<int>(note.pitch);
  }

  // Voice 3 (Pedal): 24-50.
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

TEST(FantasiaTest, AllNotesVelocity80) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
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
// Determinism
// ---------------------------------------------------------------------------

TEST(FantasiaTest, DeterministicOutput) {
  FantasiaConfig config = makeTestConfig(12345);
  FantasiaResult result1 = generateFantasia(config);
  FantasiaResult result2 = generateFantasia(config);

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

TEST(FantasiaTest, DifferentSeedsProduceDifferentOutput) {
  FantasiaConfig config1 = makeTestConfig(42);
  FantasiaConfig config2 = makeTestConfig(99);
  FantasiaResult result1 = generateFantasia(config1);
  FantasiaResult result2 = generateFantasia(config2);

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
// Voice assignment
// ---------------------------------------------------------------------------

TEST(FantasiaTest, VoiceAssignment_CorrectPerTrack) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);

  for (size_t track_idx = 0; track_idx < result.tracks.size(); ++track_idx) {
    for (const auto& note : result.tracks[track_idx].notes) {
      EXPECT_EQ(note.voice, track_idx)
          << "Note in track " << track_idx << " has wrong voice " << note.voice;
    }
  }
}

// ---------------------------------------------------------------------------
// Notes sorted
// ---------------------------------------------------------------------------

TEST(FantasiaTest, NotesSortedByStartTick) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
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

TEST(FantasiaTest, AllNotesHavePositiveDuration) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
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
// Timeline
// ---------------------------------------------------------------------------

TEST(FantasiaTest, TimelineIsPopulated) {
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);

  EXPECT_GT(result.timeline.size(), 0u) << "Timeline should have events";
  EXPECT_GT(result.timeline.totalDuration(), 0u) << "Timeline duration should be > 0";
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

TEST(FantasiaTest, MajorKeyGeneratesSuccessfully) {
  FantasiaConfig config = makeTestConfig();
  config.key = {Key::C, false};  // C major.
  FantasiaResult result = generateFantasia(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(FantasiaTest, DifferentKeysProduceDifferentOutput) {
  FantasiaConfig config_gm = makeTestConfig(42);
  config_gm.key = {Key::G, true};   // G minor.
  FantasiaConfig config_cm = makeTestConfig(42);
  config_cm.key = {Key::C, false};  // C major.

  FantasiaResult result_gm = generateFantasia(config_gm);
  FantasiaResult result_cm = generateFantasia(config_cm);

  ASSERT_TRUE(result_gm.success);
  ASSERT_TRUE(result_cm.success);

  // Same seed but different keys should produce different pitches.
  bool any_pitch_difference = false;
  const auto& notes_gm = result_gm.tracks[0].notes;
  const auto& notes_cm = result_cm.tracks[0].notes;

  size_t compare_count = std::min(notes_gm.size(), notes_cm.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_gm[idx].pitch != notes_cm[idx].pitch) {
      any_pitch_difference = true;
      break;
    }
  }
  EXPECT_TRUE(any_pitch_difference)
      << "G minor and C major should have different pitches";
}

// ---------------------------------------------------------------------------
// Structural rhythm diversity: section texture alternation
// ---------------------------------------------------------------------------

TEST(FantasiaTest, MelodyHasSectionTextureRhythmDiversity) {
  // The melody voice (Voice 0) should have both short notes (passage sections)
  // and longer notes (chordal/cadential sections), not uniform rhythm.
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  constexpr Tick kEighthDuration = kTicksPerBeat / 2;  // 240
  constexpr Tick kQuarterDuration = kTicksPerBeat;      // 480
  int short_count = 0;   // Sixteenth or eighth.
  int medium_count = 0;  // Quarter.
  int long_count = 0;    // Half or longer.

  for (const auto& note : result.tracks[0].notes) {
    if (note.duration <= kEighthDuration) {
      ++short_count;
    } else if (note.duration <= kQuarterDuration) {
      ++medium_count;
    } else {
      ++long_count;
    }
  }

  // Passage sections should produce short notes.
  EXPECT_GT(short_count, 0)
      << "Melody should have short notes from passage sections";

  // Chordal/cadential sections should produce longer notes.
  int longer_count = medium_count + long_count;
  EXPECT_GT(longer_count, 0)
      << "Melody should have quarter/half/whole notes from "
      << "chordal/cadential sections";

  // At least 3 distinct duration values for true diversity.
  std::set<Tick> unique_durations;
  for (const auto& note : result.tracks[0].notes) {
    unique_durations.insert(note.duration);
  }
  EXPECT_GE(unique_durations.size(), 3u)
      << "Melody should use at least 3 distinct duration values, "
      << "found " << unique_durations.size();
}

TEST(FantasiaTest, ChordVoiceHasSectionTextureVariation) {
  // The sustained chord voice (Voice 1) should have both shorter durations
  // in passage sections and longer durations in chordal sections.
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  constexpr Tick kHalfDuration = kTicksPerBeat * 2;   // 960
  int shorter_count = 0;  // Quarter or half (passage sections).
  int longer_count = 0;   // Whole (chordal/cadential sections).

  for (const auto& note : result.tracks[1].notes) {
    if (note.duration < kHalfDuration) {
      ++shorter_count;
    } else {
      ++longer_count;
    }
  }

  // Both duration categories should be present.
  EXPECT_GT(shorter_count, 0)
      << "Chord voice should have shorter notes in passage sections";
  EXPECT_GT(longer_count, 0)
      << "Chord voice should have longer notes in chordal sections";
}

TEST(FantasiaTest, CadentialSectionHasLongerNotes) {
  // Notes in the final 2 bars should have significantly longer average
  // duration than notes in the middle of the piece.
  FantasiaConfig config = makeTestConfig();
  FantasiaResult result = generateFantasia(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  Tick cadence_start = result.total_duration_ticks - 2 * kTicksPerBar;
  Tick mid_start = result.total_duration_ticks / 4;
  Tick mid_end = result.total_duration_ticks * 3 / 4;

  Tick total_cadence_dur = 0;
  int cadence_count = 0;
  Tick total_mid_dur = 0;
  int mid_count = 0;

  for (const auto& note : result.tracks[0].notes) {
    if (note.start_tick >= cadence_start) {
      total_cadence_dur += note.duration;
      ++cadence_count;
    } else if (note.start_tick >= mid_start && note.start_tick < mid_end) {
      total_mid_dur += note.duration;
      ++mid_count;
    }
  }

  if (cadence_count > 0 && mid_count > 0) {
    float avg_cadence = static_cast<float>(total_cadence_dur) /
                        static_cast<float>(cadence_count);
    float avg_mid = static_cast<float>(total_mid_dur) /
                    static_cast<float>(mid_count);
    EXPECT_GT(avg_cadence, avg_mid)
        << "Cadential section (avg=" << avg_cadence
        << ") should have longer notes than middle (avg=" << avg_mid << ")";
  }
}

}  // namespace
}  // namespace bach
