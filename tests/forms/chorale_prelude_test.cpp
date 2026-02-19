// Tests for forms/chorale_prelude.h -- chorale prelude generation, track
// configuration, cantus firmus placement, pitch ranges, and determinism.
// Also tests isCharacterFormCompatible from fugue/fugue_config.h.

#include "forms/chorale_prelude.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "fugue/fugue_config.h"
#include "harmony/key.h"
#include "test_helpers.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default ChoralePreludeConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return ChoralePreludeConfig with standard C major settings.
ChoralePreludeConfig makeTestConfig(uint32_t seed = 42) {
  ChoralePreludeConfig config;
  config.key = {Key::C, false};
  config.bpm = 60;
  config.seed = seed;
  return config;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, GeneratesSuccessfully) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(ChoralePreludeTest, ProducesFourTracks) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);
}

TEST(ChoralePreludeTest, AllTracksHaveNotes) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") is empty";
  }
}

TEST(ChoralePreludeTest, ReasonableNoteCount) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  size_t total = test_helpers::totalNoteCount(result);
  // Cantus has 8-16 notes, counterpoint has many more, pedal has moderate.
  EXPECT_GT(total, 30u) << "Too few notes for a chorale prelude";
}

// ---------------------------------------------------------------------------
// Track configuration (channel, program, name)
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, TrackChannelMapping) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  // Track 0: Counterpoint on Great (ch 0).
  EXPECT_EQ(result.tracks[0].channel, 0u);
  // Track 1: Cantus on Swell (ch 1).
  EXPECT_EQ(result.tracks[1].channel, 1u);
  // Track 2: Inner voice on Great (ch 0, shares with Track 0).
  EXPECT_EQ(result.tracks[2].channel, 0u);
  // Track 3: Pedal (ch 3).
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

TEST(ChoralePreludeTest, TrackProgramMapping) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[3].program, GmProgram::kChurchOrgan);
}

TEST(ChoralePreludeTest, TrackNames) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].name, "Counterpoint (Great)");
  EXPECT_EQ(result.tracks[1].name, "Cantus Firmus (Swell)");
  EXPECT_EQ(result.tracks[2].name, "Inner Voice (Great)");
  EXPECT_EQ(result.tracks[3].name, "Pedal");
}

// ---------------------------------------------------------------------------
// Cantus firmus properties
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, CantusHasLongNotes) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Cantus notes should be at least a half note (2 beats = 960 ticks).
  // Some passing tones may subdivide the original whole notes.
  const auto& cantus_notes = result.tracks[1].notes;
  ASSERT_GT(cantus_notes.size(), 0u);

  for (const auto& note : cantus_notes) {
    EXPECT_GE(note.duration, kTicksPerBeat * 2)
        << "Cantus note at tick " << note.start_tick
        << " has duration " << note.duration << " (less than a half note)";
  }
}

TEST(ChoralePreludeTest, CantusCoversEntireDuration) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  const auto& cantus_notes = result.tracks[1].notes;
  ASSERT_GT(cantus_notes.size(), 0u);

  // Sum of cantus durations should equal total duration.
  Tick cantus_total = 0;
  for (const auto& note : cantus_notes) {
    cantus_total += note.duration;
  }
  EXPECT_EQ(cantus_total, result.total_duration_ticks);
}

TEST(ChoralePreludeTest, CounterpointHasShorterNotes) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 2u);

  // Counterpoint (track 0) should have more notes than cantus (track 1)
  // because it uses shorter note values.
  EXPECT_GT(result.tracks[0].notes.size(), result.tracks[1].notes.size())
      << "Counterpoint should have more notes than cantus (shorter values)";
}

// ---------------------------------------------------------------------------
// Inner voice tests
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, InnerVoiceHasNotes) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 4u);

  EXPECT_GT(result.tracks[2].notes.size(), 0u)
      << "Inner voice track should have notes";
}

