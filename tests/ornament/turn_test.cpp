// Tests for turn ornament generation.

#include "ornament/turn.h"

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
// Basic turn generation
// ---------------------------------------------------------------------------

TEST(TurnTest, ProducesFourNotes) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateTurn(note, 62, 58);  // Upper = D4, Lower = Bb3

  ASSERT_EQ(result.size(), 4u);
}

TEST(TurnTest, PitchPattern_UpperMainLowerMain) {
  auto note = makeNote(0, 64, kTicksPerBeat);  // E4
  auto result = generateTurn(note, 66, 62);      // Upper = F#4, Lower = D4

  ASSERT_EQ(result.size(), 4u);
  EXPECT_EQ(result[0].pitch, 66);  // Upper
  EXPECT_EQ(result[1].pitch, 64);  // Main
  EXPECT_EQ(result[2].pitch, 62);  // Lower
  EXPECT_EQ(result[3].pitch, 64);  // Main
}

TEST(TurnTest, EqualDurationSplit) {
  auto note = makeNote(0, 60, kTicksPerBeat);  // 480 ticks
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 4u);
  EXPECT_EQ(result[0].duration, 120u);  // 25%
  EXPECT_EQ(result[1].duration, 120u);  // 25%
  EXPECT_EQ(result[2].duration, 120u);  // 25%
  EXPECT_EQ(result[3].duration, 120u);  // 25% (absorbs remainder)
}

TEST(TurnTest, TotalDurationEqualsOriginal) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateTurn(note, 62, 58);

  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, note.duration);
}

TEST(TurnTest, StartTicksAreContiguous) {
  auto note = makeNote(1920, 60, kTicksPerBeat);
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 4u);
  Tick expected_tick = 1920;
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(result[idx].start_tick, expected_tick);
    expected_tick += result[idx].duration;
  }
  EXPECT_EQ(result[3].start_tick, expected_tick);
}

TEST(TurnTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat, 1);
  note.velocity = 95;
  auto result = generateTurn(note, 62, 58);

  for (const auto& sub : result) {
    EXPECT_EQ(sub.voice, 1);
    EXPECT_EQ(sub.velocity, 95);
  }
}

// ---------------------------------------------------------------------------
// Duration rounding
// ---------------------------------------------------------------------------

TEST(TurnTest, OddDurationAbsorbsRemainder) {
  // 483 ticks: 120 + 120 + 120 + 123 = 483.
  auto note = makeNote(0, 60, 483);
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 4u);
  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, 483u);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(TurnTest, VeryShortNoteReturnsOriginal) {
  auto note = makeNote(0, 60, 3);  // 3 ticks: too short.
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, note.pitch);
  EXPECT_EQ(result[0].duration, note.duration);
}

TEST(TurnTest, MinimumDurationFourTicksWorks) {
  auto note = makeNote(0, 60, 4);  // Exactly 4 ticks: minimum for turn.
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 4u);
  Tick total_dur = 0;
  for (const auto& sub : result) {
    total_dur += sub.duration;
  }
  EXPECT_EQ(total_dur, 4u);
}

TEST(TurnTest, WholeNoteProducesCorrectSplit) {
  auto note = makeNote(0, 60, kTicksPerBar);  // 1920 ticks
  auto result = generateTurn(note, 62, 58);

  ASSERT_EQ(result.size(), 4u);
  EXPECT_EQ(result[0].duration, 480u);
  EXPECT_EQ(result[1].duration, 480u);
  EXPECT_EQ(result[2].duration, 480u);
  EXPECT_EQ(result[3].duration, 480u);
}

}  // namespace
}  // namespace bach
