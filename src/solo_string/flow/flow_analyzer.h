// FlowAnalyzer -- quality analysis for BWV1007-style arpeggio flow pieces.

#ifndef BACH_SOLO_STRING_FLOW_FLOW_ANALYZER_H
#define BACH_SOLO_STRING_FLOW_FLOW_ANALYZER_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "solo_string/flow/arpeggio_flow_config.h"

namespace bach {

/// @brief Result of flow analysis with all quality metrics.
///
/// Contains 10 metrics across 4 categories:
/// - Harmonic quality (harmonic_motion_score, weight_utilization_score)
/// - Structural integrity (global_arc_score, peak_uniqueness_score,
///   arc_prohibition_score, dramaturgic_order_score)
/// - Continuity (phrase_continuity_score, pattern_naturalness_score)
/// - Ending quality (cadence_score)
///
/// Four metrics are instant-FAIL conditions that must be exact values.
/// The remaining six are threshold metrics with minimum requirements.
struct FlowAnalysisResult {
  // ---- Threshold metrics (must meet minimum) ----

  /// Harmonic motion quality: notes follow harmonic changes appropriately.
  /// Threshold: >= 0.5
  float harmonic_motion_score = 0.0f;

  /// Register expansion follows arc shape (expand in Ascent, max at Peak, contract in Descent).
  /// Threshold: >= 0.3
  float register_expansion_score = 0.0f;

  /// Continuity of note stream (no silence gaps).
  /// Threshold: >= 0.95
  float phrase_continuity_score = 0.0f;

  /// Interval variety and naturalness within patterns.
  /// Threshold: >= 0.7
  float pattern_naturalness_score = 0.0f;

  /// Quality of the final cadence (register, simplification).
  /// Threshold: >= 0.7
  float cadence_score = 0.0f;

  /// How well harmonic weights influence the output.
  /// Threshold: >= 0.6
  float weight_utilization_score = 0.0f;

  // ---- Instant-FAIL metrics (must be exact) ----

  /// GlobalArc structure validity (Ascent -> Peak -> Descent, all sections assigned).
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float global_arc_score = 0.0f;

  /// Peak section occurs exactly once.
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float peak_uniqueness_score = 0.0f;

  /// ArcPhase-specific prohibition violations.
  /// Must be exactly 0.0 (FAIL if > 0.0). Value = number of violations.
  float arc_prohibition_score = 0.0f;

  /// PatternRole order within sections is valid (monotonic non-decreasing).
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float dramaturgic_order_score = 0.0f;

  /// @brief Check if all metrics pass their thresholds.
  /// @return true if every threshold metric meets its minimum and every
  ///         instant-FAIL metric has the required exact value.
  bool isPass() const;

  /// @brief Get a human-readable summary of the analysis.
  /// @return Multi-line string with all metric names, values, and pass/fail status.
  std::string summary() const;

  /// @brief Get list of failed metric names with their values.
  /// @return Vector of strings like "harmonic_motion_score: 0.40 (threshold: 0.50)".
  std::vector<std::string> getFailures() const;
};

/// @brief Analyze a generated flow piece for quality metrics.
///
/// Evaluates 10 metrics across 4 categories:
/// - Harmonic quality (motion, weight utilization)
/// - Structural integrity (global arc, peak uniqueness, arc prohibitions, dramaturgic order)
/// - Continuity (phrase continuity, pattern naturalness)
/// - Ending quality (cadence)
///
/// The analyzer is read-only: it does not modify any input data.
///
/// @param tracks Generated MIDI tracks to analyze (typically 1 track for solo string).
/// @param config The ArpeggioFlowConfig used for generation.
/// @param timeline The HarmonicTimeline used during generation.
/// @return FlowAnalysisResult with all metric scores populated.
FlowAnalysisResult analyzeFlow(const std::vector<Track>& tracks,
                               const ArpeggioFlowConfig& config,
                               const HarmonicTimeline& timeline);

}  // namespace bach

#endif  // BACH_SOLO_STRING_FLOW_FLOW_ANALYZER_H
