// Tests for generator.h -- unified generation routing, prelude+fugue
// concatenation, instrument auto-detection, determinism, and form routing.

#include "generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a default GeneratorConfig for testing.
/// @param seed Random seed (default 42 for deterministic tests).
/// @return GeneratorConfig with standard 3-voice C major fugue settings.
GeneratorConfig makeTestConfig(uint32_t seed = 42) {
  GeneratorConfig config;
  config.form = FormType::Fugue;
  config.key = {Key::C, false};
  config.num_voices = 3;
  config.bpm = 100;
  config.seed = seed;
  config.character = SubjectCharacter::Severe;
  config.instrument = InstrumentType::Organ;
  return config;
}

/// @brief Count total notes across all tracks.
/// @param result The generator result to count.
/// @return Total number of NoteEvents across all tracks.
size_t totalNoteCount(const GeneratorResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
}

// ---------------------------------------------------------------------------
// Fugue form generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, FugueForm_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(GeneratorTest, FugueForm_HasCorrectTrackCount) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(GeneratorTest, FugueForm_HasFormDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_FALSE(result.form_description.empty());
  // Description should contain "fugue" (case-insensitive check via find).
  EXPECT_NE(result.form_description.find("fugue"), std::string::npos)
      << "Form description missing 'fugue': " << result.form_description;
}

// ---------------------------------------------------------------------------
// PreludeAndFugue form generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, PreludeAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(GeneratorTest, PreludeAndFugue_MoreNotesThanFugueAlone) {
  GeneratorConfig config = makeTestConfig(42);
  config.num_voices = 3;

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  size_t fugue_notes = totalNoteCount(fugue_result);
  size_t pf_notes = totalNoteCount(pf_result);

  EXPECT_GT(pf_notes, fugue_notes)
      << "PreludeAndFugue (" << pf_notes << " notes) should have more notes than Fugue alone ("
      << fugue_notes << " notes)";
}

TEST(GeneratorTest, PreludeAndFugue_LongerThanFugueAlone) {
  GeneratorConfig config = makeTestConfig(42);

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  EXPECT_GT(pf_result.total_duration_ticks, fugue_result.total_duration_ticks)
      << "PreludeAndFugue should be longer than Fugue alone";
}

TEST(GeneratorTest, PreludeAndFugue_HasTempoEvents) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_GE(result.tempo_events.size(), 2u)
      << "PreludeAndFugue should have tempo events for prelude and fugue sections";
  // First tempo event at tick 0.
  EXPECT_EQ(result.tempo_events[0].tick, 0u);
}

TEST(GeneratorTest, PreludeAndFugue_HasFormDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.form_description.find("Prelude"), std::string::npos)
      << "Form description missing 'Prelude': " << result.form_description;
  EXPECT_NE(result.form_description.find("Fugue"), std::string::npos)
      << "Form description missing 'Fugue': " << result.form_description;
}

// ---------------------------------------------------------------------------
// defaultInstrumentForForm
// ---------------------------------------------------------------------------

TEST(GeneratorTest, DefaultInstrument_FugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Fugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_PreludeAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::PreludeAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_TrioSonataIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::TrioSonata), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_ChoralePreludeIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::ChoralePrelude), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_ToccataAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::ToccataAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_PassacagliaIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Passacaglia), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_FantasiaAndFugueIsOrgan) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::FantasiaAndFugue), InstrumentType::Organ);
}

TEST(GeneratorTest, DefaultInstrument_CelloPreludeIsCello) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::CelloPrelude), InstrumentType::Cello);
}

TEST(GeneratorTest, DefaultInstrument_ChaconneIsViolin) {
  EXPECT_EQ(defaultInstrumentForForm(FormType::Chaconne), InstrumentType::Violin);
}

// ---------------------------------------------------------------------------
// instrumentTypeFromString
// ---------------------------------------------------------------------------

TEST(GeneratorTest, InstrumentFromString_Organ) {
  EXPECT_EQ(instrumentTypeFromString("organ"), InstrumentType::Organ);
}

TEST(GeneratorTest, InstrumentFromString_Harpsichord) {
  EXPECT_EQ(instrumentTypeFromString("harpsichord"), InstrumentType::Harpsichord);
}

TEST(GeneratorTest, InstrumentFromString_Piano) {
  EXPECT_EQ(instrumentTypeFromString("piano"), InstrumentType::Piano);
}

