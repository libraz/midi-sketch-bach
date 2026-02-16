// Shared voice range constants and register-fitting logic for fugue voices.

#include "fugue/voice_registers.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <vector>

#include "core/interval.h"

namespace bach {

std::pair<uint8_t, uint8_t> getFugueVoiceRange(VoiceId voice_id, uint8_t num_voices) {
  if (num_voices == 2) {
    constexpr uint8_t kRanges2[][2] = {
        {55, 84},  // Voice 0 (upper): G3-C6
        {36, 67},  // Voice 1 (lower): C2-G4
    };
    auto idx = voice_id < 2 ? voice_id : 1;
    return {kRanges2[idx][0], kRanges2[idx][1]};
  }
  if (num_voices == 3) {
    constexpr uint8_t kRanges3[][2] = {
        {60, 96},  // Voice 0 (soprano): C4-C6
        {55, 79},  // Voice 1 (alto): G3-G5
        {48, 72},  // Voice 2 (tenor): C3-C5
    };
    auto idx = voice_id < 3 ? voice_id : 2;
    return {kRanges3[idx][0], kRanges3[idx][1]};
  }
  // 4+ voices
  constexpr uint8_t kRanges4[][2] = {
      {60, 96},  // Voice 0 (soprano): C4-C6
      {55, 79},  // Voice 1 (alto): G3-G5
      {48, 72},  // Voice 2 (tenor): C3-C5
      {24, 50},  // Voice 3 (bass/pedal): C1-D3
  };
  auto idx = voice_id < 4 ? voice_id : 3;
  return {kRanges4[idx][0], kRanges4[idx][1]};
}

namespace {

// Characteristic ranges -- the "audible identity" band, narrower than full range.
struct CharRange {
  uint8_t low;
  uint8_t high;
};

CharRange getCharacteristicRange(uint8_t range_lo, uint8_t /* range_hi */) {
  // Map voice range to characteristic sub-band by voice type.
  //   Soprano: [60,84], Alto: [55,74], Tenor: [48,67], Bass/Pedal: [36,60]
  if (range_lo >= 60) return {60, 84};   // Soprano
  if (range_lo >= 55) return {55, 74};   // Alto
  if (range_lo >= 48) return {48, 67};   // Tenor
  return {36, 60};                       // Bass / Pedal
}

int signOf(int val) {
  if (val > 0) return 1;
  if (val < 0) return -1;
  return 0;
}

}  // namespace

int fitToRegister(const uint8_t* pitches, size_t num_pitches,
                  uint8_t range_lo, uint8_t range_hi,
                  uint8_t reference_pitch,
                  uint8_t prev_reference_pitch,
                  uint8_t adjacent_last_pitch,
                  uint8_t adjacent_prev_pitch,
                  uint8_t adjacent_lo, uint8_t adjacent_hi,
                  bool is_subject_voice,
                  uint8_t last_subject_pitch,
                  bool is_exposition) {
  // Reserved for future melodic contour analysis between prev_reference and reference.
  (void)prev_reference_pitch;

  if (num_pitches == 0) return 0;

  // Find min and max pitch in the input.
  uint8_t min_pitch = pitches[0];
  uint8_t max_pitch = pitches[0];
  for (size_t idx = 1; idx < num_pitches; ++idx) {
    if (pitches[idx] < min_pitch) min_pitch = pitches[idx];
    if (pitches[idx] > max_pitch) max_pitch = pitches[idx];
  }

  uint8_t first_pitch = pitches[0];
  uint8_t second_pitch = (num_pitches >= 2) ? pitches[1] : first_pitch;

  CharRange char_range = getCharacteristicRange(range_lo, range_hi);

  int best_shift = 0;
  int best_score = INT32_MAX;

  static constexpr int kShifts[] = {-48, -36, -24, -12, 0, 12, 24, 36, 48};
  for (int shift : kShifts) {
    int shifted_first = static_cast<int>(first_pitch) + shift;
    int shifted_second = static_cast<int>(second_pitch) + shift;
    int shifted_min = static_cast<int>(min_pitch) + shift;
    int shifted_max = static_cast<int>(max_pitch) + shift;

    // (a) overflow_penalty: out-of-range overflow.
    int overflow = 0;
    if (shifted_min < static_cast<int>(range_lo)) {
      overflow += static_cast<int>(range_lo) - shifted_min;
    }
    if (shifted_max > static_cast<int>(range_hi)) {
      overflow += shifted_max - static_cast<int>(range_hi);
    }

    // (b) instant_cross: first pitch crosses adjacent voice's last pitch.
    int instant_cross = 0;
    if (adjacent_last_pitch > 0) {
      if (adjacent_lo > range_lo) {
        // Adjacent is upper voice -- shifted_first should be below adj_last.
        if (shifted_first > static_cast<int>(adjacent_last_pitch)) {
          instant_cross = shifted_first - static_cast<int>(adjacent_last_pitch);
        }
      } else {
        // Adjacent is lower voice -- shifted_first should be above adj_last.
        if (static_cast<int>(adjacent_last_pitch) > shifted_first) {
          instant_cross = static_cast<int>(adjacent_last_pitch) - shifted_first;
        }
      }
    }
    int cross_weight = is_subject_voice ? 15 : 50;

    // (c) parallel_risk: both voices move in same direction to perfect consonance.
    int parallel_risk = 0;
    if (adjacent_last_pitch > 0 && adjacent_prev_pitch > 0 && num_pitches >= 2) {
      int adj_motion = static_cast<int>(adjacent_last_pitch) -
                       static_cast<int>(adjacent_prev_pitch);
      int entry_motion = shifted_second - shifted_first;
      if (signOf(adj_motion) == signOf(entry_motion) && signOf(adj_motion) != 0) {
        int simple = interval_util::compoundToSimple(
            std::abs(shifted_second - static_cast<int>(adjacent_last_pitch)));
        if (simple == 0 || simple == 7) {
          parallel_risk = 1;
        }
      }
    }

    // (d) melodic_distance: distance from reference pitch.
    int melodic_dist = 0;
    if (reference_pitch > 0) {
      melodic_dist = std::abs(shifted_first - static_cast<int>(reference_pitch));
    }

    // (e) order_violation: sustained voice order inversion.
    int order_violation = 0;
    if (adjacent_last_pitch > 0 && num_pitches >= 2) {
      bool adj_is_lower = (adjacent_lo < range_lo) ||
                          (adjacent_lo == range_lo && adjacent_hi < range_hi);
      if (adj_is_lower) {
        // Adjacent is lower voice; this voice should be higher.
        if (shifted_first < static_cast<int>(adjacent_last_pitch) &&
            shifted_second < static_cast<int>(adjacent_last_pitch)) {
          order_violation = 1;
        }
      } else {
        // Adjacent is upper voice; this voice should be lower.
        if (shifted_first > static_cast<int>(adjacent_last_pitch) &&
            shifted_second > static_cast<int>(adjacent_last_pitch)) {
          order_violation = 1;
        }
      }
    }

    // (f) register_drift: exposition-only drift from last subject entry.
    int register_drift = 0;
    if (is_exposition && last_subject_pitch > 0) {
      int drift = std::abs(shifted_first - static_cast<int>(last_subject_pitch));
      if (drift > 12) {
        register_drift = drift - 12;
      }
    }

    // (g) clarity_penalty: distance from characteristic range.
    int clarity = 0;
    if (shifted_first < static_cast<int>(char_range.low)) {
      clarity = static_cast<int>(char_range.low) - shifted_first;
    }
    if (shifted_first > static_cast<int>(char_range.high)) {
      clarity = shifted_first - static_cast<int>(char_range.high);
    }

    // (h) center_distance: distance from voice center.
    int center = (static_cast<int>(range_lo) + static_cast<int>(range_hi)) / 2;
    int shifted_center = (shifted_min + shifted_max) / 2;
    int center_dist = std::abs(center - shifted_center);

    // (i) entry_leap_penalty: penalize >octave leap from reference_pitch.
    int entry_leap_penalty = 0;
    if (reference_pitch > 0) {
      int ref_dist = std::abs(shifted_first - static_cast<int>(reference_pitch));
      if (ref_dist > 12) entry_leap_penalty = ref_dist - 12;
    }

    // (j) max_internal_leap: penalize >octave leaps between adjacent notes.
    // Shift is uniform so internal intervals are shift-invariant.
    int max_internal_leap = 0;
    for (size_t i = 1; i < num_pitches; ++i) {
      int d = std::abs(static_cast<int>(pitches[i]) - static_cast<int>(pitches[i - 1]));
      if (d > 12) max_internal_leap = std::max(max_internal_leap, d - 12);
    }

    int score = 100 * overflow
              + cross_weight * instant_cross
              + 20 * parallel_risk
              + 10 * melodic_dist
              + 40 * entry_leap_penalty
              + 15 * max_internal_leap
              +  5 * order_violation
              +  3 * register_drift
              +  2 * clarity
              +  1 * center_dist;

    // Prefer smaller absolute shift on tie.
    if (score < best_score ||
        (score == best_score && std::abs(shift) < std::abs(best_shift))) {
      best_score = score;
      best_shift = shift;
    }
  }

  return best_shift;
}

int fitToRegister(const std::vector<NoteEvent>& notes,
                  uint8_t range_lo, uint8_t range_hi,
                  uint8_t reference_pitch,
                  uint8_t prev_reference_pitch,
                  uint8_t adjacent_last_pitch,
                  uint8_t adjacent_prev_pitch,
                  uint8_t adjacent_lo, uint8_t adjacent_hi,
                  bool is_subject_voice,
                  uint8_t last_subject_pitch,
                  bool is_exposition) {
  if (notes.empty()) return 0;
  std::vector<uint8_t> pitch_buf;
  pitch_buf.reserve(notes.size());
  for (const auto& note : notes) {
    pitch_buf.push_back(note.pitch);
  }
  return fitToRegister(pitch_buf.data(), pitch_buf.size(),
                       range_lo, range_hi,
                       reference_pitch, prev_reference_pitch,
                       adjacent_last_pitch, adjacent_prev_pitch,
                       adjacent_lo, adjacent_hi,
                       is_subject_voice, last_subject_pitch,
                       is_exposition);
}

namespace {

/// @brief Interpolate the RegisterEnvelope range ratio at a given phase position.
///
/// Uses piecewise linear interpolation between four structural phases:
///   [0.00, 0.25) opening  (exposition)
///   [0.25, 0.60) middle   (development)
///   [0.60, 0.85) climax   (stretto)
///   [0.85, 1.00] closing  (coda)
float interpolateEnvelopeRatio(float phase_pos, const RegisterEnvelope& envelope) {
  // Clamp to [0, 1].
  if (phase_pos <= 0.0f) return envelope.opening_range_ratio;
  if (phase_pos >= 1.0f) return envelope.closing_range_ratio;

  // Phase boundaries.
  constexpr float kOpenEnd = 0.25f;
  constexpr float kMidEnd = 0.60f;
  constexpr float kClimaxEnd = 0.85f;

  if (phase_pos < kOpenEnd) {
    // Within opening phase: interpolate from opening to middle.
    float frac = phase_pos / kOpenEnd;
    return envelope.opening_range_ratio +
           frac * (envelope.middle_range_ratio - envelope.opening_range_ratio);
  }
  if (phase_pos < kMidEnd) {
    // Within middle phase: interpolate from middle to climax.
    float frac = (phase_pos - kOpenEnd) / (kMidEnd - kOpenEnd);
    return envelope.middle_range_ratio +
           frac * (envelope.climax_range_ratio - envelope.middle_range_ratio);
  }
  if (phase_pos < kClimaxEnd) {
    // Within climax phase: interpolate from climax to closing.
    float frac = (phase_pos - kMidEnd) / (kClimaxEnd - kMidEnd);
    return envelope.climax_range_ratio +
           frac * (envelope.closing_range_ratio - envelope.climax_range_ratio);
  }
  // Within closing phase: hold at closing ratio.
  return envelope.closing_range_ratio;
}

}  // namespace

int fitToRegisterWithEnvelope(
    const std::vector<NoteEvent>& notes,
    uint8_t voice_id, uint8_t num_voices,
    float phase_pos,
    const RegisterEnvelope& envelope,
    uint8_t reference_pitch,
    uint8_t adjacent_last_pitch,
    int* envelope_overflow_count) {
  if (notes.empty()) return 0;

  // Get the full voice range.
  auto [range_lo, range_hi] = getFugueVoiceRange(voice_id, num_voices);

  // Interpolate the envelope ratio at this phase position.
  float ratio = interpolateEnvelopeRatio(phase_pos, envelope);

  // Calculate effective narrowed range.
  int center = (static_cast<int>(range_lo) + static_cast<int>(range_hi)) / 2;
  int full_span = static_cast<int>(range_hi) - static_cast<int>(range_lo);
  int eff_span = static_cast<int>(static_cast<float>(full_span) * ratio);
  uint8_t eff_lo = static_cast<uint8_t>(std::max(0, center - eff_span / 2));
  uint8_t eff_hi = static_cast<uint8_t>(std::min(127, center + eff_span / 2));

  // Phase A observation: count notes outside the envelope-narrowed range.
  // No penalty is applied to the fitToRegister call.
  if (envelope_overflow_count != nullptr) {
    for (const auto& note : notes) {
      if (note.pitch < eff_lo || note.pitch > eff_hi) {
        ++(*envelope_overflow_count);
      }
    }
  }

  // Apply minimum span guard: 14 semitones (short 10th) prevents melodic breakdown.
  uint8_t safe_lo = eff_lo;
  uint8_t safe_hi = eff_hi;
  if (static_cast<int>(safe_hi) - static_cast<int>(safe_lo) < 14) {
    safe_hi = static_cast<uint8_t>(
        std::min(127, static_cast<int>(safe_lo) + 14));
    if (static_cast<int>(safe_hi) - static_cast<int>(safe_lo) < 14) {
      safe_lo = static_cast<uint8_t>(
          std::max(0, static_cast<int>(safe_hi) - 14));
    }
  }

  // Delegate to fitToRegister with envelope-narrowed range.
  return fitToRegister(notes, safe_lo, safe_hi,
                       reference_pitch, /*prev_reference_pitch=*/0,
                       adjacent_last_pitch);
}

}  // namespace bach
