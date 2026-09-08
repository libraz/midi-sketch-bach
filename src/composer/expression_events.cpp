/// @file
/// @brief Arc-driven organ registration and final-ritardando caller utilities.

#include "composer/expression_events.h"

#include <algorithm>
#include <array>

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

/// Shortest value that carries a declared lift whole. An eighth is the fastest
/// note the ordinary touch is described against; below it the stroke scales down
/// with the note rather than the note being asked to absorb a fixed one.
constexpr Tick kLiftReferenceTicks = kTicksPerBeat / 2;

/// Registration terrace design values (CC#7 stop-change steps). The base sits
/// below the develop level and each step adds one stop, capped below the macro
/// climax peak (kClimaxValue = 95) so the arc's climax stays the dynamic summit.
constexpr int kTerraceBase = 78;  ///< Level of the first terrace step.
constexpr int kTerraceStep = 4;   ///< Added per subsequent terrace step.
constexpr int kTerraceCap = 92;   ///< Ceiling, below the climax peak.

/// @brief Shift a registration design value, kept inside the controller range.
/// @param base Design value.
/// @param offset Level shift (see buildRegistrationPlan).
/// @return The shifted value clamped to [1, 127]; never silent.
std::uint8_t shiftLevel(int base, int offset) {
  return static_cast<std::uint8_t>(std::clamp(base + offset, 1, 127));
}

/// @brief Append one discrete CC#7 registration point.
/// @param events Destination event stream.
/// @param tick Tick position for both controller changes.
/// @param value Controller value applied to CC#7.
void addRegistrationPoint(std::vector<CcEvent>& events, Tick tick, std::uint8_t value) {
  events.push_back({tick, kCcMainVolume, value});
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
Tick normalizedClimaxTick(std::size_t cycle_count, Tick ticks_per_bar, std::uint32_t total_ticks,
                          Tick climax_tick) {
  const Tick settle_tick = total_ticks > 0 ? total_ticks - 1 : 0;
  Tick peak_tick = static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) * 3 / 4);
  if (climax_tick == 0) {
    return peak_tick;
  }
  const Tick develop_tick = static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) / 2);
  const Tick establish_end = (cycle_count >= 3)
                                 ? develop_tick
                                 : static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) / 4);
  const Tick ceiling = (ticks_per_bar > 0 && total_ticks > ticks_per_bar)
                           ? total_ticks - ticks_per_bar
                           : settle_tick;
  Tick lo = establish_end + 1;
  Tick hi = ceiling;
  if (hi < lo) {
    hi = lo;
  }
  return std::clamp<Tick>(climax_tick, lo, hi);
}

