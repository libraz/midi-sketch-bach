// Voice dynamics model for Bach-style melodic generation.
// Provides direction persistence, WTC-based interval distribution,
// voice-profile-driven scoring, and beat-position-dependent harmonic scoring.

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
  int consecutive_leap_count = 0;  // Consecutive leaps (3+ semitones).
};

/// @brief Voice-specific melodic parameter profile.
/// Calibrated from BWV578 per-voice analysis and category summaries.
struct VoiceProfile {
  float step_prob;          ///< P(step) -- 1-2 semitones.
  float skip_prob;          ///< P(skip) -- 3-4 semitones.
  // leap_prob = 1.0 - step_prob - skip_prob

  float large_leap_prob;    ///< P(6th+) -- conditional within leaps.
  bool require_stepwise_after_large_leap;  ///< Force step after 6th+.
  uint8_t max_consecutive_leaps;           ///< Max consecutive leaps (3rd+).

  float dur_weights[6];     ///< {whole, half, dotted-qtr, qtr, 8th, 16th}.
  Tick min_duration;         ///< Minimum note duration.

  float stepwise_bonus;     ///< Score bonus for stepwise motion.
  float p4p5_bonus;         ///< Score bonus for P4/P5 leaps (bass).
  float continuation_bonus; ///< Score bonus for direction continuation.
};

namespace voice_profiles {

/// Soprano: favor shorter durations (8th/16th) for ornamental activity.
/// BWV578 v1 avg duration 0.24 beats supports rapid figuration.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
constexpr VoiceProfile kSoprano = {
    0.67f, 0.22f, 0.03f, true, 1,
    {0.3f, 1.0f, 0.8f, 2.0f, 3.0f, 2.5f}, 120,  // min=16th
    0.20f, 0.0f, 0.30f};

/// Alto: mixed duration distribution for rhythmic variety.
/// BWV578 v2 avg duration 0.47 beats -- moderate activity.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
constexpr VoiceProfile kAlto = {
    0.59f, 0.25f, 0.03f, true, 1,
    {0.8f, 1.5f, 1.2f, 3.0f, 2.5f, 1.5f}, 120,
    0.20f, 0.0f, 0.30f};

/// Tenor: favor medium durations (quarter/half) for sustained support.
/// BWV578 v3 avg duration 0.30 beats -- between upper and lower.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
constexpr VoiceProfile kTenor = {
    0.65f, 0.22f, 0.04f, true, 1,
    {0.8f, 2.5f, 1.5f, 3.5f, 1.5f, 0.8f}, 120,
    0.20f, 0.0f, 0.30f};

/// Bass: favor longer durations (half/quarter) for harmonic foundation.
/// BWV578 v4 avg duration 0.51 beats -- slow-moving bass line.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
constexpr VoiceProfile kBassLine = {
    0.44f, 0.24f, 0.12f, true, 2,
    {1.5f, 3.0f, 2.0f, 3.0f, 1.5f, 0.5f}, 240,  // min=8th
    0.10f, 0.18f, 0.30f};

constexpr VoiceProfile kPedalPoint = {
    0.30f, 0.20f, 0.08f, true, 2,
    {2.0f, 4.0f, 2.0f, 2.0f, 0.5f, 0.0f}, 480,  // min=quarter
    0.05f, 0.20f, 0.25f};

constexpr VoiceProfile kCantusFirmus = {
    0.70f, 0.25f, 0.01f, true, 1,
    {0.5f, 2.0f, 1.5f, 3.0f, 1.0f, 0.0f}, 480,  // min=quarter
    0.25f, 0.0f, 0.30f};

}  // namespace voice_profiles

/// @brief Get voice profile from voice ID and voice count.
/// Maps: voice 0 = soprano, 1 = alto, ..., last = bass.
inline VoiceProfile getVoiceProfile(uint8_t voice_id, uint8_t num_voices) {
  if (num_voices <= 1) return voice_profiles::kSoprano;
  if (voice_id == num_voices - 1) return voice_profiles::kBassLine;
  if (voice_id == 0) return voice_profiles::kSoprano;
  if (voice_id == num_voices - 2) return voice_profiles::kTenor;
  return voice_profiles::kAlto;
}

