/// @file
/// @brief Scale construction, degree lookup, and nearest-tone utilities.

#include "core/scale.h"

#include <cstdlib>

namespace bach {
namespace scale_util {

/// @brief Build a 12-element boolean lookup table indicating which pitch classes belong to a scale.
/// @param key The root key of the scale.
/// @param scale The scale type (major, minor, etc.).
/// @param pitch_in_scale Output array where pitch_in_scale[pc] is true if pc belongs to the scale.
static void buildScaleLookup(Key key, ScaleType scale, bool pitch_in_scale[12]) {
  for (int idx = 0; idx < 12; ++idx) {
    pitch_in_scale[idx] = false;
  }

  int key_offset = static_cast<int>(key);
  const int* intervals = getScaleIntervals(scale);

  for (int deg = 0; deg < kScaleDegreeCount; ++deg) {
    int pitch_class = (key_offset + intervals[deg]) % 12;
    pitch_in_scale[pitch_class] = true;
  }
}

bool isScaleTone(uint8_t pitch, Key key, ScaleType scale) {
  bool pitch_in_scale[12];
  buildScaleLookup(key, scale, pitch_in_scale);

  int pitch_class = getPitchClass(pitch);
  return pitch_in_scale[pitch_class];
}

uint8_t nearestScaleTone(uint8_t pitch, Key key, ScaleType scale) {
  // If already a scale tone, return as-is.
  if (isScaleTone(pitch, key, scale)) {
    return pitch;
  }

  // Search outward from pitch, preferring lower on tie.
  int int_pitch = static_cast<int>(pitch);
  for (int offset = 1; offset <= 6; ++offset) {
    // Check lower first (prefer lower on tie).
    int lower = int_pitch - offset;
    if (lower >= 0) {
      auto lower_u8 = static_cast<uint8_t>(lower);
      if (isScaleTone(lower_u8, key, scale)) {
        return lower_u8;
      }
    }

    int upper = int_pitch + offset;
    if (upper <= 127) {
      auto upper_u8 = static_cast<uint8_t>(upper);
      if (isScaleTone(upper_u8, key, scale)) {
        return upper_u8;
      }
    }
  }

  // Fallback: should never reach here for diatonic scales (max gap = 3 semitones)
  // but return input clamped as safety.
  return pitch;
}

bool pitchToScaleDegree(uint8_t pitch, Key key, ScaleType scale,
                        int& out_degree) {
  int key_offset = static_cast<int>(key);
  const int* intervals = getScaleIntervals(scale);
  int pitch_class = getPitchClass(pitch);

  for (int deg = 0; deg < kScaleDegreeCount; ++deg) {
    int scale_pc = (key_offset + intervals[deg]) % 12;
    if (pitch_class == scale_pc) {
      out_degree = deg;
      return true;
    }
  }

  return false;
}

}  // namespace scale_util
}  // namespace bach
