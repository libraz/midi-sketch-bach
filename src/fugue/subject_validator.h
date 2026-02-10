// Subject validation: 7-dimension quality scoring for fugue subjects.

#ifndef BACH_FUGUE_SUBJECT_VALIDATOR_H
#define BACH_FUGUE_SUBJECT_VALIDATOR_H

#include <string>

#include "fugue/subject.h"

namespace bach {

/// @brief Multi-dimensional quality score for a fugue subject.
///
/// Each dimension is scored in [0, 1]. The composite score is a weighted
/// sum of all dimensions. A subject is considered acceptable if the
/// composite score reaches 0.7 or higher.
struct SubjectScore {
  float interval_variety = 0.0f;      ///< Diversity of melodic intervals.
  float rhythm_diversity = 0.0f;      ///< Variety of note durations.
  float contour_balance = 0.0f;       ///< Single-climax contour quality.
  float range_score = 0.0f;           ///< Pitch range appropriateness.
  float step_motion_ratio = 0.0f;     ///< Proportion of stepwise motion.
  float tonal_stability = 0.0f;       ///< Tonic/dominant emphasis.
  float answer_compatibility = 0.0f;  ///< Suitability for answer derivation.

  /// @brief Compute the weighted composite score.
  /// @return Weighted sum in [0, 1].
  ///
  /// Weights: interval=0.15, rhythm=0.10, contour=0.15, range=0.15,
  ///          step=0.15, tonal=0.20, answer=0.10.
  float composite() const;

  /// @brief Check whether the subject meets the minimum quality threshold.
  /// @return True if composite() >= 0.7.
  bool isAcceptable() const;

  /// @brief Serialize the score to a JSON string.
  /// @return JSON object string with all dimensions and composite.
  std::string toJson() const;
};

/// @brief Evaluates fugue subjects across 7 quality dimensions.
///
/// Each scoring function is public so that individual dimensions can be
/// tested in isolation.
class SubjectValidator {
 public:
  /// @brief Evaluate a subject and return the full score.
  /// @param subject The subject to evaluate.
  /// @return SubjectScore with all dimensions filled.
  SubjectScore evaluate(const Subject& subject) const;

  /// @brief Score interval variety: unique intervals / possible intervals.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. Bonus for 3rds and 6ths.
  float scoreIntervalVariety(const Subject& subject) const;

  /// @brief Score rhythm diversity: penalize if one duration dominates.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. 1.0 if no duration exceeds 50% of notes.
  float scoreRhythmDiversity(const Subject& subject) const;

  /// @brief Score contour balance: single peak/climax preferred.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. Leap + contrary step = bonus.
  float scoreContourBalance(const Subject& subject) const;

  /// @brief Score pitch range appropriateness.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. Range <= 12 semitones = 1.0.
  float scoreRange(const Subject& subject) const;

  /// @brief Score stepwise motion ratio.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. 60-80% stepwise = ideal (1.0).
  float scoreStepMotionRatio(const Subject& subject) const;

  /// @brief Score tonal stability: tonic/dominant emphasis.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. Start on tonic/dominant + end on tonic = bonus.
  float scoreTonalStability(const Subject& subject) const;

  /// @brief Score answer compatibility: penalize chromaticism.
  /// @param subject The subject to evaluate.
  /// @return Score in [0, 1]. Highly chromatic subjects score lower.
  float scoreAnswerCompatibility(const Subject& subject) const;
};

}  // namespace bach

#endif  // BACH_FUGUE_SUBJECT_VALIDATOR_H
