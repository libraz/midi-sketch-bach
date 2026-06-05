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

/// @brief Append a CC#7 + CC#11 pair at one registration point.
/// @param events Destination event stream.
/// @param tick Tick position for both controller changes.
/// @param value Controller value applied to both CC#7 and CC#11.
void addRegistrationPoint(std::vector<CcEvent>& events, Tick tick, std::uint8_t value) {
  events.push_back({tick, kCcMainVolume, value});
  events.push_back({tick, kCcExpression, value});
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

std::vector<TempoEvent> buildFinalRitardando(std::uint16_t bpm, Tick total_ticks,
                                             Tick ticks_per_bar) {
  std::vector<TempoEvent> events;
  if (total_ticks == 0) {
    return events;
  }

  // Design-value deceleration: 92% entering the penultimate bar, 85% entering
  // the final bar. Round to whole BPM; clamp to >= 1 to stay valid.
  const auto scale = [bpm](int num, int den) -> std::uint16_t {
    int v = (static_cast<int>(bpm) * num) / den;
    if (v < 1)
      v = 1;
    return static_cast<std::uint16_t>(v);
  };
  const std::uint16_t bpm_92 = scale(92, 100);
  const std::uint16_t bpm_85 = scale(85, 100);

  if (ticks_per_bar == 0 || total_ticks < 2 * ticks_per_bar) {
    // Too short for a two-step ritardando: a single late step into 85%.
    Tick step_tick = total_ticks > ticks_per_bar ? total_ticks - ticks_per_bar : total_ticks / 2;
    if (step_tick == 0) {
      step_tick = total_ticks / 2;
    }
    events.push_back({step_tick, bpm_85});
    return events;
  }

  const Tick penultimate_bar_tick = total_ticks - 2 * ticks_per_bar;
  const Tick final_bar_tick = total_ticks - ticks_per_bar;
  events.push_back({penultimate_bar_tick, bpm_92});
  events.push_back({final_bar_tick, bpm_85});

  return events;
}

}  // namespace bach::composer
