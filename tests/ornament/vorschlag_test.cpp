// Tests for Vorschlag (pre-stroke grace note) ornament generation.

#include "ornament/vorschlag.h"

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
// Grace note duration
// ---------------------------------------------------------------------------

TEST(VorschlagTest, GraceDurationIs25PercentForShortNotes) {
  // Quarter note: 480 ticks. 25% = 120 ticks = min(120, 120).
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 4);  // 120 ticks
}

TEST(VorschlagTest, GraceDurationCappedAt120Ticks) {
  // Whole note: 1920 ticks. 25% = 480 > 120, so capped at 120.
  auto note = makeNote(0, 60, kTicksPerBar);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 4);  // 120 ticks (capped)
}

TEST(VorschlagTest, GraceDurationIsMinOf25PercentAnd120) {
  // Half note: 960 ticks. 25% = 240 > 120, so capped at 120.
  auto note = makeNote(0, 60, kTicksPerBeat * 2);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 4);  // 120 ticks
}

// ---------------------------------------------------------------------------
// Grace note pitch
// ---------------------------------------------------------------------------

TEST(VorschlagTest, GraceNotePitchIsCorrect) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 62);  // Grace note at upper neighbor
  EXPECT_EQ(result[1].pitch, 60);  // Main note
}

TEST(VorschlagTest, GraceNoteSourceIsOrnament) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].source, BachNoteSource::Ornament);
}

// ---------------------------------------------------------------------------
// Main note offset and shortening
// ---------------------------------------------------------------------------

TEST(VorschlagTest, MainNoteIsShortened) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  const Tick grace_dur = result[0].duration;
  EXPECT_EQ(result[1].start_tick, grace_dur);
  EXPECT_EQ(result[1].duration, kTicksPerBeat - grace_dur);
}

TEST(VorschlagTest, TotalDurationPreserved) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  Tick total = result[0].duration + result[1].duration;
  EXPECT_EQ(total, kTicksPerBeat);
}

TEST(VorschlagTest, MainNoteStartsAfterGrace) {
  auto note = makeNote(960, 64, kTicksPerBeat);
  auto result = generateVorschlag(note, 66);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].start_tick, 960u);
  EXPECT_EQ(result[1].start_tick, 960u + result[0].duration);
}

// ---------------------------------------------------------------------------
// Preserves voice and velocity
// ---------------------------------------------------------------------------

TEST(VorschlagTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat, 3);
  note.velocity = 90;
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].voice, 3);
  EXPECT_EQ(result[0].velocity, 90);
  EXPECT_EQ(result[1].voice, 3);
  EXPECT_EQ(result[1].velocity, 90);
}

// ---------------------------------------------------------------------------
// Short note returns unchanged
// ---------------------------------------------------------------------------

TEST(VorschlagTest, ShortNoteReturnsUnchanged) {
  // 60 ticks < 120 minimum: should return unchanged.
  auto note = makeNote(0, 60, kTicksPerBeat / 8);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 8);
}

TEST(VorschlagTest, ExactMinimumDurationIsEligible) {
  // Exactly 120 ticks = kTicksPerBeat / 4: should be eligible.
  auto note = makeNote(0, 60, kTicksPerBeat / 4);
  auto result = generateVorschlag(note, 62);

  ASSERT_EQ(result.size(), 2u);
}

}  // namespace
}  // namespace bach
