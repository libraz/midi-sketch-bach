// Tests for generator.h -- unified generation routing, prelude+fugue
// concatenation, instrument auto-detection, determinism, and form routing.

#include "generator.h"

#include <gtest/gtest.h>

#include <algorithm>
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
// Stub forms return failure gracefully
// ---------------------------------------------------------------------------

TEST(GeneratorTest, TrioSonata_ReturnsStub) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::TrioSonata;
  GeneratorResult result = generate(config);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
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

TEST(GeneratorTest, Chaconne_ReturnsStub) {
  GeneratorConfig config = makeTestConfig();
  config.form = FormType::Chaconne;
  GeneratorResult result = generate(config);

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
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

}  // namespace
}  // namespace bach