TEST(ChoralePreludeTest, InnerVoicePitchRange) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 4u);

  for (const auto& note : result.tracks[2].notes) {
    EXPECT_GE(note.pitch, 48u)
        << "Inner voice pitch below C3: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, 67u)
        << "Inner voice pitch above G4: " << static_cast<int>(note.pitch);
  }
}

TEST(ChoralePreludeTest, InnerVoiceBelowCantus) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  // Build cantus pitch lookup.
  auto cantus_at = [&](Tick tick) -> int {
    for (const auto& n : result.tracks[1].notes) {
      if (tick >= n.start_tick && tick < n.start_tick + n.duration) {
        return static_cast<int>(n.pitch);
      }
    }
    return -1;
  };

  int crossings = 0;
  for (const auto& note : result.tracks[2].notes) {
    int cantus_p = cantus_at(note.start_tick);
    if (cantus_p >= 0 && static_cast<int>(note.pitch) >= cantus_p) {
      ++crossings;
    }
  }
  // Allow some crossings (from post-validation adjustments), but < 5%.
  float crossing_rate = result.tracks[2].notes.empty()
      ? 0.0f
      : 100.0f * static_cast<float>(crossings) /
            static_cast<float>(result.tracks[2].notes.size());
  EXPECT_LT(crossing_rate, 5.0f)
      << "Inner voice crosses cantus too often: " << crossing_rate << "%";
}

// ---------------------------------------------------------------------------
// Pedal coverage
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, PedalCoverageAbove90Percent) {
  // Test across multiple seeds to ensure consistency.
  for (uint32_t seed : {42u, 99u, 12345u}) {
    ChoralePreludeConfig config = makeTestConfig(seed);
    ChoralePreludeResult result = generateChoralePrelude(config);
    ASSERT_TRUE(result.success);

    Tick pedal_covered = 0;
    for (const auto& note : result.tracks[3].notes) {
      pedal_covered += note.duration;
    }
    float coverage = result.total_duration_ticks > 0
        ? 100.0f * static_cast<float>(pedal_covered) /
              static_cast<float>(result.total_duration_ticks)
        : 0.0f;
    EXPECT_GE(coverage, 90.0f)
        << "Pedal coverage " << coverage << "% for seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Voice ordering (SATB median check)
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, VoiceOrderingSATB) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  // Compute medians for each voice track.
  auto median = [](std::vector<uint8_t>& pitches) -> float {
    if (pitches.empty()) return 0.0f;
    std::sort(pitches.begin(), pitches.end());
    size_t mid = pitches.size() / 2;
    return pitches.size() % 2 == 0
        ? (pitches[mid - 1] + pitches[mid]) / 2.0f
        : static_cast<float>(pitches[mid]);
  };

  std::vector<uint8_t> fig_pitches, cantus_pitches, inner_pitches, pedal_pitches;
  for (const auto& n : result.tracks[0].notes) fig_pitches.push_back(n.pitch);
  for (const auto& n : result.tracks[1].notes) cantus_pitches.push_back(n.pitch);
  for (const auto& n : result.tracks[2].notes) inner_pitches.push_back(n.pitch);
  for (const auto& n : result.tracks[3].notes) pedal_pitches.push_back(n.pitch);

  float med_fig = median(fig_pitches);
  float med_cantus = median(cantus_pitches);
  float med_inner = median(inner_pitches);
  float med_pedal = median(pedal_pitches);

  EXPECT_GT(med_fig, med_cantus) << "Figuration should be above cantus";
  EXPECT_GT(med_cantus, med_inner) << "Cantus should be above inner voice";
  EXPECT_GT(med_inner, med_pedal) << "Inner voice should be above pedal";
}

