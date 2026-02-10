// Tests for expression/phrase_detector.h -- phrase boundary detection from
// harmonic progressions and key changes.

#include "expression/phrase_detector.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a harmonic event
// ---------------------------------------------------------------------------

static HarmonicEvent makeHarmEvent(Tick tick, Tick end_tick, ChordDegree degree,
                                   Key key = Key::C, bool is_minor = false) {
  HarmonicEvent evt;
  evt.tick = tick;
  evt.end_tick = end_tick;
  evt.key = key;
  evt.is_minor = is_minor;
  evt.chord.degree = degree;
  evt.chord.quality = is_minor ? minorKeyQuality(degree) : majorKeyQuality(degree);
  evt.chord.root_pitch = 60;
  evt.bass_pitch = 48;
  evt.weight = 1.0f;
  return evt;
}

// ===========================================================================
// Empty / trivial timelines
// ===========================================================================

TEST(PhraseDetectorTest, EmptyTimelineReturnsEmpty) {
  HarmonicTimeline timeline;
  auto boundaries = detectPhraseBoundaries(timeline);
  EXPECT_TRUE(boundaries.empty());
}

TEST(PhraseDetectorTest, SingleEventReturnsEmpty) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  auto boundaries = detectPhraseBoundaries(timeline);
  EXPECT_TRUE(boundaries.empty());
}

// ===========================================================================
// Perfect cadence detection (V -> I)
// ===========================================================================

TEST(PhraseDetectorTest, PerfectCadenceDetected) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_EQ(boundaries.size(), 1u);
  EXPECT_EQ(boundaries[0].tick, kTicksPerBar);
  EXPECT_EQ(boundaries[0].cadence, CadenceType::Perfect);
}

TEST(PhraseDetectorTest, NoCadenceWhenIToIV) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::IV));

  auto boundaries = detectPhraseBoundaries(timeline);
  EXPECT_TRUE(boundaries.empty());
}

// ===========================================================================
// Deceptive cadence detection (V -> vi)
// ===========================================================================

TEST(PhraseDetectorTest, DeceptiveCadenceDetected) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::vi));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_EQ(boundaries.size(), 1u);
  EXPECT_EQ(boundaries[0].tick, kTicksPerBar);
  EXPECT_EQ(boundaries[0].cadence, CadenceType::Deceptive);
}

// ===========================================================================
// Half cadence detection (-> V)
// ===========================================================================

TEST(PhraseDetectorTest, HalfCadenceDetected) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_EQ(boundaries.size(), 1u);
  EXPECT_EQ(boundaries[0].tick, kTicksPerBar);
  EXPECT_EQ(boundaries[0].cadence, CadenceType::Half);
}

TEST(PhraseDetectorTest, SustainedDominantNotDoubleCounted) {
  // V -> V should not create a half cadence boundary.
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V));

  auto boundaries = detectPhraseBoundaries(timeline);
  EXPECT_TRUE(boundaries.empty());
}

// ===========================================================================
// Key change detection
// ===========================================================================

TEST(PhraseDetectorTest, KeyChangeBoundaryDetected) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I, Key::C));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I, Key::G));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_EQ(boundaries.size(), 1u);
  EXPECT_EQ(boundaries[0].tick, kTicksPerBar);
  EXPECT_EQ(boundaries[0].cadence, CadenceType::Perfect);  // Key changes are strong boundaries.
}

TEST(PhraseDetectorTest, ModeChangeBoundaryDetected) {
  // Same tonic but mode changes (major -> minor).
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I, Key::C, false));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I, Key::C, true));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_EQ(boundaries.size(), 1u);
  EXPECT_EQ(boundaries[0].tick, kTicksPerBar);
}

// ===========================================================================
// Multiple cadences
// ===========================================================================

TEST(PhraseDetectorTest, MultipleCadencesInOrder) {
  HarmonicTimeline timeline;
  // Phrase 1: I - V
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V));
  // Phrase 2: I continuation (V -> I = perfect cadence)
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::I));
  // Phrase 3: IV - V - I
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 3, kTicksPerBar * 4, ChordDegree::IV));
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 4, kTicksPerBar * 5, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 5, kTicksPerBar * 6, ChordDegree::I));

  auto boundaries = detectPhraseBoundaries(timeline);

  // Expected: Half (I->V), Perfect (V->I), Half (IV->V), Perfect (V->I)
  // But IV->V is a half cadence, not just any chord to V
  ASSERT_GE(boundaries.size(), 3u);

  // All boundaries should be in chronological order.
  for (size_t idx = 1; idx < boundaries.size(); ++idx) {
    EXPECT_GE(boundaries[idx].tick, boundaries[idx - 1].tick);
  }
}

TEST(PhraseDetectorTest, CadenceTicksAreChronological) {
  HarmonicTimeline timeline;
  // Build a longer progression with multiple cadences.
  timeline.addEvent(makeHarmEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::I));
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 3, kTicksPerBar * 4, ChordDegree::V));
  timeline.addEvent(makeHarmEvent(kTicksPerBar * 4, kTicksPerBar * 5, ChordDegree::vi));

  auto boundaries = detectPhraseBoundaries(timeline);
  ASSERT_GE(boundaries.size(), 2u);

  for (size_t idx = 1; idx < boundaries.size(); ++idx) {
    EXPECT_GT(boundaries[idx].tick, boundaries[idx - 1].tick);
  }
}

}  // namespace
}  // namespace bach
