// Tests for ornament engine post-processing.

#include "ornament/ornament_engine.h"

#include <gtest/gtest.h>

#include "analysis/counterpoint_analyzer.h"
#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

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
  ctx.config.enable_turn = true;
  ctx.config.enable_appoggiatura = true;
  ctx.config.enable_pralltriller = true;
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

TEST(OrnamentTypeSelectionTest, StrongBeatFallsBackToMordentIfNoTrillOrAppoggiatura) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_appoggiatura = false;
  config.enable_vorschlag = false;
  config.enable_mordent = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Mordent);
}

TEST(OrnamentTypeSelectionTest, WeakBeatFallsBackToTrillIfNoMordentOrPralltriller) {
  auto note = makeNote(kTicksPerBeat, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = false;
  config.enable_pralltriller = false;
  config.enable_nachschlag = false;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Trill);
}

TEST(OrnamentTypeSelectionTest, FallsBackToTurnIfOnlyTurnEnabled) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = true;
  config.enable_appoggiatura = false;
  config.enable_pralltriller = false;
  config.enable_vorschlag = false;
  config.enable_nachschlag = false;
  config.enable_compound = false;

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
  ctx.config.enable_appoggiatura = false;
  ctx.config.enable_pralltriller = false;
  ctx.config.enable_vorschlag = false;
  ctx.config.enable_nachschlag = false;
  ctx.config.enable_compound = false;
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

// ---------------------------------------------------------------------------
// Appoggiatura
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, ApplyAppoggiatura) {
  // Appoggiatura creates 2 notes: upper neighbor then original pitch.
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = false;
  config.enable_appoggiatura = true;
  config.enable_pralltriller = false;
  config.ornament_density = 1.0f;

  // Place note on strong beat so selectOrnamentType picks appoggiatura.
  auto note = makeNote(0, 60, kTicksPerBeat);  // Beat 0, quarter note.
  std::vector<NoteEvent> notes = {note};

  OrnamentContext ctx;
  ctx.config = config;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;

  auto result = applyOrnaments(notes, ctx);

  ASSERT_EQ(result.size(), 2u);

  // First note: upper neighbor (chromatic half step, pitch + 1), 25% duration.
  EXPECT_EQ(result[0].pitch, 61);
  EXPECT_EQ(result[0].start_tick, 0u);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 4);
  EXPECT_EQ(result[0].voice, 0);
  EXPECT_EQ(result[0].velocity, 80);

  // Second note: original pitch, 75% duration.
  EXPECT_EQ(result[1].pitch, 60);
  EXPECT_EQ(result[1].start_tick, kTicksPerBeat / 4);
  EXPECT_EQ(result[1].duration, kTicksPerBeat - kTicksPerBeat / 4);
  EXPECT_EQ(result[1].voice, 0);
  EXPECT_EQ(result[1].velocity, 80);

  // Total duration preserved.
  EXPECT_EQ(result[0].duration + result[1].duration, kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// Pralltriller
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, ApplyPralltriller) {
  // Pralltriller creates 4 notes: upper, orig, upper, orig.
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = false;
  config.enable_appoggiatura = false;
  config.enable_pralltriller = true;
  config.ornament_density = 1.0f;

  // Place note on weak beat so selectOrnamentType picks pralltriller.
  auto note = makeNote(kTicksPerBeat, 64, kTicksPerBeat);  // Beat 1, quarter note.
  std::vector<NoteEvent> notes = {note};

  OrnamentContext ctx;
  ctx.config = config;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;

  auto result = applyOrnaments(notes, ctx);

  ASSERT_EQ(result.size(), 4u);

  const Tick short_dur = kTicksPerBeat / 12;  // 40 ticks each.
  const Tick last_dur = kTicksPerBeat - (short_dur * 3);

  // Note 0: upper neighbor (chromatic half step).
  EXPECT_EQ(result[0].pitch, 65);
  EXPECT_EQ(result[0].start_tick, kTicksPerBeat);
  EXPECT_EQ(result[0].duration, short_dur);

  // Note 1: original pitch.
  EXPECT_EQ(result[1].pitch, 64);
  EXPECT_EQ(result[1].start_tick, kTicksPerBeat + short_dur);
  EXPECT_EQ(result[1].duration, short_dur);

  // Note 2: upper neighbor (chromatic half step).
  EXPECT_EQ(result[2].pitch, 65);
  EXPECT_EQ(result[2].start_tick, kTicksPerBeat + short_dur * 2);
  EXPECT_EQ(result[2].duration, short_dur);

  // Note 3: original pitch (absorbs remainder).
  EXPECT_EQ(result[3].pitch, 64);
  EXPECT_EQ(result[3].start_tick, kTicksPerBeat + short_dur * 3);
  EXPECT_EQ(result[3].duration, last_dur);

  // Total duration preserved.
  Tick total = 0;
  for (const auto& sub : result) {
    total += sub.duration;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// Turn enabled by default
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, TurnIsNowEnabled) {
  OrnamentConfig config;
  EXPECT_TRUE(config.enable_turn);
}

// ---------------------------------------------------------------------------
// Appoggiatura and Pralltriller enabled by default
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, AppoggiaturaEnabledByDefault) {
  OrnamentConfig config;
  EXPECT_TRUE(config.enable_appoggiatura);
}