/// @brief Get voice profile for a specific texture function.
/// Subject/Countersubject apply strictness modifiers on top of
/// the base voice profile.
inline VoiceProfile getVoiceProfile(TextureFunction function,
                                     uint8_t voice_id,
                                     uint8_t num_voices) {
  VoiceProfile profile = getVoiceProfile(voice_id, num_voices);
  switch (function) {
    case TextureFunction::Subject:
      profile.large_leap_prob *= 0.5f;
      profile.continuation_bonus += 0.10f;
      profile.max_consecutive_leaps = 1;
      break;
    case TextureFunction::Countersubject:
      profile.large_leap_prob *= 0.7f;
      profile.max_consecutive_leaps = 1;
      break;
    case TextureFunction::PedalPoint:
      return voice_profiles::kPedalPoint;
    case TextureFunction::CantusFirmus:
      return voice_profiles::kCantusFirmus;
    case TextureFunction::BassLine:
      return voice_profiles::kBassLine;
    default:
      break;
  }
  return profile;
}

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

/// @brief Choose interval step count using VoiceProfile distribution.
/// @param profile Voice-specific melodic parameter profile.
/// @return 1 (step), 2 (skip/3rd), 3 (leap/4th+).
inline int chooseMelodicInterval(const MelodicState& state,
                                 std::mt19937& rng,
                                 const VoiceProfile& profile) {
  // Consecutive leap limit.
  if (state.consecutive_leap_count >= profile.max_consecutive_leaps) {
    return 1;  // Force step.
  }

  // After large leap: increase step probability.
  if (state.last_large_leap != 0) {
    float step_boost = profile.require_stepwise_after_large_leap ? 0.15f : 0.0f;
    float step = std::min(1.0f, profile.step_prob + step_boost);
    float skip = profile.skip_prob * 0.8f;
    float total = step + skip;
    float roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (roll < step / total) return 1;
    if (roll < (step + skip) / total) return 2;
    return 3;
  }

  // After skip: slightly prefer step.
  if (state.last_skip_size != 0) {
    float step = profile.step_prob + 0.05f;
    float skip = profile.skip_prob - 0.03f;
    float total = step + skip;
    float roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (roll < step / total) return 1;
    if (roll < (step + skip) / total) return 2;
    return 3;
  }

  // Normal distribution from profile.
  float roll = rng::rollFloat(rng, 0.0f, 1.0f);
  if (roll < profile.step_prob) return 1;
  if (roll < profile.step_prob + profile.skip_prob) return 2;
  return 3;
}

/// @brief Backward-compatible wrapper using is_bass flag.
inline int chooseMelodicInterval(const MelodicState& state,
                                 std::mt19937& rng,
                                 bool is_bass) {
  const VoiceProfile& profile = is_bass ? voice_profiles::kBassLine
                                        : voice_profiles::kSoprano;
  return chooseMelodicInterval(state, rng, profile);
}

/// @brief Default overload (non-bass, uses soprano profile).
inline int chooseMelodicInterval(const MelodicState& state,
                                 std::mt19937& rng) {
  return chooseMelodicInterval(state, rng, voice_profiles::kSoprano);
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

  // Track consecutive leaps (3+ semitones).
  if (abs_interval >= 3) {
    state.consecutive_leap_count++;
  } else {
    state.consecutive_leap_count = 0;
  }
}

