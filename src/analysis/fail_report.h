// Failure reporting for analysis and quality gate.

#ifndef BACH_ANALYSIS_FAIL_REPORT_H
#define BACH_ANALYSIS_FAIL_REPORT_H

#include <cstdint>
#include <string>
#include <vector>

#include "analysis/fugue_analyzer.h"
#include "core/basic_types.h"

namespace bach {

/// Severity classification for analysis issues.
enum class FailSeverity : uint8_t {
  Info,      ///< Informational, no action needed.
  Warning,   ///< Minor issue, acceptable in some contexts.
  Critical   ///< Must be fixed, fails quality gate.
};

/// @brief Convert FailSeverity to human-readable string.
/// @param severity The severity level.
/// @return Null-terminated string representation.
const char* failSeverityToString(FailSeverity severity);

/// A single issue detected during analysis.
struct FailIssue {
  FailKind kind = FailKind::MusicalFail;
  FailSeverity severity = FailSeverity::Warning;
  Tick tick = 0;
  uint8_t bar = 0;
  uint8_t beat = 0;
  VoiceId voice_a = 0;
  VoiceId voice_b = 0;
  std::string rule;         ///< e.g. "parallel_fifths", "voice_crossing".
  std::string description;  ///< Human-readable explanation.
};

/// Aggregated count of issues by severity.
struct FailSummary {
  uint32_t total_critical = 0;
  uint32_t total_warning = 0;
  uint32_t total_info = 0;
};

/// @brief Collection of analysis issues with filtering and serialization.
struct FailReport {
  std::vector<FailIssue> issues;

  /// @brief Add an issue to the report.
  /// @param issue The issue to append.
  void addIssue(const FailIssue& issue);

  /// @brief Get summary counts by severity.
  /// @return Aggregated counts for each severity level.
  FailSummary summary() const;

  /// @brief Check if there are any critical issues.
  /// @return True if at least one Critical issue exists.
  bool hasCritical() const;

  /// @brief Get issues filtered by kind.
  /// @param kind The FailKind to filter on.
  /// @return Vector of matching issues.
  std::vector<FailIssue> issuesByKind(FailKind kind) const;

  /// @brief Get issues filtered by severity.
  /// @param severity The FailSeverity to filter on.
  /// @return Vector of matching issues.
  std::vector<FailIssue> issuesBySeverity(FailSeverity severity) const;

  /// @brief Serialize the entire report to a JSON string.
  /// @return Formatted JSON representation.
  std::string toJson() const;
};

/// @brief Serialize a FugueAnalysisResult to a JSON string.
///
/// Outputs all fields including cadence_detection_rate, motivic_unity_score,
/// and tonal_consistency_score.
///
/// @param result The fugue analysis result to serialize.
/// @return Pretty-printed JSON string.
std::string fugueAnalysisResultToJson(const FugueAnalysisResult& result);

}  // namespace bach

#endif  // BACH_ANALYSIS_FAIL_REPORT_H
