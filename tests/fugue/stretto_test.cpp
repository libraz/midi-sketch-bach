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

TEST(StrettoTest, GenerateStretto_ProgressiveShortening) {
  // Entry intervals should decrease (or stay at minimum) as entries progress.
  // Use a longer subject so the initial interval is well above minimum.
  Subject subject = makeSubjectQuarters({60, 62, 64, 65, 67, 65, 64, 62,
                                         60, 62, 64, 65, 67, 65, 64, 62});
  // 16 quarter notes = 4 bars = 7680 ticks. With 4 voices: 7680/4 = 1920.
  Stretto stretto = generateStretto(subject, Key::C, 0, 4, 42);

  ASSERT_GE(stretto.entryCount(), 3u);

  // Collect intervals between consecutive entries.
  std::vector<Tick> intervals;
  for (size_t idx = 1; idx < stretto.entryCount(); ++idx) {
    intervals.push_back(stretto.entries[idx].entry_tick -
                        stretto.entries[idx - 1].entry_tick);
  }

  // Each interval should be <= the previous one (progressive shortening).
  for (size_t idx = 1; idx < intervals.size(); ++idx) {
    EXPECT_LE(intervals[idx], intervals[idx - 1])
        << "Interval " << idx << " (" << intervals[idx]
        << ") is not <= previous (" << intervals[idx - 1] << ")";
  }

  // All intervals must be on beat boundaries.
  for (size_t idx = 0; idx < intervals.size(); ++idx) {
    EXPECT_EQ(intervals[idx] % kTicksPerBeat, 0u)
        << "Interval " << idx << " not on beat boundary";
  }

  // All intervals must be at least 1 bar.
  for (size_t idx = 0; idx < intervals.size(); ++idx) {
    EXPECT_GE(intervals[idx], kTicksPerBar)
        << "Interval " << idx << " below minimum 1 bar";
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

// ---------------------------------------------------------------------------
// D1: Progressive entry interval shortening (detailed)
// ---------------------------------------------------------------------------

TEST(StrettoTest, ProgressiveShortening_FourEntries_DecreasingIntervals) {
  // 4-bar subject (16 quarter notes) with 4 voices.
  // Initial interval = 16*480/4 = 1920 (exactly 1 bar).
  // 75% of 1920 = 1440, clamped to 1920 (since 1440 < kTicksPerBar).
  // All intervals clamp to kTicksPerBar when the subject is short.
  //
  // Use a longer subject (8 bars = 32 quarter notes) so shortening is visible.
  Subject subject = makeSubjectQuarters({
      60, 62, 64, 65, 67, 65, 64, 62,  // bar 1-2
      60, 62, 64, 65, 67, 65, 64, 62,  // bar 3-4
      60, 62, 64, 65, 67, 65, 64, 62,  // bar 5-6
      60, 62, 64, 65, 67, 65, 64, 62   // bar 7-8
  });
  // 32 notes * 480 = 15360 ticks = 8 bars. With 4 voices: 15360/4 = 3840 (2 bars).
  Stretto stretto = generateStretto(subject, Key::C, 0, 4, 42);

  ASSERT_EQ(stretto.entryCount(), 4u);

  // Collect intervals.
  Tick interval_01 = stretto.entries[1].entry_tick - stretto.entries[0].entry_tick;
  Tick interval_12 = stretto.entries[2].entry_tick - stretto.entries[1].entry_tick;
  Tick interval_23 = stretto.entries[3].entry_tick - stretto.entries[2].entry_tick;

  // Intervals should be monotonically non-increasing.
  EXPECT_GE(interval_01, interval_12);
  EXPECT_GE(interval_12, interval_23);

  // All at least 1 bar.
  EXPECT_GE(interval_01, kTicksPerBar);
  EXPECT_GE(interval_12, kTicksPerBar);
  EXPECT_GE(interval_23, kTicksPerBar);
}

TEST(StrettoTest, ProgressiveShortening_MinimumFloor) {
  // Even with aggressive shortening, no interval goes below 1 bar.
  Subject subject = makeTwoBarSubject();
  Stretto stretto = generateStretto(subject, Key::C, 0, 5, 42);

  ASSERT_EQ(stretto.entryCount(), 5u);
  for (size_t idx = 1; idx < stretto.entryCount(); ++idx) {
    Tick interval = stretto.entries[idx].entry_tick - stretto.entries[idx - 1].entry_tick;
    EXPECT_GE(interval, kTicksPerBar)
        << "Entry " << idx << " interval below minimum 1 bar";
  }
}

// ---------------------------------------------------------------------------
// D2: Character-based transforms for odd entries
// ---------------------------------------------------------------------------

TEST(StrettoTest, CharacterSevere_OddEntriesInverted) {
  // Severe character uses inversion for odd entries (same as legacy behavior).
  Subject subject = makeSubjectQuarters({60, 64, 67});
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42,
                                    SubjectCharacter::Severe);

  ASSERT_GE(stretto.entryCount(), 2u);
  ASSERT_GE(stretto.entries[1].notes.size(), 3u);

  // Entry 1 should be inverted around pivot=60: 60, 56, 53.
  EXPECT_EQ(stretto.entries[1].notes[0].pitch, 60);
  EXPECT_EQ(stretto.entries[1].notes[1].pitch, 56);
  EXPECT_EQ(stretto.entries[1].notes[2].pitch, 53);
}

TEST(StrettoTest, CharacterPlayful_OddEntriesRetrograde) {
  // Playful character uses retrograde for odd entries.
  Subject subject = makeSubjectQuarters({60, 64, 67, 72});
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42,
                                    SubjectCharacter::Playful);

  ASSERT_GE(stretto.entryCount(), 2u);
  ASSERT_GE(stretto.entries[1].notes.size(), 4u);

  // Entry 1 should be retrograde: pitches reversed = 72, 67, 64, 60.
  EXPECT_EQ(stretto.entries[1].notes[0].pitch, 72);
  EXPECT_EQ(stretto.entries[1].notes[1].pitch, 67);
  EXPECT_EQ(stretto.entries[1].notes[2].pitch, 64);
  EXPECT_EQ(stretto.entries[1].notes[3].pitch, 60);
}

