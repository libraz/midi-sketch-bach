// Voice dynamics model for Bach-style melodic generation.
// Provides direction persistence, WTC-based interval distribution,
// and beat-position-dependent harmonic scoring.

#ifndef BACH_CORE_MELODIC_STATE_H
#define BACH_CORE_MELODIC_STATE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/rng_util.h"

namespace bach {

/// @brief Tracks melodic inertia for voice-dynamics-based pitch selection.
struct MelodicState {
  int last_direction = 0;        // -1, 0, +1.
  int direction_run_length = 0;  // Consecutive same-direction notes.
  int last_skip_size = 0;        // Previous skip interval in semitones (3-4).
  int last_large_leap = 0;       // Previous large leap in semitones (5+).
  float phrase_progress = 0.0f;  // Progress within phrase (0..1).
  bool prev_was_reversal = false;
};

/// @brief Choose melodic direction using persistence model with noise.
/// Avoids mechanical zigzag while allowing long runs to reverse naturally.
inline int chooseMelodicDirection(const MelodicState& state,
                                  std::mt19937& rng) {
  if (state.last_direction == 0) {
    return rng::rollProbability(rng, 0.5f) ? 1 : -1;
  }

  float base_reversal = 0.35f;

  // Long runs increase reversal probability.
  if (state.direction_run_length >= 3) {
    base_reversal += 0.30f;
  }
  // Suppress zigzag: if we just reversed, resist another reversal.
  if (state.direction_run_length == 1 && state.prev_was_reversal) {
    base_reversal -= 0.40f;
    base_reversal = std::max(0.05f, base_reversal);
  }

  // After large leap: 65% contrary motion preference.
  if (state.last_large_leap != 0) {
    base_reversal = 0.65f;
  }

  // Noise prevents mechanical periodicity.
  float noise = rng::rollFloat(rng, -0.08f, 0.08f);
  base_reversal = std::clamp(base_reversal + noise, 0.05f, 0.95f);

  return rng::rollProbability(rng, base_reversal) ? -state.last_direction
                                                  : state.last_direction;
}

/// @brief Choose interval step count in diatonic degrees (WTC distribution).
/// @return 1 (step), 2 (skip/3rd), 3 (leap/4th+).
inline int chooseMelodicInterval(const MelodicState& state,
                                 std::mt19937& rng) {
  // Consecutive large leap prohibition.
  if (state.last_large_leap != 0) {
    // After large leap: 62% step, 28% skip, 10% small leap.
    float roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (roll < 0.62f) return 1;
    if (roll < 0.90f) return 2;
    return 3;
  }

  // After skip: 62% step, 28% skip, 10% leap.
  if (state.last_skip_size != 0) {
    float roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (roll < 0.62f) return 1;
    if (roll < 0.90f) return 2;
    return 3;
  }

  // Normal distribution: 60% step, 32% skip, 8% leap.
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < 0.60f) return 1;
  if (roll < 0.92f) return 2;
  return 3;
}

/// @brief Goal tension factor: relax penalties toward phrase end.
/// Returns 1.0 for first 60%, linearly decreasing to 0.4 at 100%.
inline float goalTensionFactor(float phrase_progress) {
  if (phrase_progress > 0.6f) {
    float t = (phrase_progress - 0.6f) / 0.4f;
    return 1.0f - t * 0.6f;
  }
  return 1.0f;
}

/// @brief Update melodic state after placing a note.
inline void updateMelodicState(MelodicState& state, uint8_t prev_pitch,
                               uint8_t new_pitch) {
  int interval =
      static_cast<int>(new_pitch) - static_cast<int>(prev_pitch);
  int abs_interval = std::abs(interval);
  int new_direction = (interval > 0) ? 1 : ((interval < 0) ? -1 : 0);

  bool is_reversal = (new_direction != 0 &&
                      new_direction != state.last_direction &&
                      state.last_direction != 0);

  if (new_direction == state.last_direction) {
    state.direction_run_length++;
  } else {
    state.direction_run_length = 1;
  }
  state.prev_was_reversal = is_reversal;
  state.last_direction = new_direction;

  // Track skip (3-4 semitones) and large leap (5+ semitones).
  state.last_skip_size =
      (abs_interval >= 3 && abs_interval <= 4) ? abs_interval : 0;
  state.last_large_leap = (abs_interval >= 5) ? abs_interval : 0;
}

/// @brief Score a candidate pitch for weighted selection.
/// Higher score = more desirable. Used when choosing among chord tones
/// or scale tones on strong beats.
inline float scoreCandidatePitch(const MelodicState& state,
                                 uint8_t prev_pitch, uint8_t candidate,
                                 Tick tick, bool is_chord_tone) {
  int interval =
      static_cast<int>(candidate) - static_cast<int>(prev_pitch);
  int abs_interval = std::abs(interval);
  int direction = (interval > 0) ? 1 : ((interval < 0) ? -1 : 0);

  float score = 0.0f;

  // Motion score: continuation bonus.
  if (direction == state.last_direction && direction != 0) {
    score += 0.30f;
  }

  // Step score: prefer stepwise motion.
  if (abs_interval <= 2) {
    score += 0.20f;
  } else if (abs_interval <= 4) {
    score += 0.10f;
  }

  // Harmonic penalty for non-chord tone (beat-position dependent).
  if (!is_chord_tone) {
    MetricLevel level = metricLevel(tick);
    float penalty = 0.0f;
    switch (level) {
      case MetricLevel::Bar: penalty = -0.40f; break;
      case MetricLevel::Beat: penalty = -0.15f; break;
      default: penalty = -0.05f; break;
    }
    float tension = goalTensionFactor(state.phrase_progress);
    score += penalty * tension;
  }

  // Leap penalty: consecutive leaps.
  float leap_penalty = 0.0f;
  if (abs_interval >= 5 && state.last_large_leap != 0) {
    leap_penalty = -0.50f;
  } else if (abs_interval >= 3 && state.last_skip_size != 0) {
    leap_penalty = -0.15f;
  }
  float tension = goalTensionFactor(state.phrase_progress);
  score += leap_penalty * tension;

  // After large leap: contrary motion bonus.
  if (state.last_large_leap != 0 && direction != 0 &&
      direction != state.last_direction) {
    score += 0.20f;
  }

  return score;
}

/// @brief Select the best pitch from candidates using scoring.
/// Falls back to the first candidate if all scores are very low.
inline uint8_t selectBestPitch(const MelodicState& state, uint8_t prev_pitch,
                               const std::vector<uint8_t>& candidates,
                               Tick tick, bool all_chord_tones,
                               std::mt19937& rng) {
  if (candidates.empty()) return prev_pitch;
  if (candidates.size() == 1) return candidates[0];

  // Score each candidate.
  std::vector<float> scores;
  scores.reserve(candidates.size());
  float max_score = -100.0f;
  for (uint8_t c : candidates) {
    float s = scoreCandidatePitch(state, prev_pitch, c, tick, all_chord_tones);
    scores.push_back(s);
    if (s > max_score) max_score = s;
  }

  // Shift scores to positive range and use as weights.
  float shift = (max_score < 0.0f) ? (-max_score + 0.1f) : 0.1f;
  std::vector<float> weights;
  weights.reserve(candidates.size());
  for (float s : scores) {
    weights.push_back(std::max(0.01f, s + shift));
  }

  return rng::selectWeighted(rng, candidates, weights);
}

}  // namespace bach

#endif  // BACH_CORE_MELODIC_STATE_H
