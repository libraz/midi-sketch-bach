// Tests for harmony/harmonic_timeline.h -- event lookup, key change detection,
// standard progression generation.

#include "harmony/harmonic_timeline.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a simple event
// ---------------------------------------------------------------------------

static HarmonicEvent makeEvent(Tick tick, Tick end_tick, Key key, bool is_minor,
                               ChordDegree degree) {
  HarmonicEvent event;
  event.tick = tick;
  event.end_tick = end_tick;
  event.key = key;
  event.is_minor = is_minor;
  event.chord.degree = degree;
  event.chord.quality = is_minor ? minorKeyQuality(degree) : majorKeyQuality(degree);
  event.chord.root_pitch = 60;
  event.bass_pitch = 48;
  event.weight = 1.0f;
  return event;
}

// ---------------------------------------------------------------------------
// Empty timeline
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, EmptyTimeline) {
  HarmonicTimeline timeline;
  EXPECT_EQ(timeline.size(), 0u);
  EXPECT_EQ(timeline.totalDuration(), 0u);
  EXPECT_TRUE(timeline.events().empty());
}

TEST(HarmonicTimelineTest, EmptyTimelineGetAtReturnsDefault) {
  HarmonicTimeline timeline;
  const auto& event = timeline.getAt(0);
  EXPECT_EQ(event.key, Key::C);
  EXPECT_FALSE(event.is_minor);
  EXPECT_EQ(event.chord.degree, ChordDegree::I);
  EXPECT_EQ(event.chord.quality, ChordQuality::Major);
}

TEST(HarmonicTimelineTest, EmptyTimelineGetKeyAt) {
  HarmonicTimeline timeline;
  EXPECT_EQ(timeline.getKeyAt(0), Key::C);
  EXPECT_EQ(timeline.getKeyAt(1000), Key::C);
}

TEST(HarmonicTimelineTest, EmptyTimelineIsKeyChange) {
  HarmonicTimeline timeline;
  EXPECT_FALSE(timeline.isKeyChange(0));
}

// ---------------------------------------------------------------------------
// addEvent and getAt
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, SingleEvent) {
  HarmonicTimeline timeline;
  auto event = makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I);
  timeline.addEvent(event);

  EXPECT_EQ(timeline.size(), 1u);
  EXPECT_EQ(timeline.totalDuration(), kTicksPerBar);

  const auto& found = timeline.getAt(0);
  EXPECT_EQ(found.key, Key::C);
  EXPECT_EQ(found.chord.degree, ChordDegree::I);
}

TEST(HarmonicTimelineTest, MultipleEvents) {
  HarmonicTimeline timeline;

  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::C, false, ChordDegree::IV));
  timeline.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, Key::C, false, ChordDegree::V));
  timeline.addEvent(makeEvent(kTicksPerBar * 3, kTicksPerBar * 4, Key::C, false, ChordDegree::I));

  EXPECT_EQ(timeline.size(), 4u);

  // Query within each event's range.
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::IV);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).degree, ChordDegree::I);
}

TEST(HarmonicTimelineTest, GetAtMidEvent) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::G, false, ChordDegree::V));

  // Query in the middle of the first event.
  EXPECT_EQ(timeline.getKeyAt(kTicksPerBeat * 2), Key::C);

  // Query in the middle of the second event.
  EXPECT_EQ(timeline.getKeyAt(kTicksPerBar + kTicksPerBeat), Key::G);
}

TEST(HarmonicTimelineTest, GetAtBeforeFirstEvent) {
  HarmonicTimeline timeline;
  // First event starts at tick 480 (not 0).
  timeline.addEvent(makeEvent(kTicksPerBeat, kTicksPerBar, Key::G, false, ChordDegree::V));

  // Query before the first event returns default.
  const auto& result = timeline.getAt(0);
  EXPECT_EQ(result.key, Key::C);
  EXPECT_EQ(result.chord.degree, ChordDegree::I);
}

// ---------------------------------------------------------------------------
// getKeyAt
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, GetKeyAtWithModulation) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar * 2, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 4, Key::G, false, ChordDegree::I));

  EXPECT_EQ(timeline.getKeyAt(0), Key::C);
  EXPECT_EQ(timeline.getKeyAt(kTicksPerBar), Key::C);
  EXPECT_EQ(timeline.getKeyAt(kTicksPerBar * 2), Key::G);
  EXPECT_EQ(timeline.getKeyAt(kTicksPerBar * 3), Key::G);
}

// ---------------------------------------------------------------------------
// getChordAt
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, GetChordAtReturnsCorrectChord) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::C, false, ChordDegree::V));

  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(0).quality, ChordQuality::Major);

  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).quality, ChordQuality::Major);
}