TEST(OrnamentEngineTest, PralltrillerEnabledByDefault) {
  OrnamentConfig config;
  EXPECT_TRUE(config.enable_pralltriller);
}

// ---------------------------------------------------------------------------
// selectOrnamentType: Appoggiatura on strong beat when trill disabled
// ---------------------------------------------------------------------------

TEST(OrnamentTypeSelectionTest, StrongBeatFallsBackToAppoggiaturaIfNoTrill) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // Beat 0 = strong.
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = false;
  config.enable_appoggiatura = true;
  config.enable_pralltriller = false;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Appoggiatura);
}

// ---------------------------------------------------------------------------
// selectOrnamentType: Pralltriller on weak beat when mordent disabled
// ---------------------------------------------------------------------------

TEST(OrnamentTypeSelectionTest, WeakBeatFallsBackToPralltrillerIfNoMordent) {
  auto note = makeNote(kTicksPerBeat, 60, kTicksPerBeat);  // Beat 1 = weak.
  OrnamentConfig config;
  config.enable_trill = false;
  config.enable_mordent = false;
  config.enable_turn = false;
  config.enable_appoggiatura = false;
  config.enable_pralltriller = true;

  EXPECT_EQ(selectOrnamentType(note, config), OrnamentType::Pralltriller);
}

// ---------------------------------------------------------------------------
// verifyOrnamentCounterpoint - No violation
// ---------------------------------------------------------------------------

TEST(VerifyOrnamentCounterpointTest, NoViolationPassesThrough) {
  // Voice 0: soprano at C5 (72), voice 1: alto at E4 (64).
  // A trill on voice 0 between C5 and D5 should not create parallel 5ths
  // or voice crossings with the alto at E4.
  std::vector<NoteEvent> original_notes = {
      makeNote(0, 72, kTicksPerBeat, 0),  // C5 in voice 0
  };

  // Simulate a trill: C5 -> D5 -> C5 -> D5 (ornament expansion).
  std::vector<NoteEvent> ornamented_notes;
  Tick sub_dur = kTicksPerBeat / 4;
  for (int idx = 0; idx < 4; ++idx) {
    uint8_t pitch = (idx % 2 == 0) ? 72 : 74;  // C5, D5 alternation
    ornamented_notes.push_back(
        makeNote(static_cast<Tick>(idx) * sub_dur, pitch, sub_dur, 0));
  }

  // Voice 1: sustained E4 (well below voice 0, no crossing possible).
  std::vector<NoteEvent> voice1_notes = {
      makeNote(0, 64, kTicksPerBeat, 1),  // E4
  };

  std::vector<std::vector<NoteEvent>> all_voices = {original_notes, voice1_notes};

  auto before_size = ornamented_notes.size();
  verifyOrnamentCounterpoint(ornamented_notes, original_notes, all_voices, 2);

  // No violations, so ornamented notes should remain unchanged.
  EXPECT_EQ(ornamented_notes.size(), before_size);
  // Verify the trill pitches are intact.
  EXPECT_EQ(ornamented_notes[0].pitch, 72);
  EXPECT_EQ(ornamented_notes[1].pitch, 74);
  EXPECT_EQ(ornamented_notes[2].pitch, 72);
  EXPECT_EQ(ornamented_notes[3].pitch, 74);
}