std::uint8_t macroArcValueAt(std::size_t cycle_count, Tick ticks_per_bar, Tick tick,
                             std::uint32_t total_ticks, Tick climax_tick, int level_offset) {
  struct Point {
    std::uint64_t tick;
    std::uint8_t value;
  };
  const std::uint64_t total = total_ticks;
  Point points[4];
  std::size_t count = 0;
  points[count++] = {0, shiftLevel(kOpeningValue, level_offset)};
  if (cycle_count >= 3) {
    points[count++] = {total / 2, shiftLevel(kDevelopValue, level_offset)};
  }
  if (cycle_count >= 2) {
    points[count++] = {normalizedClimaxTick(cycle_count, ticks_per_bar, total_ticks, climax_tick),
                       shiftLevel(kClimaxValue, level_offset)};
  }
  points[count++] = {total > 0 ? total - 1 : 0, shiftLevel(kSettleValue, level_offset)};

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
                                           Tick ticks_per_bar, std::uint32_t total_ticks,
                                           Tick climax_tick, int level_offset) {
  (void)bars;  // Length is expressed via total_ticks; bars kept for caller clarity.

  std::vector<CcEvent> events;
  if (total_ticks == 0) {
    return events;
  }

  // Place points within the span. The opening sits at tick 0; later points are
  // distributed so the peak lands at ~75% of the piece (the arc design value)
  // and the settle near the very end.
  const Tick develop_tick = static_cast<Tick>(static_cast<std::uint64_t>(total_ticks) * 1 / 2);
  const Tick settle_tick = total_ticks > 0 ? total_ticks - 1 : 0;
  const Tick peak_tick = normalizedClimaxTick(cycle_count, ticks_per_bar, total_ticks, climax_tick);

  events.reserve(4);

  const std::uint8_t opening = shiftLevel(kOpeningValue, level_offset);
  const std::uint8_t develop = shiftLevel(kDevelopValue, level_offset);
  const std::uint8_t climax = shiftLevel(kClimaxValue, level_offset);
  const std::uint8_t settle = shiftLevel(kSettleValue, level_offset);

  if (cycle_count <= 1) {
    // Minimal pieces: opening moderate, then settle.
    addRegistrationPoint(events, 0, opening);
    addRegistrationPoint(events, settle_tick, settle);
  } else if (cycle_count == 2) {
    // Opening, climax peak, settle.
    addRegistrationPoint(events, 0, opening);
    addRegistrationPoint(events, peak_tick, climax);
    addRegistrationPoint(events, settle_tick, settle);
  } else {
    // Full 4-point arc: opening, develop step-up, climax peak, settle.
    addRegistrationPoint(events, 0, opening);
    addRegistrationPoint(events, develop_tick, develop);
    addRegistrationPoint(events, peak_tick, climax);
    addRegistrationPoint(events, settle_tick, settle);
  }

  // Guard against any accidental tick collision producing a non-sorted stream
  // on degenerate (tiny total_ticks) inputs: stable-sort by tick.
  std::stable_sort(events.begin(), events.end(),
                   [](const CcEvent& lhs, const CcEvent& rhs) { return lhs.tick < rhs.tick; });

  return events;
}

std::vector<CcEvent> buildRegistrationTerraces(const std::vector<Tick>& step_ticks,
                                               Tick total_ticks, int level_offset) {
  std::vector<CcEvent> events;
  if (step_ticks.empty() || total_ticks == 0) {
    return events;
  }

  // Sort + deduplicate; drop ticks at the opening (0) or at/past the piece end.
  std::vector<Tick> ticks;
  ticks.reserve(step_ticks.size());
  for (const Tick tick : step_ticks) {
    if (tick == 0 || tick >= total_ticks) {
      continue;
    }
    ticks.push_back(tick);
  }
  std::sort(ticks.begin(), ticks.end());
  ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());

  events.reserve(ticks.size());
  for (std::size_t idx = 0; idx < ticks.size(); ++idx) {
    const int level = std::min(kTerraceBase + static_cast<int>(idx) * kTerraceStep, kTerraceCap);
    // One instantaneous CC#7 step: a stop change, not a ramp.
    events.push_back({ticks[idx], kCcMainVolume, shiftLevel(level, level_offset)});
  }
  return events;
}

std::vector<CcEvent> buildPhraseDynamics(std::size_t cycle_count, std::uint16_t phrase_bars,
                                         Tick ticks_per_bar, std::uint32_t total_ticks,
                                         Tick climax_tick, int level_offset) {
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
    const std::uint8_t base = macroArcValueAt(cycle_count, ticks_per_bar, static_cast<Tick>(start),
                                              total_ticks, climax_tick, level_offset);
    events.push_back({static_cast<Tick>(start), kCcExpression, base});

    // Mid-phrase swell: a small designed step above the baseline. Skipped when
    // the mid-point falls past the end (a truncated final phrase keeps its
    // settling baseline instead of swelling into the final cadence).
    const std::uint64_t mid = start + phrase_ticks / 2;
    if (mid < total_ticks) {
      const int swelled = macroArcValueAt(cycle_count, ticks_per_bar, static_cast<Tick>(mid),
                                          total_ticks, climax_tick, level_offset) +
                          kPhraseSwell;
      events.push_back({static_cast<Tick>(mid), kCcExpression,
                        static_cast<std::uint8_t>(std::min(swelled, 127))});
    }
  }

  return events;
}

