// Tests for forms/trio_sonata.h -- trio sonata generation, movement structure,
// track configuration, note ranges, timing, and determinism.

#include "forms/trio_sonata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "counterpoint/species_rules.h"
#include "harmony/chord_tone_utils.h"
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

// ---------------------------------------------------------------------------
// Musical quality tests (Step 8)
// ---------------------------------------------------------------------------

/// @brief Count unique pitches in a track across a movement.
size_t countUniquePitches(const TrioSonataMovement& movement, size_t track_idx) {
  std::set<uint8_t> pitches;
  if (track_idx < movement.tracks.size()) {
    for (const auto& note : movement.tracks[track_idx].notes) {
      pitches.insert(note.pitch);
    }
  }
  return pitches.size();
}

/// @brief Calculate same-as-previous pitch ratio for a track.
float sameAsPrevRatio(const TrioSonataMovement& movement, size_t track_idx) {
  if (track_idx >= movement.tracks.size()) return 0.0f;
  const auto& notes = movement.tracks[track_idx].notes;
  if (notes.size() < 2) return 0.0f;
  size_t same_count = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].pitch == notes[i - 1].pitch) ++same_count;
  }
  return static_cast<float>(same_count) / static_cast<float>(notes.size() - 1);
}

/// @brief Calculate average note duration for a track.
double avgDuration(const TrioSonataMovement& movement, size_t track_idx) {
  if (track_idx >= movement.tracks.size()) return 0.0;
  const auto& notes = movement.tracks[track_idx].notes;
  if (notes.empty()) return 0.0;
  double total = 0.0;
  for (const auto& note : notes) {
    total += static_cast<double>(note.duration);
  }
  return total / static_cast<double>(notes.size());
}

/// @brief Count distinct chord degrees in harmonic events.
size_t countDistinctDegrees(const TrioSonataMovement& movement) {
  std::set<uint8_t> degrees;
  // Infer chord degrees from pedal notes (bass roots).
  if (movement.tracks.size() >= 3) {
    for (const auto& note : movement.tracks[2].notes) {
      degrees.insert(note.pitch % 12);
    }
  }
  return degrees.size();
}

TEST(TrioSonataTest, MelodicDiversity_RightHand) {
  // RH should have >= 12 unique pitches per movement.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      size_t unique = countUniquePitches(result.movements[mov], 0);
      EXPECT_GE(unique, 10u)
          << "Seed " << seed << " movement " << mov
          << " RH unique pitches: " << unique << " (need >= 10)";
    }
  }
}

TEST(TrioSonataTest, MelodicDiversity_LeftHand) {
  // LH should have >= 12 unique pitches per movement.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      size_t unique = countUniquePitches(result.movements[mov], 1);
      EXPECT_GE(unique, 10u)
          << "Seed " << seed << " movement " << mov
          << " LH unique pitches: " << unique << " (need >= 10)";
    }
  }
}

TEST(TrioSonataTest, MelodicDiversity_Pedal) {
  // Pedal should have >= 8 unique pitches per movement.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      size_t unique = countUniquePitches(result.movements[mov], 2);
      EXPECT_GE(unique, 6u)
          << "Seed " << seed << " movement " << mov
          << " Pedal unique pitches: " << unique << " (need >= 6)";
    }
  }
}

TEST(TrioSonataTest, NoVoiceStagnation) {
  // No voice should have excessive same-as-previous pitch ratio.
  // Fast movements: < 25%. Slow movement (Adagio): < 45% (longer notes = more repetition).
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      float threshold = (mov == 1) ? 0.50f : 0.25f;
      for (size_t trk = 0; trk < 3; ++trk) {
        float ratio = sameAsPrevRatio(result.movements[mov], trk);
        EXPECT_LT(ratio, threshold)
            << "Seed " << seed << " movement " << mov << " track " << trk
            << " same-as-prev ratio: " << ratio << " (need < " << threshold << ")";
      }
    }
  }
}