// ---------------------------------------------------------------------------
// verifyOrnamentCounterpoint - Parallel fifths reverted
// ---------------------------------------------------------------------------

TEST(VerifyOrnamentCounterpointTest, ParallelFifthsReverted) {
  // Set up a scenario where an ornament creates parallel 5ths.
  // Voice 0 original: G4 (67) on beat 0, then C5 (72) on beat 1.
  // Voice 1: C4 (60) on beat 0, then F4 (65) on beat 1.
  // Interval beat 0: G4-C4 = 7 semitones = P5.
  // If ornament changes voice 0 beat 1 to F5 (77), interval = F5-F4 = 12 = P8.
  // That's not parallel 5ths. Let's construct a clearer case.
  //
  // Voice 0 original: C5 (72) on beat 0, then D5 (74) on beat 1.
  // Voice 1: F4 (65) on beat 0, then G4 (67) on beat 1.
  // Beat 0: C5-F4 = 7 = P5. Beat 1: D5-G4 = 7 = P5. Both move up by 2.
  // This IS parallel 5ths. If the ornament on voice 0 changes the note at
  // beat 1 to D5 (from some other pitch), the ornament creates the parallel.
  //
  // Original voice 0: C5 on beat 0, then A4 (69) on beat 1 (no parallel 5ths).
  // Beat 0: C5-F4 = 7 (P5). Beat 1: A4-G4 = 2 (not perfect).
  // Ornament on beat 1 replaces A4 with D5 (74) -> creates P5 parallel.

  std::vector<NoteEvent> original_v0 = {
      makeNote(0, 72, kTicksPerBeat, 0),           // C5 at beat 0
      makeNote(kTicksPerBeat, 69, kTicksPerBeat, 0),  // A4 at beat 1
  };

  // Ornament changes the second note from A4 to D5 (simulating an ornament
  // that shifts pitch into a parallel 5th).
  std::vector<NoteEvent> ornamented_v0 = {
      makeNote(0, 72, kTicksPerBeat, 0),              // C5 unchanged
      makeNote(kTicksPerBeat, 74, kTicksPerBeat, 0),  // D5 (ornament result)
  };

  std::vector<NoteEvent> voice1 = {
      makeNote(0, 65, kTicksPerBeat, 1),           // F4 at beat 0
      makeNote(kTicksPerBeat, 67, kTicksPerBeat, 1),  // G4 at beat 1
  };

  std::vector<std::vector<NoteEvent>> all_voices = {original_v0, voice1};

  // Verify: the ornamented version should have parallel 5ths.
  std::vector<NoteEvent> check_notes;
  for (const auto& note : ornamented_v0) check_notes.push_back(note);
  for (const auto& note : voice1) check_notes.push_back(note);
  ASSERT_GT(countParallelPerfect(check_notes, 2), 0u);

  // Verify: the original version should NOT have parallel 5ths.
  std::vector<NoteEvent> ref_notes;
  for (const auto& note : original_v0) ref_notes.push_back(note);
  for (const auto& note : voice1) ref_notes.push_back(note);
  ASSERT_EQ(countParallelPerfect(ref_notes, 2), 0u);

  verifyOrnamentCounterpoint(ornamented_v0, original_v0, all_voices, 2);

  // The second note should be reverted to the original A4 (69).
  ASSERT_EQ(ornamented_v0.size(), 2u);
  EXPECT_EQ(ornamented_v0[0].pitch, 72);  // First note unchanged.
  EXPECT_EQ(ornamented_v0[1].pitch, 69);  // Reverted to original A4.
}

