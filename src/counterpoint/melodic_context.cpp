// Implementation of melodic context scoring for voice leading quality.

#include "counterpoint/melodic_context.h"

#include <cmath>
#include <cstdlib>

#include "core/interval.h"
#include "core/pitch_utils.h"

namespace bach {

float computeGoalApproachBonus(uint8_t current_pitch, Tick current_tick,
                               const PhraseGoal& goal) {
  // No target pitch set (0 is an invalid musical target) -- no bonus.
  if (goal.target_pitch == 0 || goal.target_tick == 0) return 0.0f;

  // Pitch proximity: inverse linear within an octave. Beyond 12 semitones, no bonus.
  int pitch_distance = absoluteInterval(current_pitch, goal.target_pitch);
  constexpr int kMaxPitchDistance = 12;  // One octave.
  float pitch_factor = 0.0f;
  if (pitch_distance < kMaxPitchDistance) {
    pitch_factor = 1.0f - static_cast<float>(pitch_distance) / static_cast<float>(kMaxPitchDistance);
  }

  // Temporal proximity: ramps linearly from 0.0 (tick 0) to 1.0 (at or past target).
  float temporal_factor = 0.0f;
  if (current_tick >= goal.target_tick) {
    temporal_factor = 1.0f;
  } else {
    temporal_factor = static_cast<float>(current_tick) / static_cast<float>(goal.target_tick);
  }

  // Combined bonus: product of pitch and temporal factors, scaled by max bonus.
  return goal.bonus * pitch_factor * temporal_factor;
}

float MelodicContext::scoreMelodicQuality(const MelodicContext& ctx, uint8_t candidate,
                                          const PhraseGoal* goal, Tick current_tick) {
  // No context -> neutral score (plus optional goal bonus).
  if (ctx.prev_count == 0) {
    float base = 0.5f;
    if (goal != nullptr) {
      base += computeGoalApproachBonus(candidate, current_tick, *goal);
    }
    if (base > 1.0f) base = 1.0f;
    return base;
  }

  float score = 0.5f;  // Base score (neutral).
  uint8_t prev = ctx.prev_pitches[0];
  int interval = absoluteInterval(candidate, prev);
  int directed = static_cast<int>(candidate) - static_cast<int>(prev);
  int direction = (directed > 0) ? 1 : (directed < 0 ? -1 : 0);

  // Rule 1: Step after leap in opposite direction (+0.2)
  // A leap (>= 4 semitones) followed by stepwise motion (1-2 semitones)
  // in the opposite direction is a hallmark of good Bach voice leading.
  if (ctx.prev_count >= 2 && ctx.prev_direction != 0) {
    int prev_interval = absoluteInterval(prev, ctx.prev_pitches[1]);
    if (prev_interval >= 4 && interval >= 1 && interval <= 2 && direction != 0 &&
        direction != ctx.prev_direction) {
      score += 0.2f;
    }
  }

  // Rule 2: Stepwise motion (+0.1)
  if (interval >= 1 && interval <= 2) {
    score += 0.1f;
  }

  // Rule 3: Imperfect consonance interval (+0.05)
  int reduced = interval_util::compoundToSimple(interval);
  if (interval_util::isConsonance(reduced) && !interval_util::isPerfectConsonance(reduced)) {
    score += 0.05f;
  }

  // Rule 4: Same-pitch repetition penalty (graduated).
  // Any repetition gets a mild penalty; consecutive repetitions are progressively penalized.
  if (interval == 0) {
    score -= 0.1f;  // Any repetition: mild penalty.
    if (ctx.prev_count >= 2 && ctx.prev_pitches[0] == ctx.prev_pitches[1]) {
      score -= 0.2f;  // 3 consecutive same pitch: total -0.3.
    }
    if (ctx.prev_count >= 3 && ctx.prev_pitches[1] == ctx.prev_pitches[2]) {
      score -= 0.2f;  // 4 consecutive same pitch: total -0.5.
    }
  }

  // Rule 5: Augmented interval (tritone) penalty (-0.3)
  if (reduced == 6) {
    score -= 0.3f;
  }

  // Rule 6: Leading tone non-resolution penalty (-0.5)
  // Leading tone must resolve up by semitone to tonic.
  if (ctx.is_leading_tone && interval != 1) {
    score -= 0.5f;
  } else if (ctx.is_leading_tone && directed == 1) {
    score += 0.1f;  // Correct resolution bonus.
  }

  // Rule 7: Phrase goal approach bonus (additive, up to goal.bonus).
  if (goal != nullptr) {
    score += computeGoalApproachBonus(candidate, current_tick, *goal);
  }

  // Rule 8: Unresolved leap penalty / resolution bonus.
  // Baroque practice: leaps of a 4th or wider demand stepwise resolution
  // in the opposite direction.
  if (ctx.leap_needs_resolution) {
    bool is_step = (interval >= 1 && interval <= 2);
    bool is_opposite = (direction != 0 && direction != ctx.prev_direction);
    if (is_step && is_opposite) {
      score += 0.4f;   // Correct resolution: strong bonus.
    } else if (interval > 2) {
      score -= 0.4f;   // Consecutive leaps: strong suppression.
    } else {
      score -= 0.15f;  // Same-direction step: mild penalty.
    }
  }

  // Rule 9: Run length control.
  // Penalize excessively long runs of same-direction stepwise motion.
  // This prevents meandering scalar passages that lack the characteristic
  // leaps and direction changes of Baroque voice leading.
  if (ctx.consecutive_same_dir >= 5 && direction != 0 &&
      direction == ctx.prev_direction && interval <= 2) {
    score -= 0.15f;
  }

  // Clamp to [0.0, 1.0].
  if (score < 0.0f) score = 0.0f;
  if (score > 1.0f) score = 1.0f;

  return score;
}

}  // namespace bach