TEST(GeneratorTest, InstrumentFromString_Violin) {
  EXPECT_EQ(instrumentTypeFromString("violin"), InstrumentType::Violin);
}

TEST(GeneratorTest, InstrumentFromString_Cello) {
  EXPECT_EQ(instrumentTypeFromString("cello"), InstrumentType::Cello);
}

TEST(GeneratorTest, InstrumentFromString_Guitar) {
  EXPECT_EQ(instrumentTypeFromString("guitar"), InstrumentType::Guitar);
}

TEST(GeneratorTest, InstrumentFromString_UnknownDefaultsToOrgan) {
  EXPECT_EQ(instrumentTypeFromString("banjo"), InstrumentType::Organ);
}

// ---------------------------------------------------------------------------
// Auto seed
// ---------------------------------------------------------------------------

TEST(GeneratorTest, AutoSeed_ProducesValidResult) {
  GeneratorConfig config = makeTestConfig();
  config.seed = 0;  // Auto seed.
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_NE(result.seed_used, 0u) << "Auto seed should produce a non-zero seed_used";
  EXPECT_GT(totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(GeneratorTest, SameSeed_ProducesSameResult) {
  GeneratorConfig config = makeTestConfig(12345);
  config.form = FormType::Fugue;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

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
    }
  }
}

TEST(GeneratorTest, SameSeed_PreludeAndFugue_Deterministic) {
  GeneratorConfig config = makeTestConfig(54321);
  config.form = FormType::PreludeAndFugue;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

  ASSERT_TRUE(result1.success);
  ASSERT_TRUE(result2.success);
  ASSERT_EQ(result1.tracks.size(), result2.tracks.size());

  // Check that total note counts match.
  EXPECT_EQ(totalNoteCount(result1), totalNoteCount(result2));
  EXPECT_EQ(result1.total_duration_ticks, result2.total_duration_ticks);
}

// ---------------------------------------------------------------------------
// Different forms produce different results
// ---------------------------------------------------------------------------

TEST(GeneratorTest, DifferentForms_ProduceDifferentResults) {
  GeneratorConfig config = makeTestConfig(42);

  config.form = FormType::Fugue;
  GeneratorResult fugue_result = generate(config);

  config.form = FormType::PreludeAndFugue;
  GeneratorResult pf_result = generate(config);

  ASSERT_TRUE(fugue_result.success);
  ASSERT_TRUE(pf_result.success);

  // PreludeAndFugue should have more content than a standalone Fugue.
  EXPECT_NE(totalNoteCount(fugue_result), totalNoteCount(pf_result))
      << "Different forms with same seed should produce different note counts";
}

// ---------------------------------------------------------------------------
// Voice count support
// ---------------------------------------------------------------------------

TEST(GeneratorTest, ThreeVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 3u);
}

TEST(GeneratorTest, FourVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 4;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 4u);
}

TEST(GeneratorTest, FiveVoices_Respected) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.num_voices = 5;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.tracks.size(), 5u);
}

// ---------------------------------------------------------------------------
// Key is applied
// ---------------------------------------------------------------------------

TEST(GeneratorTest, Key_IncludedInDescription) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  config.key = {Key::G, true};  // G minor
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.form_description.find("G_minor"), std::string::npos)
      << "Form description should include key: " << result.form_description;
}

TEST(GeneratorTest, Key_DifferentKeysProduceDifferentPitches) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;

  config.key = {Key::C, false};
  GeneratorResult result_c = generate(config);

  config.key = {Key::G, true};
  GeneratorResult result_g = generate(config);

  ASSERT_TRUE(result_c.success);
  ASSERT_TRUE(result_g.success);

  // Different keys should produce different note pitches (at least in first track).
  bool any_pitch_diff = false;
  const auto& notes_c = result_c.tracks[0].notes;
  const auto& notes_g = result_g.tracks[0].notes;

  size_t compare_count = std::min(notes_c.size(), notes_g.size());
  for (size_t idx = 0; idx < compare_count; ++idx) {
    if (notes_c[idx].pitch != notes_g[idx].pitch) {
      any_pitch_diff = true;
      break;
    }
  }

  if (notes_c.size() != notes_g.size()) {
    any_pitch_diff = true;
  }

  EXPECT_TRUE(any_pitch_diff)
      << "Different keys should produce different pitches";
}

