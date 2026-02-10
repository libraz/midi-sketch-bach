// Tests for fugue/stretto.h -- stretto generation for fugue climax
// (FuguePhase::Resolve).

#include "fugue/stretto.h"

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

// Helper: build a 2-bar subject (8 quarter notes) for stretto testing.
Subject makeTwoBarSubject(Key key = Key::C) {
  return makeSubjectQuarters({60, 62, 64, 65, 67, 65, 64, 62}, key);
}

// ---------------------------------------------------------------------------
// Basic stretto generation
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_HasEntries) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);
  EXPECT_EQ(stretto.entryCount(), 3u);
}

TEST(StrettoTest, GenerateStretto_CorrectKey) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::G, 0, 3, 42);
  EXPECT_EQ(stretto.key, Key::G);
}

TEST(StrettoTest, GenerateStretto_OverlappingEntries) {
  // Entry interval should be less than subject length for stretto overlap.
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);

  ASSERT_GE(stretto.entryCount(), 2u);
  // Compute the interval between first two entries.
  Tick entry_interval = stretto.entries[1].entry_tick - stretto.entries[0].entry_tick;

  // The interval must be less than the full subject length for overlapping.
  EXPECT_LT(entry_interval, subject.length_ticks);
}

TEST(StrettoTest, GenerateStretto_EntrySpacing) {
  // All entries should be evenly spaced at the design interval.
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);

  ASSERT_GE(stretto.entryCount(), 2u);
  Tick interval = stretto.entries[1].entry_tick - stretto.entries[0].entry_tick;

  for (size_t idx = 1; idx < stretto.entryCount(); ++idx) {
    Tick actual_interval = stretto.entries[idx].entry_tick - stretto.entries[idx - 1].entry_tick;
    EXPECT_EQ(actual_interval, interval)
        << "Entry " << idx << " has inconsistent spacing";
  }
}

TEST(StrettoTest, GenerateStretto_AllVoicesPresent) {
  Subject subject = makeTwoBarSubject();
  uint8_t voices = 4;
  Stretto stretto = generateStretto(subject, Key::C, 0, voices, 42);

  ASSERT_EQ(stretto.entryCount(), voices);
  for (uint8_t idx = 0; idx < voices; ++idx) {
    EXPECT_EQ(stretto.entries[idx].voice_id, idx);
  }
}

// ---------------------------------------------------------------------------
// allNotes()
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_AllNotesReturnsAll) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);

  auto all = stretto.allNotes();

  // Total notes should equal num_voices * notes_per_entry.
  size_t expected_total = 0;
  for (const auto& entry : stretto.entries) {
    expected_total += entry.notes.size();
  }
  EXPECT_EQ(all.size(), expected_total);
}

TEST(StrettoTest, GenerateStretto_AllNotesSorted) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 4, 42);

  auto all = stretto.allNotes();

  for (size_t idx = 1; idx < all.size(); ++idx) {
    bool in_order = (all[idx].start_tick > all[idx - 1].start_tick) ||
                    (all[idx].start_tick == all[idx - 1].start_tick &&
                     all[idx].voice >= all[idx - 1].voice);
    EXPECT_TRUE(in_order) << "Notes not sorted at index " << idx;
  }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_DeterministicWithSeed) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto_a = generateStretto(subject, Key::C, 0, 3, 12345);
  Stretto stretto_b = generateStretto(subject, Key::C, 0, 3, 12345);

  ASSERT_EQ(stretto_a.entryCount(), stretto_b.entryCount());
  for (size_t idx = 0; idx < stretto_a.entryCount(); ++idx) {
    ASSERT_EQ(stretto_a.entries[idx].notes.size(), stretto_b.entries[idx].notes.size());
    for (size_t nidx = 0; nidx < stretto_a.entries[idx].notes.size(); ++nidx) {
      EXPECT_EQ(stretto_a.entries[idx].notes[nidx].pitch,
                stretto_b.entries[idx].notes[nidx].pitch);
      EXPECT_EQ(stretto_a.entries[idx].notes[nidx].start_tick,
                stretto_b.entries[idx].notes[nidx].start_tick);
    }
  }
}

// ---------------------------------------------------------------------------
// Voice count variants
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_TwoVoices) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42);
  EXPECT_EQ(stretto.entryCount(), 2u);
}

TEST(StrettoTest, GenerateStretto_FourVoices) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 4, 42);
  EXPECT_EQ(stretto.entryCount(), 4u);
}

