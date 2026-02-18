// Feasibility harness: micro-exposition simulation, voice assignment search,
// Subject x Answer pair verification, and Subject x CS solvability.

#ifndef BACH_CONSTRAINT_FEASIBILITY_HARNESS_H
#define BACH_CONSTRAINT_FEASIBILITY_HARNESS_H

#include <cstdint>
#include <vector>

#include "constraint/obligation.h"
#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/subject.h"

namespace bach {

// ---------------------------------------------------------------------------
// P1.g1: MicroExposition Simulator
// ---------------------------------------------------------------------------

/// @brief Result of a micro-exposition simulation run.
///
/// Aggregates success/failure statistics from multiple trial expositions
/// to assess whether a subject is feasible for fugue generation before
/// committing to the full pipeline.
struct MicroSimResult {
  int num_attempts = 0;
  int num_success = 0;
  int num_critical_violations = 0;
  int num_bottleneck = 0;
  float avg_register_overlap = 0.0f;
  float max_accent_collision = 0.0f;

  /// @brief Success rate as a fraction of attempts.
  /// @return 0.0 if no attempts, otherwise num_success / num_attempts.
  float success_rate() const {
    return num_attempts > 0 ? static_cast<float>(num_success) / num_attempts : 0.0f;
  }

  /// @brief Whether the simulation indicates feasibility.
  ///
  /// Requires >= 90% success rate, no critical violations (parallel 5ths/8ths),
  /// no bottlenecks, moderate register overlap, and low accent collision.
  ///
  /// @return True if the subject passes all feasibility thresholds.
  bool feasible() const {
    return success_rate() >= 0.90f
        && num_critical_violations == 0
        && num_bottleneck == 0
        && avg_register_overlap < 0.60f
        && max_accent_collision < 0.40f;
  }
};

/// @brief Run a micro-exposition simulation for feasibility probing.
///
/// For each trial (using different seeds):
/// 1. Generate answer and countersubject from the subject.
/// 2. Build an unvalidated exposition.
/// 3. Register voices and populate a CounterpointState.
/// 4. Validate with BachRuleEvaluator over the exposition notes.
/// 5. Track critical violations (parallel_fifths, parallel_octaves).
/// 6. Measure register overlap between voice pitch ranges.
///
/// @param subject The fugue subject to test.
/// @param config Fugue configuration (num_voices, key, etc.).
/// @param num_trials Number of simulation trials (default 20).
/// @return Aggregated simulation results.
MicroSimResult runMicroSim(
    const Subject& subject,
    const FugueConfig& config,
    int num_trials = 20);

// ---------------------------------------------------------------------------
// P1.g2: VoiceAssignmentSearch
// ---------------------------------------------------------------------------

/// @brief Result of voice assignment optimization.
///
/// Contains the optimal octave offset and scoring breakdown for placing
/// a subject in the fugue's voice registers.
struct VoiceAssignment {
  int8_t start_octave_offset = 0;  ///< Octave offset from original (-2 to +2).
  float separation_score = 0.0f;   ///< Voice separation quality [0, 1].
  float sim_score = 0.0f;          ///< MicroSim success rate (if run).
  float final_score = 0.0f;        ///< Combined score for ranking.
};

/// @brief Find the best octave offset for voice assignment.
///
/// Phase 1 (coarse): Evaluates all 5 octave offsets (-2 to +2) by scoring
/// voice separation, accent contour alignment, and register clarity.
///
/// Phase 2 (precise): Runs MicroSim with 3 trials for the top K=3
/// candidates. final_score = success_rate * separation_score.
///
/// @param subject The fugue subject to place.
/// @param profile Constraint profile with accent contour and register arc.
/// @param config Fugue configuration (num_voices, key, etc.).
/// @return The best voice assignment found.
VoiceAssignment findBestAssignment(
    const Subject& subject,
    const SubjectConstraintProfile& profile,
    const FugueConfig& config);

// ---------------------------------------------------------------------------
// P1.g3: Subject x Answer Pair Verification
// ---------------------------------------------------------------------------

/// @brief A conflict between two obligations from subject and answer profiles.
///
/// Detected when obligations of incompatible types overlap in time,
/// e.g., a LeadingTone resolution demand conflicting with a StrongBeatHarm
/// gate, or a Seventh resolution interrupting a LeapResolve at the same tick.
struct ObligationConflict {
  uint16_t obligation_a_id;  ///< ID from profile A (subject).
  uint16_t obligation_b_id;  ///< ID from profile B (answer).
  ObligationType type_a;     ///< Obligation type from profile A.
  ObligationType type_b;     ///< Obligation type from profile B.
  Tick conflict_tick;        ///< Tick where the conflict occurs.
};

/// @brief Result of verifying a subject x answer pair at a given offset.
///
/// Combines tonal answer feasibility, obligation density, cadence conflict,
/// and detected obligation conflicts into a single feasibility assessment.
struct PairVerificationResult {
  bool tonal_answer_feasible;    ///< Whether the answer can be tonal.
  float pair_peak_density;       ///< Peak simultaneous active debt density.
  float cadence_conflict_score;  ///< 0.0-1.0, cadence overlap severity.
  std::vector<ObligationConflict> conflicts;  ///< Detected obligation conflicts.

