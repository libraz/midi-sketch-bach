// Implementation of melodic context scoring for voice leading quality.

#include "counterpoint/melodic_context.h"

#include <cmath>
#include <cstdlib>

namespace bach {

float computeGoalApproachBonus(uint8_t current_pitch, Tick current_tick,
                               const PhraseGoal& goal) {
  // No target pitch set (0 is an invalid musical target) -- no bonus.
  if (goal.target_pitch == 0 || goal.target_tick == 0) return 0.0f;

  // Pitch proximity: inverse linear within an octave. Beyond 12 semitones, no bonus.
  int pitch_distance =
      std::abs(static_cast<int>(current_pitch) - static_cast<int>(goal.target_pitch));
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
  int interval = std::abs(static_cast<int>(candidate) - static_cast<int>(prev));
  int directed = static_cast<int>(candidate) - static_cast<int>(prev);
  int direction = (directed > 0) ? 1 : (directed < 0 ? -1 : 0);

  // Rule 1: Step after leap in opposite direction (+0.3)
  // A leap (>= 4 semitones) followed by stepwise motion (1-2 semitones)
  // in the opposite direction is a hallmark of good Bach voice leading.
  if (ctx.prev_count >= 2 && ctx.prev_direction != 0) {
    int prev_interval =
        std::abs(static_cast<int>(prev) - static_cast<int>(ctx.prev_pitches[1]));
    if (prev_interval >= 4 && interval >= 1 && interval <= 2 && direction != 0 &&
        direction != ctx.prev_direction) {
      score += 0.3f;
    }
  }

  // Rule 2: Stepwise motion (+0.2)
  if (interval >= 1 && interval <= 2) {
    score += 0.2f;
  }

  // Rule 3: Imperfect consonance interval (+0.1)
  int reduced = interval % 12;
  if (reduced == 3 || reduced == 4 || reduced == 8 || reduced == 9) {
    score += 0.1f;
  }

  // Rule 4: Same-pitch repetition penalty (-0.2)
  if (interval == 0 && ctx.prev_count >= 2 && ctx.prev_pitches[0] == ctx.prev_pitches[1]) {
    score -= 0.2f;
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

  // Clamp to [0.0, 1.0].
  if (score < 0.0f) score = 0.0f;
  if (score > 1.0f) score = 1.0f;

  return score;
}

}  // namespace bach