// ---------------------------------------------------------------------------
// verifyOrnamentCounterpoint - Voice crossing reverted
// ---------------------------------------------------------------------------

TEST(VerifyOrnamentCounterpointTest, VoiceCrossingReverted) {
  // Voice 0 (soprano) original: E5 (76) -- well above voice 1.
  // Voice 1 (alto): C5 (72).
  // Ornament on voice 0 creates a sub-note at B3 (59), which crosses below
  // voice 1's C5 (72).

  std::vector<NoteEvent> original_v0 = {
      makeNote(0, 76, kTicksPerBeat, 0),  // E5
  };

  // Ornament: two sub-notes, first dips below the alto.
  std::vector<NoteEvent> ornamented_v0 = {
      makeNote(0, 59, kTicksPerBeat / 2, 0),                  // B3 (below alto)
      makeNote(kTicksPerBeat / 2, 76, kTicksPerBeat / 2, 0),  // E5 (original)
  };

  std::vector<NoteEvent> voice1 = {
      makeNote(0, 72, kTicksPerBeat, 1),  // C5
  };

  std::vector<std::vector<NoteEvent>> all_voices = {original_v0, voice1};

  // Verify: ornamented version should have voice crossing.
  std::vector<NoteEvent> check_notes;
  for (const auto& note : ornamented_v0) check_notes.push_back(note);
  for (const auto& note : voice1) check_notes.push_back(note);
  ASSERT_GT(countVoiceCrossings(check_notes, 2), 0u);

  // Verify: original version should NOT have voice crossing.
  std::vector<NoteEvent> ref_notes;
  for (const auto& note : original_v0) ref_notes.push_back(note);
  for (const auto& note : voice1) ref_notes.push_back(note);
  ASSERT_EQ(countVoiceCrossings(ref_notes, 2), 0u);

  verifyOrnamentCounterpoint(ornamented_v0, original_v0, all_voices, 2);

  // Should revert to original single note.
  ASSERT_EQ(ornamented_v0.size(), 1u);
  EXPECT_EQ(ornamented_v0[0].pitch, 76);  // Reverted to E5.
  EXPECT_EQ(ornamented_v0[0].duration, kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// verifyOrnamentCounterpoint - Empty all_voices skips check
// ---------------------------------------------------------------------------

TEST(VerifyOrnamentCounterpointTest, EmptyAllVoicesSkipsCheck) {
  std::vector<NoteEvent> original_notes = {
      makeNote(0, 72, kTicksPerBeat, 0),
  };

  // Even if the ornament is "bad", empty all_voices means no verification.
  std::vector<NoteEvent> ornamented_notes = {
      makeNote(0, 59, kTicksPerBeat / 2, 0),
      makeNote(kTicksPerBeat / 2, 72, kTicksPerBeat / 2, 0),
  };

  std::vector<std::vector<NoteEvent>> empty_voices;

  auto before_size = ornamented_notes.size();
  verifyOrnamentCounterpoint(ornamented_notes, original_notes, empty_voices, 0);

  // Notes remain unchanged because verification was skipped.
  EXPECT_EQ(ornamented_notes.size(), before_size);
}

// ---------------------------------------------------------------------------
// applyOrnaments - Backward compatible (no all_voice_notes)
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, ApplyOrnamentsBackwardCompatible) {
  // The existing single-voice API must still work the same way.
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat, 0),
      makeNote(kTicksPerBeat, 62, kTicksPerBeat, 0),
  };

  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);

  // Two-argument overload (original API).
  auto result_legacy = applyOrnaments(notes, ctx);

  // Three-argument overload with empty all_voice_notes (should behave identically).
  std::vector<std::vector<NoteEvent>> empty_voices;
  auto result_new = applyOrnaments(notes, ctx, empty_voices);

  ASSERT_EQ(result_legacy.size(), result_new.size());
  for (size_t idx = 0; idx < result_legacy.size(); ++idx) {
    EXPECT_EQ(result_legacy[idx].pitch, result_new[idx].pitch);
    EXPECT_EQ(result_legacy[idx].start_tick, result_new[idx].start_tick);
    EXPECT_EQ(result_legacy[idx].duration, result_new[idx].duration);
  }
}