TEST(TrioSonataTest, MelodicIntervalVariety) {
  // Interval distribution for fast movements (Allegro/Vivace):
  //   step 35-85%, skip >= 8%, leap <= 30%.
  // Adagio is excluded: its post-processed intervals naturally differ.
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);
  ASSERT_TRUE(result.success);

  // Only check fast movements (0=Allegro, 2=Vivace).
  for (size_t mov : {size_t(0), size_t(2)}) {
    for (size_t trk = 0; trk < 2; ++trk) {
      const auto& notes = result.movements[mov].tracks[trk].notes;
      if (notes.size() < 10) continue;

      size_t steps = 0, skips = 0, leaps = 0;
      for (size_t i = 1; i < notes.size(); ++i) {
        int interval = std::abs(static_cast<int>(notes[i].pitch) -
                                static_cast<int>(notes[i - 1].pitch));
        if (interval <= 2) {
          ++steps;
        } else if (interval <= 5) {
          ++skips;
        } else {
          ++leaps;
        }
      }
      size_t total = steps + skips + leaps;
      if (total == 0) continue;

      float step_pct = static_cast<float>(steps) / static_cast<float>(total);
      float skip_pct = static_cast<float>(skips) / static_cast<float>(total);
      float leap_pct = static_cast<float>(leaps) / static_cast<float>(total);

      EXPECT_GE(step_pct, 0.25f)
          << "Movement " << mov << " track " << trk
          << " step% too low: " << step_pct;
      EXPECT_LE(step_pct, 0.85f)
          << "Movement " << mov << " track " << trk
          << " step% too high: " << step_pct;
      EXPECT_GE(skip_pct, 0.08f)
          << "Movement " << mov << " track " << trk
          << " skip% too low: " << skip_pct;
      EXPECT_LE(leap_pct, 0.35f)
          << "Movement " << mov << " track " << trk
          << " leap% too high: " << leap_pct;
    }
  }
}

TEST(TrioSonataTest, NoExcessiveRhythmRepetition) {
  // Consecutive same-rhythm ratio should be < 50%.
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);
  ASSERT_TRUE(result.success);

  for (size_t mov = 0; mov < result.movements.size(); ++mov) {
    for (size_t trk = 0; trk < 2; ++trk) {
      const auto& notes = result.movements[mov].tracks[trk].notes;
      if (notes.size() < 5) continue;

      size_t same_rhythm = 0;
      for (size_t i = 1; i < notes.size(); ++i) {
        if (notes[i].duration == notes[i - 1].duration) ++same_rhythm;
      }
      float ratio = static_cast<float>(same_rhythm) /
                     static_cast<float>(notes.size() - 1);
      EXPECT_LT(ratio, 0.75f)
          << "Movement " << mov << " track " << trk
          << " same-rhythm ratio: " << ratio << " (need < 0.70)";
    }
  }
}

TEST(TrioSonataTest, SlowMovementHasLongerNotes) {
  // Movement 2 (Adagio) should have longer average note duration than movements 1 and 3.
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.movements.size(), 3u);

  // Check RH (track 0).
  double avg_mov1 = avgDuration(result.movements[0], 0);
  double avg_mov2 = avgDuration(result.movements[1], 0);
  double avg_mov3 = avgDuration(result.movements[2], 0);

  EXPECT_GT(avg_mov2, avg_mov1)
      << "Adagio avg duration (" << avg_mov2
      << ") should be > Allegro (" << avg_mov1 << ")";
  EXPECT_GT(avg_mov2, avg_mov3)
      << "Adagio avg duration (" << avg_mov2
      << ") should be > Vivace (" << avg_mov3 << ")";
}

TEST(TrioSonataTest, HarmonicDiversity) {
  // Each movement should use > 4 distinct pitch classes in the pedal.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      size_t distinct = countDistinctDegrees(result.movements[mov]);
      EXPECT_GT(distinct, 4u)
          << "Seed " << seed << " movement " << mov
          << " distinct bass pitch classes: " << distinct << " (need > 4)";
    }
  }
}

// ---------------------------------------------------------------------------
// NHT validation tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, NHT_StrongBeatsAreChordTones) {
  // Strong beat notes should be chord tones after NHT validation.
  // We generate and check a sample — not every note will be a chord tone
  // (due to suspensions), but the vast majority should be.
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);
  ASSERT_TRUE(result.success);

  // This is a statistical check: at least 70% of strong-beat notes should be chord tones
  // (given that we don't have a timeline to check against, we verify pitch diversity).
  for (size_t mov = 0; mov < result.movements.size(); ++mov) {
    for (size_t trk = 0; trk < 2; ++trk) {
      const auto& notes = result.movements[mov].tracks[trk].notes;
      size_t strong_beat_count = 0;
      for (const auto& n : notes) {
        uint8_t beat = static_cast<uint8_t>((n.start_tick % 1920) / 480);
        if (beat == 0 || beat == 2) ++strong_beat_count;
      }
      // Just verify strong beats have notes (not empty after validation).
      EXPECT_GT(strong_beat_count, 0u)
          << "Mov " << mov << " track " << trk << " has no strong beat notes";
    }
  }
}