// ---------------------------------------------------------------------------
// isKeyChange
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, IsKeyChangeAtModulation) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar * 2, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 4, Key::G, false, ChordDegree::I));

  // Not a key change at tick 0 (first event in C, default is also C).
  EXPECT_FALSE(timeline.isKeyChange(0));

  // Key change at the boundary.
  EXPECT_TRUE(timeline.isKeyChange(kTicksPerBar * 2));

  // Not a key change mid-event.
  EXPECT_FALSE(timeline.isKeyChange(kTicksPerBar));
  EXPECT_FALSE(timeline.isKeyChange(kTicksPerBar * 3));
}

TEST(HarmonicTimelineTest, IsKeyChangeFirstEventNonDefault) {
  HarmonicTimeline timeline;
  // First event is G major, not C major (differs from default).
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::G, false, ChordDegree::I));

  EXPECT_TRUE(timeline.isKeyChange(0));
}

TEST(HarmonicTimelineTest, IsKeyChangeModeChange) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::C, true, ChordDegree::I));

  // Same tonic but mode change = key change.
  EXPECT_TRUE(timeline.isKeyChange(kTicksPerBar));
}

TEST(HarmonicTimelineTest, IsKeyChangeSameKey) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::C, false, ChordDegree::V));

  // Same key, different chord = not a key change.
  EXPECT_FALSE(timeline.isKeyChange(kTicksPerBar));
}

// ---------------------------------------------------------------------------
// totalDuration and size
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineTest, TotalDuration) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 3, Key::C, false, ChordDegree::V));

  EXPECT_EQ(timeline.totalDuration(), kTicksPerBar * 3);
}

TEST(HarmonicTimelineTest, SizeMatchesAdded) {
  HarmonicTimeline timeline;
  EXPECT_EQ(timeline.size(), 0u);

  timeline.addEvent(makeEvent(0, kTicksPerBar, Key::C, false, ChordDegree::I));
  EXPECT_EQ(timeline.size(), 1u);

  timeline.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, Key::C, false, ChordDegree::IV));
  EXPECT_EQ(timeline.size(), 2u);
}

// ---------------------------------------------------------------------------
// createStandard -- Beat resolution
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, BeatResolution) {
  KeySignature c_major = {Key::C, false};
  Tick duration = kTicksPerBar * 4;  // 4 bars

  auto timeline = HarmonicTimeline::createStandard(c_major, duration,
                                                   HarmonicResolution::Beat);

  // At beat resolution, one event per beat for 4 bars = 16 events.
  EXPECT_EQ(timeline.size(), 16u);
  EXPECT_EQ(timeline.totalDuration(), duration);

  // First event is I chord.
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);

  // All events have C as key.
  for (const auto& event : timeline.events()) {
    EXPECT_EQ(event.key, Key::C);
    EXPECT_FALSE(event.is_minor);
  }
}

// ---------------------------------------------------------------------------
// createStandard -- Bar resolution
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, BarResolution) {
  KeySignature c_major = {Key::C, false};
  Tick duration = kTicksPerBar * 4;  // 4 bars

  auto timeline = HarmonicTimeline::createStandard(c_major, duration,
                                                   HarmonicResolution::Bar);

  // At bar resolution, one event per bar for 4 bars = 4 events.
  EXPECT_EQ(timeline.size(), 4u);
  EXPECT_EQ(timeline.totalDuration(), duration);

  // I - IV - V - I progression.
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::IV);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).degree, ChordDegree::I);
}

// ---------------------------------------------------------------------------
// createStandard -- Section resolution
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, SectionResolution) {
  KeySignature g_minor = {Key::G, true};
  Tick duration = kTicksPerBar * 8;  // 8 bars

  auto timeline = HarmonicTimeline::createStandard(g_minor, duration,
                                                   HarmonicResolution::Section);

  // Section resolution: divides into 4 sections.
  EXPECT_EQ(timeline.size(), 4u);
  EXPECT_EQ(timeline.totalDuration(), duration);

  // First event should be i (minor tonic).
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(0).quality, ChordQuality::Minor);

  // All events should be in G minor.
  for (const auto& event : timeline.events()) {
    EXPECT_EQ(event.key, Key::G);
    EXPECT_TRUE(event.is_minor);
  }
}

// ---------------------------------------------------------------------------
// createStandard -- minor key qualities
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, MinorKeyQualities) {
  KeySignature a_minor = {Key::A, true};
  Tick duration = kTicksPerBar * 4;

  auto timeline = HarmonicTimeline::createStandard(a_minor, duration,
                                                   HarmonicResolution::Bar);

  // i = Minor, iv = Minor, V = Dominant7 (createStandard overrides V), i = Minor.
  EXPECT_EQ(timeline.getChordAt(0).quality, ChordQuality::Minor);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).quality, ChordQuality::Minor);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).quality, ChordQuality::Dominant7);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).quality, ChordQuality::Minor);
}