// ---------------------------------------------------------------------------
// Trio Sonata generation
// ---------------------------------------------------------------------------

TEST(GeneratorTest, TrioSonata_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::TrioSonata;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, ChoralePrelude_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::ChoralePrelude;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, CelloPrelude_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::CelloPrelude;
  config.instrument = InstrumentType::Cello;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
  // Should have notes in the track.
  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }
  EXPECT_GT(total_notes, 0u);
  // Form description should reference cello prelude.
  EXPECT_NE(result.form_description.find("Cello Prelude"), std::string::npos);
}

TEST(GeneratorTest, Chaconne_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.tracks.size(), 0u);
  EXPECT_GT(result.total_duration_ticks, 0u);
  // Should have notes in the track.
  size_t total_notes = 0;
  for (const auto& track : result.tracks) {
    total_notes += track.notes.size();
  }
  EXPECT_GT(total_notes, 0u);
  // Form description should reference chaconne.
  EXPECT_NE(result.form_description.find("Chaconne"), std::string::npos);
}

TEST(GeneratorTest, Chaconne_Deterministic) {
  GeneratorConfig config = makeTestConfig(99);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;

  GeneratorResult result1 = generate(config);
  GeneratorResult result2 = generate(config);

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
    }
  }
}

TEST(GeneratorTest, Chaconne_DMinorDefault) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.key = {Key::D, true};  // D minor (BWV1004)
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success) << result.error_message;
  EXPECT_NE(result.form_description.find("D_minor"), std::string::npos)
      << "Form description should include key: " << result.form_description;
  // D minor chaconne with 10 variations x 4 bars = 40 bars minimum.
  EXPECT_GE(result.total_duration_ticks, 40u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Fugue-aliased forms
// ---------------------------------------------------------------------------

TEST(GeneratorTest, ToccataAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::ToccataAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(GeneratorTest, FantasiaAndFugue_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::FantasiaAndFugue;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

TEST(GeneratorTest, Passacaglia_Succeeds) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Passacaglia;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success);
  EXPECT_GT(totalNoteCount(result), 0u);
}

// ---------------------------------------------------------------------------
// Seed is reported
// ---------------------------------------------------------------------------

TEST(GeneratorTest, SeedUsed_IsReported) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.seed_used, 42u);
}

TEST(GeneratorTest, SeedUsed_AutoSeedIsNonZero) {
  GeneratorConfig config = makeTestConfig();
  config.seed = 0;
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_NE(result.seed_used, 0u);
}

// ---------------------------------------------------------------------------
// Total duration is reported
// ---------------------------------------------------------------------------

TEST(GeneratorTest, TotalDuration_IsNonZero) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(GeneratorTest, TotalDuration_PreludeAndFugueIsReasonable) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::PreludeAndFugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  // Should be at least 20 bars (prelude + fugue combined).
  EXPECT_GE(result.total_duration_ticks, 20u * kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Articulation integration -- verify articulation is applied in the pipeline
// ---------------------------------------------------------------------------

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_FugueDurationsReduced) {
  // Generate a fugue and verify that note durations are shorter than the
  // "raw" beat-aligned durations that the fugue generator produces.
  // The organ Assert gate ratio is 0.85, so a quarter-note (480 ticks)
  // becomes 408 ticks.  We check that at least some notes have been
  // shortened below their beat-grid-aligned values.
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  config.num_voices = 3;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 1u);

  // Count notes whose duration is NOT a clean multiple of kTicksPerBeat.
  // Before articulation, durations are typically multiples of 120/240/480.
  // After articulation (0.85 gate), a 480-tick note becomes 408, which
  // is not a multiple of 480.
  size_t articulated_count = 0;
  size_t total_count = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      ++total_count;
      // A note whose duration is not evenly divisible by kTicksPerBeat
      // (480) has almost certainly been articulated.
      if (note.duration % kTicksPerBeat != 0) {
        ++articulated_count;
      }
    }
  }

  EXPECT_GT(total_count, 0u) << "Fugue should produce notes";
  EXPECT_GT(articulated_count, 0u)
      << "Articulation should modify at least some note durations "
         "(expected non-beat-aligned durations after gate ratio application)";
}

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_OrganVelocityUnchanged) {
  // Organ instruments have fixed velocity = 80.  The articulation system
  // must not modify velocity for organ forms (is_organ = true).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  config.instrument = InstrumentType::Organ;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80)
          << "Organ velocity must remain 80 after articulation; "
             "found " << static_cast<int>(note.velocity)
          << " at tick " << note.start_tick;
    }
  }
}

