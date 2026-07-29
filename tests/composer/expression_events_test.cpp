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

TEST(ExpressionEventsTest, RegistrationEmitsDiscreteVolumeOnly) {
  const auto events = buildRegistrationPlan(16, 4, kTicksPerBar, 16 * kTicksPerBar);
  ASSERT_FALSE(events.empty());
  int vol = 0;
  int expr = 0;
  for (const auto& evt : events) {
    if (evt.controller == 7)
      ++vol;
    if (evt.controller == 11)
      ++expr;
  }
  EXPECT_GT(vol, 0);
  EXPECT_EQ(expr, 0);
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
  EXPECT_EQ(buildRegistrationPlan(16, 1, kTicksPerBar, total).size(), 2u);
  EXPECT_EQ(buildRegistrationPlan(16, 2, kTicksPerBar, total).size(), 3u);
  EXPECT_EQ(buildRegistrationPlan(16, 4, kTicksPerBar, total).size(), 4u);
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

TEST(ExpressionEventsTest, RegistrationDefaultClimaxIsLegacyByteIdentical) {
  const auto legacy = buildRegistrationPlan(24, 8, kTicksPerBar, 24 * kTicksPerBar);
  const auto explicit_zero = buildRegistrationPlan(24, 8, kTicksPerBar, 24 * kTicksPerBar, 0);
  ASSERT_EQ(legacy.size(), explicit_zero.size());
  for (std::size_t idx = 0; idx < legacy.size(); ++idx) {
    EXPECT_EQ(legacy[idx].tick, explicit_zero[idx].tick);
    EXPECT_EQ(legacy[idx].controller, explicit_zero[idx].controller);
    EXPECT_EQ(legacy[idx].value, explicit_zero[idx].value);
  }
  // Pin the exact 4-point arc (opening 75, develop 85 at 1/2, climax 95 at 3/4,
  // settle 88 at end-1) so a future change to the peak placement cannot silently
  // shift the legacy (climax_tick == 0) stream. Events at equal ticks stay in
  // one CC#7 event per structural point.
  const Tick total = 24 * kTicksPerBar;
  ASSERT_EQ(legacy.size(), 4u);
  EXPECT_EQ(legacy[0].tick, 0u);
  EXPECT_EQ(legacy[0].value, 75);
  EXPECT_EQ(legacy[1].tick, total / 2);
  EXPECT_EQ(legacy[1].value, 85);
  EXPECT_EQ(legacy[2].tick, total * 3 / 4);
  EXPECT_EQ(legacy[2].value, 95);
  EXPECT_EQ(legacy[3].tick, total - 1);
  EXPECT_EQ(legacy[3].value, 88);
}

TEST(ExpressionEventsTest, RegistrationPeakLandsAtClimaxTick) {
  const Tick total = 32 * kTicksPerBar;
  const Tick climax = 20 * kTicksPerBar;  // between develop (16 bars) and the last bar.
  const auto events = buildRegistrationPlan(32, 8, kTicksPerBar, total, climax);
  bool found = false;
  for (const auto& evt : events) {
    if (evt.controller == 7 && evt.value == 95) {  // the climax peak value.
      EXPECT_EQ(evt.tick, climax);
      found = true;
    }
  }
  EXPECT_TRUE(found) << "no climax peak point emitted";
}

TEST(ExpressionEventsTest, RegistrationClimaxClampedBeforePieceEnd) {
  const Tick total = 16 * kTicksPerBar;
  // A climax past the end clamps to at most one bar before the end so the settle
  // still follows it.
  const auto events = buildRegistrationPlan(16, 8, kTicksPerBar, total, total + 5000);
  for (const auto& evt : events) {
    if (evt.controller == 7 && evt.value == 95) {
      EXPECT_LE(evt.tick, total - kTicksPerBar);
      EXPECT_LT(evt.tick, total - 1);
    }
  }
}

// ---------------------------------------------------------------------------
// buildRegistrationTerraces
// ---------------------------------------------------------------------------

TEST(ExpressionEventsTest, TerracesEmptyForNoSteps) {
  EXPECT_TRUE(buildRegistrationTerraces({}, 16 * kTicksPerBar).empty());
  EXPECT_TRUE(buildRegistrationTerraces({kTicksPerBar}, 0).empty());
}

TEST(ExpressionEventsTest, TerracesLevelsAndTicks) {
  const Tick total = 8 * kTicksPerBar;
  const std::vector<Tick> steps = {kTicksPerBar, 2 * kTicksPerBar, 3 * kTicksPerBar};
  const auto events = buildRegistrationTerraces(steps, total);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].tick, kTicksPerBar);
  EXPECT_EQ(events[0].controller, 7);  // CC#7 only (stop change, not expression).
  EXPECT_EQ(events[0].value, 78);
  EXPECT_EQ(events[1].value, 82);
  EXPECT_EQ(events[2].value, 86);
}