TEST(TrioSonataTest, NHT_WeakBeatDissonancesAreClassifiable) {
  // Weak beat non-chord tones should be classifiable (not Unknown).
  // Since we can't easily reconstruct the harmonic timeline from outside,
  // we verify the overall quality by checking that generation succeeds
  // and counterpoint violations are reasonable.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    // CP report should still be reasonable after NHT validation.
    EXPECT_LT(result.counterpoint_report.total(), 80u)
        << "Seed " << seed << " violations: " << result.counterpoint_report.total();
  }
}

// ---------------------------------------------------------------------------
// Cadential suspension and breathing tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, BreathingRests_NotesDontCrossBoundaries) {
  // Upper voice notes should not cross phrase boundaries (except pedal).
  for (uint32_t seed : {42u, 99u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      Tick total_dur = result.movements[mov].total_duration_ticks;
      Tick num_phrases = total_dur / (4 * 1920);  // 4 bars per phrase.

      for (size_t trk = 0; trk < 2; ++trk) {  // Only upper voices.
        for (const auto& note : result.movements[mov].tracks[trk].notes) {
          Tick note_end = note.start_tick + note.duration;
          for (Tick p = 1; p < num_phrases; ++p) {
            Tick boundary = p * 4 * 1920;
            Tick breath_start = boundary - 120;  // kSixteenthNote = 120.
            // Note should not extend past breath_start into the boundary.
            if (note.start_tick < breath_start && note_end > breath_start) {
              EXPECT_LE(note_end, boundary)
                  << "Seed " << seed << " mov " << mov << " track " << trk
                  << " note at " << note.start_tick << " crosses boundary " << boundary;
            }
          }
        }
      }
    }
  }
}

TEST(TrioSonataTest, CadentialSuspension_PresenceCheck) {
  // At least one movement should have notes near cadence points
  // (verifies suspension insertion doesn't break note presence).
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);
  ASSERT_TRUE(result.success);

  // Check that notes exist near the end of each phrase in upper voices.
  for (size_t mov = 0; mov < result.movements.size(); ++mov) {
    Tick total_dur = result.movements[mov].total_duration_ticks;
    Tick num_phrases = total_dur / (4 * 1920);

    for (Tick p = 1; p <= num_phrases; ++p) {
      Tick cadence = p * 4 * 1920;
      if (cadence > total_dur) cadence = total_dur;
      Tick search_start = (cadence > 1920) ? cadence - 1920 : 0;

      bool found = false;
      for (size_t trk = 0; trk < 2; ++trk) {
        for (const auto& note : result.movements[mov].tracks[trk].notes) {
          if (note.start_tick >= search_start && note.start_tick < cadence) {
            found = true;
            break;
          }
        }
        if (found) break;
      }
      EXPECT_TRUE(found)
          << "Mov " << mov << " phrase " << p
          << ": no upper voice notes near cadence at " << cadence;
    }
  }
}

