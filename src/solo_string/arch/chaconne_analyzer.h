// ChaconneAnalyzer -- quality analysis for BWV1004-style chaconne pieces.

#ifndef BACH_SOLO_STRING_ARCH_CHACONNE_ANALYZER_H
#define BACH_SOLO_STRING_ARCH_CHACONNE_ANALYZER_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "solo_string/arch/chaconne_config.h"
#include "solo_string/arch/ground_bass.h"

namespace bach {

/// @brief Result of chaconne analysis with all quality metrics.
///
/// Contains 9 metrics across 2 categories:
/// - Instant-FAIL metrics that must have exact values (ground_bass_integrity,
///   role_order_score, climax_presence_score, implied_polyphony_score)
/// - Threshold metrics with minimum requirements (variation_diversity,
///   texture_transition_score, section_balance, major_section_separation,
///   voice_switch_frequency)
///
/// Additionally stores diagnostic values (not scored) for debugging.
struct ChaconneAnalysisResult {
  // ---- Instant-FAIL metrics (must be exact) ----

  /// Ground bass integrity: all variations preserve the original bass exactly.
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float ground_bass_integrity = 0.0f;

  /// Variation role order validity (Establish -> Develop -> ... -> Resolve).
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float role_order_score = 0.0f;

  /// Climax presence: exactly 3 Accumulate variations at correct position.
  /// Must be exactly 1.0 (FAIL if != 1.0).
  float climax_presence_score = 0.0f;

  /// Implied polyphony average in ImpliedPolyphony texture variations.
  /// Must have average in [2.3, 2.8] (FAIL if outside range).
  /// If no ImpliedPolyphony textures exist, score = 1.0 (not applicable).
  float implied_polyphony_score = 0.0f;

  // ---- Threshold metrics (must meet minimum) ----

  /// Texture type diversity (Shannon entropy of texture distribution).
  /// Threshold: >= 0.7
  float variation_diversity = 0.0f;

  /// Smoothness of transitions between consecutive variation textures.
  /// Threshold: >= 0.5
  float texture_transition_score = 0.0f;

  /// Balance of minor-front/major/minor-back section proportions.
  /// Threshold: >= 0.7
  float section_balance = 0.0f;

  /// Distinctness of the major section from surrounding minor sections.
  /// Threshold: >= 0.6
  float major_section_separation = 0.0f;

  /// Frequency of register switches within a reasonable range.
  /// Score = 1.0 if switch frequency in [0.1, 0.5] per beat, lower otherwise.
  float voice_switch_frequency = 0.0f;

  // ---- Diagnostic values (not scored) ----

  /// Average implied voice count across ImpliedPolyphony sections.
  float implied_voice_count_avg = 0.0f;

  /// Number of Accumulate variations found (should be exactly 3).
  int accumulate_count = 0;

  /// @brief Check if all metrics pass their thresholds and instant-FAIL conditions.
  /// @return true if every metric meets its requirement.
  bool isPass() const;

  /// @brief Get a human-readable summary of the analysis.
  /// @return Multi-line string with all metric names, values, and pass/fail status.
  std::string summary() const;

  /// @brief Get list of failed metric names with their values.
  /// @return Vector of strings like "ground_bass_integrity: 0.00 (must be 1.0)".
  std::vector<std::string> getFailures() const;
};

/// @brief Analyze a generated chaconne for quality metrics.
///
/// Evaluates 9 metrics across 2 categories:
/// - Instant-FAIL: ground bass integrity, role order, climax presence, implied polyphony
/// - Threshold: variation diversity, texture transitions, section balance,
///   major section separation, voice switch frequency
///
/// The analyzer is read-only: it does not modify any input data.
///
/// @param tracks Generated MIDI tracks to analyze (typically 1 track for solo string).
/// @param config The ChaconneConfig used for generation.
/// @param ground_bass The original ground bass for integrity verification.
/// @return ChaconneAnalysisResult with all metric scores populated.
ChaconneAnalysisResult analyzeChaconne(const std::vector<Track>& tracks,
                                       const ChaconneConfig& config,
                                       const GroundBass& ground_bass);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_CHACONNE_ANALYZER_H
