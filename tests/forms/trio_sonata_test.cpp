// Tests for forms/trio_sonata.h -- trio sonata generation, movement structure,
// track configuration, note ranges, timing, and determinism.

#include "forms/trio_sonata.h"

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

/// @brief Create a default TrioSonataConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return TrioSonataConfig with C major settings.
TrioSonataConfig makeTestConfig(uint32_t seed = 42) {
  TrioSonataConfig config;
  config.key = {Key::C, false};
  config.bpm_fast = 120;
  config.bpm_slow = 60;
  config.seed = seed;
  return config;
}

/// @brief Count total notes across all tracks in a movement.
/// @param movement The movement to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalMovementNoteCount(const TrioSonataMovement& movement) {
  size_t count = 0;
  for (const auto& track : movement.tracks) {
    count += track.notes.size();
  }
  return count;
}

// ---------------------------------------------------------------------------
// Structure tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, ThreeMovementsGenerated) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.movements.size(), 3u);
}

TEST(TrioSonataTest, EachMovementHasThreeTracks) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    EXPECT_EQ(result.movements[mov_idx].tracks.size(), 3u)
        << "Movement " << mov_idx << " should have 3 tracks";
  }
}

TEST(TrioSonataTest, SuccessFlagIsTrue) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);
  EXPECT_TRUE(result.success);
}

// ---------------------------------------------------------------------------
// Tempo tests (fast-slow-fast)
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, MovementTempos_FastSlowFast) {
  TrioSonataConfig config = makeTestConfig();
  config.bpm_fast = 120;
  config.bpm_slow = 60;
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.movements.size(), 3u);

  EXPECT_EQ(result.movements[0].bpm, 120u) << "Movement 1 should be fast";
  EXPECT_EQ(result.movements[1].bpm, 60u) << "Movement 2 should be slow";
  EXPECT_EQ(result.movements[2].bpm, 120u) << "Movement 3 should be fast";
}

// ---------------------------------------------------------------------------
// Key tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, Movement2Key_IsRelativeOfHomeKey_Major) {
  TrioSonataConfig config = makeTestConfig();
  config.key = {Key::C, false};  // C major
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.movements.size(), 3u);

  // Movement 1 and 3: home key (C major).
  EXPECT_EQ(result.movements[0].key.tonic, Key::C);
  EXPECT_FALSE(result.movements[0].key.is_minor);
  EXPECT_EQ(result.movements[2].key.tonic, Key::C);
  EXPECT_FALSE(result.movements[2].key.is_minor);

  // Movement 2: relative minor of C major = A minor.
  KeySignature expected_slow_key = getRelative(config.key);
  EXPECT_EQ(result.movements[1].key.tonic, expected_slow_key.tonic);
  EXPECT_EQ(result.movements[1].key.is_minor, expected_slow_key.is_minor);
}

TEST(TrioSonataTest, Movement2Key_IsRelativeOfHomeKey_Minor) {
  TrioSonataConfig config = makeTestConfig();
  config.key = {Key::A, true};  // A minor
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.movements.size(), 3u);

  // Movement 2: relative major of A minor = C major.
  KeySignature expected_slow_key = getRelative(config.key);
  EXPECT_EQ(result.movements[1].key.tonic, expected_slow_key.tonic);
  EXPECT_EQ(result.movements[1].key.is_minor, expected_slow_key.is_minor);
}

// ---------------------------------------------------------------------------
// Velocity tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, AllNotesVelocity80) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    for (const auto& track : result.movements[mov_idx].tracks) {
      for (const auto& note : track.notes) {
        EXPECT_EQ(note.velocity, 80u)
            << "Organ velocity must be 80, found " << static_cast<int>(note.velocity)
            << " in movement " << mov_idx << " track " << track.name;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Channel mapping tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, TrackChannelMapping) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    const auto& tracks = result.movements[mov_idx].tracks;
    ASSERT_EQ(tracks.size(), 3u);

    EXPECT_EQ(tracks[0].channel, 0u)
        << "Movement " << mov_idx << " RH should be Ch 0";
    EXPECT_EQ(tracks[1].channel, 1u)
        << "Movement " << mov_idx << " LH should be Ch 1";
    EXPECT_EQ(tracks[2].channel, 3u)
        << "Movement " << mov_idx << " Pedal should be Ch 3";
  }
}

// ---------------------------------------------------------------------------
// Program mapping tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, TrackProgramMapping) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    const auto& tracks = result.movements[mov_idx].tracks;
    ASSERT_EQ(tracks.size(), 3u);

    EXPECT_EQ(tracks[0].program, GmProgram::kChurchOrgan)
        << "Movement " << mov_idx << " RH should be Church Organ (19)";
    EXPECT_EQ(tracks[1].program, GmProgram::kReedOrgan)
        << "Movement " << mov_idx << " LH should be Reed Organ (20)";
    EXPECT_EQ(tracks[2].program, GmProgram::kChurchOrgan)
        << "Movement " << mov_idx << " Pedal should be Church Organ (19)";
  }
}

// ---------------------------------------------------------------------------
// Track name tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, TrackNamesCorrect) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    const auto& tracks = result.movements[mov_idx].tracks;
    ASSERT_EQ(tracks.size(), 3u);

    EXPECT_EQ(tracks[0].name, "Right Hand (Great)")
        << "Movement " << mov_idx;
    EXPECT_EQ(tracks[1].name, "Left Hand (Swell)")
        << "Movement " << mov_idx;
    EXPECT_EQ(tracks[2].name, "Pedal")
        << "Movement " << mov_idx;
  }
}