// ---------------------------------------------------------------------------
// Strong-beat dissonance rate
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, StrongBeatDissonanceReasonable) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  // Cantus pitch lookup.
  auto cantus_at = [&](Tick tick) -> int {
    for (const auto& n : result.tracks[1].notes) {
      if (tick >= n.start_tick && tick < n.start_tick + n.duration) {
        return static_cast<int>(n.pitch);
      }
    }
    return -1;
  };

  int total = 0, dissonant = 0;
  // Exclude last 2 bars (cadence window).
  Tick cadence_start = result.total_duration_ticks > kTicksPerBar * 2
      ? result.total_duration_ticks - kTicksPerBar * 2
      : 0;

  for (size_t track_idx : {0u, 2u, 3u}) {
    for (const auto& note : result.tracks[track_idx].notes) {
      uint8_t beat = beatInBar(note.start_tick);
      if (beat != 0 && beat != 2) continue;
      if (note.start_tick >= cadence_start) continue;

      ++total;
      int cp = cantus_at(note.start_tick);
      if (cp >= 0) {
        int ivl = interval_util::compoundToSimple(
            std::abs(static_cast<int>(note.pitch) - cp));
        if (!interval_util::isConsonance(ivl)) {
          ++dissonant;
        }
      }
    }
  }

  float rate = total > 0 ? 100.0f * static_cast<float>(dissonant) /
                               static_cast<float>(total)
                         : 0.0f;
  EXPECT_LT(rate, 30.0f)
      << "Strong-beat dissonance rate " << rate << "% exceeds 30% threshold";
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, AllNotesVelocity80) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
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
// Pitch ranges
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, CantusPitchWithinSwellRange) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual2Low)
        << "Cantus pitch below Swell range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kManual2High)
        << "Cantus pitch above Swell range: " << static_cast<int>(note.pitch);
  }
}

TEST(ChoralePreludeTest, PedalPitchWithinRange) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  for (const auto& note : result.tracks[3].notes) {
    EXPECT_GE(note.pitch, organ_range::kPedalLow)
        << "Pedal pitch below range: " << static_cast<int>(note.pitch);
    EXPECT_LE(note.pitch, organ_range::kPedalHigh)
        << "Pedal pitch above range: " << static_cast<int>(note.pitch);
  }
}

// ---------------------------------------------------------------------------
// Voice assignment
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, VoiceAssignmentCorrect) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);

  // Track 0: voice 0 (counterpoint/figuration).
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_EQ(note.voice, 0u) << "Counterpoint note has wrong voice id";
  }
  // Track 1: voice 1 (cantus).
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_EQ(note.voice, 1u) << "Cantus note has wrong voice id";
  }
  // Track 2: voice 2 (inner voice).
  for (const auto& note : result.tracks[2].notes) {
    EXPECT_EQ(note.voice, 2u) << "Inner voice note has wrong voice id";
  }
  // Track 3: voice 3 (pedal).
  for (const auto& note : result.tracks[3].notes) {
    EXPECT_EQ(note.voice, 3u) << "Pedal note has wrong voice id";
  }
}

// ---------------------------------------------------------------------------
// Notes sorted
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, NotesSortedByStartTick) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
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

