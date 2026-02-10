// Cadence detector -- read-only analysis of harmonic timelines for cadence patterns.

#ifndef BACH_ANALYSIS_CADENCE_DETECTOR_H
#define BACH_ANALYSIS_CADENCE_DETECTOR_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief Detected cadence with type and tick location.
struct DetectedCadence {
  CadenceType type = CadenceType::Perfect;
  Tick tick = 0;           ///< Tick position of the cadence resolution chord.
  float confidence = 0.0f; ///< Detection confidence [0.0, 1.0].
};

/// @brief Detect cadences from a harmonic timeline (read-only analysis).
///
/// Pattern-matches consecutive harmonic events for cadential progressions:
///   - Perfect cadence: V(7)->I  (confidence >= 0.9)
///   - Deceptive cadence: V->vi  (confidence ~0.8)
///   - Half cadence: *->V        (confidence ~0.7)
///   - Phrygian cadence: iv6->V in minor (confidence ~0.85)
///
/// This function is purely analytical and never modifies the timeline.
///
/// @param timeline The harmonic timeline to analyze.
/// @return Vector of detected cadences sorted by tick.
std::vector<DetectedCadence> detectCadences(const HarmonicTimeline& timeline);

/// @brief Compute cadence detection rate against planned cadences.
///
/// For each planned cadence tick, checks whether a detected cadence exists
/// within the tolerance window. Returns the ratio of matched planned cadences.
///
/// @param detected Detected cadences from analysis.
/// @param planned Planned cadence ticks (from CadencePlan).
/// @param tolerance_ticks Maximum tick distance to count as a match.
/// @return Ratio of matched planned cadences [0.0, 1.0].
///         Returns 0.0 if planned is empty.
float cadenceDetectionRate(const std::vector<DetectedCadence>& detected,
                           const std::vector<Tick>& planned,
                           Tick tolerance_ticks = kTicksPerBeat);

}  // namespace bach

#endif  // BACH_ANALYSIS_CADENCE_DETECTOR_H
