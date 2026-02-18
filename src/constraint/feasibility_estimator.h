// Phase 2: FeasibilityEstimator -- candidate space estimation during generation.

#ifndef BACH_CONSTRAINT_FEASIBILITY_ESTIMATOR_H
#define BACH_CONSTRAINT_FEASIBILITY_ESTIMATOR_H

#include "constraint/constraint_state.h"
#include "constraint/obligation.h"
#include "core/basic_types.h"

namespace bach {

class CollisionResolver;
class CounterpointState;
class IRuleEvaluator;

/// @brief Estimates the solution space size at a generation point.
///
/// Reuses CollisionResolver's 6-stage cascade to enumerate valid candidates,
/// then filters through InvariantSet Hard checks. Provides min_choices
/// as a deadlock detector (min_choices == 0 → generation failure).
///
/// @note Phase 6 plan: once all forms migrate to the constraint pipeline,
///       estimate() will be replaced by estimateWithCascade() as the default.
class FeasibilityEstimator {
 public:
  /// @brief Maximum candidates to evaluate per step.
  static constexpr int kMaxCandidates = 8;

  /// @brief Lookahead horizon in beats.
  static constexpr int kHorizonBeats = 8;

  /// @brief Estimate solution space at the given tick for the given voice.
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator for validation.
  /// @param resolver Collision resolver for candidate enumeration.
  /// @param voice_id Voice to evaluate.
  /// @param tick Current tick position.
  /// @param duration Expected note duration.
  /// @param invariants Current invariant set (Hard checks applied).
  /// @param snap Current vertical snapshot.
  /// @return Feasibility with min/max choice bounds.
  Feasibility estimate(const CounterpointState& state,
                       const IRuleEvaluator& rules,
                       const CollisionResolver& resolver,
                       VoiceId voice_id, Tick tick, Tick duration,
                       const InvariantSet& invariants,
                       const VerticalSnapshot& snap) const;

  /// @brief Estimate solution space using CollisionResolver's full cascade.
  ///
  /// Unlike estimate() which only uses isSafeToPlace(), this method calls
  /// findSafePitch() to evaluate what the cascade would actually produce
  /// for sampled pitches. Provides a more accurate feasibility assessment
  /// by reflecting the real fallback strategy behavior.
  ///
  /// @note Phase 6 will deprecate the isSafeToPlace-only estimate() in favor
  ///       of this method once all forms migrate to the constraint pipeline.
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator for validation.
  /// @param resolver Collision resolver for cascade evaluation.
  /// @param voice_id Voice to evaluate.
  /// @param tick Current tick position.
  /// @param duration Expected note duration.
  /// @param invariants Current invariant set (Hard checks applied).
  /// @param snap Current vertical snapshot.
  /// @return Feasibility with min/max choice bounds reflecting cascade solutions.
  Feasibility estimateWithCascade(const CounterpointState& state,
                                  const IRuleEvaluator& rules,
                                  const CollisionResolver& resolver,
                                  VoiceId voice_id, Tick tick, Tick duration,
                                  const InvariantSet& invariants,
                                  const VerticalSnapshot& snap) const;
};

}  // namespace bach

#endif  // BACH_CONSTRAINT_FEASIBILITY_ESTIMATOR_H
