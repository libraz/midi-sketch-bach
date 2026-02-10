// Expression curve generation (CC#11) for dynamic phrasing.

#ifndef BACH_EXPRESSION_EXPRESSION_CURVE_H
#define BACH_EXPRESSION_EXPRESSION_CURVE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "expression/phrase_detector.h"

namespace bach {

/// @brief A single CC#11 expression event for MIDI output.
///
/// Expression events control the dynamic envelope within phrases,
/// creating the subtle swells and decays characteristic of musical phrasing.
struct ExpressionEvent {
  Tick tick = 0;         ///< Tick position of the CC event
  uint8_t channel = 0;  ///< MIDI channel (0-15)
  uint8_t value = 100;  ///< CC#11 value (0-127)
};

/// @brief Generate a CC#11 expression curve from phrase boundaries.
///
/// Creates a smooth expression curve that shapes dynamics within each phrase:
/// - Phrase start: value 100
/// - First quarter: interpolated 100 -> 108
/// - Phrase midpoint (peak): value 115
/// - Third quarter: interpolated 115 -> 105
/// - Phrase end: value 95
/// - Between phrases (breath): value 80
///
/// The curve provides musically natural dynamic shaping without requiring
/// per-note velocity changes (important for organ where velocity is fixed).
///
/// @param phrases Detected phrase boundaries defining phrase start/end points.
/// @param channel MIDI channel for the expression events.
/// @param total_duration Total duration of the piece in ticks.
/// @return Vector of ExpressionEvent structs in chronological order.
///         Returns a single event at tick 0 with value 100 if phrases is empty.
std::vector<ExpressionEvent> generateExpressionCurve(
    const std::vector<PhraseBoundary>& phrases, uint8_t channel, Tick total_duration);

}  // namespace bach

#endif  // BACH_EXPRESSION_EXPRESSION_CURVE_H