TEST(StrettoTest, CharacterNoble_OddEntriesAugmented) {
  // Noble character uses augmentation for odd entries (doubled duration).
  Subject subject = makeSubjectQuarters({60, 64, 67});
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42,
                                    SubjectCharacter::Noble);

  ASSERT_GE(stretto.entryCount(), 2u);
  ASSERT_GE(stretto.entries[1].notes.size(), 3u);

  // Entry 1 should be augmented: same pitches, doubled duration.
  EXPECT_EQ(stretto.entries[1].notes[0].pitch, 60);
  EXPECT_EQ(stretto.entries[1].notes[1].pitch, 64);
  EXPECT_EQ(stretto.entries[1].notes[2].pitch, 67);

  // Durations should be doubled (kTicksPerBeat * 2 = 960).
  EXPECT_EQ(stretto.entries[1].notes[0].duration, kTicksPerBeat * 2);
  EXPECT_EQ(stretto.entries[1].notes[1].duration, kTicksPerBeat * 2);
  EXPECT_EQ(stretto.entries[1].notes[2].duration, kTicksPerBeat * 2);
}

TEST(StrettoTest, CharacterRestless_OddEntriesInverted) {
  // Restless character uses inversion (same as Severe).
  Subject subject = makeSubjectQuarters({60, 64, 67});
  Stretto stretto = generateStretto(subject, Key::C, 0, 2, 42,
                                    SubjectCharacter::Restless);

  ASSERT_GE(stretto.entryCount(), 2u);
  ASSERT_GE(stretto.entries[1].notes.size(), 3u);

  // Entry 1 should be inverted around pivot=60: 60, 56, 53.
  EXPECT_EQ(stretto.entries[1].notes[0].pitch, 60);
  EXPECT_EQ(stretto.entries[1].notes[1].pitch, 56);
  EXPECT_EQ(stretto.entries[1].notes[2].pitch, 53);
}

TEST(StrettoTest, CharacterDefault_IsServe) {
  // Default character parameter should behave identically to explicit Severe.
  Subject subject = makeSubjectQuarters({60, 64, 67});
  Stretto stretto_default = generateStretto(subject, Key::C, 0, 2, 42);
  Stretto stretto_severe = generateStretto(subject, Key::C, 0, 2, 42,
                                           SubjectCharacter::Severe);

  ASSERT_EQ(stretto_default.entryCount(), stretto_severe.entryCount());
  for (size_t idx = 0; idx < stretto_default.entryCount(); ++idx) {
    ASSERT_EQ(stretto_default.entries[idx].notes.size(),
              stretto_severe.entries[idx].notes.size());
    for (size_t nidx = 0; nidx < stretto_default.entries[idx].notes.size(); ++nidx) {
      EXPECT_EQ(stretto_default.entries[idx].notes[nidx].pitch,
                stretto_severe.entries[idx].notes[nidx].pitch);
    }
  }
}