// ---------------------------------------------------------------------------
// applyOrnaments with all_voice_notes - Reverts violating ornaments
// ---------------------------------------------------------------------------

TEST(OrnamentEngineTest, ApplyOrnamentsWithCounterpointVerification) {
  // Create a scenario where voice 0 and voice 1 are in a valid position,
  // and verify that the overload with all_voice_notes doesn't break valid
  // ornaments while still providing the verification mechanism.
  std::vector<NoteEvent> notes_v0;
  for (uint32_t idx = 0; idx < 4; ++idx) {
    notes_v0.push_back(makeNote(idx * kTicksPerBeat, 72, kTicksPerBeat, 0));
  }

  std::vector<NoteEvent> notes_v1;
  for (uint32_t idx = 0; idx < 4; ++idx) {
    notes_v1.push_back(makeNote(idx * kTicksPerBeat, 48, kTicksPerBeat, 1));
  }

  auto ctx = makeContext(VoiceRole::Respond, 42, 1.0f);
  std::vector<std::vector<NoteEvent>> all_voices = {notes_v0, notes_v1};

  auto result = applyOrnaments(notes_v0, ctx, all_voices);

  // Result should not be empty (ornaments were applied or passed through).
  EXPECT_FALSE(result.empty());

  // All notes should still have voice 0.
  for (const auto& note : result) {
    EXPECT_EQ(note.voice, 0);
  }
}

// ---------------------------------------------------------------------------
// Scale-aware ornament neighbors [Task B]
// ---------------------------------------------------------------------------

TEST(ScaleAwareOrnamentTest, CMajorETrillUsesF) {
  // In C major, E(64) upper neighbor should be F(65) -- a half step,
  // not the chromatic G(66).
  KeySignature key_sig{Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar * 2,
                                                    HarmonicResolution::Beat);

  auto note = makeNote(0, 64, kTicksPerBeat);  // E4 on beat 0

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.enable_mordent = false;
  ctx.config.enable_turn = false;
  ctx.config.enable_appoggiatura = false;
  ctx.config.enable_pralltriller = false;
  ctx.config.enable_vorschlag = false;
  ctx.config.enable_nachschlag = false;
  ctx.config.enable_compound = false;
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;
  ctx.timeline = &timeline;

  auto result = applyOrnaments({note}, ctx);

  // Should produce trill notes alternating between E4(64) and F4(65),
  // with Nachschlag lower neighbor Eb4(63) near the end.
  ASSERT_GT(result.size(), 1u);
  for (const auto& sub : result) {
    EXPECT_TRUE(sub.pitch == 64 || sub.pitch == 65 || sub.pitch == 63)
        << "Expected E4(64), F4(65), or Eb4(63), got " << static_cast<int>(sub.pitch);
  }
}

TEST(ScaleAwareOrnamentTest, AMinorGSharpTrillUsesA) {
  // In A harmonic minor, G#(68) upper neighbor should be A(69).
  KeySignature key_sig{Key::A, true};
  auto timeline = HarmonicTimeline::createStandard(key_sig, kTicksPerBar * 2,
                                                    HarmonicResolution::Beat);

  auto note = makeNote(0, 68, kTicksPerBeat);  // G#4 on beat 0

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.enable_mordent = false;
  ctx.config.enable_turn = false;
  ctx.config.enable_appoggiatura = false;
  ctx.config.enable_pralltriller = false;
  ctx.config.enable_vorschlag = false;
  ctx.config.enable_nachschlag = false;
  ctx.config.enable_compound = false;
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;
  ctx.timeline = &timeline;

  auto result = applyOrnaments({note}, ctx);

  // Trill between G#4(68) and A4(69), with Nachschlag lower neighbor G4(67).
  ASSERT_GT(result.size(), 1u);
  for (const auto& sub : result) {
    EXPECT_TRUE(sub.pitch == 68 || sub.pitch == 69 || sub.pitch == 67)
        << "Expected G#4(68), A4(69), or G4(67), got " << static_cast<int>(sub.pitch);
  }
}

