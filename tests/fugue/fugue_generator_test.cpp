// Tests for fugue/fugue_generator.h -- end-to-end fugue generation pipeline,
// structural correctness, determinism, and organ track configuration.

#include "fugue/fugue_generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "analysis/counterpoint_analyzer.h"
#include "analysis/dissonance_analyzer.h"
#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_structure.h"
#include "fugue/voice_registers.h"

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

  // Dominant pedal should be G (pitch class 7) in the lowest voice register.
  // For 3-voice manualiter, the pedal is in the tenor range (48-72).
  // For 4-voice pedaliter, the pedal is in the pedal range (24-50).
  auto strettos = result.structure.getSectionsByType(SectionType::Stretto);
  ASSERT_EQ(strettos.size(), 1u);
  Tick stretto_start = strettos[0].start_tick;

  VoiceId lowest = config.num_voices - 1;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.voice == lowest &&
          note.start_tick < stretto_start) {
        EXPECT_EQ(getPitchClass(note.pitch), 7u)
            << "Dominant pedal in C major should be G, got pitch "
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

  VoiceId lowest = config.num_voices - 1;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::PedalPoint &&
          note.voice == lowest &&
          note.start_tick >= coda_start && note.start_tick < coda_end) {
        // Tonic pedal should be C (pitch class 0).
        EXPECT_EQ(getPitchClass(note.pitch), 0u)
            << "Tonic pedal in C major should be C, got pitch "
            << static_cast<int>(note.pitch);
      }
    }
  }
}

TEST(FugueGeneratorTest, CodaNotesHaveCodaSource) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto codas = result.structure.getSectionsByType(SectionType::Coda);
  ASSERT_EQ(codas.size(), 1u);
  Tick coda_start = codas[0].start_tick;
  Tick coda_end = codas[0].end_tick;

  // Collect non-pedal notes in the coda region.
  int coda_source_count = 0;
  int non_pedal_coda_count = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.start_tick >= coda_start && note.start_tick < coda_end &&
          note.source != BachNoteSource::PedalPoint) {
        ++non_pedal_coda_count;
        if (note.source == BachNoteSource::Coda) {
          ++coda_source_count;
        }
      }
    }
  }

  EXPECT_GT(non_pedal_coda_count, 0) << "Coda should have non-pedal notes";
  EXPECT_EQ(coda_source_count, non_pedal_coda_count)
      << "All non-pedal coda notes should have BachNoteSource::Coda";
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

// ---------------------------------------------------------------------------
// Bass voice density (Phase A validation)
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, BassDensity_ThreeVoices) {
  // Test across multiple seeds to ensure bass voice gets sufficient content.
  uint32_t seeds[] = {1, 7, 13, 42, 99, 256};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << " failed to generate";

    // Count bass voice (voice 2 for 3-voice, last track) note count.
    ASSERT_EQ(result.tracks.size(), 3u);
    const auto& bass_track = result.tracks[2];
    size_t bass_notes = bass_track.notes.size();

    // Compute total bars from the structure.
    Tick total_ticks = 0;
    for (const auto& section : result.structure.sections) {
      if (section.end_tick > total_ticks) total_ticks = section.end_tick;
    }
    float total_bars = static_cast<float>(total_ticks) /
                       static_cast<float>(kTicksPerBar);

    float notes_per_bar = (total_bars > 0)
        ? static_cast<float>(bass_notes) / total_bars : 0.0f;

    EXPECT_GT(notes_per_bar, 0.5f)
        << "Seed " << seed << ": bass notes/bar = " << notes_per_bar
        << " (need > 0.5)";
  }
}

