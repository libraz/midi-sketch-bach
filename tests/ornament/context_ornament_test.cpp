// Tests for context-dependent ornament selection using harmonic information.

#include "ornament/ornament_engine.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "ornament/trill.h"

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

/// Helper: create a C major harmonic timeline covering 4 bars.
HarmonicTimeline makeCMajorTimeline() {
  HarmonicTimeline timeline;

  // I chord (C major): bar 0-1
  HarmonicEvent event1;
  event1.tick = 0;
  event1.end_tick = kTicksPerBar * 2;
  event1.key = Key::C;
  event1.is_minor = false;
  event1.chord.degree = ChordDegree::I;
  event1.chord.quality = ChordQuality::Major;
  event1.chord.root_pitch = 60;  // C4
  timeline.addEvent(event1);

  // V chord (G major): bar 2-3
  HarmonicEvent event2;
  event2.tick = kTicksPerBar * 2;
  event2.end_tick = kTicksPerBar * 4;
  event2.key = Key::C;
  event2.is_minor = false;
  event2.chord.degree = ChordDegree::V;
  event2.chord.quality = ChordQuality::Major;
  event2.chord.root_pitch = 67;  // G4
  timeline.addEvent(event2);

  return timeline;
}

/// Helper: create an OrnamentConfig with all types enabled.
OrnamentConfig makeFullConfig() {
  OrnamentConfig config;
  config.enable_trill = true;
  config.enable_mordent = true;
  config.enable_turn = true;
  config.enable_appoggiatura = true;
  config.enable_pralltriller = true;
  config.enable_vorschlag = true;
  config.enable_nachschlag = true;
  config.enable_compound = true;
  return config;
}

// ---------------------------------------------------------------------------
// Chord tone selection
// ---------------------------------------------------------------------------

TEST(ContextOrnamentTest, ChordTonePrefersTrillOnWeakBeat) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();
  config.enable_compound = false;  // Disable compound to test basic selection.

  // C4 (pitch 60) is root of I chord = chord tone.
  // Weak beat 1 position.
  auto note = makeNote(kTicksPerBeat, 60, kTicksPerBeat / 2);
  auto type = selectOrnamentType(note, config, timeline, kTicksPerBeat);

  EXPECT_EQ(type, OrnamentType::Trill);
}

TEST(ContextOrnamentTest, ChordToneOnStrongBeatGetsCompound) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();

  // C4 on strong beat 0, long note: compound trill with nachschlag.
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto type = selectOrnamentType(note, config, timeline, 0);

  EXPECT_EQ(type, OrnamentType::CompoundTrillNachschlag);
}

// ---------------------------------------------------------------------------
// Non-chord tone selection
// ---------------------------------------------------------------------------

TEST(ContextOrnamentTest, NonChordTonePrefersVorschlag) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();
  config.enable_compound = false;

  // D4 (pitch 62) is NOT a chord tone of C major triad (C-E-G).
  auto note = makeNote(0, 62, kTicksPerBeat / 2);
  auto type = selectOrnamentType(note, config, timeline, 0);

  EXPECT_EQ(type, OrnamentType::Vorschlag);
}

TEST(ContextOrnamentTest, NonChordToneOnWeakBeatLongNoteGetsCompoundTurnTrill) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();

  // D4 on weak beat 1, long note: compound turn-trill.
  auto note = makeNote(kTicksPerBeat, 62, kTicksPerBeat);
  auto type = selectOrnamentType(note, config, timeline, kTicksPerBeat);

  EXPECT_EQ(type, OrnamentType::CompoundTurnTrill);
}

// ---------------------------------------------------------------------------
// Compound ornament eligibility
// ---------------------------------------------------------------------------

TEST(ContextOrnamentTest, ShortChordToneOnStrongBeatGetsTrillNotCompound) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();

  // C4 on strong beat, but only eighth note (240 ticks < kTicksPerBeat).
  auto note = makeNote(0, 60, kTicksPerBeat / 2);
  auto type = selectOrnamentType(note, config, timeline, 0);

  // Not eligible for compound, falls to chord tone preference: trill.
  EXPECT_EQ(type, OrnamentType::Trill);
}

TEST(ContextOrnamentTest, CompoundDisabledFallsToBasic) {
  auto timeline = makeCMajorTimeline();
  auto config = makeFullConfig();
  config.enable_compound = false;

  // C4 on strong beat, long note -- but compound disabled.
  auto note = makeNote(0, 60, kTicksPerBeat);
  auto type = selectOrnamentType(note, config, timeline, 0);

  // Falls to chord tone preference: trill.
  EXPECT_EQ(type, OrnamentType::Trill);
}

// ---------------------------------------------------------------------------
// computeTrillSpeed for various BPMs
// ---------------------------------------------------------------------------

TEST(ComputeTrillSpeedTest, BPM40Returns2) {
  EXPECT_EQ(computeTrillSpeed(40), 2);
}

TEST(ComputeTrillSpeedTest, BPM90Returns3) {
  EXPECT_EQ(computeTrillSpeed(90), 3);
}

TEST(ComputeTrillSpeedTest, BPM120Returns4) {
  EXPECT_EQ(computeTrillSpeed(120), 4);
}

TEST(ComputeTrillSpeedTest, BPM150Returns5) {
  EXPECT_EQ(computeTrillSpeed(150), 5);
}

TEST(ComputeTrillSpeedTest, BPM200Returns6) {
  EXPECT_EQ(computeTrillSpeed(200), 6);
}

// ---------------------------------------------------------------------------
// Integration: applyOrnaments with timeline context
// ---------------------------------------------------------------------------

TEST(ContextOrnamentTest, ApplyOrnamentsWithTimeline) {
  auto timeline = makeCMajorTimeline();

  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat),           // C4: chord tone, strong beat
      makeNote(kTicksPerBeat, 62, kTicksPerBeat),  // D4: non-chord tone, weak beat
  };

  OrnamentContext ctx;
  ctx.config = makeFullConfig();
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;
  ctx.timeline = &timeline;

  auto result = applyOrnaments(notes, ctx);

  // Both notes should be ornamented (density = 1.0).
  EXPECT_GT(result.size(), 2u);
}

TEST(ContextOrnamentTest, ApplyOrnamentsWithoutTimelineFallsBackToMetric) {
  std::vector<NoteEvent> notes = {
      makeNote(0, 60, kTicksPerBeat),
  };

  OrnamentContext ctx;
  ctx.config = makeFullConfig();
  ctx.config.ornament_density = 1.0f;
  ctx.role = VoiceRole::Respond;
  ctx.seed = 42;
  ctx.timeline = nullptr;  // No timeline = legacy behavior.

  auto result = applyOrnaments(notes, ctx);
  EXPECT_GT(result.size(), 1u);
}

}  // namespace
}  // namespace bach
