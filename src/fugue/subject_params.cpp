/// @file
/// @brief Subject generation parameters and helper functions.
/// Extracted from subject.cpp for testability.

#include "fugue/subject_params.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/archetype_policy.h"
#include "fugue/subject.h"

namespace bach {

CharacterParams getCharacterParams(SubjectCharacter character, std::mt19937& rng_eng) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {rng::rollFloat(rng_eng, 0.10f, 0.20f), 7,
              kSevereDurations, kSevereDurCount};    // octave (BWV 542/547/578)
    case SubjectCharacter::Playful:
      return {rng::rollFloat(rng_eng, 0.35f, 0.50f), 7,
              kPlayfulDurations, kPlayfulDurCount};  // octave (12 semitones)
    case SubjectCharacter::Noble:
      return {rng::rollFloat(rng_eng, 0.20f, 0.30f), 8,
              kNobleDurations, kNobleDurCount};      // 9th (~14 semitones)
    case SubjectCharacter::Restless:
      return {rng::rollFloat(rng_eng, 0.30f, 0.45f), 9,
              kRestlessDurations, kRestlessDurCount}; // 10th (~16 semitones)
  }
  return {0.20f, 7, kSevereDurations, kSevereDurCount};
}

int clampLeap(int pitch, int prev_pitch, SubjectCharacter character,
              Key key, ScaleType scale, int pitch_floor, int pitch_ceil,
              std::mt19937& gen, int* large_leap_count) {
  if (prev_pitch < 0) return pitch;
  int interval = std::abs(pitch - prev_pitch);
  int max_leap = maxLeapForCharacter(character);
  if (interval <= max_leap) return pitch;

  // Playful/Restless: allow 8-9st (6th) but only once per subject.
  if ((character == SubjectCharacter::Playful ||
       character == SubjectCharacter::Restless) &&
      interval <= 9) {
    int current_count = large_leap_count ? *large_leap_count : 0;
    if (current_count < 1 && rng::rollProbability(gen, 0.20f)) {
      if (large_leap_count) ++(*large_leap_count);
      return pitch;
    }
  }

  int direction = (pitch > prev_pitch) ? 1 : -1;
  // Candidate 1: same direction, clamped to max_leap.
  int c1 = prev_pitch + direction * max_leap;
  // Candidate 2: octave correction (+-12).
  int c2 = pitch - 12 * direction;
  // Choose the candidate closest to the original pitch.
  int best = (std::abs(c1 - pitch) <= std::abs(c2 - pitch)) ? c1 : c2;
  best = snapToScale(best, key, scale, pitch_floor, pitch_ceil);

  // Final check: if snapToScale pushed the pitch beyond max_leap,
  // progressively reduce until a scale tone fits within the limit.
  for (int attempt = max_leap; attempt >= 1; --attempt) {
    if (std::abs(best - prev_pitch) <= max_leap) break;
    best = snapToScale(prev_pitch + direction * attempt,
                       key, scale, pitch_floor, pitch_ceil);
  }
  return best;
}

void varyDurationPair(Tick dur_a, Tick dur_b, SubjectCharacter character,
                      std::mt19937& gen, Tick& out_a, Tick& out_b) {
  out_a = dur_a;
  out_b = dur_b;

  // Determine probability and allowed substitutions by character.
  float prob = 0.0f;
  switch (character) {
    case SubjectCharacter::Severe:
      prob = rng::rollFloat(gen, 0.10f, 0.20f);
      break;
    case SubjectCharacter::Playful:
      prob = rng::rollFloat(gen, 0.25f, 0.40f);
      break;
    case SubjectCharacter::Noble:
      prob = rng::rollFloat(gen, 0.10f, 0.20f);
      break;
    case SubjectCharacter::Restless:
      prob = rng::rollFloat(gen, 0.25f, 0.40f);
      break;
  }

  if (!rng::rollProbability(gen, prob)) return;

  Tick sum = dur_a + dur_b;

  switch (character) {
    case SubjectCharacter::Severe:
      // Only Q -> 8+8 division.
      if (dur_a == kQuarterNote && dur_b == kQuarterNote) {
        // Q+Q (960) -> 8+8+8+8 is not pair-preserving.
        // Instead: Q+Q -> Q+8+8 would change note count.
        // Pair substitution: just swap long/short within pair.
        if (rng::rollProbability(gen, 0.5f)) {
          // Keep durations but swap order if they differ.
          out_a = dur_b;
          out_b = dur_a;
        }
      }
      break;

    case SubjectCharacter::Noble:
      // Only H -> DQ+8 allowed. Never subdivide below quarter note.
      if (sum >= kHalfNote + kQuarterNote) {
        // DH+Q or similar: allow DH+Q <-> H+H if sum matches.
        if (sum == kDottedHalf + kQuarterNote) {
          out_a = kHalfNote;
          out_b = kHalfNote;
        } else if (sum == kHalfNote + kHalfNote) {
          out_a = kDottedHalf;
          out_b = kQuarterNote;
        }
      } else if (sum == kHalfNote) {
        // H -> DQ+8 is allowed.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        }
      }
      break;

    case SubjectCharacter::Playful:
    case SubjectCharacter::Restless:
      // All substitutions allowed.
      if (sum == kQuarterNote + kQuarterNote) {
        // Q+Q (960) -> DQ+8 or 8+DQ.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        } else {
          out_a = kEighthNote;
          out_b = kDottedQuarter;
        }
      } else if (sum == kEighthNote + kEighthNote) {
        // 8+8 (480) -> D8+16 or 16+D8.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedEighth;
          out_b = kSixteenthNote;
        } else {
          out_a = kSixteenthNote;
          out_b = kDottedEighth;
        }
      } else if (sum == kHalfNote) {
        // H (960) -> DQ+8 or 8+DQ.
        if (rng::rollProbability(gen, 0.5f)) {
          out_a = kDottedQuarter;
          out_b = kEighthNote;
        } else {
          out_a = kEighthNote;
          out_b = kDottedQuarter;
        }
      }
      break;
  }
}

