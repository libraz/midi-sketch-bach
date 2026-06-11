/// @file
/// @brief Arc-driven organ registration and final-ritardando caller utilities.

#include "composer/expression_events.h"

#include <algorithm>

namespace bach::composer {

namespace {

/// MIDI controller numbers (mirrors the legacy organ registration plan).
constexpr std::uint8_t kCcMainVolume = 7;   ///< CC#7: Main Volume.
constexpr std::uint8_t kCcExpression = 11;  ///< CC#11: Expression.

/// Registration design values ported from src/organ/registration.cpp.
constexpr std::uint8_t kOpeningValue = 75;  ///< Establish: legacy exposition hint.
constexpr std::uint8_t kDevelopValue = 85;  ///< Develop: legacy episode/middle-entry mid-range.
constexpr std::uint8_t kClimaxValue = 95;   ///< Climax: legacy stretto hint.
constexpr std::uint8_t kSettleValue = 88;   ///< Resolve: softened close below the climax peak.

/// Mid-phrase swell above the macro-arc value (CC#11 steps, a gentle breath).
constexpr int kPhraseSwell = 6;

/// @brief Append a CC#7 + CC#11 pair at one registration point.
/// @param events Destination event stream.
/// @param tick Tick position for both controller changes.
/// @param value Controller value applied to both CC#7 and CC#11.
void addRegistrationPoint(std::vector<CcEvent>& events, Tick tick, std::uint8_t value) {
  events.push_back({tick, kCcMainVolume, value});
  events.push_back({tick, kCcExpression, value});
}

/// @brief Macro energy-arc value at a tick, by linear interpolation.
///
/// Traces the same design points as buildRegistrationPlan for the given
/// cycle_count tier (opening at 0, develop at 1/2, climax at 3/4, settle at the
/// end), so phrase-level events agree with the registration plan wherever the
/// two streams coincide.
/// @param cycle_count Arc cycle count (selects the tier, as in the plan).
/// @param tick Query position.
/// @param total_ticks Piece length (> 0).
/// @return Interpolated controller value in [kOpeningValue, kClimaxValue].
std::uint8_t macroArcValueAt(std::size_t cycle_count, Tick tick, std::uint32_t total_ticks) {
  struct Point {
    std::uint64_t tick;
    std::uint8_t value;
  };
  const std::uint64_t total = total_ticks;
  Point points[4];
  std::size_t count = 0;
  points[count++] = {0, kOpeningValue};
  if (cycle_count >= 3) {
    points[count++] = {total / 2, kDevelopValue};
  }
  if (cycle_count >= 2) {
    points[count++] = {total * 3 / 4, kClimaxValue};
  }
  points[count++] = {total > 0 ? total - 1 : 0, kSettleValue};

  const std::uint64_t t = tick;
  if (t <= points[0].tick) {
    return points[0].value;
  }
  for (std::size_t idx = 1; idx < count; ++idx) {
    if (t <= points[idx].tick) {
      const std::uint64_t lo = points[idx - 1].tick;
      const std::uint64_t hi = points[idx].tick;
      const int lo_v = points[idx - 1].value;
      const int hi_v = points[idx].value;
      if (hi == lo) {
        return points[idx].value;
      }
      const int v = lo_v + static_cast<int>((static_cast<std::int64_t>(hi_v - lo_v) *
                                             static_cast<std::int64_t>(t - lo)) /
                                            static_cast<std::int64_t>(hi - lo));
      return static_cast<std::uint8_t>(std::clamp(v, 0, 127));
    }
  }
  return points[count - 1].value;
}

}  // namespace

std::vector<CcEvent> buildRegistrationPlan(std::uint16_t bars, std::size_t cycle_count,
                                           Tick ticks_per_bar, std::uint32_t total_ticks) {
  (void)bars;           // Length is expressed via total_ticks; bars kept for caller clarity.
  (void)ticks_per_bar;  // Reserved for future per-bar alignment; not needed for placement.

  std::vector<CcEvent> events;
  if (total_ticks == 0) {
    return events;
  }

  // Place points within the span. The opening sits at tick 0; later points are
  // distributed so the climax lands at ~75% of the piece (the arc design value)
  // and the settle near the very end.
  const Tick climax_tick = static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) * 3 / 4);
  const Tick develop_tick = static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) * 1 / 2);
  const Tick settle_tick = total_ticks > 0 ? total_ticks - 1 : 0;

  events.reserve(8);  // At most 4 points x 2 controllers.

  if (cycle_count <= 1) {
    // Minimal pieces: opening moderate, then settle.
    addRegistrationPoint(events, 0, kOpeningValue);
    addRegistrationPoint(events, settle_tick, kSettleValue);
  } else if (cycle_count == 2) {
    // Opening, climax peak, settle.
    addRegistrationPoint(events, 0, kOpeningValue);
    addRegistrationPoint(events, climax_tick, kClimaxValue);
    addRegistrationPoint(events, settle_tick, kSettleValue);
  } else {
    // Full 4-point arc: opening, develop step-up, climax peak, settle.
    addRegistrationPoint(events, 0, kOpeningValue);
    addRegistrationPoint(events, develop_tick, kDevelopValue);
    addRegistrationPoint(events, climax_tick, kClimaxValue);
    addRegistrationPoint(events, settle_tick, kSettleValue);
  }

  // Guard against any accidental tick collision producing a non-sorted stream
  // on degenerate (tiny total_ticks) inputs: stable-sort by tick.
  std::stable_sort(events.begin(), events.end(),
                   [](const CcEvent& lhs, const CcEvent& rhs) { return lhs.tick < rhs.tick; });

  return events;
}