TEST(StrettoTest, EvenEntries_AlwaysOriginal_RegardlessOfCharacter) {
  // Even-indexed entries (0, 2, 4) always use the original (transposed) subject.
  Subject subject = makeSubjectQuarters({60, 64, 67});
  auto characters = {SubjectCharacter::Severe, SubjectCharacter::Playful,
                     SubjectCharacter::Noble, SubjectCharacter::Restless};

  for (auto character : characters) {
    Stretto stretto = generateStretto(subject, Key::C, 0, 3, 42, character);
    ASSERT_GE(stretto.entryCount(), 3u);

    // Entry 0 (even): original pitches.
    EXPECT_EQ(stretto.entries[0].notes[0].pitch, 60);
    EXPECT_EQ(stretto.entries[0].notes[1].pitch, 64);
    EXPECT_EQ(stretto.entries[0].notes[2].pitch, 67);

    // Entry 2 (even): original pitches.
    EXPECT_EQ(stretto.entries[2].notes[0].pitch, 60);
    EXPECT_EQ(stretto.entries[2].notes[1].pitch, 64);
    EXPECT_EQ(stretto.entries[2].notes[2].pitch, 67);
  }
}

// ---------------------------------------------------------------------------
// D3: findValidStrettoIntervals
// ---------------------------------------------------------------------------

TEST(FindValidStrettoIntervalsTest, SimpleSubject) {
  // Create a simple subject with consonant self-overlap.
  std::vector<NoteEvent> subject;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.pitch = 60; note.start_tick = 0; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 67; note.start_tick = kTicksPerBeat * 2; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 60; note.start_tick = kTicksPerBeat * 3; note.duration = kTicksPerBeat;
  subject.push_back(note);

  auto intervals = findValidStrettoIntervals(subject, kTicksPerBeat * 4);
  // At least some intervals should be valid for a simple consonant subject.
  EXPECT_FALSE(intervals.empty());
  // All returned intervals should be multiples of kTicksPerBeat.
  for (auto ivl : intervals) {
    EXPECT_EQ(ivl % kTicksPerBeat, 0u);
  }
}

TEST(FindValidStrettoIntervalsTest, EmptySubject) {
  std::vector<NoteEvent> subject;
  auto intervals = findValidStrettoIntervals(subject, kTicksPerBeat * 4);
  EXPECT_TRUE(intervals.empty());
}

TEST(FindValidStrettoIntervalsTest, SingleNote) {
  std::vector<NoteEvent> subject;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.pitch = 60; note.start_tick = 0; note.duration = kTicksPerBeat;
  subject.push_back(note);
  auto intervals = findValidStrettoIntervals(subject, kTicksPerBeat * 4);
  // Single note can't overlap with itself meaningfully.
  EXPECT_TRUE(intervals.empty());
}

TEST(FindValidStrettoIntervalsTest, IntervalsAreSorted) {
  std::vector<NoteEvent> subject;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.pitch = 60; note.start_tick = 0; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 67; note.start_tick = kTicksPerBeat; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat * 2; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 60; note.start_tick = kTicksPerBeat * 3; note.duration = kTicksPerBeat;
  subject.push_back(note);

  auto intervals = findValidStrettoIntervals(subject, kTicksPerBeat * 4);
  for (size_t idx = 1; idx < intervals.size(); ++idx) {
    EXPECT_LE(intervals[idx - 1], intervals[idx]);
  }
}

TEST(FindValidStrettoIntervalsTest, DissonantSubject_FewerIntervals) {
  // Subject with tritone interval: should reject some offsets.
  std::vector<NoteEvent> subject;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.pitch = 60; note.start_tick = 0; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 66; note.start_tick = kTicksPerBeat; note.duration = kTicksPerBeat;
  subject.push_back(note);  // tritone from 60
  note.pitch = 60; note.start_tick = kTicksPerBeat * 2; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 66; note.start_tick = kTicksPerBeat * 3; note.duration = kTicksPerBeat;
  subject.push_back(note);

  auto intervals = findValidStrettoIntervals(subject, kTicksPerBeat * 4);
  // A highly dissonant subject should have fewer (possibly zero) valid intervals.
  // The key test is that the function runs and returns sorted results.
  for (size_t idx = 1; idx < intervals.size(); ++idx) {
    EXPECT_LE(intervals[idx - 1], intervals[idx]);
  }
}

TEST(FindValidStrettoIntervalsTest, AllIntervalsWithinRange) {
  // All returned intervals must be >= kTicksPerBeat and < max_offset.
  std::vector<NoteEvent> subject;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.pitch = 60; note.start_tick = 0; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 67; note.start_tick = kTicksPerBeat * 2; note.duration = kTicksPerBeat;
  subject.push_back(note);
  note.pitch = 60; note.start_tick = kTicksPerBeat * 3; note.duration = kTicksPerBeat;
  subject.push_back(note);

  Tick max_offset = kTicksPerBeat * 4;
  auto intervals = findValidStrettoIntervals(subject, max_offset);
  for (auto ivl : intervals) {
    EXPECT_GE(ivl, kTicksPerBeat);
    EXPECT_LT(ivl, max_offset);
  }
}

}  // namespace
}  // namespace bach