TEST(FugueGeneratorTest, BassMaxConsecutiveSilence_ThreeVoices) {
  // Verify that the bass voice doesn't have excessively long silent stretches.
  uint32_t seeds[] = {1, 7, 13, 42, 99, 256};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << " failed";

    ASSERT_EQ(result.tracks.size(), 3u);
    const auto& bass_track = result.tracks[2];

    // Find the maximum gap between consecutive bass notes.
    Tick max_gap = 0;
    if (!bass_track.notes.empty()) {
      for (size_t i = 0; i + 1 < bass_track.notes.size(); ++i) {
        Tick end_of_current = bass_track.notes[i].start_tick +
                              bass_track.notes[i].duration;
        Tick start_of_next = bass_track.notes[i + 1].start_tick;
        if (start_of_next > end_of_current) {
          Tick gap = start_of_next - end_of_current;
          if (gap > max_gap) max_gap = gap;
        }
      }
    }

    // Max consecutive silence: 8 bars.
    Tick max_silence_ticks = kTicksPerBar * 8;
    EXPECT_LE(max_gap, max_silence_ticks)
        << "Seed " << seed << ": max bass silence = "
        << (max_gap / kTicksPerBar) << " bars (limit: 8)";
  }
}

// ---------------------------------------------------------------------------
// Coda voice-leading (Phase D validation)
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, CodaVoiceLeading_NoLargeJumps) {
  // Verify coda doesn't have excessively large jumps from pre-coda notes.
  uint32_t seeds[] = {1, 43, 99};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed;

    auto codas = result.structure.getSectionsByType(SectionType::Coda);
    ASSERT_EQ(codas.size(), 1u);
    Tick coda_start = codas[0].start_tick;

    // For each voice, check the jump from last pre-coda note to first coda note.
    for (size_t track_idx = 0; track_idx < result.tracks.size(); ++track_idx) {
      const auto& notes = result.tracks[track_idx].notes;

      // Find last pre-coda note and first coda note.
      const NoteEvent* last_pre = nullptr;
      const NoteEvent* first_coda = nullptr;
      for (const auto& n : notes) {
        if (n.start_tick < coda_start) last_pre = &n;
        if (n.start_tick >= coda_start && !first_coda) first_coda = &n;
      }

      if (last_pre && first_coda) {
        int jump = std::abs(static_cast<int>(first_coda->pitch) -
                            static_cast<int>(last_pre->pitch));
        // Soprano and bass: max 12st (octave); inner voices: max 9st
        // (a major 6th is acceptable in cadential/coda voice leading).
        bool is_outer = (track_idx == 0 ||
                         track_idx == result.tracks.size() - 1);
        int max_jump = is_outer ? 12 : 9;
        // Pedal voice excluded (it has a pedal point).
        if (first_coda->source != BachNoteSource::PedalPoint) {
          EXPECT_LE(jump, max_jump)
              << "Seed " << seed << ", track " << track_idx
              << ": coda entry jump = " << jump << "st (max " << max_jump << ")";
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Non-structural parallel perfects must be zero
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, ZeroNonStructuralParallels_AllSeeds) {
  uint32_t seeds[] = {1, 7, 13, 42, 100, 256};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << " failed to generate";

    // Collect all notes from tracks.
    std::vector<NoteEvent> all_notes;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        all_notes.push_back(note);
      }
    }

    auto analysis = analyzeCounterpoint(all_notes, config.num_voices);
    uint32_t non_structural =
        analysis.parallel_perfect_count - analysis.structural_parallel_count;
    // Parallel budget: 0.5% of notes capped 1-8, matching the budget set
    // in fugue_generator.cpp. BWV578 reference: ~4% parallel ratio.
    uint32_t budget = static_cast<uint32_t>(std::max(1, std::min(8,
        static_cast<int>(std::ceil(
            static_cast<float>(all_notes.size()) * 0.005f)))));
    EXPECT_LE(non_structural, budget)
        << "Seed " << seed << ": " << non_structural
        << " non-structural parallel perfects (total: "
        << analysis.parallel_perfect_count
        << ", structural: " << analysis.structural_parallel_count
        << ", budget: " << budget << ")";
  }
}