std::vector<CcEvent> buildPhraseDynamics(std::size_t cycle_count, std::uint16_t phrase_bars,
                                         Tick ticks_per_bar, std::uint32_t total_ticks) {
  std::vector<CcEvent> events;
  if (total_ticks == 0 || ticks_per_bar == 0) {
    return events;
  }

  // Clamp the phrase period into the musically sensible range: below 2 bars
  // the "breath" degenerates into per-bar pumping, above 8 it stops reading
  // as phrasing at all.
  const std::uint16_t period_bars = std::clamp<std::uint16_t>(phrase_bars, 2, 8);
  const std::uint64_t phrase_ticks =
      static_cast<std::uint64_t>(period_bars) * static_cast<std::uint64_t>(ticks_per_bar);

  for (std::uint64_t start = 0; start < total_ticks; start += phrase_ticks) {
    // Phrase start: return to the macro-arc baseline for this position.
    const std::uint8_t base = macroArcValueAt(cycle_count, static_cast<Tick>(start), total_ticks);
    events.push_back({static_cast<Tick>(start), kCcExpression, base});

    // Mid-phrase swell: a small designed step above the baseline. Skipped when
    // the mid-point falls past the end (a truncated final phrase keeps its
    // settling baseline instead of swelling into the final cadence).
    const std::uint64_t mid = start + phrase_ticks / 2;
    if (mid < total_ticks) {
      const int swelled =
          macroArcValueAt(cycle_count, static_cast<Tick>(mid), total_ticks) + kPhraseSwell;
      events.push_back({static_cast<Tick>(mid), kCcExpression,
                        static_cast<std::uint8_t>(std::min(swelled, 127))});
    }
  }

  return events;
}

std::vector<TempoEvent> buildFinalRitardando(std::uint16_t bpm, Tick total_ticks,
                                             Tick ticks_per_bar) {
  std::vector<TempoEvent> events;
  if (total_ticks == 0) {
    return events;
  }

  // Design-value poco-a-poco deceleration on the half-bar grid: 94% entering
  // the penultimate bar, 90% at its mid-point, 85% entering the final bar,
  // 78% at the final mid-point (the allargando floor). Round to whole BPM;
  // clamp to >= 1 to stay valid.
  const auto scale = [bpm](int num, int den) -> std::uint16_t {
    int v = (static_cast<int>(bpm) * num) / den;
    if (v < 1)
      v = 1;
    return static_cast<std::uint16_t>(v);
  };

  if (ticks_per_bar == 0 || total_ticks < 2 * ticks_per_bar) {
    // Too short for a graded ritardando: a single late step into 85%.
    Tick step_tick = total_ticks > ticks_per_bar ? total_ticks - ticks_per_bar : total_ticks / 2;
    if (step_tick == 0) {
      step_tick = total_ticks / 2;
    }
    events.push_back({step_tick, scale(85, 100)});
    return events;
  }

  const Tick half_bar = ticks_per_bar / 2;
  const Tick penultimate_bar_tick = total_ticks - 2 * ticks_per_bar;
  const Tick final_bar_tick = total_ticks - ticks_per_bar;
  events.push_back({penultimate_bar_tick, scale(94, 100)});
  events.push_back({penultimate_bar_tick + half_bar, scale(90, 100)});
  events.push_back({final_bar_tick, scale(85, 100)});
  events.push_back({final_bar_tick + half_bar, scale(78, 100)});

  // Integer rounding at low BPM can collapse adjacent percentages to the same
  // value; keep the stream strictly decreasing by dropping non-step events.
  std::vector<TempoEvent> steps;
  steps.reserve(events.size());
  for (const auto& evt : events) {
    if (steps.empty() || evt.bpm < steps.back().bpm) {
      steps.push_back(evt);
    }
  }

  return steps;
}

}  // namespace bach::composer
