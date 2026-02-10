// Failure reporting implementation.

#include "analysis/fail_report.h"

#include <string>
#include <vector>

#include "core/json_helpers.h"

namespace bach {

// ---------------------------------------------------------------------------
// FailSeverity to string
// ---------------------------------------------------------------------------

const char* failSeverityToString(FailSeverity severity) {
  switch (severity) {
    case FailSeverity::Info:     return "info";
    case FailSeverity::Warning:  return "warning";
    case FailSeverity::Critical: return "critical";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// FailKind to lowercase JSON key
// ---------------------------------------------------------------------------

namespace {

/// @brief Convert FailKind to lowercase JSON-friendly string.
const char* failKindToJsonString(FailKind kind) {
  switch (kind) {
    case FailKind::StructuralFail: return "structural";
    case FailKind::MusicalFail:    return "musical";
    case FailKind::ConfigFail:     return "config";
  }
  return "unknown";
}

}  // namespace

// ---------------------------------------------------------------------------
// FailReport methods
// ---------------------------------------------------------------------------

void FailReport::addIssue(const FailIssue& issue) {
  issues.push_back(issue);
}

FailSummary FailReport::summary() const {
  FailSummary result;
  for (const auto& issue : issues) {
    switch (issue.severity) {
      case FailSeverity::Critical:
        ++result.total_critical;
        break;
      case FailSeverity::Warning:
        ++result.total_warning;
        break;
      case FailSeverity::Info:
        ++result.total_info;
        break;
    }
  }
  return result;
}

bool FailReport::hasCritical() const {
  for (const auto& issue : issues) {
    if (issue.severity == FailSeverity::Critical) {
      return true;
    }
  }
  return false;
}

std::vector<FailIssue> FailReport::issuesByKind(FailKind kind) const {
  std::vector<FailIssue> result;
  for (const auto& issue : issues) {
    if (issue.kind == kind) {
      result.push_back(issue);
    }
  }
  return result;
}

std::vector<FailIssue> FailReport::issuesBySeverity(FailSeverity severity) const {
  std::vector<FailIssue> result;
  for (const auto& issue : issues) {
    if (issue.severity == severity) {
      result.push_back(issue);
    }
  }
  return result;
}

std::string FailReport::toJson() const {
  JsonWriter writer;
  writer.beginObject();

  // Summary section.
  auto sum = summary();
  writer.key("summary");
  writer.beginObject();
  writer.key("critical");
  writer.value(static_cast<uint32_t>(sum.total_critical));
  writer.key("warning");
  writer.value(static_cast<uint32_t>(sum.total_warning));
  writer.key("info");
  writer.value(static_cast<uint32_t>(sum.total_info));
  writer.endObject();

  // Issues array.
  writer.key("issues");
  writer.beginArray();
  for (const auto& issue : issues) {
    writer.beginObject();
    writer.key("kind");
    writer.value(std::string_view(failKindToJsonString(issue.kind)));
    writer.key("severity");
    writer.value(std::string_view(failSeverityToString(issue.severity)));
    writer.key("bar");
    writer.value(static_cast<int>(issue.bar));
    writer.key("beat");
    writer.value(static_cast<int>(issue.beat));
    writer.key("voice_a");
    writer.value(static_cast<int>(issue.voice_a));
    writer.key("voice_b");
    writer.value(static_cast<int>(issue.voice_b));
    writer.key("rule");
    writer.value(std::string_view(issue.rule));
    writer.key("description");
    writer.value(std::string_view(issue.description));
    writer.endObject();
  }
  writer.endArray();

  writer.endObject();
  return writer.toPrettyString();
}

// ---------------------------------------------------------------------------
// fugueAnalysisResultToJson
// ---------------------------------------------------------------------------

std::string fugueAnalysisResultToJson(const FugueAnalysisResult& result) {
  JsonWriter writer;
  writer.beginObject();

  writer.key("answer_accuracy_score");
  writer.value(static_cast<double>(result.answer_accuracy_score));
  writer.key("exposition_completeness_score");
  writer.value(static_cast<double>(result.exposition_completeness_score));
  writer.key("episode_motif_usage_rate");
  writer.value(static_cast<double>(result.episode_motif_usage_rate));
  writer.key("tonal_plan_score");
  writer.value(static_cast<double>(result.tonal_plan_score));
  writer.key("cadence_detection_rate");
  writer.value(static_cast<double>(result.cadence_detection_rate));
  writer.key("motivic_unity_score");
  writer.value(static_cast<double>(result.motivic_unity_score));
  writer.key("tonal_consistency_score");
  writer.value(static_cast<double>(result.tonal_consistency_score));

  writer.endObject();
  return writer.toPrettyString();
}

}  // namespace bach