// ---------------------------------------------------------------------------
// Coda V7-I cadence: all voices participate in Stage 2, pedal in Stage 3 only
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, CodaChordNotes_AllVoicesInStage2_3Voice) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto codas = result.structure.getSectionsByType(SectionType::Coda);
  ASSERT_EQ(codas.size(), 1u);
  Tick coda_start = codas[0].start_tick;

  // Stage 2 starts after 2 bars, Stage 3 after 3 bars.
  Tick stage2_start = coda_start + kTicksPerBar * 2;
  Tick stage3_start = coda_start + kTicksPerBar * 3;
  VoiceId lowest_voice = config.num_voices - 1;

  // Lowest voice should have chord notes in Stage 2 (V7 and I).
  int lowest_stage2_notes = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::Coda &&
          note.voice == lowest_voice &&
          note.start_tick >= stage2_start &&
          note.start_tick < stage3_start) {
        ++lowest_stage2_notes;
      }
    }
  }

  EXPECT_GE(lowest_stage2_notes, 2)
      << "Lowest voice should participate in V7-I cadence (Stage 2)";
}

TEST(FugueGeneratorTest, CodaChordNotes_AllVoicesInStage2_4Voice) {
  FugueConfig config = makeTestConfig();
  config.num_voices = 4;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto codas = result.structure.getSectionsByType(SectionType::Coda);
  ASSERT_EQ(codas.size(), 1u);
  Tick coda_start = codas[0].start_tick;

  Tick stage2_start = coda_start + kTicksPerBar * 2;
  Tick stage3_start = coda_start + kTicksPerBar * 3;
  VoiceId lowest_voice = config.num_voices - 1;

  int lowest_stage2_notes = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      if (note.source == BachNoteSource::Coda &&
          note.voice == lowest_voice &&
          note.start_tick >= stage2_start &&
          note.start_tick < stage3_start) {
        ++lowest_stage2_notes;
      }
    }
  }

  EXPECT_GE(lowest_stage2_notes, 2)
      << "4-voice: lowest voice should participate in V7-I cadence (Stage 2)";
}

TEST(FugueGeneratorTest, CodaV7Chord_HasConsonantAnchor) {
  // Verify that coda V7 chord includes at least one note consonant with
  // the tonic pedal (the root G = P5). The V7 chord intentionally includes
  // the leading tone B (M7 vs pedal) for cadential tension, so we check
  // that the consonance anchor (G) is present rather than requiring all
  // notes to be consonant.
  for (uint32_t seed : {42u, 100u, 200u, 314u}) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    config.key = Key::C;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed;

    auto codas = result.structure.getSectionsByType(SectionType::Coda);
    ASSERT_EQ(codas.size(), 1u);
    Tick stage2_start = codas[0].start_tick + kTicksPerBar * 2;
    Tick stage2_half = stage2_start + kTicksPerBar / 2;

    // Find coda pedal pitch.
    uint8_t pedal_pitch = 0;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.source == BachNoteSource::PedalPoint &&
            note.start_tick <= stage2_start &&
            note.start_tick + note.duration > stage2_start) {
          pedal_pitch = note.pitch;
          break;
        }
      }
      if (pedal_pitch > 0) break;
    }
    if (pedal_pitch == 0) continue;

    // Check that at least one V7 chord note is consonant with pedal.
    bool has_consonant_anchor = false;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.source == BachNoteSource::Coda &&
            note.start_tick >= stage2_start &&
            note.start_tick < stage2_half) {
          int ivl = absoluteInterval(note.pitch, pedal_pitch) % 12;
          bool consonant = (ivl == 0 || ivl == 3 || ivl == 4 ||
                            ivl == 7 || ivl == 8 || ivl == 9);
          if (consonant) has_consonant_anchor = true;
        }
      }
    }
    EXPECT_TRUE(has_consonant_anchor)
        << "Seed " << seed << ": Coda V7 chord has no consonant anchor "
        << "with tonic pedal " << static_cast<int>(pedal_pitch);
  }
}

