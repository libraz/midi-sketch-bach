// Implementation of melodic context scoring for voice leading quality.

#include "counterpoint/melodic_context.h"

#include <cmath>
#include <cstdlib>

namespace bach {

float MelodicContext::scoreMelodicQuality(const MelodicContext& ctx, uint8_t candidate) {
  // No context -> neutral score.
  if (ctx.prev_count == 0) return 0.5f;

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

  // Clamp to [0.0, 1.0].
  if (score < 0.0f) score = 0.0f;
  if (score > 1.0f) score = 1.0f;

  return score;
}

}  // namespace bach
