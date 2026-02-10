// Tests for fugue/middle_entry.h -- middle entry generation for fugue
// development sections.

#include "fugue/middle_entry.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "fugue/voice_registers.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// Helper: build a subject from pitches (all quarter notes, starting at tick 0).
Subject makeSubjectQuarters(const std::vector<uint8_t>& pitches, Key key = Key::C) {
  Subject subject;
  subject.key = key;
  Tick current_tick = 0;
  for (const auto& pitch : pitches) {
    subject.notes.push_back({current_tick, kTicksPerBeat, pitch, 80, 0});
    current_tick += kTicksPerBeat;
  }
  subject.length_ticks = current_tick;
  return subject;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(MiddleEntryTest, GenerateMiddleEntry_HasNotes) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, kTicksPerBar * 8, 1);
  EXPECT_FALSE(entry.notes.empty());
  EXPECT_EQ(entry.notes.size(), subject.notes.size());
}

TEST(MiddleEntryTest, GenerateMiddleEntry_CorrectKey) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, 0, 0);
  EXPECT_EQ(entry.key, Key::G);
}

TEST(MiddleEntryTest, GenerateMiddleEntry_CorrectTiming) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Tick start = kTicksPerBar * 4;
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, start, 0);

  EXPECT_EQ(entry.start_tick, start);
  EXPECT_EQ(entry.end_tick, start + subject.length_ticks);
}

TEST(MiddleEntryTest, GenerateMiddleEntry_CorrectVoice) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  VoiceId target_voice = 2;
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, 0, target_voice);

  for (const auto& note : entry.notes) {
    EXPECT_EQ(note.voice, target_voice);
  }
}

TEST(MiddleEntryTest, GenerateMiddleEntry_Transposed) {
  // Subject in C: C4 D4 E4 F4. Middle entry in G (7 semitones up).
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, 0, 1);

  ASSERT_EQ(entry.notes.size(), 4u);
  // Each note transposed up by 7 semitones (G - C = 7).
  EXPECT_EQ(entry.notes[0].pitch, 67);  // C4+7 = G4
  EXPECT_EQ(entry.notes[1].pitch, 69);  // D4+7 = A4
  EXPECT_EQ(entry.notes[2].pitch, 71);  // E4+7 = B4
  EXPECT_EQ(entry.notes[3].pitch, 72);  // F4+7 = C5
}

TEST(MiddleEntryTest, GenerateMiddleEntry_SameNoteCount) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  MiddleEntry entry = generateMiddleEntry(subject, Key::F, 0, 0);
  EXPECT_EQ(entry.notes.size(), subject.notes.size());
}

TEST(MiddleEntryTest, GenerateMiddleEntry_SameDuration) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Tick start = kTicksPerBar * 2;
  MiddleEntry entry = generateMiddleEntry(subject, Key::D, start, 1);
  EXPECT_EQ(entry.durationTicks(), subject.length_ticks);
}

TEST(MiddleEntryTest, GenerateMiddleEntry_EmptySubject) {
  Subject subject;
  subject.key = Key::C;
  subject.length_ticks = 0;

  MiddleEntry entry = generateMiddleEntry(subject, Key::G, kTicksPerBar, 0);

  EXPECT_TRUE(entry.notes.empty());
  EXPECT_EQ(entry.key, Key::G);
  EXPECT_EQ(entry.start_tick, kTicksPerBar);
  EXPECT_EQ(entry.end_tick, kTicksPerBar);  // start + 0
}

TEST(MiddleEntryTest, GenerateMiddleEntry_SameKey) {
  // When target == subject key, pitches should be unchanged.
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::C, 0, 1);

  ASSERT_EQ(entry.notes.size(), subject.notes.size());
  for (size_t idx = 0; idx < entry.notes.size(); ++idx) {
    EXPECT_EQ(entry.notes[idx].pitch, subject.notes[idx].pitch);
  }
}

// ---------------------------------------------------------------------------
// Tick offset verification
// ---------------------------------------------------------------------------

TEST(MiddleEntryTest, GenerateMiddleEntry_TicksOffsetCorrectly) {
  // Subject starts at tick 0 with quarter notes.
  Subject subject = makeSubjectQuarters({60, 62, 64});
  Tick start = kTicksPerBar * 10;
  MiddleEntry entry = generateMiddleEntry(subject, Key::C, start, 0);

  ASSERT_EQ(entry.notes.size(), 3u);
  EXPECT_EQ(entry.notes[0].start_tick, start);
  EXPECT_EQ(entry.notes[1].start_tick, start + kTicksPerBeat);
  EXPECT_EQ(entry.notes[2].start_tick, start + 2 * kTicksPerBeat);
}

