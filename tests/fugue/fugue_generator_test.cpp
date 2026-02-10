// Tests for fugue/fugue_generator.h -- end-to-end fugue generation pipeline,
// structural correctness, determinism, and organ track configuration.

#include "fugue/fugue_generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_structure.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default FugueConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return FugueConfig with standard 3-voice C major settings.
FugueConfig makeTestConfig(uint32_t seed = 42) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.bpm = 72;
  config.seed = seed;
  config.character = SubjectCharacter::Severe;
  config.max_subject_retries = 10;
  return config;
}

// ---------------------------------------------------------------------------
// Basic generation success
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_DefaultConfig_Succeeds) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.attempts, 0);
}

TEST(FugueGeneratorTest, GenerateFugue_HasTracks) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(FugueGeneratorTest, GenerateFugue_TracksHaveNotes) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") has no notes";
  }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_DeterministicWithSeed) {
  FugueConfig config = makeTestConfig(12345);
  FugueResult result1 = generateFugue(config);
  FugueResult result2 = generateFugue(config);

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

TEST(FugueGeneratorTest, GenerateFugue_DifferentSeeds) {
  FugueConfig config1 = makeTestConfig(42);
  FugueConfig config2 = makeTestConfig(99);
  FugueResult result1 = generateFugue(config1);
  FugueResult result2 = generateFugue(config2);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);

  // Different seeds should produce different output. Compare first track notes.
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
// Structure validation
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_StructureHasSections) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_GE(result.structure.sectionCount(), 4u)
      << "Fugue should have at least Exposition + Episode + Stretto + Coda";
}

TEST(FugueGeneratorTest, GenerateFugue_StructureStartsWithExposition) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.structure.sections.size(), 0u);
  EXPECT_EQ(result.structure.sections.front().type, SectionType::Exposition);
  EXPECT_EQ(result.structure.sections.front().phase, FuguePhase::Establish);
}

TEST(FugueGeneratorTest, GenerateFugue_StructureEndsWithCoda) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.structure.sections.size(), 0u);
  EXPECT_EQ(result.structure.sections.back().type, SectionType::Coda);
  EXPECT_EQ(result.structure.sections.back().phase, FuguePhase::Resolve);
}

TEST(FugueGeneratorTest, GenerateFugue_StructurePhaseOrder) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Verify phases are monotonically non-decreasing: Establish -> Develop -> Resolve.
  for (size_t idx = 1; idx < result.structure.sections.size(); ++idx) {
    auto prev_phase = static_cast<uint8_t>(result.structure.sections[idx - 1].phase);
    auto curr_phase = static_cast<uint8_t>(result.structure.sections[idx].phase);
    EXPECT_GE(curr_phase, prev_phase)
        << "Phase regression at section " << idx << ": "
        << fuguePhaseToString(result.structure.sections[idx].phase) << " after "
        << fuguePhaseToString(result.structure.sections[idx - 1].phase);
  }
}

TEST(FugueGeneratorTest, GenerateFugue_StructureValidates) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto violations = result.structure.validate();
  EXPECT_TRUE(violations.empty())
      << "Structure validation failed with " << violations.size() << " violation(s)";
  for (const auto& violation : violations) {
    ADD_FAILURE() << "Violation: " << violation;
  }
}

// ---------------------------------------------------------------------------
// Voice count variations
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_TwoVoices) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 2;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u);
}

TEST(FugueGeneratorTest, GenerateFugue_FourVoices) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

// ---------------------------------------------------------------------------
// Character variations
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_SevereCharacter) {
  FugueConfig config = makeTestConfig();
  config.character = SubjectCharacter::Severe;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_GT(result.tracks.size(), 0u);
}

TEST(FugueGeneratorTest, GenerateFugue_PlayfulCharacter) {
  FugueConfig config = makeTestConfig(77);
  config.character = SubjectCharacter::Playful;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_GT(result.tracks.size(), 0u);
}

TEST(FugueGeneratorTest, GenerateFugue_NobleCharacter) {
  FugueConfig config = makeTestConfig(88);
  config.character = SubjectCharacter::Noble;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_GT(result.tracks.size(), 0u);
}

TEST(FugueGeneratorTest, GenerateFugue_RestlessCharacter) {
  FugueConfig config = makeTestConfig(99);
  config.character = SubjectCharacter::Restless;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_GT(result.tracks.size(), 0u);
}