/// @brief Score a candidate pitch for weighted selection using VoiceProfile.
/// Higher score = more desirable. Used when choosing among chord tones
/// or scale tones on strong beats.
/// @param profile Voice-specific melodic parameter profile.
inline float scoreCandidatePitch(const MelodicState& state,
                                 uint8_t prev_pitch, uint8_t candidate,
                                 Tick tick, bool is_chord_tone,
                                 const VoiceProfile& profile) {
  int interval = static_cast<int>(candidate) - static_cast<int>(prev_pitch);
  int abs_interval = std::abs(interval);
  int direction = (interval > 0) ? 1 : ((interval < 0) ? -1 : 0);

  float score = 0.0f;

  // Motion score: continuation bonus from profile.
  if (direction == state.last_direction && direction != 0) {
    score += profile.continuation_bonus;
  }

  // Step score from profile.
  if (abs_interval <= 2) {
    score += profile.stepwise_bonus;
  } else if (abs_interval <= 4) {
    score += profile.stepwise_bonus * 0.5f;
  }

  // P4/P5 bonus from profile.
  if (abs_interval == 5 || abs_interval == 7) {
    score += profile.p4p5_bonus;
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

  // After large leap: direction-dependent resolution bonus.
  if (state.last_large_leap != 0 && direction != 0) {
    if (direction != state.last_direction) {
      // Contrary motion after leap.
      score += 0.20f;
      // Direction-dependent resolution: prefer stepwise contrary.
      if (abs_interval <= 2) {
        score += 0.25f;  // Strong bonus for stepwise resolution.
      }
    }
  }

  // Consecutive leap suppression.
  if (abs_interval >= 3 &&
      state.consecutive_leap_count >= profile.max_consecutive_leaps) {
    score -= 0.40f;
  }

  return score;
}

/// @brief Backward-compatible wrapper using is_bass flag.
inline float scoreCandidatePitch(const MelodicState& state,
                                 uint8_t prev_pitch, uint8_t candidate,
                                 Tick tick, bool is_chord_tone,
                                 bool is_bass) {
  const VoiceProfile& profile = is_bass ? voice_profiles::kBassLine
                                        : voice_profiles::kSoprano;
  return scoreCandidatePitch(state, prev_pitch, candidate, tick,
                             is_chord_tone, profile);
}

/// @brief Default overload (non-bass).
inline float scoreCandidatePitch(const MelodicState& state,
                                 uint8_t prev_pitch, uint8_t candidate,
                                 Tick tick, bool is_chord_tone) {
  return scoreCandidatePitch(state, prev_pitch, candidate, tick,
                             is_chord_tone, voice_profiles::kSoprano);
}

/// @brief Select the best pitch from candidates using VoiceProfile scoring.
/// @param profile Voice-specific melodic parameter profile.
inline uint8_t selectBestPitch(const MelodicState& state, uint8_t prev_pitch,
                               const std::vector<uint8_t>& candidates,
                               Tick tick, bool all_chord_tones,
                               std::mt19937& rng,
                               const VoiceProfile& profile) {
  if (candidates.empty()) return prev_pitch;
  if (candidates.size() == 1) return candidates[0];

  std::vector<float> scores;
  scores.reserve(candidates.size());
  float max_score = -100.0f;
  for (uint8_t c : candidates) {
    float s = scoreCandidatePitch(state, prev_pitch, c, tick, all_chord_tones,
                                  profile);
    scores.push_back(s);
    if (s > max_score) max_score = s;
  }

  float shift = (max_score < 0.0f) ? (-max_score + 0.1f) : 0.1f;
  std::vector<float> weights;
  weights.reserve(candidates.size());
  for (float s : scores) {
    weights.push_back(std::max(0.01f, s + shift));
  }

  return rng::selectWeighted(rng, candidates, weights);
}

/// @brief Backward-compatible wrapper using is_bass flag.
inline uint8_t selectBestPitch(const MelodicState& state, uint8_t prev_pitch,
                               const std::vector<uint8_t>& candidates,
                               Tick tick, bool all_chord_tones,
                               std::mt19937& rng,
                               bool is_bass) {
  const VoiceProfile& profile = is_bass ? voice_profiles::kBassLine
                                        : voice_profiles::kSoprano;
  return selectBestPitch(state, prev_pitch, candidates, tick,
                         all_chord_tones, rng, profile);
}

/// @brief Default overload (non-bass).
inline uint8_t selectBestPitch(const MelodicState& state, uint8_t prev_pitch,
                               const std::vector<uint8_t>& candidates,
                               Tick tick, bool all_chord_tones,
                               std::mt19937& rng) {
  return selectBestPitch(state, prev_pitch, candidates, tick,
                         all_chord_tones, rng, voice_profiles::kSoprano);
}

}  // namespace bach

#endif  // BACH_CORE_MELODIC_STATE_H
