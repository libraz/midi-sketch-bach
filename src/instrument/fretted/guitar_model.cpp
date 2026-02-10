// Classical guitar instrument model implementation.

#include "instrument/fretted/guitar_model.h"

#include <cstdint>
#include <limits>

namespace bach {

GuitarModel::GuitarModel() = default;

bool GuitarModel::isPitchPlayable(uint8_t pitch) const {
  if (pitch < kLowestPitch || pitch > kHighestPitch) return false;

  // Check if at least one string can produce this pitch within fret range.
  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch >= kOpenStrings[idx] &&
        pitch <= kOpenStrings[idx] + kMaxFret) {
      return true;
    }
  }
  return false;
}

PlayabilityCost GuitarModel::calculateCost(uint8_t pitch) const {
  PlayabilityCost result;

  uint8_t string_idx = 0;
  uint8_t fret = 0;
  if (!findBestString(pitch, string_idx, fret)) {
    result.is_playable = false;
    result.total = 1e6f;
    return result;
  }

  result.is_playable = true;

  // Open string: zero cost.
  if (fret == 0) {
    result.total = 0.0f;
    return result;
  }

  // Stretch cost: higher frets require more effort and precision.
  // Low frets (1-5) are comfortable, middle (6-12) moderate, high (13+) hard.
  if (fret <= 5) {
    result.stretch = static_cast<float>(fret) * 0.02f;
  } else if (fret <= 12) {
    result.stretch = 0.1f + static_cast<float>(fret - 5) * 0.04f;
  } else {
    result.stretch = 0.38f + static_cast<float>(fret - 12) * 0.08f;
  }

  // Position shift cost: distance from a neutral "open position" (frets 1-4).
  // Higher positions require more left-hand shifting.
  if (fret > 4) {
    result.position_shift = static_cast<float>(fret - 4) * 0.03f;
  }

  result.total = result.stretch + result.position_shift;
  return result;
}

bool GuitarModel::findBestString(uint8_t pitch, uint8_t& out_string_idx,
                                 uint8_t& out_fret) const {
  uint8_t best_fret = std::numeric_limits<uint8_t>::max();
  bool found = false;

  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch < kOpenStrings[idx]) continue;

    uint8_t fret_on_string = pitch - kOpenStrings[idx];
    if (fret_on_string > kMaxFret) continue;

    // Prefer the string that gives the lowest fret number (most comfortable).
    if (fret_on_string < best_fret) {
      best_fret = fret_on_string;
      out_string_idx = idx;
      out_fret = fret_on_string;
      found = true;
    }
  }

  return found;
}

}  // namespace bach
