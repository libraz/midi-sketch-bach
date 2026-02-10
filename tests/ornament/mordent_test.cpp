// Tests for mordent ornament generation.

#include "ornament/mordent.h"

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
// Basic mordent generation
// ---------------------------------------------------------------------------

TEST(MordentTest, ProducesThreeNotes) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateMordent(note, 58);  // Lower = Bb3

  ASSERT_EQ(result.size(), 3u);
}

TEST(MordentTest, PitchPattern_MainLowerMain) {
  auto note = makeNote(0, 64, kTicksPerBeat);  // E4
  auto result = generateMordent(note, 62);       // Lower = D4

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 64);  // Main
  EXPECT_EQ(result[1].pitch, 62);  // Lower
  EXPECT_EQ(result[2].pitch, 64);  // Main
}

TEST(MordentTest, DurationSplit_50_25_25) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // 480 ticks
  auto result = generateMordent(note, 58);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].duration, 240u);  // 50%
  EXPECT_EQ(result[1].duration, 120u);  // 25%
  // Third note absorbs rounding remainder.
  EXPECT_EQ(result[2].duration, 120u);  // 25%
}

TEST(MordentTest, TotalDurationEqualsOriginal) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateMordent(note, 58);

  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, note.duration);
}

TEST(MordentTest, StartTicksAreContiguous) {
  auto note = makeNote(960, 60, kTicksPerBeat);
  auto result = generateMordent(note, 58);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].start_tick, 960u);
  EXPECT_EQ(result[1].start_tick, 960u + result[0].duration);
  EXPECT_EQ(result[2].start_tick, 960u + result[0].duration + result[1].duration);
}

TEST(MordentTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat, 3);
  note.velocity = 90;
  auto result = generateMordent(note, 58);

  for (const auto& sub : result) {
    EXPECT_EQ(sub.voice, 3);
    EXPECT_EQ(sub.velocity, 90);
  }
}

// ---------------------------------------------------------------------------
// Duration rounding
// ---------------------------------------------------------------------------

TEST(MordentTest, OddDurationAbsorbsRemainder) {
  // 481 ticks: 240 + 120 + 121 = 481.
  auto note = makeNote(0, 60, 481);
  auto result = generateMordent(note, 58);

  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, 481u);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(MordentTest, VeryShortNoteReturnsOriginal) {
  auto note = makeNote(0, 60, 3);  // 3 ticks: too short.
  auto result = generateMordent(note, 58);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, note.pitch);
  EXPECT_EQ(result[0].duration, note.duration);
}

TEST(MordentTest, MinimumDurationFourTicksWorks) {
  auto note = makeNote(0, 60, 4);  // Exactly 4 ticks: minimum for mordent.
  auto result = generateMordent(note, 58);

  ASSERT_EQ(result.size(), 3u);
  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, 4u);
}

TEST(MordentTest, HalfNoteProducesCorrectSplit) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);  // 960 ticks
  auto result = generateMordent(note, 58);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].duration, 480u);  // 50%
  EXPECT_EQ(result[1].duration, 240u);  // 25%
  EXPECT_EQ(result[2].duration, 240u);  // 25%
}

}  // namespace
}  // namespace bach