TEST(StrettoTest, GenerateStretto_ClampsBelowTwo) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 1, 42);
  EXPECT_EQ(stretto.entryCount(), 2u);  // Clamped to minimum 2
}

TEST(StrettoTest, GenerateStretto_ClampsAboveFive) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 8, 42);
  EXPECT_EQ(stretto.entryCount(), 5u);  // Clamped to maximum 5
}

// ---------------------------------------------------------------------------
// Minimum interval
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_MinimumInterval) {
  // Short subject: 2 quarter notes = 960 ticks < 1 bar.
  // With 3 voices: 960/3 = 320 ticks < 1 bar.
  // Entry interval should be clamped to kTicksPerBar.
  Subject subject = makeSubjectQuarters({60, 62});
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);

  ASSERT_GE(stretto.entryCount(), 2u);
  Tick interval = stretto.entries[1].entry_tick - stretto.entries[0].entry_tick;
  EXPECT_GE(interval, kTicksPerBar);
}

// ---------------------------------------------------------------------------
// Empty subject
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_EmptySubject) {
  Subject subject;
  subject.key = Key::C;
  subject.length_ticks = 0;

  Stretto stretto = generateStretto(subject, Key::C, kTicksPerBar, 3, 42);
  EXPECT_EQ(stretto.start_tick, kTicksPerBar);
  EXPECT_EQ(stretto.end_tick, kTicksPerBar);
  EXPECT_TRUE(stretto.entries.empty());
}

// ---------------------------------------------------------------------------
// Duration calculation
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_Duration) {
  Subject subject = makeTwoBarSubject();
  Tick start = kTicksPerBar * 20;
  Stretto stretto = generateStretto(subject, Key::C, start, 3, 42);

  EXPECT_EQ(stretto.durationTicks(), stretto.end_tick - stretto.start_tick);
  EXPECT_GT(stretto.durationTicks(), 0u);
}

TEST(StrettoTest, GenerateStretto_EndTickConsistent) {
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42);

  // end_tick should be: last entry tick + subject length.
  Tick last_entry_tick = stretto.entries.back().entry_tick;
  EXPECT_EQ(stretto.end_tick, last_entry_tick + subject.length_ticks);
}

// ---------------------------------------------------------------------------
// Key transposition
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_TransposesToHomeKey) {
  // Subject in C, stretto in G. First entry (even index) should use
  // transposed pitches.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65});
  Stretto stretto = generateStretto(subject, Key::G, 0, 2, 42);

  ASSERT_FALSE(stretto.entries.empty());
  ASSERT_FALSE(stretto.entries[0].notes.empty());
  // First note of first entry should be C4+7 = G4 = 67.
  EXPECT_EQ(stretto.entries[0].notes[0].pitch, 67);
}

TEST(StrettoTest, GenerateStretto_OddEntriesInverted) {
  // Odd-indexed entries use melodic inversion around the first note's pitch.
  Subject subject = makeSubjectQuarters({60, 64, 67});  // C E G (ascending)
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42);

  ASSERT_GE(stretto.entryCount(), 2u);
  ASSERT_GE(stretto.entries[1].notes.size(), 3u);

  // First entry (idx 0): original pitches 60, 64, 67
  EXPECT_EQ(stretto.entries[0].notes[0].pitch, 60);
  EXPECT_EQ(stretto.entries[0].notes[1].pitch, 64);
  EXPECT_EQ(stretto.entries[0].notes[2].pitch, 67);

  // Second entry (idx 1): inverted around pivot=60
  // Inversion: 2*60 - pitch => 60, 56, 53
  EXPECT_EQ(stretto.entries[1].notes[0].pitch, 60);
  EXPECT_EQ(stretto.entries[1].notes[1].pitch, 56);
  EXPECT_EQ(stretto.entries[1].notes[2].pitch, 53);
}

// ---------------------------------------------------------------------------
// Start tick offset
// ---------------------------------------------------------------------------

TEST(StrettoTest, GenerateStretto_RespectsStartTick) {
  Subject subject = makeTwoBarSubject();
  Tick start = kTicksPerBar * 50;
  Stretto stretto = generateStretto(subject, Key::C, start, 3, 42);

  EXPECT_EQ(stretto.start_tick, start);
  EXPECT_EQ(stretto.entries[0].entry_tick, start);
  EXPECT_GE(stretto.end_tick, start);
}

}  // namespace
}  // namespace bach