TEST(ScaleAwareOrnamentTest, LegacyWithoutTimelineUsesHalfStep) {
  // Without timeline, legacy behavior: upper neighbor = pitch + 1 (chromatic).
  auto note = makeNote(0, 64, kTicksPerBeat);

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.enable_mordent = false;
  ctx.config.enable_turn = false;
  ctx.config.enable_appoggiatura = false;
  ctx.config.enable_pralltriller = false;
  ctx.config.enable_vorschlag = false;
  ctx.config.enable_nachschlag = false;
  ctx.config.enable_compound = false;
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;
  ctx.timeline = nullptr;  // No timeline

  auto result = applyOrnaments({note}, ctx);

  ASSERT_GT(result.size(), 1u);
  for (const auto& sub : result) {
    // Legacy chromatic: E4(64) + 1 = F4(65), or Nachschlag lower = Eb4(63).
    EXPECT_TRUE(sub.pitch == 64 || sub.pitch == 65 || sub.pitch == 63)
        << "Expected E4(64), F4(65), or Eb4(63), got " << static_cast<int>(sub.pitch);
  }
}

// ---------------------------------------------------------------------------
// Cadence trill obligation [Task D]
// ---------------------------------------------------------------------------

TEST(CadenceTrillTest, TrillForcedBeforeCadence) {
  // Place a note 1 beat before a cadence tick; it should be trilled.
  Tick cadence_tick = kTicksPerBar;
  auto note = makeNote(cadence_tick - kTicksPerBeat, 67, kTicksPerBeat, 0);

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.ornament_density = 0.0f;  // Zero base density -- only cadence forces
  ctx.role = VoiceRole::Assert;
  ctx.seed = 42;
  ctx.cadence_ticks = {cadence_tick};

  auto result = applyOrnaments({note}, ctx);

  // With 95% probability and seed=42, should be ornamented (trill).
  EXPECT_GT(result.size(), 1u) << "Expected trill at cadence, got pass-through";
}

TEST(CadenceTrillTest, GroundVoiceNotTrillAtCadence) {
  Tick cadence_tick = kTicksPerBar;
  auto note = makeNote(cadence_tick - kTicksPerBeat, 48, kTicksPerBeat, 0);

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.ornament_density = 0.0f;
  ctx.role = VoiceRole::Ground;  // Ground voice
  ctx.seed = 42;
  ctx.cadence_ticks = {cadence_tick};

  auto result = applyOrnaments({note}, ctx);

  // Ground voice never gets ornaments.
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 48);
}

TEST(CadenceTrillTest, PropelVoiceNotForcedAtCadence) {
  // Only Assert/Respond get cadence trill obligation.
  Tick cadence_tick = kTicksPerBar;
  auto note = makeNote(cadence_tick - kTicksPerBeat, 67, kTicksPerBeat, 0);

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.ornament_density = 0.0f;
  ctx.role = VoiceRole::Propel;
  ctx.seed = 42;
  ctx.cadence_ticks = {cadence_tick};

  auto result = applyOrnaments({note}, ctx);

  // Propel doesn't get forced cadence trill (density=0 means no ornaments).
  ASSERT_EQ(result.size(), 1u);
}

TEST(CadenceTrillTest, NoCadenceTicksNoForce) {
  auto note = makeNote(0, 67, kTicksPerBeat, 0);

  OrnamentContext ctx;
  ctx.config.enable_trill = true;
  ctx.config.ornament_density = 0.0f;
  ctx.role = VoiceRole::Assert;
  ctx.seed = 42;
  // No cadence_ticks set

  auto result = applyOrnaments({note}, ctx);

  // With zero density and no cadence, nothing should happen.
  ASSERT_EQ(result.size(), 1u);
}

}  // namespace
}  // namespace bach
