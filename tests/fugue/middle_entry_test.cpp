// Tests for fugue/middle_entry.h -- middle entry generation for fugue
// development sections.

#include "fugue/middle_entry.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"

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
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::G, 0, 0);

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
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  MiddleEntry entry = generateMiddleEntry(subject, Key::C, 0, 0);

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
  Subject subject = makeSubjectQuarters({67, 69, 71});
  subject.key = Key::G;

  MiddleEntry entry = generateMiddleEntry(subject, Key::C, 0, 0);
  ASSERT_EQ(entry.notes.size(), 3u);
  EXPECT_EQ(entry.notes[0].pitch, 60);  // G4-7 = C4
  EXPECT_EQ(entry.notes[1].pitch, 62);  // A4-7 = D4
  EXPECT_EQ(entry.notes[2].pitch, 64);  // B4-7 = E4
}

TEST(MiddleEntryTest, GenerateMiddleEntry_SubdominantKey) {
  // Subject in C: C4 E4 G4. Middle entry in F (key=5) => +5 semitones.
  Subject subject = makeSubjectQuarters({60, 64, 67});
  MiddleEntry entry = generateMiddleEntry(subject, Key::F, 0, 0);

  ASSERT_EQ(entry.notes.size(), 3u);
  EXPECT_EQ(entry.notes[0].pitch, 65);  // C4+5 = F4
  EXPECT_EQ(entry.notes[1].pitch, 69);  // E4+5 = A4
  EXPECT_EQ(entry.notes[2].pitch, 72);  // G4+5 = C5
}

}  // namespace
}  // namespace bach