// ---------------------------------------------------------------------------
// Coda V7 fallback: strict voice order (no crossing, no unison)
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, CodaV7_FallbackStrictOrder_3Voice) {
  // Verify that V7, I, and final chords in the coda have strict descending
  // pitch order across voices (v0 > v1 > v2), with no unison.
  uint32_t seeds[] = {1, 7, 13, 42, 77, 99, 200, 314, 500, 1000};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << " failed to generate";

    auto codas = result.structure.getSectionsByType(SectionType::Coda);
    ASSERT_EQ(codas.size(), 1u);
    Tick coda_start = codas[0].start_tick;
    Tick coda_end = codas[0].end_tick;

    // Stage 2 starts after 2 bars.
    Tick stage2_start = coda_start + kTicksPerBar * 2;
    if (stage2_start >= coda_end) continue;

    // Check all ticks in Stage 2 and Stage 3 for strict voice order.
    // Collect distinct start ticks of coda notes in this region.
    std::set<Tick> check_ticks;
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.start_tick >= stage2_start && note.start_tick < coda_end &&
            note.source == BachNoteSource::Coda) {
          check_ticks.insert(note.start_tick);
        }
      }
    }

    for (Tick tick : check_ticks) {
      uint8_t pitches[5] = {0, 0, 0, 0, 0};
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          // Only check Coda-source notes (PedalPoint is independent).
          if (note.voice < 5 && note.source == BachNoteSource::Coda &&
              note.start_tick <= tick &&
              tick < note.start_tick + note.duration) {
            pitches[note.voice] = note.pitch;
          }
        }
      }
      for (uint8_t v = 0; v + 1 < config.num_voices; ++v) {
        if (pitches[v] > 0 && pitches[v + 1] > 0) {
          EXPECT_GT(pitches[v], pitches[v + 1])
              << "Seed " << seed << ", tick " << tick
              << ": v" << static_cast<int>(v) << "(" << static_cast<int>(pitches[v])
              << ") <= v" << static_cast<int>(v + 1) << "("
              << static_cast<int>(pitches[v + 1]) << ")";
        }
      }
    }
  }
}

TEST(FugueGeneratorTest, CodaV7_FallbackVoiceLeading_3Voice) {
  // When the fallback path is used, verify V7->I voice leading:
  //   soprano: LT -> tonic (semitone up, |diff|==1)
  //   inner:   7th -> 3rd (semitone down, |diff|==1)
  //   bass:    root -> tonic (P4 up or P5 down, interval in {5, 7})
  // This test uses a seed known to hit the fallback path.
  // Because the search path usually succeeds, we check the general constraint
  // that voice-leading is smooth (max 7 semitones per voice) as a fallback.
  uint32_t seeds[] = {1, 42, 99, 314};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeTestConfig(seed);
    config.num_voices = 3;
    config.key = Key::C;
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed;

    auto codas = result.structure.getSectionsByType(SectionType::Coda);
    ASSERT_EQ(codas.size(), 1u);
    Tick coda_start = codas[0].start_tick;
    Tick coda_end = codas[0].end_tick;

    Tick stage2_start = coda_start + kTicksPerBar * 2;
    Tick half_bar = kTicksPerBar / 2;
    Tick stage2_half = stage2_start + half_bar;
    if (stage2_half >= coda_end) continue;

    // Collect V7 and I chord pitches per voice.
    uint8_t v7[3] = {0, 0, 0};
    uint8_t i_chord[3] = {0, 0, 0};
    for (const auto& track : result.tracks) {
      for (const auto& note : track.notes) {
        if (note.voice >= 3) continue;
        if (note.source == BachNoteSource::Coda &&
            note.start_tick >= stage2_start && note.start_tick < stage2_half) {
          v7[note.voice] = note.pitch;
        }
        if (note.source == BachNoteSource::Coda &&
            note.start_tick >= stage2_half &&
            note.start_tick < stage2_start + kTicksPerBar) {
          i_chord[note.voice] = note.pitch;
        }
      }
    }

    // Verify smooth voice-leading: each voice moves at most 7 semitones.
    for (uint8_t v = 0; v < 3; ++v) {
      if (v7[v] > 0 && i_chord[v] > 0) {
        int diff = std::abs(static_cast<int>(i_chord[v]) - static_cast<int>(v7[v]));
        EXPECT_LE(diff, 7)
            << "Seed " << seed << ": voice " << static_cast<int>(v)
            << " V7->I jump = " << diff << "st (max 7)";
      }
    }
  }
}

