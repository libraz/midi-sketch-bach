// Implementation of the analysis runner -- routes to system-specific analyzers.

#include "analysis/analysis_runner.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <vector>

#include "analysis/counterpoint_analyzer.h"
#include "analysis/dissonance_analyzer.h"
#include "core/basic_types.h"
#include "core/interval.h"

namespace bach {

namespace {

constexpr float kSelectionScoreTarget = 0.70f;
constexpr float kPenaltyDensityLimit = 0.70f;

float clamp01(float value) {
  if (value < 0.0f)
    return 0.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

/// @brief Collect all NoteEvents from all tracks into a single vector.
std::vector<NoteEvent> collectAllNotes(const std::vector<Track>& tracks) {
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });
  return all_notes;
}

float computeSelectionScore(const AnalysisReport& report) {
  float dissonance_score =
      clamp01(1.0f - report.dissonance.summary.weighted_density_per_beat / 2.0f);
  float counterpoint_score =
      report.has_counterpoint ? clamp01(report.counterpoint.overall_compliance_rate) : 1.0f;
  float rhythm_score = report.rhythm_diversity >= 0.0f ? clamp01(report.rhythm_diversity) : 0.75f;
  float bass_score =
      report.bass_stepwise_ratio >= 0.0f ? clamp01(report.bass_stepwise_ratio) : 0.75f;

  return clamp01(dissonance_score * 0.45f + counterpoint_score * 0.35f + rhythm_score * 0.10f +
                 bass_score * 0.10f);
}

bool isFlexibleMelodicSource(BachNoteSource source) {
  return source == BachNoteSource::FreeCounterpoint || source == BachNoteSource::EpisodeMaterial ||
         source == BachNoteSource::SequenceNote;
}

void computeMelodicStructureGate(const std::vector<Track>& tracks, AnalysisReport& report) {
  uint32_t flexible_notes = 0;
  for (const auto& track : tracks) {
    std::array<const NoteEvent*, 8> previous_by_voice{};
    for (const auto& note : track.notes) {
      if (isFlexibleMelodicSource(note.source)) {
        ++flexible_notes;
      }
      if (note.voice < previous_by_voice.size()) {
        const NoteEvent* previous = previous_by_voice[note.voice];
        if (previous != nullptr && isFlexibleMelodicSource(previous->source) &&
            isFlexibleMelodicSource(note.source)) {
          Tick gap = note.start_tick > previous->start_tick + previous->duration
                         ? note.start_tick - (previous->start_tick + previous->duration)
                         : 0;
          if (gap <= duration::kHalfNote) {
            uint32_t leap = static_cast<uint32_t>(
                std::abs(static_cast<int>(note.pitch) - static_cast<int>(previous->pitch)));
            report.max_flexible_leap = std::max(report.max_flexible_leap, leap);
            if (leap > interval::kPerfect5th) {
              ++report.flexible_large_leap_count;
            }
            if (leap > interval::kOctave) {
              ++report.flexible_remote_leap_count;
            }
          }
        }
        previous_by_voice[note.voice] = &note;
      }
    }
  }

  report.melodic_structure_pass =
      report.flexible_remote_leap_count == 0 &&
      report.flexible_large_leap_count <= std::max<uint32_t>(2u, flexible_notes / 35u);
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
    case FormType::GoldbergVariations:
      return AnalysisSystem::Organ;

    case FormType::CelloPrelude:
    case FormType::Chaconne:
      return AnalysisSystem::SoloString;
  }
  return AnalysisSystem::Organ;
}

