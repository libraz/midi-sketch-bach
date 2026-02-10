// Fugue structure quality analysis.

#ifndef BACH_ANALYSIS_FUGUE_ANALYZER_H
#define BACH_ANALYSIS_FUGUE_ANALYZER_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// Aggregated fugue analysis metrics.
struct FugueAnalysisResult {
  float answer_accuracy_score = 0.0f;          ///< Transposition accuracy [0,1].
  float exposition_completeness_score = 0.0f;  ///< All voices enter with subject [0,1].
  float episode_motif_usage_rate = 0.0f;       ///< Motif-derived material in episodes [0,1].
  float tonal_plan_score = 0.0f;               ///< Tonal variety and correctness [0,1].
};

/// @brief Analyze fugue structure quality.
/// @param notes All notes with voice assignments.
/// @param num_voices Number of voices in the fugue.
/// @param subject_notes The original subject notes for comparison.
/// @return Analysis result with scores in [0,1].
FugueAnalysisResult analyzeFugue(const std::vector<NoteEvent>& notes,
                                 uint8_t num_voices,
                                 const std::vector<NoteEvent>& subject_notes);

/// @brief Check exposition completeness (each voice should enter once with subject).
/// @param notes All notes with voice assignments.
/// @param num_voices Number of voices in the fugue.
/// @param subject_notes The original subject notes for interval pattern matching.
/// @return Score in [0,1]; 1.0 means every voice has a subject entry.
float expositionCompletenessScore(const std::vector<NoteEvent>& notes,
                                  uint8_t num_voices,
                                  const std::vector<NoteEvent>& subject_notes);

/// @brief Evaluate tonal plan diversity across the piece.
/// @param notes All notes in the fugue.
/// @return Score in [0,1]; higher means better variety of tonal centers.
float tonalPlanScore(const std::vector<NoteEvent>& notes);

}  // namespace bach

#endif  // BACH_ANALYSIS_FUGUE_ANALYZER_H