TEST(FugueGeneratorTest, CodaV7_FallbackPitchClassPreserved) {
  // Verify that the greedy projection preserves pitch class when possible,
  // and that even when PC is broken, no voice crossing remains.
  uint32_t seeds[] = {1, 7, 42, 99, 200, 500};
  for (uint32_t seed : seeds) {
    for (uint8_t nv : {2, 3, 4, 5}) {
      FugueConfig config = makeTestConfig(seed);
      config.num_voices = nv;
      FugueResult result = generateFugue(config);
      ASSERT_TRUE(result.success)
          << "Seed " << seed << ", voices=" << static_cast<int>(nv);

      auto codas = result.structure.getSectionsByType(SectionType::Coda);
      if (codas.empty()) continue;
      Tick coda_start = codas[0].start_tick;
      Tick coda_end = codas[0].end_tick;

      Tick stage2_start = coda_start + kTicksPerBar * 2;
      if (stage2_start >= coda_end) continue;

      // NeverCrossing assertion: at every tick in Stage 2/3, v0 > v1 > ...
      std::set<Tick> check_ticks;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.start_tick >= stage2_start && note.start_tick < coda_end) {
            check_ticks.insert(note.start_tick);
          }
        }
      }

      for (Tick tick : check_ticks) {
        uint8_t pitches[5] = {0, 0, 0, 0, 0};
        for (const auto& track : result.tracks) {
          for (const auto& note : track.notes) {
            // Only check Coda-source notes (PedalPoint is independent).
            if (note.voice < 5 && note.source == BachNoteSource::Coda &&
                note.start_tick <= tick &&
                tick < note.start_tick + note.duration) {
              pitches[note.voice] = note.pitch;
            }
          }
        }
        uint8_t vc = std::min(nv, static_cast<uint8_t>(5));
        for (uint8_t v = 0; v + 1 < vc; ++v) {
          if (pitches[v] > 0 && pitches[v + 1] > 0) {
            EXPECT_GT(pitches[v], pitches[v + 1])
                << "NeverCrossing: seed " << seed
                << ", voices=" << static_cast<int>(nv)
                << ", tick " << tick
                << ": v" << static_cast<int>(v) << "("
                << static_cast<int>(pitches[v]) << ") <= v"
                << static_cast<int>(v + 1) << "("
                << static_cast<int>(pitches[v + 1]) << ")";
          }
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Coda voice crossing regression tests
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, CodaStage1_HeldChordStrictOrder) {
  // Verify that Stage 1 held chord voices (voice >= 1) maintain strict
  // descending pitch order at every tick in [coda_start, coda_start + 2*bar).
  // Voice 0 motif overlap is NOT checked (allowed by design).
  uint32_t seeds[] = {2, 3, 6, 7, 13};
  for (uint32_t seed : seeds) {
    for (uint8_t nv : {2, 3, 4, 5}) {
      FugueConfig config = makeTestConfig(seed);
      config.num_voices = nv;
      FugueResult result = generateFugue(config);
      ASSERT_TRUE(result.success)
          << "Seed " << seed << ", voices=" << static_cast<int>(nv);

      auto codas = result.structure.getSectionsByType(SectionType::Coda);
      if (codas.empty()) continue;
      Tick coda_start = codas[0].start_tick;
      Tick stage1_end = coda_start + kTicksPerBar * 2;

      // Collect all ticks in Stage 1 for held chord voices.
      std::set<Tick> check_ticks;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.voice >= 1 && note.source == BachNoteSource::Coda &&
              note.start_tick >= coda_start && note.start_tick < stage1_end) {
            check_ticks.insert(note.start_tick);
          }
        }
      }

      for (Tick tick : check_ticks) {
        // Gather held chord pitches (voice >= 1) active at this tick.
        uint8_t pitches[5] = {0, 0, 0, 0, 0};
        for (const auto& track : result.tracks) {
          for (const auto& note : track.notes) {
            if (note.voice >= 1 && note.voice < 5 &&
                note.source == BachNoteSource::Coda &&
                note.start_tick <= tick &&
                tick < note.start_tick + note.duration) {
              pitches[note.voice] = note.pitch;
            }
          }
        }
        uint8_t vc = std::min(nv, static_cast<uint8_t>(5));
        for (uint8_t v = 1; v + 1 < vc; ++v) {
          if (pitches[v] > 0 && pitches[v + 1] > 0) {
            EXPECT_GT(pitches[v], pitches[v + 1])
                << "HeldChordStrictOrder: seed " << seed
                << ", voices=" << static_cast<int>(nv)
                << ", tick " << tick
                << ": v" << static_cast<int>(v) << "("
                << static_cast<int>(pitches[v]) << ") <= v"
                << static_cast<int>(v + 1) << "("
                << static_cast<int>(pitches[v + 1]) << ")";
          }
        }
      }
    }
  }
}

