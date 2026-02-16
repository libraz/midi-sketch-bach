// Voice dynamics model for Bach-style melodic generation.
// Provides direction persistence, WTC-based interval distribution,
// voice-profile-driven scoring, beat-position-dependent harmonic scoring,
// phrase contour planning, and section boundary state management.

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

// ---------------------------------------------------------------------------
// Phrase contour
// ---------------------------------------------------------------------------

/// @brief Phrase-level directional contour for melodic shaping.
/// Controls the expected pitch direction at each point within a phrase.
struct PhraseContour {
  enum Shape : uint8_t { Arch, Descent, Ascent, Wave, Neutral };
  Shape shape = Arch;
  float peak_progress = 0.4f;   ///< Where the peak falls in the phrase (0..1).
  float strength = 0.25f;       ///< Maximum influence on scoring (0..1).
};

/// @brief Tracks melodic inertia for voice-dynamics-based pitch selection.
struct MelodicState {
  int last_direction = 0;        // -1, 0, +1.
  int direction_run_length = 0;  // Consecutive same-direction notes.
  int last_skip_size = 0;        // Previous skip interval in semitones (3-4).
  int last_large_leap = 0;       // Previous large leap in semitones (5+).
  float phrase_progress = 0.0f;  // Progress within phrase (0..1).
  bool prev_was_reversal = false;
  int consecutive_leap_count = 0;  // Consecutive leaps (3+ semitones).
  PhraseContour contour;           // Phrase-level directional contour.
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

  float gravity_bias;       ///< Downward gravity bias: higher = stronger descent tendency.
                            ///< Chain-length dependent: weak at run start, full at run_length >= 3.
                            ///< Decays to 0 in cadence window (phrase_progress > 0.85).
};

