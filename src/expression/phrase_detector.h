// Phrase boundary detection from harmonic analysis.

#ifndef BACH_EXPRESSION_PHRASE_DETECTOR_H
#define BACH_EXPRESSION_PHRASE_DETECTOR_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief A detected phrase boundary with tick position and cadence type.
///
/// Phrase boundaries are inferred from harmonic progressions. The cadence
/// type indicates the strength and character of the boundary.
struct PhraseBoundary {
  Tick tick = 0;                                ///< Tick position of the boundary
  CadenceType cadence = CadenceType::Perfect;   ///< Type of cadence at boundary
};

/// @brief Detect phrase boundaries by scanning a harmonic timeline.
///
/// Scans consecutive harmonic events looking for cadential patterns:
/// - Perfect cadence (V -> I): strong boundary at the I chord
/// - Half cadence (any -> V at phrase end): moderate boundary
/// - Deceptive cadence (V -> vi): boundary at the vi chord
/// - Key changes: strong boundary at the point of modulation
///
/// Boundaries are returned sorted by tick position.
///
/// @param timeline The harmonic timeline to scan for cadential patterns.
/// @return Vector of PhraseBoundary structs in chronological order.
///         Empty if the timeline has fewer than 2 events.
std::vector<PhraseBoundary> detectPhraseBoundaries(const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_EXPRESSION_PHRASE_DETECTOR_H