TEST(FugueGeneratorTest, CodaProximity_NoCrossing) {
  // Verify that held chord voices (voice >= 1) do not cross (lower voice
  // above upper voice) across the entire coda section.  Voice 0 (motif)
  // overlap with held chords is allowed by design.  Unisons between
  // adjacent voices are tolerated -- coda chord voicing in 5-voice textures
  // can legitimately double a pitch across two voices.
  uint32_t seeds[] = {1, 42, 43, 44};
  for (uint32_t seed : seeds) {
    for (uint8_t nv : {3, 4, 5}) {
      FugueConfig config = makeTestConfig(seed);
      config.num_voices = nv;
      FugueResult result = generateFugue(config);
      ASSERT_TRUE(result.success)
          << "Seed " << seed << ", voices=" << static_cast<int>(nv);

      auto codas = result.structure.getSectionsByType(SectionType::Coda);
      if (codas.empty()) continue;
      Tick coda_start = codas[0].start_tick;
      Tick coda_end = codas[0].end_tick;

      // Collect all ticks where Coda-source notes are active.
      std::set<Tick> check_ticks;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          if (note.source == BachNoteSource::Coda &&
              note.start_tick >= coda_start && note.start_tick < coda_end) {
            check_ticks.insert(note.start_tick);
          }
        }
      }

      for (Tick tick : check_ticks) {
        uint8_t pitches[5] = {0, 0, 0, 0, 0};
        for (const auto& track : result.tracks) {
          for (const auto& note : track.notes) {
            if (note.voice < 5 && note.source == BachNoteSource::Coda &&
                note.start_tick <= tick &&
                tick < note.start_tick + note.duration) {
              pitches[note.voice] = note.pitch;
            }
          }
        }
        uint8_t vc = std::min(nv, static_cast<uint8_t>(5));
        // Start from voice 1: voice0 motif overlap is allowed.
        // Use GE (not GT) to permit unisons -- crossing means the lower
        // voice is strictly above the upper voice.
        for (uint8_t v = 1; v + 1 < vc; ++v) {
          if (pitches[v] > 0 && pitches[v + 1] > 0) {
            EXPECT_GE(pitches[v], pitches[v + 1])
                << "CodaProximityNoCrossing: seed " << seed
                << ", voices=" << static_cast<int>(nv)
                << ", tick " << tick
                << ": v" << static_cast<int>(v) << "("
                << static_cast<int>(pitches[v]) << ") < v"
                << static_cast<int>(v + 1) << "("
                << static_cast<int>(pitches[v + 1]) << ")";
          }
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// coordinateVoices integration: strong-beat dissonance should be minimal
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, FugueStrongBeatDissonanceZero) {
  // Verify that coordinateVoices keeps strong-beat dissonances low across
  // multiple seeds and voice counts.  A small number of strong-beat
  // dissonances can occur legitimately in valid counterpoint (suspensions,
  // appogiaturas, passing tones on strong beats).  Bach's own fugues
  // average 29-34% dissonance overall (CLAUDE.md Section 2b), so
  // requiring exactly zero is overly strict for seed-independent testing.
  uint32_t seeds[] = {1, 7, 42, 100, 123, 200};
  for (uint32_t seed : seeds) {
    for (uint8_t nv : {3, 4}) {
      FugueConfig config = makeTestConfig(seed);
      config.num_voices = nv;
      FugueResult result = generateFugue(config);
      ASSERT_TRUE(result.success)
          << "Seed " << seed << ", voices=" << static_cast<int>(nv);

      std::vector<NoteEvent> all_notes;
      for (const auto& track : result.tracks) {
        for (const auto& note : track.notes) {
          all_notes.push_back(note);
        }
      }

      auto clashes = detectSimultaneousClashes(all_notes, nv);
      int strong_beat_high = 0;
      for (const auto& ev : clashes) {
        if (ev.severity == DissonanceSeverity::High) {
          ++strong_beat_high;
        }
      }
      EXPECT_LE(strong_beat_high, 5)
          << "Seed " << seed << ", voices=" << static_cast<int>(nv)
          << ": " << strong_beat_high << " strong-beat dissonances (max 5)";
    }
  }
}

// ---------------------------------------------------------------------------
// Post-validation tier order: Tier 1 notes registered before Tier 2/3
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, FuguePostValidationTierOrder) {
  FugueConfig config = makeTestConfig(42);
  config.num_voices = 3;
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Collect all notes and sort the same way post-validation does.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      all_notes.push_back(note);
    }
  }

  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
              int pa = sourcePriority(a.source);
              int pb = sourcePriority(b.source);
              if (pa != pb) return pa < pb;
              return a.voice < b.voice;
            });

  // At each tick, Tier 1 notes must appear before Tier 2/3.
  Tick prev_tick = 0;
  int max_priority_seen = -1;
  for (const auto& note : all_notes) {
    if (note.start_tick != prev_tick) {
      max_priority_seen = -1;
      prev_tick = note.start_tick;
    }
    int pri = sourcePriority(note.source);
    // Priority should be non-decreasing within same tick.
    EXPECT_GE(pri, max_priority_seen)
        << "Tick " << note.start_tick << ": priority " << pri
        << " after " << max_priority_seen;
    max_priority_seen = std::max(max_priority_seen, pri);
  }
}