TEST(ExpressionEventsTest, TerracesSortDedupAndDropOutOfRange) {
  const Tick total = 8 * kTicksPerBar;
  // Unsorted, with a duplicate, a zero tick, and two out-of-range ticks.
  const std::vector<Tick> steps = {3 * kTicksPerBar,    kTicksPerBar, kTicksPerBar, 0, total,
                                   total + kTicksPerBar};
  const auto events = buildRegistrationTerraces(steps, total);
  ASSERT_EQ(events.size(), 2u);  // only 1*bar and 3*bar survive, deduped + sorted.
  EXPECT_EQ(events[0].tick, kTicksPerBar);
  EXPECT_EQ(events[1].tick, 3 * kTicksPerBar);
  EXPECT_LT(events[0].tick, events[1].tick);
}

TEST(ExpressionEventsTest, TerracesCapAt92) {
  const Tick total = 32 * kTicksPerBar;
  std::vector<Tick> steps;
  for (int idx = 1; idx <= 8; ++idx) {
    steps.push_back(static_cast<Tick>(idx) * kTicksPerBar);
  }
  const auto events = buildRegistrationTerraces(steps, total);
  ASSERT_EQ(events.size(), 8u);
  for (std::size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_GE(events[idx].value, events[idx - 1].value) << "terraces are non-decreasing";
    EXPECT_LE(events[idx].value, 92) << "terraces cap below the climax peak";
  }
  EXPECT_EQ(events.back().value, 92);
}

TEST(ExpressionEventsTest, TerracesDeterministic) {
  const std::vector<Tick> steps = {kTicksPerBar, 4 * kTicksPerBar};
  const auto a = buildRegistrationTerraces(steps, 16 * kTicksPerBar);
  const auto b = buildRegistrationTerraces(steps, 16 * kTicksPerBar);
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t idx = 0; idx < a.size(); ++idx) {
    EXPECT_EQ(a[idx].tick, b[idx].tick);
    EXPECT_EQ(a[idx].controller, b[idx].controller);
    EXPECT_EQ(a[idx].value, b[idx].value);
  }
}

// ---------------------------------------------------------------------------
// buildPhraseDynamics
// ---------------------------------------------------------------------------

TEST(PhraseDynamicsTest, EmitsExpressionOnlyEvents) {
  const auto events = buildPhraseDynamics(4, 4, kTicksPerBar, 16 * kTicksPerBar);
  ASSERT_FALSE(events.empty());
  for (const auto& evt : events) {
    EXPECT_EQ(evt.controller, 11) << "phrase dynamics must touch CC#11 only";
    EXPECT_LE(evt.value, 127);
  }
}

TEST(PhraseDynamicsTest, MidPhraseSwellsAbovePhraseStart) {
  const Tick total = 16 * kTicksPerBar;
  const auto events = buildPhraseDynamics(4, 4, kTicksPerBar, total);
  // Events alternate phrase-start / mid-phrase; each full phrase's swell sits
  // above its own start value (the breath rises and falls with the phrasing).
  ASSERT_GE(events.size(), 4u);
  for (std::size_t idx = 0; idx + 1 < events.size(); idx += 2) {
    const auto& start = events[idx];
    const auto& mid = events[idx + 1];
    if (mid.tick - start.tick != 2 * kTicksPerBar) {
      break;  // truncated final phrase: no swell emitted.
    }
    EXPECT_GT(mid.value, start.value)
        << "mid-phrase swell at tick " << mid.tick << " should exceed the phrase-start baseline";
  }
}

TEST(PhraseDynamicsTest, BaselineFollowsMacroArcShape) {
  const Tick total = 32 * kTicksPerBar;
  const auto events = buildPhraseDynamics(8, 4, kTicksPerBar, total);
  // The phrase-start baseline rises towards the ~75% climax point and settles
  // after it, mirroring the registration plan's arc.
  std::uint8_t opening = 0;
  std::uint8_t near_climax = 0;
  std::uint8_t closing = 0;
  for (const auto& evt : events) {
    if (evt.tick == 0)
      opening = evt.value;
    if (evt.tick == 24 * kTicksPerBar)
      near_climax = evt.value;
    if (evt.tick == 28 * kTicksPerBar)
      closing = evt.value;
  }
  EXPECT_GT(near_climax, opening);
  EXPECT_LT(closing, near_climax);
}

TEST(PhraseDynamicsTest, FormClimaxTickMovesMacroPeak) {
  const Tick total = 32 * kTicksPerBar;
  const Tick form_climax = 20 * kTicksPerBar;
  const auto events = buildPhraseDynamics(8, 4, kTicksPerBar, total, form_climax);
  std::uint8_t at_form_climax = 0;
  std::uint8_t at_legacy_climax = 0;
  for (const auto& event : events) {
    if (event.tick == form_climax) {
      at_form_climax = event.value;
    }
    if (event.tick == 24 * kTicksPerBar) {
      at_legacy_climax = event.value;
    }
  }
  EXPECT_EQ(at_form_climax, 95u);
  EXPECT_LT(at_legacy_climax, at_form_climax);
}

