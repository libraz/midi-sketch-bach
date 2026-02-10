// Tests for expression/expression_curve.h -- CC#11 expression curve generation
// from phrase boundaries.

#include "expression/expression_curve.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ===========================================================================
// Empty phrases
// ===========================================================================

TEST(ExpressionCurveTest, EmptyPhrasesReturnsSingleEvent) {
  std::vector<PhraseBoundary> empty_phrases;
  auto events = generateExpressionCurve(empty_phrases, 0, kTicksPerBar * 4);

  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].channel, 0);
  EXPECT_EQ(events[0].value, 100);
}

// ===========================================================================
// Single phrase
// ===========================================================================

TEST(ExpressionCurveTest, SinglePhraseCorrectRamp) {
  // One phrase boundary at bar 2 -> creates two phrase spans:
  // phrase 0: [0, kTicksPerBar*2) and phrase 1: [kTicksPerBar*2, kTicksPerBar*4)
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 2, CadenceType::Perfect});

  Tick total = kTicksPerBar * 4;
  auto events = generateExpressionCurve(phrases, 0, total);

  // Should have multiple events (start, q1, mid, q3, end for each phrase,
  // plus breath between phrases).
  ASSERT_GE(events.size(), 5u);

  // First event should be at tick 0 with value 100 (phrase start).
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].value, 100);
}

TEST(ExpressionCurveTest, PeakValueIs115) {
  // Single phrase spanning 4 bars (long enough for interior points).
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 4, CadenceType::Perfect});

  Tick total = kTicksPerBar * 8;
  auto events = generateExpressionCurve(phrases, 0, total);

  // Find the peak value (should be 115 at the phrase midpoint).
  bool found_peak = false;
  for (const auto& evt : events) {
    if (evt.value == 115) {
      found_peak = true;
      break;
    }
  }
  EXPECT_TRUE(found_peak) << "Expected to find peak value 115 in expression curve";
}

TEST(ExpressionCurveTest, PhraseEndValueIs95) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 4, CadenceType::Perfect});

  Tick total = kTicksPerBar * 8;
  auto events = generateExpressionCurve(phrases, 0, total);

  // At least one event should have value 95 (phrase end).
  bool found_end = false;
  for (const auto& evt : events) {
    if (evt.value == 95) {
      found_end = true;
      break;
    }
  }
  EXPECT_TRUE(found_end) << "Expected to find phrase end value 95";
}

// ===========================================================================
// Multiple phrases with inter-phrase dips
// ===========================================================================

TEST(ExpressionCurveTest, MultiplePhrasesHaveBreathDips) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 2, CadenceType::Perfect});
  phrases.push_back({kTicksPerBar * 4, CadenceType::Half});

  Tick total = kTicksPerBar * 6;
  auto events = generateExpressionCurve(phrases, 0, total);

  // Look for breath value (80) between phrases.
  bool found_breath = false;
  for (const auto& evt : events) {
    if (evt.value == 80) {
      found_breath = true;
      break;
    }
  }
  EXPECT_TRUE(found_breath) << "Expected breath dip (value 80) between phrases";
}

TEST(ExpressionCurveTest, BreathDipBetweenPhrases) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 4, CadenceType::Perfect});

  Tick total = kTicksPerBar * 8;
  auto events = generateExpressionCurve(phrases, 0, total);

  // The breath event should be placed just before the second phrase start.
  bool found_breath = false;
  for (const auto& evt : events) {
    if (evt.value == 80 && evt.tick > 0) {
      found_breath = true;
      // Breath should be near phrase boundary.
      EXPECT_NEAR(evt.tick, kTicksPerBar * 4, kTicksPerBar);
      break;
    }
  }
  EXPECT_TRUE(found_breath);
}

// ===========================================================================
// MIDI range validation
// ===========================================================================

TEST(ExpressionCurveTest, AllValuesInValidMidiRange) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 2, CadenceType::Perfect});
  phrases.push_back({kTicksPerBar * 4, CadenceType::Half});
  phrases.push_back({kTicksPerBar * 6, CadenceType::Deceptive});

  Tick total = kTicksPerBar * 8;
  auto events = generateExpressionCurve(phrases, 0, total);

  for (const auto& evt : events) {
    EXPECT_LE(evt.value, 127) << "CC value exceeds 127 at tick " << evt.tick;
    // CC#11 values should be > 0 for expression (silence is not useful).
    EXPECT_GT(evt.value, 0) << "CC value is 0 at tick " << evt.tick;
  }
}

// ===========================================================================
// Channel routing
// ===========================================================================

TEST(ExpressionCurveTest, CorrectChannelAssignment) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 2, CadenceType::Perfect});

  uint8_t target_channel = 3;
  auto events = generateExpressionCurve(phrases, target_channel, kTicksPerBar * 4);

  for (const auto& evt : events) {
    EXPECT_EQ(evt.channel, target_channel);
  }
}

// ===========================================================================
// Events are chronological
// ===========================================================================

TEST(ExpressionCurveTest, EventsAreChronological) {
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar * 2, CadenceType::Perfect});
  phrases.push_back({kTicksPerBar * 5, CadenceType::Half});
  phrases.push_back({kTicksPerBar * 8, CadenceType::Perfect});

  Tick total = kTicksPerBar * 10;
  auto events = generateExpressionCurve(phrases, 0, total);

  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_GE(events[idx].tick, events[idx - 1].tick)
        << "Events not chronological at index " << idx;
  }
}

// ===========================================================================
// Short phrase (below 1 bar) -- no interior points
// ===========================================================================

TEST(ExpressionCurveTest, ShortPhraseMinimalEvents) {
  // Boundary very close to start: phrase 0 is only half a bar.
  std::vector<PhraseBoundary> phrases;
  phrases.push_back({kTicksPerBar / 2, CadenceType::Perfect});

  Tick total = kTicksPerBar * 2;
  auto events = generateExpressionCurve(phrases, 0, total);

  // The short first phrase should still generate at least start and end events.
  ASSERT_GE(events.size(), 2u);

  // All values should be valid.
  for (const auto& evt : events) {
    EXPECT_LE(evt.value, 127);
    EXPECT_GT(evt.value, 0);
  }
}

}  // namespace
}  // namespace bach
