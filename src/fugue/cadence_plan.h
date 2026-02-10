// Cadence plan: strategic placement of cadences within a fugue.

#ifndef BACH_FUGUE_CADENCE_PLAN_H
#define BACH_FUGUE_CADENCE_PLAN_H

#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief A single cadence placement point within a fugue.
struct CadencePoint {
  Tick tick = 0;                         ///< Tick position for cadence.
  CadenceType type = CadenceType::Half;  ///< Cadence type to apply.
  KeySignature key;                      ///< Key context for cadence construction.
};

/// @brief Strategic cadence placement plan for a fugue.
///
/// Cadence placement follows Bach's practice:
///   - Exposition end: Half cadence (V) -> drives into development
///   - Episode ends: Half cadence (V) -> prepares next key
///   - Final episode: Perfect cadence (V7->I) -> confirms return
///   - Pre-stretto: Deceptive cadence (V->vi) -> avoids resolution, builds tension
///   - Final: Perfect cadence (V7->I), with Picardy third in minor keys
struct CadencePlan {
  std::vector<CadencePoint> points;  ///< All cadence points in chronological order.

  /// @brief Create a cadence plan from a fugue structure and tonal plan.
  ///
  /// Analyzes section boundaries and assigns cadence types based on
  /// structural position and the key at each boundary.
  ///
  /// @param structure The fugue's formal structure (sections and phases).
  /// @param home_key The fugue's home key.
  /// @param is_minor True if the fugue is in a minor key.
  /// @return CadencePlan with cadence points at structural boundaries.
  static CadencePlan createForFugue(const FugueStructure& structure,
                                     const KeySignature& home_key,
                                     bool is_minor);

  /// @brief Apply all cadence points to a harmonic timeline.
  ///
  /// For each cadence point, creates a local timeline segment covering
  /// the cadence region and applies the cadence type.
  ///
  /// @param timeline The harmonic timeline to modify.
  void applyTo(HarmonicTimeline& timeline) const;

  /// @brief Get the number of cadence points.
  /// @return Point count.
  size_t size() const { return points.size(); }
};

}  // namespace bach

#endif  // BACH_FUGUE_CADENCE_PLAN_H