// ---------------------------------------------------------------------------
// 4-5 voice generation
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_FourVoices_HasAllTracks) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);

  // All 4 tracks should have notes.
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") has no notes";
  }
}

TEST(FugueGeneratorTest, GenerateFugue_FourVoices_HasPedalTrack) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);
  EXPECT_EQ(result.tracks[3].name, "Pedal");
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

TEST(FugueGeneratorTest, GenerateFugue_FourVoices_StructureValid) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto violations = result.structure.validate();
  EXPECT_TRUE(violations.empty())
      << "4-voice structure validation failed with " << violations.size()
      << " violation(s)";
}

TEST(FugueGeneratorTest, GenerateFugue_FiveVoices_HasAllTracks) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 5;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u);

  // All 5 tracks should have notes.
  for (size_t idx = 0; idx < result.tracks.size(); ++idx) {
    EXPECT_GT(result.tracks[idx].notes.size(), 0u)
        << "Track " << idx << " (" << result.tracks[idx].name << ") has no notes";
  }
}

TEST(FugueGeneratorTest, GenerateFugue_FiveVoices_TwoOnGreat) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 5;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 5u);

  // 5-voice layout: voices 0,1 on Great; voice 2 on Swell; voice 3 on Positiv;
  // voice 4 on Pedal. Channels: Great=0 (shared by 2 voices), Swell=1, Positiv=2, Pedal=3.
  EXPECT_EQ(result.tracks[0].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[1].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[2].name, "Manual II (Swell)");
  EXPECT_EQ(result.tracks[3].name, "Manual III (Positiv)");
  EXPECT_EQ(result.tracks[4].name, "Pedal");
}

TEST(FugueGeneratorTest, GenerateFugue_FiveVoices_StructureValid) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 5;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto violations = result.structure.validate();
  EXPECT_TRUE(violations.empty())
      << "5-voice structure validation failed with " << violations.size()
      << " violation(s)";
}

TEST(FugueGeneratorTest, GenerateFugue_FourVoices_NotesSorted) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Notes not sorted in track " << track.name << " at index " << idx;
    }
  }
}

TEST(FugueGeneratorTest, GenerateFugue_FiveVoices_AllVelocity80) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 5;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u)
          << "Organ velocity must be 80, found " << static_cast<int>(note.velocity);
    }
  }
}

TEST(FugueGeneratorTest, GenerateFugue_FourVoices_NobleCharacter) {
  FugueConfig config = makeTestConfig(101);
  config.num_voices = 4;
  config.character = SubjectCharacter::Noble;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

TEST(FugueGeneratorTest, GenerateFugue_FiveVoices_RestlessCharacter) {
  FugueConfig config = makeTestConfig(102);
  config.num_voices = 5;
  config.character = SubjectCharacter::Restless;
  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u);
}

// ---------------------------------------------------------------------------
// Track configuration (organ system)
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_TrackNames) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].name, "Manual I (Great)");
  EXPECT_EQ(result.tracks[1].name, "Manual II (Swell)");
  EXPECT_EQ(result.tracks[2].name, "Manual III (Positiv)");
  EXPECT_EQ(result.tracks[3].name, "Pedal");
}

TEST(FugueGeneratorTest, GenerateFugue_TrackPrograms) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[1].program, GmProgram::kReedOrgan);
  EXPECT_EQ(result.tracks[2].program, GmProgram::kChurchOrgan);
  EXPECT_EQ(result.tracks[3].program, GmProgram::kChurchOrgan);
}

TEST(FugueGeneratorTest, GenerateFugue_TrackChannels) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.tracks.size(), 4u);

  EXPECT_EQ(result.tracks[0].channel, 0u);
  EXPECT_EQ(result.tracks[1].channel, 1u);
  EXPECT_EQ(result.tracks[2].channel, 2u);
  EXPECT_EQ(result.tracks[3].channel, 3u);
}

// ---------------------------------------------------------------------------
// Note range and ordering
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_NotesInValidRange) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      // All organ pitches should be within [24, 96] (Pedal low to Manual high).
      EXPECT_GE(note.pitch, organ_range::kPedalLow)
          << "Pitch " << static_cast<int>(note.pitch)
          << " below organ range in track " << track.name;
      EXPECT_LE(note.pitch, organ_range::kManual1High)
          << "Pitch " << static_cast<int>(note.pitch)
          << " above organ range in track " << track.name;
    }
  }
}

