// Tests for ornament engine post-processing.

#include "ornament/ornament_engine.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

/// Helper: create a note at a given tick with specified pitch and duration.
NoteEvent makeNote(Tick start, uint8_t pitch, Tick duration, uint8_t voice = 0) {
  NoteEvent note;
  note.start_tick = start;
  note.pitch = pitch;
  note.duration = duration;
  note.velocity = 80;
  note.voice = voice;
  return note;
}

/// Helper: create a default ornament context with a given role and seed.
OrnamentContext makeContext(VoiceRole role, uint32_t seed = 42,
                           float density = 1.0f) {
  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.enable_mordent = true;
  ctx.config.enable_turn = false;
  ctx.config.ornament_density = density;
  ctx.role = role;
  ctx.seed = seed;
  return ctx;
}

// ---------------------------------------------------------------------------
// isEligibleForOrnament
// ---------------------------------------------------------------------------

TEST(OrnamentEligibilityTest, GroundVoiceNeverEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  EXPECT_FALSE(isEligibleForOrnament(note, VoiceRole::Ground));
}

TEST(OrnamentEligibilityTest, QuarterNoteEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // 480 ticks
  EXPECT_TRUE(isEligibleForOrnament(note, VoiceRole::Respond));
}

TEST(OrnamentEligibilityTest, EighthNoteEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat / 2);  // 240 ticks = minimum
  EXPECT_TRUE(isEligibleForOrnament(note, VoiceRole::Propel));
}

TEST(OrnamentEligibilityTest, SixteenthNoteNotEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat / 4);  // 120 ticks = too short
  EXPECT_FALSE(isEligibleForOrnament(note, VoiceRole::Respond));
}

TEST(OrnamentEligibilityTest, AssertVoiceEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  EXPECT_TRUE(isEligibleForOrnament(note, VoiceRole::Assert));
}

TEST(OrnamentEligibilityTest, PropelVoiceEligible) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  EXPECT_TRUE(isEligibleForOrnament(note, VoiceRole::Propel));
}

// ---------------------------------------------------------------------------
// selectOrnamentType
// ---------------------------------------------------------------------------

TEST(OrnamentTypeSelectionTest, StrongBeatPrefersTrill) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // Beat 0 = strong
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Trill);
}

TEST(OrnamentTypeSelectionTest, Beat2IsStrongBeat) {
  // Beat 2 starts at tick 960 within a bar.
  auto note = makeNote(kTicksPerBeat * 2, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Trill);
}

TEST(OrnamentTypeSelectionTest, WeakBeatPrefersMordent) {
  auto note = makeNote(kTicksPerBeat, 60, kTicksPerBeat);  // Beat 1 = weak
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Mordent);
}

TEST(OrnamentTypeSelectionTest, Beat3IsWeakBeat) {
  auto note = makeNote(kTicksPerBeat * 3, 60, kTicksPerBeat);  // Beat 3 = weak
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Mordent);
}

TEST(OrnamentTypeSelectionTest, StrongBeatFallsBackToMordentIfNoTrill) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Mordent);
}

TEST(OrnamentTypeSelectionTest, WeakBeatFallsBackToTrillIfNoMordent) {
  auto note = makeNote(kTicksPerBeat, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = false;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Trill);
}

TEST(OrnamentTypeSelectionTest, FallsBackToTurnIfOnlyTurnEnabled) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Turn);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Ground voice
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, GroundVoiceReturnsUnchanged) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 48, kTicksPerBeat),
      makeNote(kTicksPerBeat, 50, kTicksPerBeat),
  };

  auto ctx = makeContext(VoiceRole::Ground);
  auto result = applyOrnaments(notes, ctx);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 48);
  EXPECT_EQ(result[1].pitch, 50);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Density = 0
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, ZeroDensityReturnsUnchanged) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat),
      makeNote(kTicksPerBeat, 62, kTicksPerBeat),
  };

  auto ctx = makeContext(VoiceRole::Respond, 42, 0.0f);
  auto result = applyOrnaments(notes, ctx);

  ASSERT_EQ(result.size(), 2u);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Full density (all eligible notes get ornamented)
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, FullDensityOrnamentsSomeNotes) {
  // With density = 1.0, all eligible notes should be ornamented.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat),  // Eligible, strong beat
  };

  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);
  auto result = applyOrnaments(notes, ctx);

  // A single quarter note with full density should produce more than 1 note.
  EXPECT_GT(result.size(), 1u);
}