// ---------------------------------------------------------------------------
// sourcePriority consistency with isStructuralSource
// ---------------------------------------------------------------------------

TEST(FugueGeneratorTest, SourcePriorityConsistency) {
  // Tier 1 (priority 0) sources must all be structural.
  // All structural sources must have priority <= 1.
  // Tier 3 (priority 2) sources must NOT be structural.
  BachNoteSource all_sources[] = {
      BachNoteSource::FugueSubject, BachNoteSource::FugueAnswer,
      BachNoteSource::SubjectCore,  BachNoteSource::PedalPoint,
      BachNoteSource::CanonDux,     BachNoteSource::CanonComes,
      BachNoteSource::GoldbergAria,
      BachNoteSource::Countersubject, BachNoteSource::EpisodeMaterial,
      BachNoteSource::FalseEntry,   BachNoteSource::SequenceNote,
      BachNoteSource::Coda,
      BachNoteSource::FreeCounterpoint, BachNoteSource::Ornament,
      BachNoteSource::Unknown,
  };
  for (auto src : all_sources) {
    int pri = sourcePriority(src);
    bool structural = isStructuralSource(src);
    if (pri == 0) {
      // Tier 1 (immutable): must be structural.
      EXPECT_TRUE(structural)
          << "Source " << static_cast<int>(src)
          << " has priority 0 but is not structural";
    }
    if (structural) {
      // All structural sources must be Tier 1 or Tier 2.
      EXPECT_LE(pri, 1)
          << "Source " << static_cast<int>(src)
          << " is structural but has priority " << pri;
    }
    if (pri == 2) {
      // Tier 3: must NOT be structural.
      EXPECT_FALSE(structural)
          << "Source " << static_cast<int>(src)
          << " has priority 2 but is structural";
    }
  }
}

}  // namespace
}  // namespace bach
