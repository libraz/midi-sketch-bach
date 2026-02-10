// Tests for Nachschlag (ornamental ending) ornament generation.

#include "ornament/nachschlag.h"

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
// Resolution notes added at end
// ---------------------------------------------------------------------------

TEST(NachschlagTest, ProducesThreeNotes) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
}

TEST(NachschlagTest, FirstNoteIsShortenedMain) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 60);  // Main pitch
  EXPECT_EQ(result[0].start_tick, 0u);

  // Main note shortened by 120 ticks (2 * 60).
  constexpr Tick kEndingDuration = (kTicksPerBeat / 8) * 2;
  EXPECT_EQ(result[0].duration, kTicksPerBeat - kEndingDuration);
}

TEST(NachschlagTest, ResolutionNoteAtCorrectPitch) {
  auto note = makeNote(0, 64, kTicksPerBeat);
  auto result = generateNachschlag(note, 62);  // Resolution = D4

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[1].pitch, 62);  // Resolution pitch
}

TEST(NachschlagTest, ReturnNoteIsMainPitch) {
  auto note = makeNote(0, 64, kTicksPerBeat);
  auto result = generateNachschlag(note, 62);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[2].pitch, 64);  // Returns to main pitch
}

TEST(NachschlagTest, EndingNotesAreSixtiethTicks) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  constexpr Tick kSubNoteDuration = kTicksPerBeat / 8;  // 60 ticks
  EXPECT_EQ(result[1].duration, kSubNoteDuration);
  EXPECT_EQ(result[2].duration, kSubNoteDuration);
}

TEST(NachschlagTest, EndingNotesAreContiguous) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  // Main end = resolution start.
  Tick main_end = result[0].start_tick + result[0].duration;
  EXPECT_EQ(result[1].start_tick, main_end);
  // Resolution end = return start.
  Tick res_end = result[1].start_tick + result[1].duration;
  EXPECT_EQ(result[2].start_tick, res_end);
}

// ---------------------------------------------------------------------------
// Main note shortened by correct amount
// ---------------------------------------------------------------------------

TEST(NachschlagTest, MainNoteShortenedBy120Ticks) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  constexpr Tick kEndingDuration = (kTicksPerBeat / 8) * 2;
  EXPECT_EQ(result[0].duration, kTicksPerBeat - kEndingDuration);
}

// ---------------------------------------------------------------------------
// Total duration preserved
// ---------------------------------------------------------------------------

TEST(NachschlagTest, TotalDurationPreserved) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  Tick total = 0;
  for (const auto& sub : result) {
    total += sub.duration;
  }
  EXPECT_EQ(total, kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// Short note returns unchanged
// ---------------------------------------------------------------------------

TEST(NachschlagTest, ShortNoteReturnsUnchanged) {
  // 120 ticks < 240 minimum: should return unchanged.
  auto note = makeNote(0, 60, kTicksPerBeat / 4);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[0].duration, kTicksPerBeat / 4);
}

TEST(NachschlagTest, ExactMinimumDurationIsEligible) {
  // Exactly 240 ticks = kTicksPerBeat / 2: should be eligible.
  auto note = makeNote(0, 60, kTicksPerBeat / 2);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
}

// ---------------------------------------------------------------------------
// Preserves voice and velocity
// ---------------------------------------------------------------------------

TEST(NachschlagTest, PreservesVoiceAndVelocity) {
  auto note = makeNote(0, 60, kTicksPerBeat, 2);
  note.velocity = 95;
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  for (const auto& sub : result) {
    EXPECT_EQ(sub.voice, 2);
    EXPECT_EQ(sub.velocity, 95);
  }
}

// ---------------------------------------------------------------------------
// Ornament source
// ---------------------------------------------------------------------------

TEST(NachschlagTest, EndingNotesHaveOrnamentSource) {
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto result = generateNachschlag(note, 58);

  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[1].source, BachNoteSource::Ornament);
  EXPECT_EQ(result[2].source, BachNoteSource::Ornament);
}

}  // namespace
}  // namespace bach