// ---------------------------------------------------------------------------
// createStandard -- zero duration
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, ZeroDuration) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, 0, HarmonicResolution::Bar);

  EXPECT_EQ(timeline.size(), 0u);
  EXPECT_EQ(timeline.totalDuration(), 0u);
}

// ---------------------------------------------------------------------------
// createStandard -- bass pitch is in low register
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, BassPitchInLowRegister) {
  KeySignature c_major = {Key::C, false};
  Tick duration = kTicksPerBar * 4;

  auto timeline = HarmonicTimeline::createStandard(c_major, duration,
                                                   HarmonicResolution::Bar);

  for (const auto& event : timeline.events()) {
    // Bass pitch should be in octave 2 range (roughly 36-47 for C2 chromatic).
    EXPECT_GE(event.bass_pitch, 36);
    EXPECT_LE(event.bass_pitch, 59);
  }
}

// ---------------------------------------------------------------------------
// createStandard -- event boundaries are contiguous
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, ContiguousEvents) {
  KeySignature c_major = {Key::C, false};
  Tick duration = kTicksPerBar * 4;

  auto timeline = HarmonicTimeline::createStandard(c_major, duration,
                                                   HarmonicResolution::Bar);

  const auto& evts = timeline.events();
  ASSERT_GE(evts.size(), 2u);

  // Check that each event's end_tick equals the next event's tick.
  for (size_t idx = 0; idx + 1 < evts.size(); ++idx) {
    EXPECT_EQ(evts[idx].end_tick, evts[idx + 1].tick)
        << "Gap between event " << idx << " and " << idx + 1;
  }

  // First event starts at 0.
  EXPECT_EQ(evts[0].tick, 0u);

  // Last event ends at duration.
  EXPECT_EQ(evts.back().end_tick, duration);
}

// ---------------------------------------------------------------------------
// createStandard -- weight pattern
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, WeightPattern) {
  KeySignature c_major = {Key::C, false};
  Tick duration = kTicksPerBar * 4;

  auto timeline = HarmonicTimeline::createStandard(c_major, duration,
                                                   HarmonicResolution::Bar);

  // I-IV-V-I weights: 1.0, 0.5, 0.75, 1.0
  const auto& evts = timeline.events();
  ASSERT_EQ(evts.size(), 4u);
  EXPECT_FLOAT_EQ(evts[0].weight, 1.0f);
  EXPECT_FLOAT_EQ(evts[1].weight, 0.5f);
  EXPECT_FLOAT_EQ(evts[2].weight, 0.75f);
  EXPECT_FLOAT_EQ(evts[3].weight, 1.0f);
}

// ---------------------------------------------------------------------------
// createStandard -- V chord is now Dominant7
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateStandardTest, VChordIsDominant7) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  // V chord (3rd event, index 2) should now be Dominant7.
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).quality, ChordQuality::Dominant7);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::V);
}

// ---------------------------------------------------------------------------
// createProgression -- CircleOfFifths
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateProgressionTest, CircleOfFifths_BarResolution) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createProgression(c_major, kTicksPerBar * 5,
                                                      HarmonicResolution::Bar,
                                                      ProgressionType::CircleOfFifths);
  EXPECT_EQ(timeline.size(), 5u);
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::vi);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::ii);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).quality, ChordQuality::Dominant7);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 4).degree, ChordDegree::I);
}

TEST(HarmonicTimelineCreateProgressionTest, Subdominant_BarResolution) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createProgression(c_major, kTicksPerBar * 5,
                                                      HarmonicResolution::Bar,
                                                      ProgressionType::Subdominant);
  EXPECT_EQ(timeline.size(), 5u);
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::IV);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::ii);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).quality, ChordQuality::Dominant7);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 4).degree, ChordDegree::I);
}

TEST(HarmonicTimelineCreateProgressionTest, BasicFallsBackToStandard) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createProgression(c_major, kTicksPerBar * 4,
                                                      HarmonicResolution::Bar,
                                                      ProgressionType::Basic);
  EXPECT_EQ(timeline.size(), 4u);
  EXPECT_EQ(timeline.getChordAt(0).degree, ChordDegree::I);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar).degree, ChordDegree::IV);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 2).degree, ChordDegree::V);
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).degree, ChordDegree::I);
}