TEST(MiddleEntryTest, GenerateMiddleEntry_DurationsPreserved) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, kTicksPerBar, 0);

  ASSERT_EQ(entry.notes.size(), subject.notes.size());
  for (size_t idx = 0; idx < entry.notes.size(); ++idx) {
    EXPECT_EQ(entry.notes[idx].duration, subject.notes[idx].duration);
  }
}

// ---------------------------------------------------------------------------
// Transposition to different keys
// ---------------------------------------------------------------------------

TEST(MiddleEntryTest, GenerateMiddleEntry_TransposeDown) {
  // Subject in G (key=7): G4 A4 B4. Middle entry in C (key=0) => -7 semitones.
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({67, 69, 71});
  subject.key = Key::G;

  MiddleEntry entry = generateMiddleEntry(subject, Key::C, 0, 1);
  ASSERT_EQ(entry.notes.size(), 3u);
  EXPECT_EQ(entry.notes[0].pitch, 60);  // G4-7 = C4
  EXPECT_EQ(entry.notes[1].pitch, 62);  // A4-7 = D4
  EXPECT_EQ(entry.notes[2].pitch, 64);  // B4-7 = E4
}

TEST(MiddleEntryTest, GenerateMiddleEntry_SubdominantKey) {
  // Subject in C: C4 E4 G4. Middle entry in F (key=5) => +5 semitones.
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 64, 67});
  MiddleEntry entry = generateMiddleEntry(subject, Key::F, 0, 1);

  ASSERT_EQ(entry.notes.size(), 3u);
  EXPECT_EQ(entry.notes[0].pitch, 65);  // C4+5 = F4
  EXPECT_EQ(entry.notes[1].pitch, 69);  // E4+5 = A4
  EXPECT_EQ(entry.notes[2].pitch, 72);  // G4+5 = C5
}

// ---------------------------------------------------------------------------
// False entry tests
// ---------------------------------------------------------------------------

TEST(FalseEntryTest, FalseEntryHasCorrectLength) {
  // 6-note subject, default quote_notes=3 -> 3 quoted + 3 divergent = 6.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 69});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 0);

  // 3 quoted notes + 3 divergent notes = 6 total.
  EXPECT_EQ(entry.notes.size(), 6u);
}

TEST(FalseEntryTest, FalseEntryDivergesFromSubject) {
  // Subject: C4 D4 E4 F4 G4 A4 (ascending stepwise).
  // Quoted: C4 D4 E4. Last interval = E4-D4 = +2 (ascending).
  // Divergent notes should go in the OPPOSITE direction (descending).
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 69});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 0);

  ASSERT_GE(entry.notes.size(), 4u);

  // The 4th note (index 3) is the first divergent note, should be lower than
  // the 3rd note (last quoted, E4=64) since the subject was ascending.
  EXPECT_LT(entry.notes[3].pitch, entry.notes[2].pitch)
      << "First divergent note should move opposite to subject direction";

  // Verify divergent notes are NOT the same as the subject continuation.
  // Subject would continue with F4=65, but divergent should go down from E4.
  EXPECT_NE(entry.notes[3].pitch, 65u)
      << "Divergent note should differ from subject continuation (F4)";
}

TEST(FalseEntryTest, FalseEntrySourceIsFalseEntry) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 0);

  ASSERT_FALSE(entry.notes.empty());
  for (const auto& note : entry.notes) {
    EXPECT_EQ(note.source, BachNoteSource::FalseEntry)
        << "All false entry notes must have BachNoteSource::FalseEntry";
  }
}

TEST(FalseEntryTest, FalseEntryTransposedToKey) {
  // Subject in C: C4 D4 E4 F4 G4. False entry in G (+7 semitones).
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  MiddleEntry entry = generateFalseEntry(subject, Key::G, 0, 1, 3, 3);

  ASSERT_GE(entry.notes.size(), 3u);
  EXPECT_EQ(entry.key, Key::G);

  // Quoted notes should be transposed up by 7 semitones.
  EXPECT_EQ(entry.notes[0].pitch, 67u);  // C4+7 = G4
  EXPECT_EQ(entry.notes[1].pitch, 69u);  // D4+7 = A4
  EXPECT_EQ(entry.notes[2].pitch, 71u);  // E4+7 = B4
}

TEST(FalseEntryTest, FalseEntryDefaultQuoteThreeNotes) {
  // Default quote_notes=3, with a 5-note subject -> 3 quoted + 2 divergent.
  // (subject <= 3 notes gives 2 divergent, but 5 notes > 3 gives 3 divergent).
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 1);

  // With 5-note subject (>3), default is 3 quoted + 3 divergent = 6.
  EXPECT_EQ(entry.notes.size(), 6u);

  // Verify the first 3 notes match the transposed subject (same key = same pitches).
  EXPECT_EQ(entry.notes[0].pitch, 60u);  // C4
  EXPECT_EQ(entry.notes[1].pitch, 62u);  // D4
  EXPECT_EQ(entry.notes[2].pitch, 64u);  // E4
}

