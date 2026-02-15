// Tests for counterpoint/coordinate_voices.h -- unified coordination pass.

#include "counterpoint/coordinate_voices.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice,
                   BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.voice = voice;
  n.velocity = 80;
  n.source = source;
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
  ev.bass_pitch = 48;
  tl.addEvent(ev);
  return tl;
}

CoordinationConfig makeBasicConfig(const HarmonicTimeline& tl,
                                   uint8_t num_voices = 2) {
  CoordinationConfig config;
  config.num_voices = num_voices;
  config.tonic = Key::C;
  config.timeline = &tl;
  config.voice_range = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
    if (v == 0) return {60, 84};
    return {36, 60};
  };
  config.form_name = "Test";
  return config;
}

// ---- Immutable sources pass through unchanged ----

TEST(CoordinateVoicesTest, ImmutableSourceAcceptedUnchanged) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);
  config.immutable_sources = {BachNoteSource::PedalPoint};

  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 36, 1, BachNoteSource::PedalPoint),
  };

  auto result = coordinateVoices(std::move(notes), config);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(36, result[0].pitch);
  EXPECT_EQ(BachNoteSource::PedalPoint, result[0].source);
}

// ---- Empty input returns empty ----

TEST(CoordinateVoicesTest, EmptyInputReturnsEmpty) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);

  auto result = coordinateVoices({}, config);
  EXPECT_TRUE(result.empty());
}

// ---- Lightweight source: consonant pitch accepted ----

TEST(CoordinateVoicesTest, LightweightConsonantAccepted) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);
  config.lightweight_sources = {BachNoteSource::ArpeggioFlow};

  // Beat 0: E4(64) is a chord tone of C major I -- accepted.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64, 0, BachNoteSource::ArpeggioFlow),
  };

  auto result = coordinateVoices(std::move(notes), config);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(64, result[0].pitch);
}

// ---- Lightweight source: out-of-range pitch rejected ----

TEST(CoordinateVoicesTest, LightweightOutOfRangeRejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);
  config.lightweight_sources = {BachNoteSource::ArpeggioFlow};

  // Voice 0 range is [60, 84]. Pitch 50 is out of range.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 50, 0, BachNoteSource::ArpeggioFlow),
  };

  auto result = coordinateVoices(std::move(notes), config);
  EXPECT_TRUE(result.empty());
}

// ---- Lightweight: weak beat m2 rejected ----

TEST(CoordinateVoicesTest, LightweightWeakBeatM2Rejected) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);
  config.immutable_sources = {BachNoteSource::PedalPoint};
  config.lightweight_sources = {BachNoteSource::EpisodeMaterial};

  // Pedal C3(48) lasts whole bar -- immutable.
  // Beat 1: Db3(49) in voice 0 = m2 over C3 -> rejected.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBar, 48, 1, BachNoteSource::PedalPoint),
      makeNote(kTicksPerBeat, kTicksPerBeat, 61, 0,
               BachNoteSource::EpisodeMaterial),
  };

  auto result = coordinateVoices(std::move(notes), config);
  // Only pedal accepted.
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ(BachNoteSource::PedalPoint, result[0].source);
}

// ---- Full tier: createBachNote pipeline processes notes ----

TEST(CoordinateVoicesTest, FullTierProcessesNote) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);

  // A FreeCounterpoint note on beat 0: C4(60) chord tone -- should be accepted.
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60, 0, BachNoteSource::FreeCounterpoint),
  };

  auto result = coordinateVoices(std::move(notes), config);
  ASSERT_GE(result.size(), 1u);
}

// ---- Multiple immutable sources ----

TEST(CoordinateVoicesTest, MultipleTiersMixed) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);
  config.immutable_sources = {BachNoteSource::PedalPoint,
                              BachNoteSource::CantusFixed};

  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 48, 1, BachNoteSource::PedalPoint),
      makeNote(0, kTicksPerBeat, 72, 0, BachNoteSource::CantusFixed),
  };

  auto result = coordinateVoices(std::move(notes), config);
  ASSERT_EQ(2u, result.size());
}

// ---- Acceptance rate is reasonable with consonant chord tones ----

TEST(CoordinateVoicesTest, AcceptanceRateReasonable) {
  auto tl = makeCMajorTimeline(kTicksPerBar * 4);
  auto config = makeBasicConfig(tl);

  // Generate a sequence of chord tones (C, E, G) at different beats.
  std::vector<NoteEvent> notes;
  uint8_t chord_tones[] = {60, 64, 67, 72};
  for (int i = 0; i < 4; ++i) {
    notes.push_back(makeNote(static_cast<Tick>(i * kTicksPerBeat),
                             kTicksPerBeat, chord_tones[i], 0));
  }

  auto result = coordinateVoices(std::move(notes), config);
  // All notes are chord tones in the correct range -- most should be accepted.
  EXPECT_GE(result.size(), 2u);
}

}  // namespace
}  // namespace bach
