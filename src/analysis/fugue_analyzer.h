// Fugue structure quality analysis.

#ifndef BACH_ANALYSIS_FUGUE_ANALYZER_H
#define BACH_ANALYSIS_FUGUE_ANALYZER_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

class HarmonicTimeline;

/// Aggregated fugue analysis metrics.
struct FugueAnalysisResult {
  float answer_accuracy_score = 0.0f;          ///< Transposition accuracy [0,1].
  float exposition_completeness_score = 0.0f;  ///< All voices enter with subject [0,1].
  float episode_motif_usage_rate = 0.0f;       ///< Motif-derived material in episodes [0,1].
  float tonal_plan_score = 0.0f;               ///< Tonal variety and correctness [0,1].
  float cadence_detection_rate = 0.0f;         ///< Detected cadences / planned cadences [0,1].
  float motivic_unity_score = 0.0f;            ///< Motif fragment reuse rate across piece [0,1].
  float tonal_consistency_score = 0.0f;        ///< Tonal fit of pitch class distribution [0,1].
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

/// @brief Compute cadence detection rate using the cadence detector.
///
/// Compares detected cadences (from harmonic timeline pattern matching)
/// against planned cadence positions (from section boundaries).
///
/// @param timeline The harmonic timeline to analyze for cadences.
/// @param section_end_ticks Section boundary ticks (planned cadence positions).
/// @return Detection rate [0.0, 1.0]. Returns 0.0 if no planned cadences.
float computeCadenceDetectionRate(const HarmonicTimeline& timeline,
                                  const std::vector<Tick>& section_end_ticks);

/// @brief Compute motivic unity score.
///
/// Measures how pervasively the subject's characteristic motif appears
/// throughout the piece. Higher scores indicate better thematic integration.
/// The piece is divided into 4 equal time quarters; for each quarter and voice,
/// checks whether a 3-interval fragment extracted from the subject is present.
///
/// @param notes All notes in the fugue.
/// @param subject_notes The original subject for motif extraction.
/// @param num_voices Number of voices.
/// @return Unity score [0.0, 1.0].
float computeMotivicUnityScore(const std::vector<NoteEvent>& notes,
                                const std::vector<NoteEvent>& subject_notes,
                                uint8_t num_voices);

/// @brief Compute tonal consistency score.
///
/// Evaluates how well the pitch class distribution fits the expected
/// tonal center. A piece strongly in C major should have high counts for
/// C, E, G and lower counts for chromatic tones. Awards a bonus of 0.1
/// when the tonic pitch class is the most frequent.
///
/// @param notes All notes in the fugue.
/// @param tonic_key The expected tonic key.
/// @param is_minor Whether the key is minor.
/// @return Consistency score [0.0, 1.0].
float computeTonalConsistencyScore(const std::vector<NoteEvent>& notes,
                                    Key tonic_key, bool is_minor);

/// @brief Compute invertible counterpoint score.
///
/// Evaluates whether the subject and countersubject can be inverted at the
/// octave (double counterpoint) without creating forbidden parallels.
/// Inverts the pitch relationship and checks for parallel 5ths/8ths.
///
/// @param subject_notes Subject melody notes.
/// @param counter_notes Countersubject melody notes.
/// @param num_voices Number of voices.
/// @return Score in [0,1]; 1.0 = fully invertible, 0.0 = many violations.
float invertibleCounterpointScore(const std::vector<NoteEvent>& subject_notes,
                                   const std::vector<NoteEvent>& counter_notes,
                                   uint8_t num_voices);

}  // namespace bach

#endif  // BACH_ANALYSIS_FUGUE_ANALYZER_H