TEST(PhraseDynamicsTest, TicksSortedAndWithinPiece) {
  const Tick total = 24 * kTicksPerBar;
  const auto events = buildPhraseDynamics(6, 4, kTicksPerBar, total);
  ASSERT_FALSE(events.empty());
  Tick prev = 0;
  for (const auto& evt : events) {
    EXPECT_LT(evt.tick, total);
    EXPECT_GE(evt.tick, prev);
    prev = evt.tick;
  }
  EXPECT_EQ(events.front().tick, 0u);
}

TEST(PhraseDynamicsTest, ClampsPhraseLengthIntoRange) {
  const Tick total = 16 * kTicksPerBar;
  // phrase_bars 0 and 1 clamp to 2; 16 clamps to 8.
  const auto clamped_low = buildPhraseDynamics(4, 0, kTicksPerBar, total);
  ASSERT_GE(clamped_low.size(), 2u);
  EXPECT_EQ(clamped_low[1].tick, kTicksPerBar);  // mid-point of a 2-bar phrase.
  const auto clamped_high = buildPhraseDynamics(4, 16, kTicksPerBar, total);
  ASSERT_GE(clamped_high.size(), 2u);
  EXPECT_EQ(clamped_high[1].tick, 4 * kTicksPerBar);  // mid-point of an 8-bar phrase.
}

TEST(PhraseDynamicsTest, EmptyForDegenerateInput) {
  EXPECT_TRUE(buildPhraseDynamics(4, 4, kTicksPerBar, 0).empty());
  EXPECT_TRUE(buildPhraseDynamics(4, 4, 0, 16 * kTicksPerBar).empty());
}

TEST(PhraseDynamicsTest, Deterministic) {
  const auto a = buildPhraseDynamics(5, 4, kTicksPerBar, 20 * kTicksPerBar);
  const auto b = buildPhraseDynamics(5, 4, kTicksPerBar, 20 * kTicksPerBar);
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
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(events[0].bpm, 94);  // 94% entering the penultimate bar
  EXPECT_EQ(events[1].bpm, 90);  // 90% at its mid-point
  EXPECT_EQ(events[2].bpm, 85);  // 85% entering the final bar
  EXPECT_EQ(events[3].bpm, 78);  // 78% allargando floor
}

TEST(ExpressionEventsTest, RitardandoNoneEmitsNoTempoSteps) {
  EXPECT_TRUE(
      buildFinalRitardando(100, 16 * kTicksPerBar, kTicksPerBar, RitardandoStyle::None).empty());
}

TEST(ExpressionEventsTest, GentleRitardandoStaysCloserToBaseTempo) {
  const auto gentle =
      buildFinalRitardando(100, 16 * kTicksPerBar, kTicksPerBar, RitardandoStyle::Gentle);
  const auto rhetorical =
      buildFinalRitardando(100, 16 * kTicksPerBar, kTicksPerBar, RitardandoStyle::Rhetorical);
  ASSERT_EQ(gentle.size(), 4u);
  ASSERT_EQ(rhetorical.size(), 4u);
  EXPECT_GT(gentle.back().bpm, rhetorical.back().bpm);
  EXPECT_EQ(gentle.back().bpm, 90);
}

TEST(ExpressionEventsTest, RitardandoLastEventBeforeTotalTicks) {
  const Tick total = 12 * kTicksPerBar;
  const auto events = buildFinalRitardando(120, total, kTicksPerBar);
  ASSERT_FALSE(events.empty());
  EXPECT_LT(events.back().tick, total);
  // The steps span the final two bars on the half-bar grid.
  EXPECT_EQ(events.front().tick, total - 2 * kTicksPerBar);
  EXPECT_EQ(events.back().tick, total - kTicksPerBar / 2);
}

TEST(ExpressionEventsTest, TripleMeterRitardandoUsesBeatGrid) {
  constexpr Tick kTripleBar = 3 * kTicksPerBeat;
  const auto events =
      buildFinalRitardando(100, 12 * kTripleBar, kTripleBar, RitardandoStyle::Rhetorical, 3);
  ASSERT_EQ(events.size(), 4u);
  for (const auto& event : events) {
    EXPECT_EQ(event.tick % kTicksPerBeat, 0u);
  }
  EXPECT_EQ(events[1].tick - events[0].tick, kTicksPerBeat);
  EXPECT_EQ(events[3].tick - events[2].tick, kTicksPerBeat);
}

TEST(ExpressionEventsTest, RitardandoCollapsesEqualStepsAtLowBpm) {
  // At a very low base tempo, integer rounding can make adjacent percentage
  // steps equal; the stream must stay strictly decreasing.
  const auto events = buildFinalRitardando(8, 16 * kTicksPerBar, kTicksPerBar);
  ASSERT_FALSE(events.empty());
  for (std::size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LT(events[idx].bpm, events[idx - 1].bpm);
  }
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