void applyArticulation(const std::vector<ArticulationDecl>& plan, std::vector<NoteEvent>* notes,
                       std::vector<NoteProvenance>* provenance) {
  if (plan.empty() || notes == nullptr || notes->empty()) {
    return;
  }

  // Last onset per voice. Touch separates a note from its successor, so a voice
  // that has stopped has nothing to separate from: leaving its final onset whole
  // keeps the closing chord at its notated length under the ritardando.
  // VoiceId is a byte, so one slot per value indexes any voice without a bound.
  std::array<Tick, 256> last_onset{};
  for (const auto& note : *notes) {
    last_onset[note.voice] = std::max(last_onset[note.voice], note.start_tick);
  }

  for (std::size_t idx = 0; idx < notes->size(); ++idx) {
    NoteEvent& note = (*notes)[idx];
    if (note.start_tick == last_onset[note.voice]) {
      continue;
    }
    Tick separation = 0;
    for (const auto& decl : plan) {
      if (decl.voice == note.voice && note.start_tick >= decl.start_tick &&
          note.start_tick < decl.end_tick) {
        separation = decl.separation;
        break;
      }
    }
    // A lift is a physical gesture and takes about the same time whatever note
    // it follows, so a long value gives up a small proportion of itself and an
    // ordinary one gives up more. Below an eighth that stops being playable: the
    // hand shortens the stroke rather than turning a run into a chain of dots,
    // so the declared lift scales with the note there and the proportion the ear
    // hears stops rising as the figure gets faster.
    //
    // Capping the lift at a quarter of the note instead does the opposite. The
    // cap binds hardest exactly where the notes are shortest -- a sixteenth
    // surrenders a quarter of itself while a whole note surrenders a fiftieth --
    // and because every stroke wider than a sixteenth clamps to the same value
    // there, it also flattens three of the four characters onto one touch in the
    // fastest figuration, which is where the touch is most audible.
    if (note.duration < kLiftReferenceTicks) {
      separation = separation * note.duration / kLiftReferenceTicks;
    }
    // A lift never takes more than half of what it follows, whatever a plan
    // declares. Nothing the characters ask for comes close; this is what keeps
    // a wider declaration from silencing a note rather than parting it.
    separation = std::min(separation, note.duration / 2);
    if (separation == 0) {
      continue;
    }
    note.duration -= separation;
    if (provenance != nullptr && idx < provenance->size()) {
      (*provenance)[idx].satisfied_rules |= ruleBitMask(RuleBit::ArticulationApplied);
    }
  }
}

std::vector<TempoEvent> buildFinalRitardando(std::uint16_t bpm, Tick total_ticks,
                                             Tick ticks_per_bar, RitardandoStyle style,
                                             std::uint8_t ts_numerator) {
  std::vector<TempoEvent> events;
  if (total_ticks == 0 || style == RitardandoStyle::None) {
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
    events.push_back(
        {step_tick, style == RitardandoStyle::Gentle ? scale(92, 100) : scale(85, 100)});
    return events;
  }

  // Common time retains the historical half-bar placement. In triple meter a
  // half bar falls between beats; use the actual quarter-note beat instead.
  const Tick step_within_bar = ts_numerator == 3 ? ticks_per_bar / 3 : ticks_per_bar / 2;
  const Tick penultimate_bar_tick = total_ticks - 2 * ticks_per_bar;
  const Tick final_bar_tick = total_ticks - ticks_per_bar;
  if (style == RitardandoStyle::Gentle) {
    events.push_back({penultimate_bar_tick, scale(98, 100)});
    events.push_back({penultimate_bar_tick + step_within_bar, scale(96, 100)});
    events.push_back({final_bar_tick, scale(93, 100)});
    events.push_back({final_bar_tick + step_within_bar, scale(90, 100)});
  } else {
    events.push_back({penultimate_bar_tick, scale(94, 100)});
    events.push_back({penultimate_bar_tick + step_within_bar, scale(90, 100)});
    events.push_back({final_bar_tick, scale(85, 100)});
    events.push_back({final_bar_tick + step_within_bar, scale(78, 100)});
  }

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