namespace voice_profiles {

/// Soprano: favor shorter durations (8th/16th) for ornamental activity.
/// BWV578 v1 avg duration 0.24 beats supports rapid figuration.
/// Gravity 0.08: mild descent bias (soprano needs upward drive too).
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
/// Strongly differentiated: 8th+16th dominate, long notes suppressed.
constexpr VoiceProfile kSoprano = {
    0.67f, 0.22f, 0.03f, true, 1,
    {0.1f, 0.5f, 0.6f, 1.5f, 4.0f, 3.5f}, 120,  // min=16th
    0.20f, 0.0f, 0.30f, 0.08f};

/// Alto: balanced distribution, slightly favoring quarter notes.
/// BWV578 v2 avg duration 0.47 beats -- moderate activity.
/// Gravity 0.12: standard inner voice descent bias.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
/// Quarter-centered with moderate 8th activity, distinct from soprano.
constexpr VoiceProfile kAlto = {
    0.59f, 0.25f, 0.03f, true, 1,
    {0.4f, 1.2f, 1.5f, 3.5f, 2.0f, 1.0f}, 120,
    0.20f, 0.0f, 0.30f, 0.12f};

/// Tenor: favor medium durations (quarter/dotted-quarter/half) for sustained support.
/// BWV578 v3 avg duration 0.30 beats -- between upper and lower.
/// Gravity 0.12: standard inner voice descent bias.
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
/// Half and dotted-quarter weighted higher than alto; 8th/16th suppressed.
constexpr VoiceProfile kTenor = {
    0.65f, 0.22f, 0.04f, true, 1,
    {0.6f, 3.0f, 2.0f, 3.5f, 1.0f, 0.3f}, 120,
    0.20f, 0.0f, 0.30f, 0.12f};

/// Bass: favor longer durations (half/quarter) for harmonic foundation.
/// BWV578 v4 avg duration 0.51 beats -- slow-moving bass line.
/// Gravity 0.0: bass leaps are symmetric (up/down equally common).
/// Weights: {whole, half, dotted-qtr, qtr, 8th, 16th}
/// Half note dominant; whole note viable; 8th/16th strongly suppressed.
constexpr VoiceProfile kBassLine = {
    0.44f, 0.24f, 0.12f, true, 2,
    {2.0f, 4.0f, 2.5f, 3.0f, 0.8f, 0.2f}, 240,  // min=8th
    0.10f, 0.18f, 0.30f, 0.0f};

constexpr VoiceProfile kPedalPoint = {
    0.30f, 0.20f, 0.08f, true, 2,
    {2.0f, 4.0f, 2.0f, 2.0f, 0.5f, 0.0f}, 480,  // min=quarter
    0.05f, 0.20f, 0.25f, 0.0f};

constexpr VoiceProfile kCantusFirmus = {
    0.70f, 0.25f, 0.01f, true, 1,
    {0.5f, 2.0f, 1.5f, 3.0f, 1.0f, 0.0f}, 480,  // min=quarter
    0.25f, 0.0f, 0.30f, 0.0f};

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

// ---------------------------------------------------------------------------
// Contour direction computation
// ---------------------------------------------------------------------------

/// @brief Compute the expected pitch direction from a phrase contour.
/// @return Value in [-1.0, +1.0]: positive = upward, negative = downward.
inline float computeContourDirection(PhraseContour::Shape shape,
                                     float progress,
                                     float peak_progress) {
  switch (shape) {
    case PhraseContour::Arch:
      if (peak_progress <= 0.0f) return -1.0f;
      if (peak_progress >= 1.0f) return 1.0f;
      if (progress < peak_progress) {
        return 1.0f - (progress / peak_progress);
      }
      return -(progress - peak_progress) / (1.0f - peak_progress);

    case PhraseContour::Descent:
      return -1.0f;

    case PhraseContour::Ascent:
      return 1.0f;

    case PhraseContour::Wave: {
      // Two sub-arches over the phrase: each half gets its own arch.
      float local = std::fmod(progress * 2.0f, 1.0f);
      return 1.0f - 2.0f * local;
    }

    case PhraseContour::Neutral:
    default:
      return 0.0f;
  }
}

// ---------------------------------------------------------------------------
// Melodic direction choice
// ---------------------------------------------------------------------------

/// @brief Choose melodic direction using persistence model with gravity bias.
/// Gravity encourages descent via chain-length-dependent reversal adjustment.
/// Decays to zero in cadence windows to preserve leading-tone resolution.
/// @param profile Voice-specific profile providing gravity_bias.
inline int chooseMelodicDirection(const MelodicState& state,
                                  const VoiceProfile& profile,
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

  // Gravity bias: chain-length dependent, cadence-decayed.
  float gravity = profile.gravity_bias;
  if (gravity > 0.0f) {
    // Chain-length modulation: weak at run start, full after 3+ notes.
    if (state.direction_run_length <= 1) {
      gravity *= 0.3f;
    } else if (state.direction_run_length == 2) {
      gravity *= 0.7f;
    }
    // Cadence window decay: linear decay from 0.85 to 1.0.
    if (state.phrase_progress > 0.85f) {
      float decay = (state.phrase_progress - 0.85f) / 0.15f;
      gravity *= (1.0f - decay);
    }
    // Apply: descending stays (less reversal), ascending reverses (more reversal).
    if (state.last_direction == -1) {
      base_reversal -= gravity;  // Encourage continued descent.
    } else if (state.last_direction == 1) {
      base_reversal += gravity * 0.7f;  // Encourage reversal from ascent.
    }
  }

  // Noise prevents mechanical periodicity.
  float noise = rng::rollFloat(rng, -0.08f, 0.08f);
  base_reversal = std::clamp(base_reversal + noise, 0.05f, 0.95f);

  return rng::rollProbability(rng, base_reversal) ? -state.last_direction
                                                  : state.last_direction;
}

/// @brief Backward-compatible wrapper: uses kSoprano profile (gravity_bias=0.08).
inline int chooseMelodicDirection(const MelodicState& state,
                                  std::mt19937& rng) {
  return chooseMelodicDirection(state, voice_profiles::kSoprano, rng);
}

// ---------------------------------------------------------------------------
// Melodic interval choice
// ---------------------------------------------------------------------------

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

/// @brief Choose interval with beat-position-dependent probability modulation.
/// Bar: favors 3rd/4th skips (×1.12), reduces steps (×0.94).
/// Offbeat: favors steps (×1.08), reduces skips (×0.95).
inline int chooseMelodicInterval(const MelodicState& state,
                                 std::mt19937& rng,
                                 const VoiceProfile& profile,
                                 Tick tick) {
  VoiceProfile adjusted = profile;
  MetricLevel level = metricLevel(tick);
  if (level == MetricLevel::Bar) {
    adjusted.skip_prob *= 1.12f;
    adjusted.step_prob *= 0.94f;
  } else if (level == MetricLevel::Offbeat) {
    adjusted.step_prob *= 1.08f;
    adjusted.skip_prob *= 0.95f;
  }
  return chooseMelodicInterval(state, rng, adjusted);
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

// ---------------------------------------------------------------------------
// Scoring helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Candidate pitch scoring
// ---------------------------------------------------------------------------

/// @brief Score a candidate pitch for weighted selection using VoiceProfile.
/// Higher score = more desirable. Integrates direction continuation,
/// interval-size preference, harmonic penalty, leap management,
/// beat-position × interval cross terms, and phrase contour.
/// @param profile Voice-specific melodic parameter profile.
inline float scoreCandidatePitch(const MelodicState& state,
                                 uint8_t prev_pitch, uint8_t candidate,
                                 Tick tick, bool is_chord_tone,
                                 const VoiceProfile& profile) {
  int interval = static_cast<int>(candidate) - static_cast<int>(prev_pitch);
  int abs_interval = std::abs(interval);
  int direction = (interval > 0) ? 1 : ((interval < 0) ? -1 : 0);

  float score = 0.0f;
  float tension = goalTensionFactor(state.phrase_progress);
  MetricLevel level = metricLevel(tick);

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
    float penalty = 0.0f;
    switch (level) {
      case MetricLevel::Bar: penalty = -0.40f; break;
      case MetricLevel::Beat: penalty = -0.15f; break;
      default: penalty = -0.05f; break;
    }
    score += penalty * tension;
  }

  // Leap penalty: consecutive leaps.
  float leap_penalty = 0.0f;
  if (abs_interval >= 5 && state.last_large_leap != 0) {
    leap_penalty = -0.50f;
  } else if (abs_interval >= 3 && state.last_skip_size != 0) {
    leap_penalty = -0.15f;
  }
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

  // Beat-position x interval cross terms.
  // Bar: encourage 3rd/4th skips, discourage steps.
  // Offbeat: encourage steps, discourage leaps.
  if (level == MetricLevel::Bar) {
    if (abs_interval >= 3 && abs_interval <= 5) {
      score += 0.12f;  // 3rd/4th skip on bar.
    } else if (abs_interval == 7) {
      score += 0.04f;  // P5 on bar (mild).
    }
    if (abs_interval <= 2 && abs_interval > 0) {
      score -= 0.06f;  // Step on bar: mild discouragement.
    }
  } else if (level == MetricLevel::Offbeat) {
    if (abs_interval >= 1 && abs_interval <= 2) {
      score += 0.08f;  // Step on offbeat.
    } else if (abs_interval >= 3 && abs_interval <= 4) {
      score -= 0.03f;  // 3rd skip on offbeat.
    } else if (abs_interval >= 5) {
      score -= 0.06f;  // 4th+ leap on offbeat.
    }
  }

  // Phrase contour bonus: bias toward the contour's expected direction.
  // Stronger on bar beats, weaker on offbeats.
  if (state.contour.strength > 0.0f && direction != 0) {
    float contour_dir = computeContourDirection(
        state.contour.shape, state.phrase_progress,
        state.contour.peak_progress);
    float metric_mult = 1.0f;
    if (level == MetricLevel::Bar) metric_mult = 1.3f;
    else if (level == MetricLevel::Offbeat) metric_mult = 0.6f;
    float dir_f = (direction > 0) ? 1.0f : -1.0f;
    score += dir_f * contour_dir * state.contour.strength * 0.15f * metric_mult;
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

// ---------------------------------------------------------------------------
// Pitch selection
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Section boundary state management
// ---------------------------------------------------------------------------

/// @brief Boundary type for section transitions.
/// Controls how MelodicState is carried across section boundaries.
enum class BoundaryType : uint8_t {
  Cadence,      ///< After cadence: reset contour, weaken direction inertia.
  Development,  ///< Continuing development: preserve contour and direction.
  Reentry       ///< Subject re-entry: preserve direction, set new arch contour.
};

/// @brief Carry melodic state across a section boundary.
/// Preserves direction inertia (capped) while resetting leap tracking.
/// Contour management depends on boundary type.
inline MelodicState carryMelodicState(const MelodicState& prev,
                                       BoundaryType boundary) {
  MelodicState next;
  next.last_direction = prev.last_direction;
  next.direction_run_length = std::min(prev.direction_run_length, 2);
  next.phrase_progress = 0.0f;  // Always reset for new phrase.

  switch (boundary) {
    case BoundaryType::Cadence:
      next.contour = {PhraseContour::Neutral, 0.4f, 0.15f};
      next.direction_run_length = 0;  // Weaken inertia after cadence.
      break;
    case BoundaryType::Development:
      next.contour = prev.contour;  // Preserve ongoing contour.
      break;
    case BoundaryType::Reentry:
      next.contour = {PhraseContour::Arch, 0.4f, 0.25f};  // Fresh arch.
      break;
  }

  // Leap tracking always resets for new phrase.
  next.last_skip_size = 0;
  next.last_large_leap = 0;
  next.consecutive_leap_count = 0;
  next.prev_was_reversal = false;
  return next;
}

}  // namespace bach

#endif  // BACH_CORE_MELODIC_STATE_H