TEST(ChoralePreludeTest, AllNotesHavePositiveDuration) {
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
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
// Determinism
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, DeterministicWithSameSeed) {
  ChoralePreludeConfig config = makeTestConfig(12345);
  ChoralePreludeResult result1 = generateChoralePrelude(config);
  ChoralePreludeResult result2 = generateChoralePrelude(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  for (size_t track_idx = 0; track_idx < result1.tracks.size(); ++track_idx) {
    const auto& notes1 = result1.tracks[track_idx].notes;
    const auto& notes2 = result2.tracks[track_idx].notes;
    ASSERT_EQ(notes1.size(), notes2.size())
        << "Track " << track_idx << " note count differs";

    for (size_t note_idx = 0; note_idx < notes1.size(); ++note_idx) {
      EXPECT_EQ(notes1[note_idx].start_tick, notes2[note_idx].start_tick);
      EXPECT_EQ(notes1[note_idx].pitch, notes2[note_idx].pitch);
      EXPECT_EQ(notes1[note_idx].duration, notes2[note_idx].duration);
    }
  }
}

TEST(ChoralePreludeTest, DifferentSeedsProduceDifferentOutput) {
  ChoralePreludeConfig config1 = makeTestConfig(42);
  ChoralePreludeConfig config2 = makeTestConfig(99);
  ChoralePreludeResult result1 = generateChoralePrelude(config1);
  ChoralePreludeResult result2 = generateChoralePrelude(config2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Counterpoint figuration should differ with different seeds.
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
// Chorale melody selection
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, SeedSelectsDifferentChorales) {
  // seed % 3: 0 -> Wachet auf, 1 -> Nun komm, 2 -> Ein feste Burg.
  ChoralePreludeConfig config0 = makeTestConfig(0);
  ChoralePreludeConfig config1 = makeTestConfig(1);
  ChoralePreludeConfig config2 = makeTestConfig(2);
  ChoralePreludeConfig config3 = makeTestConfig(3);  // 3 % 3 = 0, same as seed 0.

  ChoralePreludeResult result0 = generateChoralePrelude(config0);
  ChoralePreludeResult result1 = generateChoralePrelude(config1);
  ChoralePreludeResult result2 = generateChoralePrelude(config2);
  ChoralePreludeResult result3 = generateChoralePrelude(config3);

  ASSERT_TRUE(result0.success);
  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_TRUE(result3.success);

  // Seeds 0 and 3 select the same chorale, so cantus note count must match.
  EXPECT_EQ(result0.tracks[1].notes.size(), result3.tracks[1].notes.size())
      << "Same chorale (seed % 3 == 0) should have same cantus length";

  // At least two of the three chorales should have different total durations.
  bool lengths_differ =
      (result0.total_duration_ticks != result1.total_duration_ticks) ||
      (result1.total_duration_ticks != result2.total_duration_ticks) ||
      (result0.total_duration_ticks != result2.total_duration_ticks);
  EXPECT_TRUE(lengths_differ) << "Different chorales should have different durations";
}

// ---------------------------------------------------------------------------
// Key handling
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, MinorKeyGeneratesSuccessfully) {
  ChoralePreludeConfig config = makeTestConfig();
  config.key = {Key::A, true};  // A minor.
  ChoralePreludeResult result = generateChoralePrelude(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(test_helpers::totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// isCharacterFormCompatible tests
// ---------------------------------------------------------------------------

TEST(CharacterFormCompatTest, SevereAllowedEverywhere) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::Fugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::PreludeAndFugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::ChoralePrelude));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::ToccataAndFugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::Passacaglia));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::FantasiaAndFugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::TrioSonata));
}

TEST(CharacterFormCompatTest, PlayfulForbiddenForChoralePrelude) {
  EXPECT_FALSE(isCharacterFormCompatible(SubjectCharacter::Playful, FormType::ChoralePrelude));
}

TEST(CharacterFormCompatTest, RestlessForbiddenForChoralePrelude) {
  EXPECT_FALSE(isCharacterFormCompatible(SubjectCharacter::Restless, FormType::ChoralePrelude));
}

TEST(CharacterFormCompatTest, NobleForbiddenForToccataAndFugue) {
  EXPECT_FALSE(isCharacterFormCompatible(SubjectCharacter::Noble, FormType::ToccataAndFugue));
}

TEST(CharacterFormCompatTest, NobleAllowedForChoralePrelude) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Noble, FormType::ChoralePrelude));
}

TEST(CharacterFormCompatTest, PlayfulAllowedForToccataAndFugue) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Playful, FormType::ToccataAndFugue));
}

TEST(CharacterFormCompatTest, RestlessAllowedForToccataAndFugue) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Restless, FormType::ToccataAndFugue));
}

