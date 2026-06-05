// Tests for composer/expression_events.h -- arc-driven registration CC plan and
// final ritardando tempo events.

#include "composer/expression_events.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach::composer {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Extract the CC#7 (volume) value sequence in tick order.
std::vector<std::uint8_t> volumeSequence(const std::vector<CcEvent>& events) {
  std::vector<std::uint8_t> values;
  for (const auto& evt : events) {
    if (evt.controller == 7) {
      values.push_back(evt.value);
    }
  }
  return values;
}

// ---------------------------------------------------------------------------
// buildRegistrationPlan
// ---------------------------------------------------------------------------

TEST(ExpressionEventsTest, RegistrationEmitsVolumeAndExpressionPairs) {
  const auto events = buildRegistrationPlan(16, 4, kTicksPerBar, 16 * kTicksPerBar);
  ASSERT_FALSE(events.empty());
  // Equal counts of CC#7 and CC#11.
  int vol = 0;
  int expr = 0;
  for (const auto& evt : events) {
    if (evt.controller == 7)
      ++vol;
    if (evt.controller == 11)
      ++expr;
  }
  EXPECT_EQ(vol, expr);
  EXPECT_GT(vol, 0);
}

TEST(ExpressionEventsTest, RegistrationValuesInMidiRange) {
  const auto events = buildRegistrationPlan(32, 6, kTicksPerBar, 32 * kTicksPerBar);
  for (const auto& evt : events) {
    EXPECT_LE(evt.value, 127);
  }
}

TEST(ExpressionEventsTest, RegistrationRisesToClimaxThenSettles) {
  const auto events = buildRegistrationPlan(24, 8, kTicksPerBar, 24 * kTicksPerBar);
  const auto vols = volumeSequence(events);
  ASSERT_GE(vols.size(), 4u);  // 4-point arc

  // Locate the peak.
  std::size_t peak_idx = 0;
  for (std::size_t idx = 1; idx < vols.size(); ++idx) {
    if (vols[idx] > vols[peak_idx])
      peak_idx = idx;
  }
  // Rise is monotone non-decreasing up to the peak.
  for (std::size_t idx = 1; idx <= peak_idx; ++idx) {
    EXPECT_GE(vols[idx], vols[idx - 1]) << "non-monotone rise at " << idx;
  }
  // After the peak the curve settles strictly below the peak (relaxation).
  ASSERT_LT(peak_idx, vols.size() - 1);
  EXPECT_LT(vols.back(), vols[peak_idx]) << "final value should settle below climax";
}

TEST(ExpressionEventsTest, RegistrationTicksWithinPieceAndSorted) {
  const Tick total = 20 * kTicksPerBar;
  const auto events = buildRegistrationPlan(20, 5, kTicksPerBar, total);
  ASSERT_FALSE(events.empty());
  Tick prev = 0;
  for (const auto& evt : events) {
    EXPECT_LT(evt.tick, total) << "registration point must fall before piece end";
    EXPECT_GE(evt.tick, prev) << "events must be in non-decreasing tick order";
    prev = evt.tick;
  }
  // Opening point is at tick 0.
  EXPECT_EQ(events.front().tick, 0u);
}

TEST(ExpressionEventsTest, RegistrationPointCountScalesWithCycles) {
  const Tick total = 16 * kTicksPerBar;
  // 2 points (opening + settle) -> 4 CC events.
  EXPECT_EQ(buildRegistrationPlan(16, 1, kTicksPerBar, total).size(), 4u);
  // 3 points -> 6 CC events.
  EXPECT_EQ(buildRegistrationPlan(16, 2, kTicksPerBar, total).size(), 6u);
  // 4 points -> 8 CC events.
  EXPECT_EQ(buildRegistrationPlan(16, 4, kTicksPerBar, total).size(), 8u);
}

TEST(ExpressionEventsTest, RegistrationEmptyForZeroTicks) {
  EXPECT_TRUE(buildRegistrationPlan(0, 4, kTicksPerBar, 0).empty());
}

TEST(ExpressionEventsTest, RegistrationDeterministic) {
  const auto a = buildRegistrationPlan(24, 7, kTicksPerBar, 24 * kTicksPerBar);
  const auto b = buildRegistrationPlan(24, 7, kTicksPerBar, 24 * kTicksPerBar);
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t idx = 0; idx < a.size(); ++idx) {
    EXPECT_EQ(a[idx].tick, b[idx].tick);
    EXPECT_EQ(a[idx].controller, b[idx].controller);
    EXPECT_EQ(a[idx].value, b[idx].value);
  }
}

// ---------------------------------------------------------------------------
// buildFinalRitardando
// ---------------------------------------------------------------------------

TEST(ExpressionEventsTest, RitardandoMonotoneDecreasing) {
  const std::uint16_t bpm = 100;
  const Tick total = 16 * kTicksPerBar;
  const auto events = buildFinalRitardando(bpm, total, kTicksPerBar);
  ASSERT_GE(events.size(), 2u);
  for (std::size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LT(events[idx].bpm, events[idx - 1].bpm) << "tempo must step down";
    EXPECT_GT(events[idx].tick, events[idx - 1].tick) << "ticks must advance";
  }
  // All decel steps stay below the base tempo.
  for (const auto& evt : events) {
    EXPECT_LT(evt.bpm, bpm);
  }
}

TEST(ExpressionEventsTest, RitardandoValuesMatchDesign) {
  const std::uint16_t bpm = 100;
  const auto events = buildFinalRitardando(bpm, 16 * kTicksPerBar, kTicksPerBar);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].bpm, 92);  // 92%
  EXPECT_EQ(events[1].bpm, 85);  // 85%
}

TEST(ExpressionEventsTest, RitardandoLastEventBeforeTotalTicks) {
  const Tick total = 12 * kTicksPerBar;
  const auto events = buildFinalRitardando(120, total, kTicksPerBar);
  ASSERT_FALSE(events.empty());
  EXPECT_LT(events.back().tick, total);
  // The two steps land in the final two bars.
  EXPECT_EQ(events.front().tick, total - 2 * kTicksPerBar);
  EXPECT_EQ(events.back().tick, total - kTicksPerBar);
}

TEST(ExpressionEventsTest, RitardandoShortPieceSingleStep) {
  // One-bar piece: a single 85% step, placed before the end.
  const Tick total = kTicksPerBar;
  const auto events = buildFinalRitardando(120, total, kTicksPerBar);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].bpm, 102);  // 85% of 120
  EXPECT_LT(events[0].tick, total);
}

TEST(ExpressionEventsTest, RitardandoEmptyForZeroTicks) {
  EXPECT_TRUE(buildFinalRitardando(120, 0, kTicksPerBar).empty());
}

TEST(ExpressionEventsTest, RitardandoDeterministic) {
  const auto a = buildFinalRitardando(108, 20 * kTicksPerBar, kTicksPerBar);
  const auto b = buildFinalRitardando(108, 20 * kTicksPerBar, kTicksPerBar);
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t idx = 0; idx < a.size(); ++idx) {
    EXPECT_EQ(a[idx].tick, b[idx].tick);
    EXPECT_EQ(a[idx].bpm, b[idx].bpm);
  }
}

}  // namespace
}  // namespace bach::composer
