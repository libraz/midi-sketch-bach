// Tests for counterpoint/vertical_context.h -- VerticalContext generation-time
// vertical safety checks.

#include "counterpoint/vertical_context.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.source = BachNoteSource::FreeCounterpoint;
  return n;
}

HarmonicTimeline makeCMajorTimeline(Tick duration) {
  HarmonicTimeline tl;
  HarmonicEvent ev;
  ev.tick = 0;
  ev.end_tick = duration;
  ev.key = Key::C;
  ev.is_minor = false;
  ev.chord = Chord{ChordDegree::I, ChordQuality::Major, 60, 0};
  ev.bass_pitch = 48;  // C3
  tl.addEvent(ev);
  return tl;
}

// ---- isSafe tests ----

TEST(VerticalContextTest, NullContextAlwaysSafe) {
  VerticalContext vctx;
  // No placed_notes or timeline — should return true.
  EXPECT_TRUE(vctx.isSafe(0, 0, 60));
}

TEST(VerticalContextTest, StrongBeatConsonanceAccepted) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 0: C4(60) over C3(48) = P8 = consonant.
  EXPECT_TRUE(vctx.isSafe(0, 0, 60));
  // Beat 0: E4(64) over C3(48) = M3 = consonant.
  EXPECT_TRUE(vctx.isSafe(0, 0, 64));
  // Beat 0: G4(67) over C3(48) = P5 = consonant.
  EXPECT_TRUE(vctx.isSafe(0, 0, 67));
}

TEST(VerticalContextTest, StrongBeatDissonanceRejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 0: D4(62) over C3(48) = M2 (non-chord-tone, non-consonant).
  // D is not a chord tone in C major (root=C, chord=I).
  EXPECT_FALSE(vctx.isSafe(0, 0, 62));
}

TEST(VerticalContextTest, StrongBeatChordToneAlwaysSafe) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 49, 1)};  // Db3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 0: C4(60) is a chord tone of C major I chord.
  // Db3(49) to C4(60) = M7 = dissonant.
  // Vertical sovereignty: chord-tone status does NOT exempt from consonance check.
  EXPECT_FALSE(vctx.isSafe(0, 0, 60));
}

TEST(VerticalContextTest, WeakBeatM2Rejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  // Place a C3 lasting the whole bar.
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 1 (tick 480): Db4(49) over C3(48) = m2(1) → rejected.
  EXPECT_FALSE(vctx.isSafe(kTicksPerBeat, 0, 49));
}

TEST(VerticalContextTest, WeakBeatTTRejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 1 (tick 480): F#4(66) over C3(48) = TT(6) → rejected.
  EXPECT_FALSE(vctx.isSafe(kTicksPerBeat, 0, 66));
}

TEST(VerticalContextTest, WeakBeatM7Rejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 3 (tick 1440): B4(71) over C3(48) = M7(11) → rejected.
  EXPECT_FALSE(vctx.isSafe(kTicksPerBeat * 3, 0, 71));
}

TEST(VerticalContextTest, WeakBeatConsonanceAccepted) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 1: E4(64) over C3(48) = M3(4) → allowed.
  EXPECT_TRUE(vctx.isSafe(kTicksPerBeat, 0, 64));
  // Beat 1: G4(67) over C3(48) = P5(7) → allowed.
  EXPECT_TRUE(vctx.isSafe(kTicksPerBeat, 0, 67));
}

TEST(VerticalContextTest, WeakBeatM2AllowedByPredicate) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Allow all weak-beat dissonances.
  vctx.weak_beat_allow = [](Tick, uint8_t, uint8_t, uint8_t, int, uint8_t) {
    return true;
  };

  // Beat 1: m2(1) now allowed by predicate.
  EXPECT_TRUE(vctx.isSafe(kTicksPerBeat, 0, 49));
}

TEST(VerticalContextTest, WeakBeatNonHarshDissonanceAllowed) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBar, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // Beat 1: D4(50) over C3(48) = M2(2) — not in {1, 6, 11}, allowed.
  EXPECT_TRUE(vctx.isSafe(kTicksPerBeat, 0, 50));
  // Beat 1: Bb3(58) over C3(48) = m7(10) — not in {1, 6, 11}, allowed.
  EXPECT_TRUE(vctx.isSafe(kTicksPerBeat, 0, 58));
}

// ---- score tests ----

TEST(VerticalContextTest, ScoreZeroForUnsafe) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 48, 1)};
  VerticalContext vctx{&placed, &tl, 2};

  // Strong beat dissonance → score = 0.
  EXPECT_FLOAT_EQ(0.0f, vctx.score(0, 0, 62));
}

TEST(VerticalContextTest, ScorePerfectConsonance) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // P8(12): C4(60) over C3(48) → score = 1.0.
  EXPECT_FLOAT_EQ(1.0f, vctx.score(0, 0, 60));
}

TEST(VerticalContextTest, ScoreImperfectConsonance) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {makeNote(0, kTicksPerBeat, 48, 1)};  // C3
  VerticalContext vctx{&placed, &tl, 2};

  // M3(4): E4(64) over C3(48) → score = 0.8.
  EXPECT_FLOAT_EQ(0.8f, vctx.score(0, 0, 64));
}

TEST(VerticalContextTest, ScoreNoSoundingNotes) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed;
  VerticalContext vctx{&placed, &tl, 2};

  // No other notes sounding → score = 1.0.
  EXPECT_FLOAT_EQ(1.0f, vctx.score(0, 0, 60));
}

// ---- findPrevPitch tests ----

TEST(VerticalContextTest, FindPrevPitchFound) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed = {
      makeNote(0, kTicksPerBeat, 60, 0),
      makeNote(kTicksPerBeat, kTicksPerBeat, 64, 0),
  };
  VerticalContext vctx{&placed, &tl, 2};

  EXPECT_EQ(64, vctx.findPrevPitch(0, kTicksPerBeat * 2));
  EXPECT_EQ(60, vctx.findPrevPitch(0, kTicksPerBeat));
}

TEST(VerticalContextTest, FindPrevPitchNotFound) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  std::vector<NoteEvent> placed;
  VerticalContext vctx{&placed, &tl, 2};

  EXPECT_EQ(0, vctx.findPrevPitch(0, kTicksPerBeat));
}

}  // namespace
}  // namespace bach