TEST(CharacterFormCompatTest, SoloStringFormsAlwaysCompatible) {
  // Solo string forms do not use SubjectCharacter, so all should pass.
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Playful, FormType::CelloPrelude));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Restless, FormType::Chaconne));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Noble, FormType::CelloPrelude));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::Chaconne));
}

TEST(CharacterFormCompatTest, AllCharactersAllowedForFugue) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::Fugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Playful, FormType::Fugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Noble, FormType::Fugue));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Restless, FormType::Fugue));
}

TEST(CharacterFormCompatTest, AllCharactersAllowedForPassacaglia) {
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Severe, FormType::Passacaglia));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Playful, FormType::Passacaglia));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Noble, FormType::Passacaglia));
  EXPECT_TRUE(isCharacterFormCompatible(SubjectCharacter::Restless, FormType::Passacaglia));
}

// ---------------------------------------------------------------------------
// Structural rhythm diversity: beat-position-aware figuration
// ---------------------------------------------------------------------------

TEST(ChoralePreludeTest, FigurationDownbeatsHaveLongerNotes) {
  // Downbeat (beat 0) figuration notes should predominantly use quarter notes
  // as structural anchors, rather than the shorter eighth/sixteenth notes
  // used on middle beats.
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  Tick total_downbeat_dur = 0;
  int downbeat_count = 0;
  Tick total_midbeat_dur = 0;
  int midbeat_count = 0;

  for (const auto& note : result.tracks[0].notes) {
    uint8_t beat = beatInBar(note.start_tick);
    if (beat == 0) {
      total_downbeat_dur += note.duration;
      ++downbeat_count;
    } else if (beat == 1 || beat == 2) {
      total_midbeat_dur += note.duration;
      ++midbeat_count;
    }
  }

  if (downbeat_count > 0 && midbeat_count > 0) {
    float avg_downbeat = static_cast<float>(total_downbeat_dur) /
                         static_cast<float>(downbeat_count);
    float avg_midbeat = static_cast<float>(total_midbeat_dur) /
                        static_cast<float>(midbeat_count);
    EXPECT_GT(avg_downbeat, avg_midbeat)
        << "Downbeat figuration (avg=" << avg_downbeat
        << ") should have longer durations than midbeat (avg=" << avg_midbeat
        << ") for structural rhythm anchoring";
  }
}

TEST(ChoralePreludeTest, FigurationHasDurationDiversity) {
  // The figuration voice should use at least 3 distinct duration values
  // across the piece (e.g. sixteenth, eighth, quarter, half).
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  std::set<Tick> unique_durations;
  for (const auto& note : result.tracks[0].notes) {
    unique_durations.insert(note.duration);
  }

  EXPECT_GE(unique_durations.size(), 3u)
      << "Figuration should use at least 3 distinct duration values, "
      << "found " << unique_durations.size();
}

TEST(ChoralePreludeTest, InnerVoiceDownbeatsHaveQuarterNotes) {
  // Inner voice downbeats should use quarter notes for structural clarity,
  // while middle beats may use eighth notes for activity.
  ChoralePreludeConfig config = makeTestConfig();
  ChoralePreludeResult result = generateChoralePrelude(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);

  int downbeat_quarter_count = 0;
  int downbeat_total = 0;

  for (const auto& note : result.tracks[2].notes) {
    if (beatInBar(note.start_tick) == 0) {
      ++downbeat_total;
      if (note.duration >= duration::kQuarterNote) {
        ++downbeat_quarter_count;
      }
    }
  }

  if (downbeat_total > 0) {
    float ratio = static_cast<float>(downbeat_quarter_count) /
                  static_cast<float>(downbeat_total);
    EXPECT_GE(ratio, 0.70f)
        << "Inner voice downbeats should use quarter notes >= 70% of the time, "
        << "got " << (ratio * 100.0f) << "%";
  }
}

}  // namespace
}  // namespace bach