TEST(OrnamentEngineTest, ShortNotesPassThrough) {
  // 16th notes are too short for ornamentation.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat / 4),
      makeNote(120, 62, kTicksPerBeat / 4),
  };

  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);
  auto result = applyOrnaments(notes, ctx);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[1].pitch, 62);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Assert voice has reduced density
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, AssertVoiceReducedDensity) {
  // With many notes, Assert should ornament fewer than Respond at same density.
  // We test by comparing output sizes.
  std::vector<NoteEvent> notes;
  for (uint32_t idx = 0; idx < 20; ++idx) {
    notes.push_back(makeNote(idx * kTicksPerBeat, 60, kTicksPerBeat));
  }

  auto assert_ctx = makeContext(VoiceRole::Assert, 100, 0.5f);
  auto respond_ctx = makeContext(VoiceRole::Respond, 100, 0.5f);

  auto assert_result = applyOrnaments(notes, assert_ctx);
  auto respond_result = applyOrnaments(notes, respond_ctx);

  // Assert density = 0.5 * 0.5 = 0.25, Respond density = 0.5.
  // Assert should produce fewer ornamented notes (smaller output).
  // Both should have more notes than input (some ornamented).
  EXPECT_GE(respond_result.size(), assert_result.size());
}

// ---------------------------------------------------------------------------
// applyOrnaments - No ornament types enabled
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, NoTypesEnabledReturnsUnchanged) {
  std::vector<NoteEvent> notes = {makeNote(0, 60, kTicksPerBeat)};

  OrnamentContext ctx;
  ctx.config.enable_trill = false;
  ctx.config.enable_mordent = false;
  ctx.config.enable_turn = false;
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;

  auto result = applyOrnaments(notes, ctx);
  ASSERT_EQ(result.size(), 1u);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Determinism
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, DeterministicWithSameSeed) {
  std::vector<NoteEvent> notes;
  for (uint32_t idx = 0; idx < 10; ++idx) {
    notes.push_back(makeNote(idx * kTicksPerBeat, 60 + (idx % 12), kTicksPerBeat));
  }

  auto ctx = makeContext(VoiceRole::Respond, 12345, 0.5f);
  auto result1 = applyOrnaments(notes, ctx);
  auto result2 = applyOrnaments(notes, ctx);

  ASSERT_EQ(result1.size(), result2.size());
  for (size_t idx = 0; idx < result1.size(); ++idx) {
    EXPECT_EQ(result1[idx].pitch, result2[idx].pitch);
    EXPECT_EQ(result1[idx].start_tick, result2[idx].start_tick);
    EXPECT_EQ(result1[idx].duration, result2[idx].duration);
  }
}

TEST(OrnamentEngineTest, DifferentSeedsProduceDifferentResults) {
  std::vector<NoteEvent> notes;
  for (uint32_t idx = 0; idx < 20; ++idx) {
    notes.push_back(makeNote(idx * kTicksPerBeat, 60, kTicksPerBeat));
  }

  auto ctx1 = makeContext(VoiceRole::Respond, 1, 0.5f);
  auto ctx2 = makeContext(VoiceRole::Respond, 999, 0.5f);

  auto result1 = applyOrnaments(notes, ctx1);
  auto result2 = applyOrnaments(notes, ctx2);

  // Different seeds should produce different ornamentation patterns.
  // With enough notes, the output sizes should differ.
  bool any_difference = (result1.size() != result2.size());
  if (!any_difference) {
    for (size_t idx = 0; idx < result1.size(); ++idx) {
      if (result1[idx].pitch != result2[idx].pitch ||
          result1[idx].start_tick != result2[idx].start_tick) {
        any_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(any_difference);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Empty input
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, EmptyInputReturnsEmpty) {
  std::vector<NoteEvent> notes;
  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);
  auto result = applyOrnaments(notes, ctx);

  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// applyOrnaments - Total duration preservation
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, TotalDurationPreserved) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat),
      makeNote(kTicksPerBeat, 64, kTicksPerBeat),
      makeNote(kTicksPerBeat * 2, 67, kTicksPerBeat),
  };

  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);
  auto result = applyOrnaments(notes, ctx);

  // The last note's end tick should not exceed the original sequence end.
  Tick original_end = kTicksPerBeat * 3;
  for (const auto& note : result) {
    Tick note_end = note.start_tick + note.duration;
    EXPECT_LE(note_end, original_end);
  }
}

}  // namespace
}  // namespace bach
