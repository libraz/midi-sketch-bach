// Implementation of the analysis runner -- routes to system-specific analyzers.

#include "analysis/analysis_runner.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
#include "analysis/dissonance_analyzer.h"
#include "core/basic_types.h"

namespace bach {

namespace {

/// @brief Collect all NoteEvents from all tracks into a single vector.
std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks) {
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });
  return all_notes;
}

}  // namespace

AnalysisSystem analysisSystemForForm(FormType form) {
  switch (form) {
    case FormType::Fugue:
    case FormType::PreludeAndFugue:
    case FormType::TrioSonata:
    case FormType::ChoralePrelude:
    case FormType::ToccataAndFugue:
    case FormType::Passacaglia:
    case FormType::FantasiaAndFugue:
      return AnalysisSystem::Organ;

    case FormType::CelloPrelude:
    case FormType::Chaconne:
      return AnalysisSystem::SoloString;
  }
  return AnalysisSystem::Organ;
}

AnalysisReport runAnalysis(const std::vector<Track>& tracks, FormType form,
                           uint8_t num_voices, const HarmonicTimeline& timeline,
                           const KeySignature& key_sig,
                           const HarmonicTimeline* generation_timeline) {
  AnalysisReport report;
  auto all_notes = collectAllNotes(tracks);

  AnalysisSystem system = analysisSystemForForm(form);

  if (system == AnalysisSystem::Organ) {
    // Organ: counterpoint + all 4 dissonance phases.
    report.has_counterpoint = true;
    report.counterpoint = analyzeCounterpoint(all_notes, num_voices);
    report.dissonance = analyzeOrganDissonance(all_notes, num_voices, timeline, key_sig,
                                               generation_timeline);
  } else {
    // Solo String: dissonance phases 2 + 4 only.
    report.has_counterpoint = false;
    report.dissonance = analyzeSoloStringDissonance(all_notes, timeline, key_sig);
  }

  // Determine overall pass: no High severity + counterpoint compliance > 0.8.
  report.overall_pass = (report.dissonance.summary.high_count == 0);
  if (report.has_counterpoint) {
    report.overall_pass = report.overall_pass &&
        (report.counterpoint.overall_compliance_rate > 0.8f);
  }

  return report;
}

// ===========================================================================
// AnalysisReport methods
// ===========================================================================

std::string AnalysisReport::toTextSummary(FormType form, uint8_t num_voices) const {
  std::ostringstream oss;

  AnalysisSystem system = analysisSystemForForm(form);
  const char* system_name = (system == AnalysisSystem::Organ) ? "Organ" : "Solo String";

  oss << dissonance.toTextSummary(system_name, num_voices);

  if (has_counterpoint) {
    oss << "\n=== Counterpoint ===\n";
    oss << "Parallel perfects: " << counterpoint.parallel_perfect_count
        << " | Voice crossings: " << counterpoint.voice_crossing_count
        << " | Compliance: ";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", counterpoint.overall_compliance_rate);
    oss << buf << "\n";
  }

  return oss.str();
}

std::string AnalysisReport::toJson(FormType form, uint8_t num_voices) const {
  std::ostringstream oss;
  oss << "{\n";

  AnalysisSystem system = analysisSystemForForm(form);
  oss << "  \"system\": \"" << (system == AnalysisSystem::Organ ? "Organ" : "SoloString")
      << "\",\n";
  oss << "  \"form\": \"" << formTypeToString(form) << "\",\n";
  oss << "  \"num_voices\": " << static_cast<int>(num_voices) << ",\n";
  oss << "  \"overall_pass\": " << (overall_pass ? "true" : "false") << ",\n";

  // Dissonance section.
  oss << "  \"dissonance\": " << dissonance.toJson();

  // Counterpoint section.
  if (has_counterpoint) {
    // Remove trailing newline/brace from dissonance JSON and add comma.
    // Instead, add counterpoint as a separate field.
    oss << ",\n  \"counterpoint\": {\n";
    oss << "    \"parallel_perfect_count\": " << counterpoint.parallel_perfect_count << ",\n";
    oss << "    \"hidden_perfect_count\": " << counterpoint.hidden_perfect_count << ",\n";
    oss << "    \"voice_crossing_count\": " << counterpoint.voice_crossing_count << ",\n";
    oss << "    \"augmented_leap_count\": " << counterpoint.augmented_leap_count << ",\n";

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", counterpoint.dissonance_resolution_rate);
    oss << "    \"dissonance_resolution_rate\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", counterpoint.overall_compliance_rate);
    oss << "    \"overall_compliance_rate\": " << buf << "\n";
    oss << "  }\n";
  } else {
    oss << "\n";
  }

  oss << "}\n";
  return oss.str();
}

}  // namespace bach
