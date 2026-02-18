// Phase 2: FeasibilityEstimator implementation.

#include "constraint/feasibility_estimator.h"

#include <algorithm>

#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {

Feasibility FeasibilityEstimator::estimate(
    const CounterpointState& state, const IRuleEvaluator& rules,
    const CollisionResolver& resolver, VoiceId voice_id, Tick tick,
    Tick duration, const InvariantSet& invariants,
    const VerticalSnapshot& snap) const {
  Feasibility result;

  // Enumerate candidates by trying pitches around the voice's range.
  const auto* range = state.getVoiceRange(voice_id);
  if (!range) return result;

  int valid_count = 0;
  int total_tried = 0;

  // Sample pitches in the voice range at semitone intervals.
  // Use a stride to keep evaluation count bounded.
  int range_size = range->high - range->low + 1;
  int stride = std::max(1, range_size / kMaxCandidates);

  for (uint8_t p = range->low; p <= range->high && total_tried < kMaxCandidates;
       p += stride) {
    total_tried++;

    // Check if pitch is safe via collision resolver.
    bool safe = resolver.isSafeToPlace(state, rules, voice_id, p, tick,
                                       duration);
    if (!safe) continue;

    // Check Hard invariants.
    auto inv_check = invariants.satisfies(
        p, voice_id, tick, snap, &rules, nullptr, &state, nullptr, 0);
    if (inv_check.hard_violations > 0) continue;

    valid_count++;
  }

  // Scale up estimate based on sampling ratio.
  float scale = static_cast<float>(range_size) /
                static_cast<float>(total_tried > 0 ? total_tried : 1);
  result.min_choices = static_cast<uint16_t>(valid_count);
  result.max_choices = static_cast<uint16_t>(
      std::min(static_cast<int>(valid_count * scale), 65535));

  return result;
}

Feasibility FeasibilityEstimator::estimateWithCascade(
    const CounterpointState& state, const IRuleEvaluator& rules,
    const CollisionResolver& resolver, VoiceId voice_id, Tick tick,
    Tick duration, const InvariantSet& invariants,
    const VerticalSnapshot& snap) const {
  Feasibility result;

  const auto* range = state.getVoiceRange(voice_id);
  if (!range) return result;

  int valid_count = 0;
  int total_tried = 0;

  int range_size = range->high - range->low + 1;
  int stride = std::max(1, range_size / kMaxCandidates);

  for (uint8_t p = range->low; p <= range->high && total_tried < kMaxCandidates;
       p += stride) {
    total_tried++;

    // Use the full cascade instead of just isSafeToPlace.
    PlacementResult placement = resolver.findSafePitch(
        state, rules, voice_id, p, tick, duration);
    if (!placement.accepted) continue;

    // Check Hard invariants on the cascade's chosen pitch.
    auto inv_check = invariants.satisfies(
        placement.pitch, voice_id, tick, snap, &rules, nullptr, &state, nullptr, 0);
    if (inv_check.hard_violations > 0) continue;

    valid_count++;
  }

  float scale = static_cast<float>(range_size) /
                static_cast<float>(total_tried > 0 ? total_tried : 1);
  result.min_choices = static_cast<uint16_t>(valid_count);
  result.max_choices = static_cast<uint16_t>(
      std::min(static_cast<int>(valid_count * scale), 65535));

  return result;
}

}  // namespace bach
