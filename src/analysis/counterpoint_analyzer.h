// Counterpoint quality analysis for multi-voice textures.

#ifndef BACH_ANALYSIS_COUNTERPOINT_ANALYZER_H
#define BACH_ANALYSIS_COUNTERPOINT_ANALYZER_H

#include <cstdint>
#include <vector>

#include "analysis/fail_report.h"
#include "core/basic_types.h"

namespace bach {

/// Aggregated counterpoint analysis metrics.
struct CounterpointAnalysisResult {
  uint32_t parallel_perfect_count = 0;      ///< Parallel 5ths + 8ths.
  uint32_t hidden_perfect_count = 0;        ///< Hidden 5ths + 8ths.
  uint32_t voice_crossing_count = 0;        ///< Voice crossing violations.
  uint32_t augmented_leap_count = 0;        ///< Augmented interval leaps.
  float dissonance_resolution_rate = 1.0f;  ///< Resolved / total dissonances [0,1].
  float overall_compliance_rate = 1.0f;     ///< 1 - (violations / total_beats) [0,1].
  uint32_t cross_relation_count = 0;        ///< Cross-relations between voices.
};

/// @brief Analyze counterpoint quality of a set of notes.
/// @param notes All notes across all voices.
/// @param num_voices Number of distinct voices (IDs 0..num_voices-1).
/// @return Aggregated analysis result with all sub-metrics.
CounterpointAnalysisResult analyzeCounterpoint(const std::vector<NoteEvent>& notes,
                                               uint8_t num_voices);

/// @brief Count parallel perfect intervals (5ths and octaves).
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Number of parallel perfect interval violations.
/// @note Two voices moving in the same direction arriving at P5 or P8.
uint32_t countParallelPerfect(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Count hidden (direct) perfect intervals.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Number of hidden perfect interval violations.
/// @note Same-direction motion arriving at P5/P8 where the upper voice leaps.
uint32_t countHiddenPerfect(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Count voice crossing violations.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Number of beat positions where an upper voice dips below a lower voice.
/// @note Voice 0 is highest (soprano); larger IDs are lower voices.
uint32_t countVoiceCrossings(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Calculate the rate at which dissonances resolve by step to consonance.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Fraction in [0,1]; 1.0 if no dissonances exist.
float dissonanceResolutionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Count augmented leaps (tritone) within individual voices.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Number of consecutive-note pairs with a tritone (6 semitones) leap.
uint32_t countAugmentedLeaps(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Count cross-relations between voices.
///
/// A cross-relation occurs when two voices use conflicting chromatic
/// alterations of the same pitch class within close temporal proximity
/// (e.g., B-natural in soprano vs B-flat in alto within one beat).
///
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @param proximity_threshold Maximum tick distance for cross-relation (default: 1 beat).
/// @return Number of cross-relation violations.
uint32_t countCrossRelations(const std::vector<NoteEvent>& notes, uint8_t num_voices,
                              Tick proximity_threshold = kTicksPerBeat);

/// @brief Build a FailReport from counterpoint analysis.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return FailReport containing one FailIssue per detected violation.
FailReport buildCounterpointReport(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Calculate the ratio of stepwise motion in the bass (lowest) voice.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Fraction in [0,1]; 1.0 if all bass motion is stepwise (1-2 semitones).
///         Returns 1.0 if fewer than 2 bass notes exist.
float bassLineStepwiseRatio(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Calculate average leap size across all voices.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Average interval in semitones between consecutive notes across all voices.
///         Returns 0.0 if fewer than 2 notes in any voice.
float voiceLeadingSmoothness(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Calculate the rate of contrary motion between adjacent voice pairs.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Fraction in [0,1] of beat transitions where adjacent voices move in opposite directions.
///         Returns 0.0 if fewer than 2 voices or insufficient notes.
float contraryMotionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Calculate rate of leaps (>=4 semitones) followed by stepwise contrary motion.
/// @param notes All notes across all voices.
/// @param num_voices Number of voices.
/// @return Fraction in [0,1]; 1.0 if no leaps exist.
float leapResolutionRate(const std::vector<NoteEvent>& notes, uint8_t num_voices);

}  // namespace bach

#endif  // BACH_ANALYSIS_COUNTERPOINT_ANALYZER_H