// ---------------------------------------------------------------------------
// Organ range tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, NotesWithinOrganRanges) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    const auto& tracks = result.movements[mov_idx].tracks;

    // Track 0 (RH on Great): C2-C6 (36-96) -- actually uses upper range 60-96.
    for (const auto& note : tracks[0].notes) {
      EXPECT_GE(note.pitch, organ_range::kManual1Low)
          << "Movement " << mov_idx << " RH pitch below Great range: "
          << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, organ_range::kManual1High)
          << "Movement " << mov_idx << " RH pitch above Great range: "
          << static_cast<int>(note.pitch);
    }

    // Track 1 (LH on Swell): C2-C6 (36-96) -- actually uses lower range 36-71.
    for (const auto& note : tracks[1].notes) {
      EXPECT_GE(note.pitch, organ_range::kManual2Low)
          << "Movement " << mov_idx << " LH pitch below Swell range: "
          << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, organ_range::kManual2High)
          << "Movement " << mov_idx << " LH pitch above Swell range: "
          << static_cast<int>(note.pitch);
    }

    // Track 2 (Pedal): C1-D3 (24-50).
    for (const auto& note : tracks[2].notes) {
      EXPECT_GE(note.pitch, organ_range::kPedalLow)
          << "Movement " << mov_idx << " Pedal pitch below range: "
          << static_cast<int>(note.pitch);
      EXPECT_LE(note.pitch, organ_range::kPedalHigh)
          << "Movement " << mov_idx << " Pedal pitch above range: "
          << static_cast<int>(note.pitch);
    }
  }
}

// ---------------------------------------------------------------------------
// Determinism tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, DeterministicWithSameSeed) {
  TrioSonataConfig config = makeTestConfig(12345);
  TrioSonataResult result1 = generateTrioSonata(config);
  TrioSonataResult result2 = generateTrioSonata(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.movements.size(), result2.movements.size());

  for (size_t mov_idx = 0; mov_idx < result1.movements.size(); ++mov_idx) {
    const auto& tracks1 = result1.movements[mov_idx].tracks;
    const auto& tracks2 = result2.movements[mov_idx].tracks;
    ASSERT_EQ(tracks1.size(), tracks2.size());

    for (size_t trk_idx = 0; trk_idx < tracks1.size(); ++trk_idx) {
      const auto& notes1 = tracks1[trk_idx].notes;
      const auto& notes2 = tracks2[trk_idx].notes;
      ASSERT_EQ(notes1.size(), notes2.size())
          << "Movement " << mov_idx << " track " << trk_idx << " note count differs";

      for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
        EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick)
            << "Movement " << mov_idx << " track " << trk_idx << " note " << note_idx;
        EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch)
            << "Movement " << mov_idx << " track " << trk_idx << " note " << note_idx;
        EXPECT_EQ(notes1[note_idx].duration, notes2[note_idx].duration)
            << "Movement " << mov_idx << " track " << trk_idx << " note " << note_idx;
      }
    }
  }
}

TEST(TrioSonataTest, DifferentSeedsProduceDifferentOutput) {
  TrioSonataConfig config1 = makeTestConfig(42);
  TrioSonataConfig config2 = makeTestConfig(99);
  TrioSonataResult result1 = generateTrioSonata(config1);
  TrioSonataResult result2 = generateTrioSonata(config2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Compare first movement, first track notes for differences.
  bool any_difference = false;
  const auto& notes1 = result1.movements[0].tracks[0].notes;
  const auto& notes2 = result2.movements[0].tracks[0].notes;

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
// Duration tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, MovementDurationsArePositive) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    EXPECT_GT(result.movements[mov_idx].total_duration_ticks, 0u)
        << "Movement " << mov_idx << " has zero duration";
  }
}

TEST(TrioSonataTest, AllNotesHavePositiveDuration) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    for (const auto& track : result.movements[mov_idx].tracks) {
      for (const auto& note : track.notes) {
        EXPECT_GT(note.duration, 0u)
            << "Note at tick " << note.start_tick << " has zero duration in movement "
            << mov_idx << " track " << track.name;
      }
    }
  }
}

TEST(TrioSonataTest, EachMovementHasNotes) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    size_t total = totalMovementNoteCount(result.movements[mov_idx]);
    EXPECT_GT(total, 0u) << "Movement " << mov_idx << " has no notes";
  }
}

TEST(TrioSonataTest, EachTrackHasNotes) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    for (size_t trk_idx = 0; trk_idx < result.movements[mov_idx].tracks.size(); ++trk_idx) {
      EXPECT_GT(result.movements[mov_idx].tracks[trk_idx].notes.size(), 0u)
          << "Movement " << mov_idx << " track " << trk_idx << " has no notes";
    }
  }
}

// ---------------------------------------------------------------------------
// Notes sorted
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, NotesSortedByStartTick) {
  TrioSonataConfig config = makeTestConfig();
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    for (const auto& track : result.movements[mov_idx].tracks) {
      for (size_t idx = 1; idx < track.notes.size(); ++idx) {
        EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
            << "Notes not sorted in movement " << mov_idx << " track " << track.name
            << " at index " << idx;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Minor key works
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, MinorKey_GeneratesSuccessfully) {
  TrioSonataConfig config = makeTestConfig();
  config.key = {Key::G, true};  // G minor
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.movements.size(), 3u);
  for (size_t mov_idx = 0; mov_idx < result.movements.size(); ++mov_idx) {
    EXPECT_GT(totalMovementNoteCount(result.movements[mov_idx]), 0u)
        << "Movement " << mov_idx << " has no notes in G minor";
  }
}

}  // namespace
}  // namespace bach