int avoidUnison(int pitch, int prev_pitch, Key key, ScaleType scale,
                int floor_pitch, int ceil_pitch) {
  if (prev_pitch < 0 || pitch != prev_pitch) return pitch;

  // Try up: nearest scale tone above.
  for (int delta = 1; delta <= 2; ++delta) {
    int candidate = static_cast<int>(scale_util::nearestScaleTone(
        static_cast<uint8_t>(std::max(0, std::min(127, pitch + delta))), key, scale));
    if (candidate != pitch && candidate <= ceil_pitch && candidate >= floor_pitch) {
      return candidate;
    }
  }
  // Try down: nearest scale tone below.
  for (int delta = 1; delta <= 2; ++delta) {
    int candidate = static_cast<int>(scale_util::nearestScaleTone(
        static_cast<uint8_t>(std::max(0, std::min(127, pitch - delta))), key, scale));
    if (candidate != pitch && candidate >= floor_pitch && candidate <= ceil_pitch) {
      return candidate;
    }
  }
  return pitch;  // Unavoidable in extremely narrow range.
}

int snapToScale(int pitch, Key key, ScaleType scale, int floor_pitch,
                int ceil_pitch) {
  pitch = std::max(floor_pitch, std::min(ceil_pitch, pitch));
  int snapped = static_cast<int>(scale_util::nearestScaleTone(
      static_cast<uint8_t>(std::max(0, std::min(127, pitch))), key, scale));
  // Ensure snapped result respects ceiling (nearestScaleTone may snap up).
  if (snapped > ceil_pitch) {
    // Find the scale tone below the ceiling.
    int abs_deg = scale_util::pitchToAbsoluteDegree(
        static_cast<uint8_t>(std::max(0, std::min(127, ceil_pitch))),
        key, scale);
    int candidate = static_cast<int>(
        scale_util::absoluteDegreeToPitch(abs_deg, key, scale));
    if (candidate > ceil_pitch && abs_deg > 0) {
      candidate = static_cast<int>(
          scale_util::absoluteDegreeToPitch(abs_deg - 1, key, scale));
    }
    snapped = std::max(floor_pitch, candidate);
  }
  return snapped;
}

Tick quantizeToStrongBeat(Tick raw_tick, SubjectCharacter character,
                          Tick total_ticks) {
  Tick bar = raw_tick / kTicksPerBar;
  Tick beat1 = bar * kTicksPerBar;
  Tick beat3 = bar * kTicksPerBar + kTicksPerBeat * 2;

  Tick result = beat1;

  if (character == SubjectCharacter::Noble) {
    // Noble: always beat 1 for gravitas.
    result = beat1;
  } else {
    // Snap to nearest strong beat.
    int dist_beat1 = std::abs(static_cast<int>(raw_tick) - static_cast<int>(beat1));
    int dist_beat3 = std::abs(static_cast<int>(raw_tick) - static_cast<int>(beat3));
    result = (dist_beat1 <= dist_beat3) ? beat1 : beat3;
  }

  // Ensure the climax is not at the very beginning -- leave at least 1 bar
  // for Motif A to develop.
  if (result < kTicksPerBar && total_ticks > kTicksPerBar) {
    result = kTicksPerBar;  // Beat 1 of bar 1.
  }

  return result;
}

CadentialFormula getCadentialFormula(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {kSevereCadDegrees, kSevereCadDurations, 3};
    case SubjectCharacter::Playful:
      return {kPlayfulCadDegrees, kPlayfulCadDurations, 5};
    case SubjectCharacter::Noble:
      return {kNobleCadDegrees, kNobleCadDurations, 2};
    case SubjectCharacter::Restless:
      return {kRestlessCadDegrees, kRestlessCadDurations, 4};
  }
  return {kSevereCadDegrees, kSevereCadDurations, 3};
}

void applyArchetypeConstraints(CharacterParams& params,
                                const ArchetypePolicy& policy) {
  // Tighten range: use the intersection of character and archetype ranges.
  if (params.max_range_degrees > policy.max_range_degrees) {
    params.max_range_degrees = policy.max_range_degrees;
  }
  if (params.max_range_degrees < policy.min_range_degrees) {
    params.max_range_degrees = policy.min_range_degrees;
  }
}

}  // namespace bach