TEST(FugueGeneratorTest, GenerateFugue_NotesSorted) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (size_t idx = 1; idx < track.notes.size(); ++idx) {
      EXPECT_LE(track.notes[idx - 1].start_tick, track.notes[idx].start_tick)
          << "Notes not sorted in track " << track.name << " at index " << idx;
    }
  }
}

TEST(FugueGeneratorTest, GenerateFugue_AllVelocity80) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u)
          << "Organ velocity must be 80, found " << static_cast<int>(note.velocity);
    }
  }
}

// ---------------------------------------------------------------------------
// Structure section content
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_HasExpositionSection) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto expositions = result.structure.getSectionsByType(SectionType::Exposition);
  EXPECT_EQ(expositions.size(), 1u);
}

TEST(FugueGeneratorTest, GenerateFugue_HasEpisodeSections) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto episodes = result.structure.getSectionsByType(SectionType::Episode);
  EXPECT_GE(episodes.size(), 1u);
}

TEST(FugueGeneratorTest, GenerateFugue_HasStrettoSection) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto strettos = result.structure.getSectionsByType(SectionType::Stretto);
  EXPECT_EQ(strettos.size(), 1u);
}

TEST(FugueGeneratorTest, GenerateFugue_HasAllThreePhases) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto establish = result.structure.getSectionsByPhase(FuguePhase::Establish);
  auto develop = result.structure.getSectionsByPhase(FuguePhase::Develop);
  auto resolve = result.structure.getSectionsByPhase(FuguePhase::Resolve);

  EXPECT_GE(establish.size(), 1u) << "No Establish phase sections";
  EXPECT_GE(develop.size(), 1u) << "No Develop phase sections";
  EXPECT_GE(resolve.size(), 1u) << "No Resolve phase sections";
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_VoiceCountClampedLow) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 1;  // Below minimum
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 2u) << "Should clamp to 2 voices minimum";
}

TEST(FugueGeneratorTest, GenerateFugue_VoiceCountClampedHigh) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 10;  // Above maximum
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u) << "Should clamp to 5 voices maximum";
}

TEST(FugueGeneratorTest, GenerateFugue_SectionsNonOverlapping) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (size_t idx = 1; idx < result.structure.sections.size(); ++idx) {
    EXPECT_GE(result.structure.sections[idx].start_tick,
              result.structure.sections[idx - 1].end_tick)
        << "Section " << idx << " overlaps with section " << (idx - 1);
  }
}

TEST(FugueGeneratorTest, GenerateFugue_AllSectionsPositiveDuration) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (size_t idx = 0; idx < result.structure.sections.size(); ++idx) {
    EXPECT_GT(result.structure.sections[idx].end_tick,
              result.structure.sections[idx].start_tick)
        << "Section " << idx << " has zero or negative duration";
  }
}

// ---------------------------------------------------------------------------
// Timeline propagation
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, GenerateFugue_HasTimeline) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  EXPECT_GT(result.timeline.size(), 0u)
      << "Fugue result should include a harmonic timeline from tonal plan";
}

TEST(FugueGeneratorTest, GenerateFugue_TimelineCoversDuration) {
  FugueConfig config = makeTestConfig();
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.timeline.size(), 0u);

  // The timeline should have events covering the entire piece.
  Tick last_tick = result.timeline.events().back().end_tick;
  EXPECT_GT(last_tick, 0u);
}

// ---------------------------------------------------------------------------
// Pedal point generation
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, DominantPedalBeforeStretto) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Collect all pedal point notes across all tracks.
  std::vector<NoteEvent> pedal_notes;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint) {
        pedal_notes.push_back(note);
      }
    }
  }
  // Should have some pedal point notes (dominant + tonic).
  EXPECT_GT(pedal_notes.size(), 0u);

  // At least one pedal note should be before the stretto section.
  auto strettos = result.structure.getSectionsByType(SectionType::Stretto);
  ASSERT_EQ(strettos.size(), 1u);
  Tick stretto_start = strettos[0].start_tick;

  bool has_pre_stretto_pedal = false;
  for (const auto& note : pedal_notes) {
    if (note.start_tick < stretto_start) {
      has_pre_stretto_pedal = true;
      break;
    }
  }
  EXPECT_TRUE(has_pre_stretto_pedal)
      << "Expected dominant pedal point notes before stretto start";
}