TEST(HarmonicTimelineCreateProgressionTest, CircleOfFifths_MinorKey) {
  KeySignature a_minor = {Key::A, true};
  auto timeline = HarmonicTimeline::createProgression(a_minor, kTicksPerBar * 5,
                                                      HarmonicResolution::Bar,
                                                      ProgressionType::CircleOfFifths);
  EXPECT_EQ(timeline.size(), 5u);
  // i in minor should be Minor quality.
  EXPECT_EQ(timeline.getChordAt(0).quality, ChordQuality::Minor);
  // V7 override should still be Dominant7.
  EXPECT_EQ(timeline.getChordAt(kTicksPerBar * 3).quality, ChordQuality::Dominant7);
}

TEST(HarmonicTimelineCreateProgressionTest, ZeroDuration) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createProgression(c_major, 0,
                                                      HarmonicResolution::Bar,
                                                      ProgressionType::CircleOfFifths);
  EXPECT_EQ(timeline.size(), 0u);
}

// ---------------------------------------------------------------------------
// applyCadence
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCadenceTest, PerfectCadence) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  timeline.applyCadence(CadenceType::Perfect, c_major);
  const auto& evts = timeline.events();
  // Penultimate should be V7.
  EXPECT_EQ(evts[evts.size() - 2].chord.degree, ChordDegree::V);
  EXPECT_EQ(evts[evts.size() - 2].chord.quality, ChordQuality::Dominant7);
  // Last should be I.
  EXPECT_EQ(evts.back().chord.degree, ChordDegree::I);
}

TEST(HarmonicTimelineCadenceTest, DeceptiveCadence) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  timeline.applyCadence(CadenceType::Deceptive, c_major);
  const auto& evts = timeline.events();
  // Penultimate should be V7.
  EXPECT_EQ(evts[evts.size() - 2].chord.degree, ChordDegree::V);
  EXPECT_EQ(evts[evts.size() - 2].chord.quality, ChordQuality::Dominant7);
  // Last should be vi.
  EXPECT_EQ(evts.back().chord.degree, ChordDegree::vi);
}

TEST(HarmonicTimelineCadenceTest, HalfCadence) {
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  timeline.applyCadence(CadenceType::Half, c_major);
  // Last chord should be V.
  EXPECT_EQ(timeline.events().back().chord.degree, ChordDegree::V);
}

TEST(HarmonicTimelineCadenceTest, PhrygianCadence) {
  KeySignature a_minor = {Key::A, true};
  auto timeline = HarmonicTimeline::createStandard(a_minor, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  timeline.applyCadence(CadenceType::Phrygian, a_minor);
  const auto& evts = timeline.events();
  // Penultimate should be IV in first inversion.
  EXPECT_EQ(evts[evts.size() - 2].chord.degree, ChordDegree::IV);
  EXPECT_EQ(evts[evts.size() - 2].chord.inversion, 1);
  // Last should be V.
  EXPECT_EQ(evts.back().chord.degree, ChordDegree::V);
}

TEST(HarmonicTimelineCadenceTest, PicardyThird) {
  KeySignature a_minor = {Key::A, true};
  auto timeline = HarmonicTimeline::createStandard(a_minor, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  timeline.applyCadence(CadenceType::PicardyThird, a_minor);
  // Final chord should be Major quality (Picardy third).
  EXPECT_EQ(timeline.events().back().chord.degree, ChordDegree::I);
  EXPECT_EQ(timeline.events().back().chord.quality, ChordQuality::Major);
}

TEST(HarmonicTimelineCadenceTest, EmptyTimelineNoCrash) {
  HarmonicTimeline timeline;
  KeySignature c_major = {Key::C, false};
  timeline.applyCadence(CadenceType::Perfect, c_major);  // Should not crash.
  EXPECT_EQ(timeline.size(), 0u);
}

// ---------------------------------------------------------------------------
// Chord inversions affect bass pitch
// ---------------------------------------------------------------------------

TEST(HarmonicTimelineCreateProgressionTest, InversionAffectsBassPitch) {
  // Test that first inversion changes the bass pitch from root to third.
  KeySignature c_major = {Key::C, false};
  auto timeline = HarmonicTimeline::createStandard(c_major, kTicksPerBar * 4,
                                                   HarmonicResolution::Bar);
  // Apply Phrygian cadence which sets IV to first inversion.
  timeline.applyCadence(CadenceType::Phrygian, c_major);
  const auto& evts = timeline.events();
  auto& penult = evts[evts.size() - 2];
  // IV in C major = F major chord. First inversion = A in bass.
  // A in octave 2 = MIDI 45.
  // F root pitch class = 5, third interval for Major = 4, so bass_pc = (5+4)%12 = 9 = A
  // A2 = (2+1)*12 + 9 = 45
  EXPECT_EQ(penult.bass_pitch, 45);
}

}  // namespace
}  // namespace bach
