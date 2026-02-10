// Analysis runner -- routes to Organ or Solo String analysis based on FormType.

#ifndef BACH_ANALYSIS_ANALYSIS_RUNNER_H
#define BACH_ANALYSIS_ANALYSIS_RUNNER_H

#include <string>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
#include "analysis/dissonance_analyzer.h"
#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// Which generation system a form belongs to for analysis routing.
enum class AnalysisSystem : uint8_t {
  Organ,
  SoloString
};

/// @brief Determine the analysis system for a given form type.
/// @param form The form type.
/// @return AnalysisSystem::Organ or AnalysisSystem::SoloString.
AnalysisSystem analysisSystemForForm(FormType form);

/// Unified analysis report combining counterpoint and dissonance results.
struct AnalysisReport {
  bool has_counterpoint = false;
  CounterpointAnalysisResult counterpoint;
  DissonanceAnalysisResult dissonance;
  bool overall_pass = true;  ///< No High severity + compliance > 0.8.

  /// @brief Generate a human-readable text summary.
  std::string toTextSummary(FormType form, uint8_t num_voices) const;

  /// @brief Serialize to JSON string.
  std::string toJson(FormType form, uint8_t num_voices) const;
};

/// @brief Run the appropriate analysis pipeline for a set of generated tracks.
/// @param tracks Generated MIDI tracks.
/// @param form The form type (determines routing).
/// @param num_voices Number of voices (used for organ analysis).
/// @param timeline Harmonic timeline for chord context.
/// @param key_sig Key signature for diatonic analysis.
/// @return Unified AnalysisReport.
AnalysisReport runAnalysis(const std::vector<Track>& tracks, FormType form,
                           uint8_t num_voices, const HarmonicTimeline& timeline,
                           const KeySignature& key_sig);

}  // namespace bach

#endif  // BACH_ANALYSIS_ANALYSIS_RUNNER_H