  /// @brief Check overall pair feasibility.
  /// @return True if no conflicts exist and cadence conflict is below threshold.
  bool feasible() const {
    return conflicts.empty() && cadence_conflict_score < 0.5f;
  }
};

/// @brief Verify feasibility of a subject x answer pair at a given temporal offset.
///
/// Algorithm:
/// 1. Time-shift answer obligations by offset_ticks.
/// 2. Merge subject and answer obligations into a combined timeline.
/// 3. Compute simultaneous active debt density (pair_peak_density).
/// 4. Detect obligation conflicts:
///    - LeadingTone vs StrongBeatHarm overlap at same tick.
///    - Seventh vs LeapResolve interruption at same tick.
/// 5. Compute cadence conflict score: both subject and answer having
///    CadenceApproach/CadenceStable active simultaneously.
///
/// @param subject_prof Constraint profile of the subject.
/// @param answer_prof Constraint profile of the answer.
/// @param offset_ticks Temporal offset of the answer relative to the subject (in ticks).
/// @return Pair verification result with feasibility assessment.
PairVerificationResult verifyPair(
    const SubjectConstraintProfile& subject_prof,
    const SubjectConstraintProfile& answer_prof,
    int offset_ticks);

// ---------------------------------------------------------------------------
// P1.g4: Subject x Countersubject Solvability
// ---------------------------------------------------------------------------

/// @brief Result of testing subject x countersubject solvability.
///
/// Evaluates whether a countersubject can work alongside the subject
/// based on vertical interval consonance and register separation.
struct SolvabilityResult {
  float vertical_clash_rate;          ///< Ratio of dissonant strong-beat intervals.
  float strong_beat_dissonance_rate;  ///< Strong-beat non-chord-tone rate.
  float register_overlap;            ///< Pitch range overlap between S and CS.

  /// @brief Check overall solvability.
  /// @return True if all metrics are within acceptable thresholds.
  bool solvable() const {
    return vertical_clash_rate <= 0.15f &&
           strong_beat_dissonance_rate <= 0.05f &&
           register_overlap <= 0.50f;
  }
};

/// @brief Test solvability of a subject x countersubject combination.
///
/// Algorithm:
/// 1. Overlay subject and CS notes at the same timeframe.
/// 2. At each strong beat (ticks divisible by kTicksPerBeat*2), check vertical interval.
/// 3. Count dissonant strong-beat intervals (seconds, tritones, sevenths).
/// 4. Compute vertical_clash_rate = dissonant_strong_beats / total_strong_beats.
/// 5. Compute register overlap = intersection of pitch ranges / union of pitch ranges.
///
/// @param subject_notes Subject note sequence.
/// @param cs_notes Countersubject note sequence.
/// @param key Musical key context.
/// @param is_minor True for minor key context.
/// @return Solvability result with vertical clash, dissonance, and register metrics.
SolvabilityResult testSolvability(
    const std::vector<NoteEvent>& subject_notes,
    const std::vector<NoteEvent>& cs_notes,
    Key key, bool is_minor);

}  // namespace bach

#endif  // BACH_CONSTRAINT_FEASIBILITY_HARNESS_H