TEST(FalseEntryTest, FalseEntryCorrectVoiceAssignment) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  VoiceId target_voice = 2;
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, target_voice);

  for (const auto& note : entry.notes) {
    EXPECT_EQ(note.voice, target_voice);
  }
}

TEST(FalseEntryTest, FalseEntryCorrectTiming) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  Tick start = kTicksPerBar * 5;
  MiddleEntry entry = generateFalseEntry(subject, Key::C, start, 0);

  EXPECT_EQ(entry.start_tick, start);
  EXPECT_GT(entry.end_tick, start);

  // First note should start at start_tick.
  ASSERT_FALSE(entry.notes.empty());
  EXPECT_EQ(entry.notes[0].start_tick, start);
}

TEST(FalseEntryTest, FalseEntryEmptySubject) {
  Subject subject;
  subject.key = Key::C;
  subject.length_ticks = 0;

  MiddleEntry entry = generateFalseEntry(subject, Key::G, kTicksPerBar, 0);
  EXPECT_TRUE(entry.notes.empty());
  EXPECT_EQ(entry.key, Key::G);
}

TEST(FalseEntryTest, FalseEntryQuoteTwoNotes) {
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 1, 3, 2);

  // 2 quoted + 3 divergent (subject has 5 notes, > 3) = 5 total.
  EXPECT_EQ(entry.notes.size(), 5u);

  // First 2 should match transposed subject.
  EXPECT_EQ(entry.notes[0].pitch, 60u);
  EXPECT_EQ(entry.notes[1].pitch, 62u);
}

TEST(FalseEntryTest, FalseEntryQuoteFourNotes) {
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 69});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 1, 3, 4);

  // 4 quoted + 3 divergent = 7 total.
  EXPECT_EQ(entry.notes.size(), 7u);

  // First 4 should match transposed subject.
  EXPECT_EQ(entry.notes[0].pitch, 60u);
  EXPECT_EQ(entry.notes[1].pitch, 62u);
  EXPECT_EQ(entry.notes[2].pitch, 64u);
  EXPECT_EQ(entry.notes[3].pitch, 65u);
}

TEST(FalseEntryTest, FalseEntryClampQuoteToSubjectSize) {
  // Subject with 3 notes, requesting quote_notes=4 -> clamped to 3.
  // Use voice 1 (alto, center 67) so register shift is 0 for these pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 1, 3, 4);

  // 3 quoted (clamped from 4) + 2 divergent (subject.size() <= 3) = 5.
  EXPECT_EQ(entry.notes.size(), 5u);

  // All 3 quoted notes should match the subject.
  EXPECT_EQ(entry.notes[0].pitch, 60u);
  EXPECT_EQ(entry.notes[1].pitch, 62u);
  EXPECT_EQ(entry.notes[2].pitch, 64u);
}

TEST(FalseEntryTest, FalseEntryDescendingSubjectDivergesUp) {
  // Descending subject: G4 F4 E4 D4 C4. Last quoted interval is negative.
  // Divergent notes should go UP (opposite direction).
  Subject subject = makeSubjectQuarters({67, 65, 64, 62, 60});
  MiddleEntry entry = generateFalseEntry(subject, Key::C, 0, 0, 3, 3);

  ASSERT_GE(entry.notes.size(), 4u);

  // Last quoted note is E4=64, last interval was E4-F4=-1 (descending).
  // Divergent should go up.
  EXPECT_GT(entry.notes[3].pitch, entry.notes[2].pitch)
      << "First divergent note should move up when subject descends";
}

// ---------------------------------------------------------------------------
// Validated overload tests
// ---------------------------------------------------------------------------

TEST(MiddleEntryTest, ValidatedOverload_RegistersInState) {
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Tick start = kTicksPerBar * 4;
  uint8_t num_voices = 3;

  CounterpointState cp_state;
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);
  CollisionResolver cp_resolver;

  for (uint8_t v = 0; v < num_voices; ++v) {
    auto [lo, hi] = getFugueVoiceRange(v, num_voices);
    cp_state.registerVoice(v, lo, hi);
  }
  cp_state.setKey(Key::C);

  HarmonicTimeline tl = HarmonicTimeline::createStandard(
      {Key::C, false}, kTicksPerBar * 12, HarmonicResolution::Bar);

  VoiceId voice = 1;
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, start, voice, num_voices,
                                          cp_state, cp_rules, cp_resolver, tl);

  EXPECT_FALSE(entry.notes.empty());

  // Verify notes are registered in counterpoint state by checking that
  // getNoteAt returns a valid note for the entry voice at the start tick.
  if (!entry.notes.empty()) {
    const NoteEvent* registered = cp_state.getNoteAt(voice, entry.notes[0].start_tick);
    EXPECT_NE(registered, nullptr)
        << "Validated middle entry notes should be registered in CounterpointState";
  }
}

}  // namespace
}  // namespace bach