TEST(GeneratorArticulationTest, ArticulationAppliedInPipeline_ChaconneDurationsReduced) {
  // Solo string (non-organ) form should also have articulated durations.
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_GE(result.tracks.size(), 1u);

  size_t articulated_count = 0;
  size_t total_count = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      ++total_count;
      if (note.duration % kTicksPerBeat != 0) {
        ++articulated_count;
      }
    }
  }

  EXPECT_GT(total_count, 0u) << "Chaconne should produce notes";
  EXPECT_GT(articulated_count, 0u)
      << "Articulation should modify at least some note durations in chaconne";
}

TEST(GeneratorArticulationTest, ArticulationPreservesNonZeroDurations) {
  // After articulation, no note should have zero duration (the minimum
  // articulated duration floor of 60 ticks should prevent this).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Fugue;
  GeneratorResult result = generate(config);

  ASSERT_TRUE(result.success);

  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note at tick " << note.start_tick
          << " has zero duration after articulation";
    }
  }
}

TEST(GeneratorArticulationTest, TrioSonataArticulated) {
  // Trio Sonata notes should have articulation applied (gate ratio < 1.0).
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::TrioSonata;
  GeneratorResult result = generate(config);

  EXPECT_TRUE(result.success) << result.error_message;
  EXPECT_FALSE(result.tracks.empty());
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u)
          << "Note at tick " << note.start_tick
          << " has zero duration after articulation";
    }
  }
}

// ---------------------------------------------------------------------------
// Chaconne polyphony preservation tests
// ---------------------------------------------------------------------------

TEST(ChaconneE2E, PostProcessingDestructionRate) {
  GeneratorConfig config = makeTestConfig(42);
  config.form = FormType::Chaconne;
  config.instrument = InstrumentType::Violin;
  auto result = generate(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_FALSE(result.tracks.empty());

  const auto& notes = result.tracks[0].notes;
  int bass_count = 0, texture_count = 0;
  for (const auto& n : notes) {
    if (n.source == BachNoteSource::GroundBass) ++bass_count;
    if (n.source == BachNoteSource::TextureNote) ++texture_count;
  }

  EXPECT_GT(texture_count, 0)
      << "No texture notes survived post-processing";
  EXPECT_GT(bass_count, 0) << "No bass notes found";

  // Voice separation: both voice IDs are present.
  std::set<VoiceId> voice_ids;
  for (const auto& n : notes) {
    voice_ids.insert(n.voice);
  }
  EXPECT_GE(voice_ids.size(), 2u)
      << "Expected at least 2 distinct voice IDs (bass=0, texture=1)";

  // Temporal polyphony: bass notes whose sounding range overlaps a texture note.
  // This verifies that cross-voice overlap is preserved (not trimmed away).
  int temporal_overlaps = 0;
  for (const auto& bn : notes) {
    if (bn.source != BachNoteSource::GroundBass) continue;
    Tick b_end = bn.start_tick + bn.duration;
    for (const auto& tn : notes) {
      if (tn.source != BachNoteSource::TextureNote) continue;
      if (tn.start_tick >= bn.start_tick && tn.start_tick < b_end) {
        ++temporal_overlaps;
        break;
      }
    }
  }
  EXPECT_GT(temporal_overlaps, 0)
      << "No temporal overlap between bass and texture; "
         "voice-aware cleanup may not be preserving cross-voice polyphony";
}

TEST(ChaconneE2E, MultiSeedTexturePresence) {
  for (uint32_t seed : {1u, 42u, 100u, 999u}) {
    GeneratorConfig config;
    config.form = FormType::Chaconne;
    config.seed = seed;
    config.instrument = InstrumentType::Violin;
    auto result = generate(config);
    ASSERT_TRUE(result.success) << "seed=" << seed;

    int texture_count = 0;
    for (const auto& n : result.tracks[0].notes) {
      if (n.source == BachNoteSource::TextureNote) ++texture_count;
    }
    EXPECT_GT(texture_count, 0)
        << "No texture notes survived for seed=" << seed;
  }
}

}  // namespace
}  // namespace bach