TEST(FugueGeneratorTest, TonicPedalInCoda) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Find the coda section boundaries.
  auto codas = result.structure.getSectionsByType(SectionType::Coda);
  ASSERT_EQ(codas.size(), 1u);
  Tick coda_start = codas[0].start_tick;
  Tick coda_end = codas[0].end_tick;

  // Check that there is at least one pedal point note in the coda.
  bool has_coda_pedal = false;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.start_tick >= coda_start && note.start_tick < coda_end) {
        has_coda_pedal = true;
        break;
      }
    }
    if (has_coda_pedal) break;
  }
  EXPECT_TRUE(has_coda_pedal) << "Expected tonic pedal point notes in coda";
}

TEST(FugueGeneratorTest, PedalPointInLowestVoice) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  VoiceId expected_lowest = config.num_voices - 1;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint) {
        EXPECT_EQ(note.voice, expected_lowest)
            << "Pedal point at tick " << note.start_tick
            << " should be in lowest voice (" << static_cast<int>(expected_lowest)
            << ") but was in voice " << static_cast<int>(note.voice);
      }
    }
  }
}

TEST(FugueGeneratorTest, PedalPointInLowestVoice_FourVoices) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  VoiceId expected_lowest = config.num_voices - 1;
  bool found_pedal = false;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint) {
        found_pedal = true;
        EXPECT_EQ(note.voice, expected_lowest);
      }
    }
  }
  EXPECT_TRUE(found_pedal) << "4-voice fugue should have pedal point notes";
}

TEST(FugueGeneratorTest, DominantPedalPitchIsFifthAboveTonic) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  config.key = Key::C;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // For C major, tonic bass = C2 (MIDI 36), dominant = G2 (MIDI 43).
  // The dominant pedal notes should all have pitch 43.
  auto strettos = result.structure.getSectionsByType(SectionType::Stretto);
  ASSERT_EQ(strettos.size(), 1u);
  Tick stretto_start = strettos[0].start_tick;

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.start_tick < stretto_start) {
        // C2 (36) + perfect 5th (7) = G2 (43).
        EXPECT_EQ(note.pitch, 43u)
            << "Dominant pedal in C major should be G2 (MIDI 43), got "
            << static_cast<int>(note.pitch);
      }
    }
  }
}

TEST(FugueGeneratorTest, TonicPedalPitchIsRoot) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  config.key = Key::C;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto codas = result.structure.getSectionsByType(SectionType::Coda);
  ASSERT_EQ(codas.size(), 1u);
  Tick coda_start = codas[0].start_tick;
  Tick coda_end = codas[0].end_tick;

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.start_tick >= coda_start && note.start_tick < coda_end) {
        // C2 = MIDI 36.
        EXPECT_EQ(note.pitch, 36u)
            << "Tonic pedal in C major should be C2 (MIDI 36), got "
            << static_cast<int>(note.pitch);
      }
    }
  }
}

TEST(FugueGeneratorTest, PedalPointVelocityIsOrganDefault) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint) {
        EXPECT_EQ(note.velocity, 80u)
            << "Pedal point velocity must be organ default (80)";
      }
    }
  }
}

TEST(FugueGeneratorTest, DominantPedalSpansFourBars) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Find dominant pedal notes (those before stretto).
  auto strettos = result.structure.getSectionsByType(SectionType::Stretto);
  ASSERT_EQ(strettos.size(), 1u);
  Tick stretto_start = strettos[0].start_tick;

  std::vector<NoteEvent> dominant_pedals;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.start_tick < stretto_start) {
        dominant_pedals.push_back(note);
      }
    }
  }

  ASSERT_FALSE(dominant_pedals.empty());

  // The dominant pedal should span exactly 4 bars (4 * 1920 = 7680 ticks).
  // It is split into bar-length notes, so there should be 4 notes.
  EXPECT_EQ(dominant_pedals.size(), 4u)
      << "Dominant pedal should be split into 4 bar-length notes";

  // Total duration should equal 4 bars.
  Tick total_duration = 0;
  for (const auto& note : dominant_pedals) {
    total_duration += note.duration;
  }
  EXPECT_EQ(total_duration, kTicksPerBar * 4u);
}

}  // namespace
}  // namespace bach
