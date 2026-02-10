// Implementation of CC#11 expression curve generation.

#include "expression/expression_curve.h"

#include <algorithm>

namespace bach {

// ---------------------------------------------------------------------------
// Expression value constants
// ---------------------------------------------------------------------------

static constexpr uint8_t kPhraseStartValue = 100;
static constexpr uint8_t kPhrasePeakValue = 115;
static constexpr uint8_t kPhraseEndValue = 95;
static constexpr uint8_t kBreathValue = 80;

/// @brief Linearly interpolate between two uint8_t values.
/// @param val_a Start value.
/// @param val_b End value.
/// @param ratio Interpolation ratio (0.0 = val_a, 1.0 = val_b).
/// @return Interpolated value, clamped to [0, 127].
static uint8_t interpolateValue(uint8_t val_a, uint8_t val_b, float ratio) {
  float result = static_cast<float>(val_a) + (static_cast<float>(val_b) - static_cast<float>(val_a)) * ratio;
  if (result < 0.0f) result = 0.0f;
  if (result > 127.0f) result = 127.0f;
  return static_cast<uint8_t>(result + 0.5f);
}

/// @brief Emit expression events for a single phrase span.
///
/// Generates control points within the phrase:
///   - Start (0%):   kPhraseStartValue (100)
///   - Q1 (25%):     108
///   - Mid (50%):    kPhrasePeakValue (115)
///   - Q3 (75%):     105
///   - End (100%):   kPhraseEndValue (95) -- only emitted if is_last_phrase
///
/// For non-final phrases, the end event is omitted because a breath event
/// will be emitted at the phrase boundary, maintaining chronological order.
///
/// @param output Vector to append events to.
/// @param channel MIDI channel.
/// @param phrase_start Start tick of the phrase.
/// @param phrase_end End tick of the phrase.
/// @param is_last_phrase If true, emit the end value at phrase_end.
static void emitPhraseEvents(std::vector<ExpressionEvent>& output, uint8_t channel,
                             Tick phrase_start, Tick phrase_end, bool is_last_phrase) {
  if (phrase_end <= phrase_start) {
    return;
  }

  Tick span = phrase_end - phrase_start;

  // Q1 interpolation target: midpoint between start and peak.
  uint8_t kQ1Value = interpolateValue(kPhraseStartValue, kPhrasePeakValue, 0.5f);
  // Q3 interpolation target: midpoint between peak and end.
  uint8_t kQ3Value = interpolateValue(kPhrasePeakValue, kPhraseEndValue, 0.5f);

  // Start.
  output.push_back({phrase_start, channel, kPhraseStartValue});

  // Only emit interior points if the phrase is long enough to be meaningful.
  // Minimum: 4 beats (1 bar) for intermediate control points.
  if (span >= kTicksPerBar) {
    Tick q1_tick = phrase_start + span / 4;
    Tick mid_tick = phrase_start + span / 2;
    Tick q3_tick = phrase_start + (span * 3) / 4;

    output.push_back({q1_tick, channel, kQ1Value});
    output.push_back({mid_tick, channel, kPhrasePeakValue});
    output.push_back({q3_tick, channel, kQ3Value});
  }

  // End: only emit for the last phrase. For intermediate phrases, a breath
  // event at the boundary will serve as the transition.
  if (is_last_phrase) {
    output.push_back({phrase_end, channel, kPhraseEndValue});
  }
}

std::vector<ExpressionEvent> generateExpressionCurve(
    const std::vector<PhraseBoundary>& phrases, uint8_t channel, Tick total_duration) {
  std::vector<ExpressionEvent> events;

  // Empty phrase list: emit a single default expression value.
  if (phrases.empty()) {
    events.push_back({0, channel, kPhraseStartValue});
    return events;
  }

  // Build phrase spans from boundaries.
  // Each boundary marks the START of a new phrase (the resolution point of a cadence).
  // The first phrase runs from tick 0 to the first boundary.
  // Subsequent phrases run between consecutive boundaries.
  // The final phrase runs from the last boundary to total_duration.

  std::vector<Tick> phrase_starts;
  phrase_starts.push_back(0);
  for (const auto& boundary : phrases) {
    phrase_starts.push_back(boundary.tick);
  }

  for (size_t idx = 0; idx < phrase_starts.size(); ++idx) {
    Tick start = phrase_starts[idx];
    Tick end = (idx + 1 < phrase_starts.size()) ? phrase_starts[idx + 1] : total_duration;
    bool is_last = (idx + 1 >= phrase_starts.size());

    if (end <= start) {
      continue;
    }

    // Emit breath event at the phrase boundary (except before the first phrase).
    // The breath is placed at the boundary tick, which is the end of the
    // previous phrase and the start of the current phrase. This ensures
    // chronological ordering: previous Q3 < boundary <= current start.
    if (idx > 0) {
      events.push_back({start, channel, kBreathValue});
    }

    emitPhraseEvents(events, channel, start, end, is_last);
  }

  return events;
}

}  // namespace bach
