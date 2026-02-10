// Tests for compound ornament generation.

#include "ornament/compound_ornament.h"

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
// TrillWithNachschlag
// ---------------------------------------------------------------------------

TEST(CompoundOrnamentTest, TrillWithNachschlagProducesMultipleNotes) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);  // Half note
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  // Should produce trill sub-notes + nachschlag sub-notes.
  EXPECT_GT(result.size(), 3u);
}

TEST(CompoundOrnamentTest, TrillWithNachschlagPreservesTotalDuration) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  Tick total = 0;
  for (const auto& sub : result) {
    total += sub.duration;
  }
  EXPECT_EQ(total, kTicksPerBeat * 2);
}

TEST(CompoundOrnamentTest, TrillWithNachschlagStartsAtOriginalTick) {
  auto note = makeNote(960, 64, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 66, 62);

  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.front().start_tick, 960u);
}

TEST(CompoundOrnamentTest, TrillWithNachschlagEndsOnMainPitch) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  // The last note of the nachschlag should return to the main pitch.
  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.back().pitch, 60);
}

// ---------------------------------------------------------------------------
// TurnThenTrill
// ---------------------------------------------------------------------------

TEST(CompoundOrnamentTest, TurnThenTrillProducesMultipleNotes) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, 62, 58);

  // Turn (4 notes) + trill (multiple notes).
  EXPECT_GT(result.size(), 5u);
}

TEST(CompoundOrnamentTest, TurnThenTrillPreservesTotalDuration) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, 62, 58);

  Tick total = 0;
  for (const auto& sub : result) {
    total += sub.duration;
  }
  EXPECT_EQ(total, kTicksPerBeat * 2);
}

TEST(CompoundOrnamentTest, TurnThenTrillStartsWithTurnPattern) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, 62, 58);

  // Turn pattern: upper, main, lower, main.
  ASSERT_GE(result.size(), 4u);
  EXPECT_EQ(result[0].pitch, 62);  // Upper
  EXPECT_EQ(result[1].pitch, 60);  // Main
  EXPECT_EQ(result[2].pitch, 58);  // Lower
  EXPECT_EQ(result[3].pitch, 60);  // Main
}

TEST(CompoundOrnamentTest, TurnThenTrillEndsOnMainPitch) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, 62, 58);

  ASSERT_FALSE(result.empty());
  EXPECT_EQ(result.back().pitch, 60);
}

// ---------------------------------------------------------------------------
// Eligibility (minimum kTicksPerBeat duration)
// ---------------------------------------------------------------------------

TEST(CompoundOrnamentTest, ShortNoteReturnsUnchanged) {
  // 240 ticks < 480 minimum: should return unchanged.
  auto note = makeNote(0, 60, kTicksPerBeat / 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 2);
}

TEST(CompoundOrnamentTest, ExactMinimumDurationIsEligible) {
  // Exactly kTicksPerBeat = 480: should be eligible.
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  EXPECT_GT(result.size(), 1u);
}

// ---------------------------------------------------------------------------
// Preserves voice and velocity
// ---------------------------------------------------------------------------

TEST(CompoundOrnamentTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2, 1);
  note.velocity = 85;
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, 62, 58);

  for (const auto& sub : result) {
    EXPECT_EQ(sub.voice, 1);
    EXPECT_EQ(sub.velocity, 85);
  }
}

// ---------------------------------------------------------------------------
// Sub-notes are contiguous
// ---------------------------------------------------------------------------

TEST(CompoundOrnamentTest, SubNotesAreContiguous) {
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag, 62, 58);

  for (size_t idx = 1; idx < result.size(); ++idx) {
    Tick prev_end = result[idx - 1].start_tick + result[idx - 1].duration;
    EXPECT_LE(result[idx].start_tick, prev_end + 1)
        << "Sub-note gap at index " << idx;
  }
}

}  // namespace
}  // namespace bach