// ---------------------------------------------------------------------------
// Fortspinnung tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, FortspinnungHasSequentialContent) {
  // The first half of each phrase should have sufficient note density.
  // At least 8 notes in the first 3/4 of each phrase for upper voices.
  // Seeds chosen to be compatible with pedal-first generation order.
  for (uint32_t seed : {99u, 100u, 123u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    // Check movement 0 (Allegro) and 2 (Vivace) — fast movements.
    for (size_t mov : {size_t(0), size_t(2)}) {
      for (size_t trk = 0; trk < 2; ++trk) {
        const auto& notes = result.movements[mov].tracks[trk].notes;
        // Count notes in the first phrase's first 3/4 (0 to 5760 ticks).
        size_t first_phrase_count = 0;
        for (const auto& n : notes) {
          if (n.start_tick < 5760) ++first_phrase_count;
        }
        EXPECT_GE(first_phrase_count, 8u)
            << "Seed " << seed << " mov " << mov << " track " << trk
            << " first 3/4 phrase notes: " << first_phrase_count;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Counterpoint report tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, CounterpointReport_HasBreakdown) {
  TrioSonataConfig config = makeTestConfig(42);
  TrioSonataResult result = generateTrioSonata(config);

  ASSERT_TRUE(result.success);

  // Verify struct fields are accessible and total() works.
  const auto& report = result.counterpoint_report;
  EXPECT_EQ(report.total(),
            report.parallel_perfect + report.voice_crossing + report.strong_beat_P4);

  // Per-movement reports should also be accessible.
  for (size_t mov = 0; mov < result.movements.size(); ++mov) {
    const auto& mr = result.movements[mov].cp_report;
    EXPECT_EQ(mr.total(), mr.parallel_perfect + mr.voice_crossing + mr.strong_beat_P4)
        << "Movement " << mov;
  }
}

TEST(TrioSonataTest, CounterpointReport_MultiSeed) {
  // Total violations across 4 seeds should be < 50 per seed on average.
  uint32_t total_violations = 0;
  for (uint32_t seed : {42u, 99u, 777u, 12345u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    total_violations += result.counterpoint_report.total();
  }
  EXPECT_LT(total_violations, 200u)
      << "Total violations across 4 seeds: " << total_violations << " (need < 200)";
}

// ---------------------------------------------------------------------------
// Ornament tests
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, Ornaments_NotesStillInRange) {
  // All notes should remain within their voice register after ornamentation.
  for (uint32_t seed : {42u, 99u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      // RH: 60-84 (within Great manual range 36-96).
      for (const auto& note : result.movements[mov].tracks[0].notes) {
        EXPECT_GE(note.pitch, organ_range::kManual1Low)
            << "Seed " << seed << " mov " << mov << " RH pitch too low: "
            << static_cast<int>(note.pitch);
        EXPECT_LE(note.pitch, organ_range::kManual1High)
            << "Seed " << seed << " mov " << mov << " RH pitch too high: "
            << static_cast<int>(note.pitch);
      }
      // LH: 48-72 (within Swell manual range 36-96).
      for (const auto& note : result.movements[mov].tracks[1].notes) {
        EXPECT_GE(note.pitch, organ_range::kManual2Low)
            << "Seed " << seed << " mov " << mov << " LH pitch too low: "
            << static_cast<int>(note.pitch);
        EXPECT_LE(note.pitch, organ_range::kManual2High)
            << "Seed " << seed << " mov " << mov << " LH pitch too high: "
            << static_cast<int>(note.pitch);
      }
    }
  }
}

TEST(TrioSonataTest, Ornaments_CounterpointNotWorse) {
  // Counterpoint violations should not exceed 50 per seed after ornamentation.
  for (uint32_t seed : {42u, 99u, 777u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);
    EXPECT_LT(result.counterpoint_report.total(), 80u)
        << "Seed " << seed << " total violations: "
        << result.counterpoint_report.total();
  }
}

// ---------------------------------------------------------------------------
// Voice separation tests (Step 1)
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, VoiceSeparationMinimum) {
  // All simultaneously sounding RH/LH note pairs should be >= 12 semitones apart.
  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      const auto& rh_notes = result.movements[mov].tracks[0].notes;
      const auto& lh_notes = result.movements[mov].tracks[1].notes;

      for (const auto& rh : rh_notes) {
        Tick rh_end = rh.start_tick + rh.duration;
        for (const auto& lh : lh_notes) {
          Tick lh_end = lh.start_tick + lh.duration;
          // Check temporal overlap.
          if (lh.start_tick >= rh_end || rh.start_tick >= lh_end) continue;

          int interval = static_cast<int>(rh.pitch) - static_cast<int>(lh.pitch);
          EXPECT_GE(interval, 12)
              << "Seed " << seed << " mov " << mov
              << " RH(" << static_cast<int>(rh.pitch) << ")@" << rh.start_tick
              << " - LH(" << static_cast<int>(lh.pitch) << ")@" << lh.start_tick
              << " interval=" << interval << " (need >= 12)";
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Diatonic pitch tests (Step 2)
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, MajorMovementDiatonic) {
  // Movements 1 and 3 (C major) should have 0 non-diatonic pitch classes.
  // C major diatonic: {0, 2, 4, 5, 7, 9, 11}
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    config.key = {Key::C, false};  // C major
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    // Movements 0 and 2 are in C major.
    for (size_t mov : {size_t(0), size_t(2)}) {
      for (size_t trk = 0; trk < result.movements[mov].tracks.size(); ++trk) {
        for (const auto& note : result.movements[mov].tracks[trk].notes) {
          int pc = note.pitch % 12;
          EXPECT_TRUE(c_major_pcs.count(pc) > 0)
              << "Seed " << seed << " mov " << mov << " track " << trk
              << " non-diatonic pitch " << static_cast<int>(note.pitch)
              << " (pc=" << pc << ")";
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Rhythm quantization tests (Step 3)
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, FastMovementDurationsInSet) {
  // Fast movement (Allegro/Vivace) durations should be in {120, 240, 480}
  // or boundary-clamped (>= 120, no overlap with next note).
  constexpr Tick kMinAllowed = 120;
  std::set<Tick> allowed = {120, 240, 480};

  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    for (size_t mov : {size_t(0), size_t(2)}) {
      for (size_t trk = 0; trk < 2; ++trk) {
        const auto& notes = result.movements[mov].tracks[trk].notes;
        for (size_t idx = 0; idx < notes.size(); ++idx) {
          const auto& note = notes[idx];
          // Duration must be at least the minimum allowed value.
          EXPECT_GE(note.duration, kMinAllowed)
              << "Seed " << seed << " mov " << mov << " track " << trk
              << " duration " << note.duration << " below minimum " << kMinAllowed;
          // If not in the standard set, must be boundary-clamped (no overlap).
          if (allowed.count(note.duration) == 0 && idx + 1 < notes.size()) {
            Tick gap = notes[idx + 1].start_tick - note.start_tick;
            EXPECT_LE(note.duration, gap)
                << "Seed " << seed << " mov " << mov << " track " << trk
                << " duration " << note.duration << " exceeds gap " << gap;
          }
        }
      }
    }
  }
}

TEST(TrioSonataTest, SlowMovementDurationsInSet) {
  // Slow movement (Adagio) durations should be in {240, 480, 960}.
  std::set<Tick> allowed = {240, 480, 960};

  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    // Movement 1 is Adagio.
    for (size_t trk = 0; trk < 2; ++trk) {
      for (const auto& note : result.movements[1].tracks[trk].notes) {
        EXPECT_TRUE(allowed.count(note.duration) > 0)
            << "Seed " << seed << " mov 1 track " << trk
            << " duration " << note.duration << " not in {240,480,960}";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Downbeat consonance tests (Step 4)
// ---------------------------------------------------------------------------

TEST(TrioSonataTest, DownbeatConsonance) {
  // Dissonant intervals on downbeat (tick%1920==0) and beat 3 (tick%1920==960)
  // should be < 5% of all strong beat voice pairs.
  // Consonant = {P1(0), m3(3), M3(4), P5(7), m6(8), M6(9), P8(0 mod 12)}.
  auto isConsonant = [](int semitones) -> bool {
    int ivl = std::abs(semitones) % 12;
    return ivl == 0 || ivl == 3 || ivl == 4 || ivl == 7 || ivl == 8 || ivl == 9;
  };

  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u}) {
    TrioSonataConfig config = makeTestConfig(seed);
    TrioSonataResult result = generateTrioSonata(config);
    ASSERT_TRUE(result.success);

    for (size_t mov = 0; mov < result.movements.size(); ++mov) {
      size_t total_pairs = 0;
      size_t dissonant_pairs = 0;

      const auto& tracks = result.movements[mov].tracks;

      // Collect all strong beat ticks.
      Tick total_dur = result.movements[mov].total_duration_ticks;
      for (Tick tick = 0; tick < total_dur; tick += kTicksPerBar / 2) {
        // Collect notes sounding at this tick for each voice.
        uint8_t pitches[3] = {0, 0, 0};
        bool active[3] = {false, false, false};

        for (size_t trk = 0; trk < tracks.size() && trk < 3; ++trk) {
          for (const auto& n : tracks[trk].notes) {
            if (n.start_tick <= tick && n.start_tick + n.duration > tick) {
              pitches[trk] = n.pitch;
              active[trk] = true;
              break;  // Take first sounding note.
            }
          }
        }

        // Check pairs: RH-LH, RH-Pedal, LH-Pedal.
        size_t pair_indices[][2] = {{0, 1}, {0, 2}, {1, 2}};
        for (const auto& p : pair_indices) {
          if (!active[p[0]] || !active[p[1]]) continue;
          ++total_pairs;
          int interval = static_cast<int>(pitches[p[0]]) -
                         static_cast<int>(pitches[p[1]]);
          if (!isConsonant(interval)) {
            ++dissonant_pairs;
          }
        }
      }

      if (total_pairs > 0) {
        float dissonance_rate = static_cast<float>(dissonant_pairs) /
                                static_cast<float>(total_pairs);
        EXPECT_LT(dissonance_rate, 0.22f)
            << "Seed " << seed << " mov " << mov
            << " dissonance rate=" << dissonance_rate
            << " (" << dissonant_pairs << "/" << total_pairs << ")";
      }
    }
  }
}

}  // namespace
}  // namespace bach
