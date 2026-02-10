// Tests for trill ornament generation.

#include "ornament/trill.h"

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

// ---------------------------------------------------------------------------
// Basic trill generation
// ---------------------------------------------------------------------------

TEST(TrillTest, QuarterNoteTrillProducesMultipleSubnotes) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // C4, quarter note
  auto result = generateTrill(note, 62);        // Upper = D4

  // Should produce more than 1 note.
  EXPECT_GT(result.size(), 1u);
  // Should produce an odd number of sub-notes so last index is even (main note).
  // Pattern: idx 0=main, 1=upper, 2=main, ... last(even)=main.
  EXPECT_EQ(result.size() % 2, 1u);
}

TEST(TrillTest, StartsOnMainNote) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateTrill(note, 62);

  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.front().pitch, 60);  // Starts on main note.
}

TEST(TrillTest, EndsOnMainNote) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateTrill(note, 62);

  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.back().pitch, 60);  // Ends on main note.
}

TEST(TrillTest, AlternatesBetweenMainAndUpper) {
  auto note = makeNote(0, 64, kTicksPerBeat);  // E4
  auto result = generateTrill(note, 66);        // Upper = F#4

  for (size_t idx = 0; idx < result.size(); ++idx) {
    if (idx % 2 == 0) {
      EXPECT_EQ(result[idx].pitch, 64) << "Even index should be main note";
    } else {
      EXPECT_EQ(result[idx].pitch, 66) << "Odd index should be upper note";
    }
  }
}

TEST(TrillTest, TotalDurationEqualsOriginal) {
  auto note = makeNote(480, 60, kTicksPerBeat);
  auto result = generateTrill(note, 62);

  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, note.duration);
}

TEST(TrillTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat, 2);
  note.velocity = 100;
  auto result = generateTrill(note, 62);

  for (const auto& sub : result) {
    EXPECT_EQ(sub.voice, 2);
    EXPECT_EQ(sub.velocity, 100);
  }
}

TEST(TrillTest, PreservesStartTick) {
  auto note = makeNote(1920, 60, kTicksPerBeat);
  auto result = generateTrill(note, 62);

  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.front().start_tick, 1920u);
}

// ---------------------------------------------------------------------------
// Speed parameter
// ---------------------------------------------------------------------------

TEST(TrillTest, HigherSpeedProducesMoreSubnotes) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);  // Half note

  auto slow = generateTrill(note, 62, 2);
  auto fast = generateTrill(note, 62, 8);

  EXPECT_GT(fast.size(), slow.size());
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(TrillTest, VeryShortNoteReturnsOriginal) {
  auto note = makeNote(0, 60, 2);  // 2 ticks: too short to subdivide.
  auto result = generateTrill(note, 62);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, note.pitch);
  EXPECT_EQ(result[0].duration, note.duration);
}

TEST(TrillTest, WholeNoteTrillCoversFullDuration) {
  auto note = makeNote(0, 60, kTicksPerBar);  // Whole note
  auto result = generateTrill(note, 62);

  Tick total_dur = 0;
  Tick last_end = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
    Tick end_tick = sub.start_tick + sub.duration;
    if (end_tick > last_end) last_end = end_tick;
  }
  EXPECT_EQ(total_dur, kTicksPerBar);
  EXPECT_EQ(last_end, kTicksPerBar);
}

TEST(TrillTest, SubnotesAreContiguous) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateTrill(note, 62);

  for (size_t idx = 1; idx < result.size(); ++idx) {
    Tick prev_end = result[idx - 1].start_tick + result[idx - 1].duration;
    // Allow small tolerance due to integer division.
    EXPECT_LE(result[idx].start_tick, prev_end + 1)
        << "Sub-note gap at index " << idx;
  }
}

}  // namespace
}  // namespace bach