AnalysisReport runAnalysis(const std::vector<Track>& tracks, FormType form, uint8_t num_voices,
                           const HarmonicTimeline& timeline, const KeySignature& key_sig,
                           const HarmonicTimeline* generation_timeline) {
  AnalysisReport report;
  auto all_notes = collectAllNotes(tracks);

  AnalysisSystem system = analysisSystemForForm(form);

  if (system == AnalysisSystem::Organ) {
    // Organ: counterpoint + all 4 dissonance phases.
    report.has_counterpoint = true;
    report.counterpoint = analyzeCounterpoint(all_notes, num_voices);
    report.dissonance =
        analyzeOrganDissonance(all_notes, num_voices, timeline, key_sig, generation_timeline);
  } else {
    // Solo String: dissonance phases 2 + 4 only.
    report.has_counterpoint = false;
    report.dissonance = analyzeSoloStringDissonance(all_notes, timeline, key_sig);
  }

  // Info-level metrics (always computed for organ system).
  if (system == AnalysisSystem::Organ) {
    report.rhythm_diversity = rhythmDiversityScore(all_notes, num_voices);
    report.texture_density_var = textureDensityVariance(all_notes, num_voices);
    report.bass_stepwise_ratio = bassLineStepwiseRatio(all_notes, num_voices);
    computeMelodicStructureGate(tracks, report);
  }

  report.penalty_affecting_violations =
      report.dissonance.summary.high_count + report.dissonance.summary.medium_count;
  if (report.has_counterpoint) {
    report.penalty_affecting_violations += report.counterpoint.parallel_perfect_count +
                                           report.counterpoint.hidden_perfect_count +
                                           report.counterpoint.structural_parallel_count;
  }
  report.penalty_affecting_density = report.dissonance.summary.weighted_density_per_beat;
  report.selection_score = computeSelectionScore(report);
  report.selection_pass = report.selection_score >= kSelectionScoreTarget &&
                          report.penalty_affecting_density <= kPenaltyDensityLimit &&
                          report.melodic_structure_pass;

  // Determine overall pass: no High severity + counterpoint compliance > 0.8.
  report.overall_pass = (report.dissonance.summary.high_count == 0);
  if (report.has_counterpoint) {
    report.overall_pass =
        report.overall_pass && (report.counterpoint.overall_compliance_rate > 0.8f);
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
        << " (structural: " << counterpoint.structural_parallel_count << ")"
        << " | Voice crossings: " << counterpoint.voice_crossing_count << " | Compliance: ";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", counterpoint.overall_compliance_rate);
    oss << buf << "\n";
  }

  oss << "\n=== Selection Gate ===\n";
  char gate_buf[16];
  std::snprintf(gate_buf, sizeof(gate_buf), "%.2f", selection_score);
  oss << "Selection score: " << gate_buf << " (target: " << kSelectionScoreTarget << ")"
      << " | Penalty-affecting: " << penalty_affecting_violations;
  std::snprintf(gate_buf, sizeof(gate_buf), "%.2f", penalty_affecting_density);
  oss << " | Penalty density: " << gate_buf << " (limit: " << kPenaltyDensityLimit << ")"
      << " | Melodic structure: " << (melodic_structure_pass ? "pass" : "fail")
      << " | Pass: " << (selection_pass ? "yes" : "no") << "\n";

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
  {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", selection_score);
    oss << "  \"selection_score\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", kSelectionScoreTarget);
    oss << "  \"selection_score_target\": " << buf << ",\n";
    oss << "  \"selection_pass\": " << (selection_pass ? "true" : "false") << ",\n";
    oss << "  \"analysis_gate_pass\": " << (selection_pass && overall_pass ? "true" : "false")
        << ",\n";
    oss << "  \"melodic_structure_pass\": " << (melodic_structure_pass ? "true" : "false") << ",\n";
    oss << "  \"flexible_large_leap_count\": " << flexible_large_leap_count << ",\n";
    oss << "  \"flexible_remote_leap_count\": " << flexible_remote_leap_count << ",\n";
    oss << "  \"max_flexible_leap\": " << max_flexible_leap << ",\n";
    oss << "  \"penalty_affecting_violations\": " << penalty_affecting_violations << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", penalty_affecting_density);
    oss << "  \"penalty_affecting_density\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", kPenaltyDensityLimit);
    oss << "  \"penalty_affecting_density_limit\": " << buf << ",\n";
  }

  // Dissonance section.
  oss << "  \"dissonance\": " << dissonance.toJson();

  // Counterpoint section.
  if (has_counterpoint) {
    // Remove trailing newline/brace from dissonance JSON and add comma.
    // Instead, add counterpoint as a separate field.
    oss << ",\n  \"counterpoint\": {\n";
    oss << "    \"parallel_perfect_count\": " << counterpoint.parallel_perfect_count << ",\n";
    oss << "    \"structural_parallel_count\": " << counterpoint.structural_parallel_count << ",\n";
    oss << "    \"hidden_perfect_count\": " << counterpoint.hidden_perfect_count << ",\n";
    oss << "    \"voice_crossing_count\": " << counterpoint.voice_crossing_count << ",\n";
    oss << "    \"augmented_leap_count\": " << counterpoint.augmented_leap_count << ",\n";

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", counterpoint.dissonance_resolution_rate);
    oss << "    \"dissonance_resolution_rate\": " << buf << ",\n";
    std::snprintf(buf, sizeof(buf), "%.4f", counterpoint.overall_compliance_rate);
    oss << "    \"overall_compliance_rate\": " << buf << "\n";
    oss << "  }";
  }

  // Info metrics.
  if (rhythm_diversity >= 0.0f) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", rhythm_diversity);
    oss << ",\n  \"rhythm_diversity\": " << buf;
  }
  if (texture_density_var >= 0.0f) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", texture_density_var);
    oss << ",\n  \"texture_density_variance\": " << buf;
  }
  if (bass_stepwise_ratio >= 0.0f) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", bass_stepwise_ratio);
    oss << ",\n  \"bass_stepwise_ratio\": " << buf;
  }

  oss << "\n}\n";
  return oss.str();
}

}  // namespace bach
