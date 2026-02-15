/// @file
/// @brief Figure matching: score how well a pitch sequence matches a MelodicFigure.

#include "core/figure_match.h"

#include <algorithm>
#include <cmath>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/scale.h"

namespace bach {
namespace figure_match {

/// @brief Score a single interval in semitone mode.
/// @param actual Actual directed semitone interval.
/// @param expected Expected directed semitone interval.
/// @return Score for this interval: 1.0 exact, 0.3 off-by-one, 0.0 otherwise.
static float scoreSemitoneInterval(int actual, int expected) {
  int diff = std::abs(actual - expected);
  if (diff == 0) return 1.0f;
  if (diff == 1) return 0.3f;
  return 0.0f;
}

/// @brief Compute directed scale degree difference between two pitches.
///
/// Unlike pitchToAbsoluteDegree (which is anchored to MIDI octaves and can
/// produce incorrect diffs for keys other than C), this function computes
/// the degree diff by counting scale steps in the direction indicated by
/// the semitone interval.
///
/// @param pitch_a Starting MIDI pitch.
/// @param pitch_b Ending MIDI pitch.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @return Signed degree difference (positive = ascending, negative = descending).
static int computeDegreeDiff(uint8_t pitch_a, uint8_t pitch_b,
                             Key key, ScaleType scale) {
  // Snap both pitches to scale tones for degree counting.
  uint8_t snapped_a = scale_util::nearestScaleTone(pitch_a, key, scale);
  uint8_t snapped_b = scale_util::nearestScaleTone(pitch_b, key, scale);

  int abs_deg_a = scale_util::pitchToAbsoluteDegree(snapped_a, key, scale);
  int abs_deg_b = scale_util::pitchToAbsoluteDegree(snapped_b, key, scale);
  int raw_diff = abs_deg_b - abs_deg_a;

  // Determine the intended direction from the actual pitch interval.
  int semitone_dir = static_cast<int>(pitch_b) - static_cast<int>(pitch_a);

  // If the degree diff direction doesn't match the semitone direction,
  // correct by adding/subtracting 7 (one key-relative octave).
  if (semitone_dir > 0 && raw_diff <= 0) {
    raw_diff += scale_util::kScaleDegreeCount;
  } else if (semitone_dir < 0 && raw_diff >= 0) {
    raw_diff -= scale_util::kScaleDegreeCount;
  }
  // If semitone_dir == 0 and raw_diff != 0, the pitches are enharmonically
  // the same after snapping; keep raw_diff as-is (should be 0).

  return raw_diff;
}

/// @brief Score a single interval in degree mode.
/// @param pitches Array of MIDI pitch values.
/// @param idx Index of the first note of the interval pair.
/// @param figure The figure being matched.
/// @param key Current musical key.
/// @param scale Current scale type.
/// @return Score for this interval in [0.0, 1.1].
static float scoreDegreeInterval(const uint8_t* pitches, int idx,
                                 const MelodicFigure& figure,
                                 Key key, ScaleType scale) {
  int actual_deg_diff = computeDegreeDiff(pitches[idx], pitches[idx + 1], key, scale);
  int expected_deg_diff = figure.degree_intervals[idx].degree_diff;

  // Direction check.
  bool same_dir = (actual_deg_diff > 0 && expected_deg_diff > 0) ||
                  (actual_deg_diff < 0 && expected_deg_diff < 0) ||
                  (actual_deg_diff == 0 && expected_deg_diff == 0);

  float interval_score = 0.0f;

  if (actual_deg_diff == expected_deg_diff) {
    interval_score = 1.0f;
  } else if (same_dir && std::abs(actual_deg_diff - expected_deg_diff) == 1) {
    interval_score = 0.3f;
  } else if (same_dir) {
    interval_score = 0.1f;
  }
  // else: wrong direction, score stays 0.0.

  // Chroma offset bonus: only when degree diff matches exactly.
  if (actual_deg_diff == expected_deg_diff) {
    // Compute what the "natural" (diatonic) semitone interval would be
    // for this degree diff, then compare actual vs natural to get chroma.
    uint8_t snapped_p0 = scale_util::nearestScaleTone(pitches[idx], key, scale);
    int abs_deg_0 = scale_util::pitchToAbsoluteDegree(snapped_p0, key, scale);
    uint8_t natural_p1 =
        scale_util::absoluteDegreeToPitch(abs_deg_0 + actual_deg_diff, key, scale);
    int actual_semitones = static_cast<int>(pitches[idx + 1]) - static_cast<int>(pitches[idx]);
    int natural_semitones = static_cast<int>(natural_p1) - static_cast<int>(snapped_p0);
    int actual_chroma = actual_semitones - natural_semitones;
    if (actual_chroma == figure.degree_intervals[idx].chroma_offset) {
      interval_score += 0.1f;
    }
  }

  return interval_score;
}

float matchFigure(const uint8_t* pitches, int count,
                  const MelodicFigure& figure,
                  Key key, ScaleType scale) {
  if (count != figure.note_count) return 0.0f;
  if (count < 2) return 0.0f;

  int interval_count = count - 1;
  float total_score = 0.0f;

  // Choose scoring mode based on primary_mode.
  bool use_semitone =
      (figure.primary_mode == IntervalMode::Semitone) || (!figure.allow_transposition);

  if (use_semitone && figure.semitone_intervals != nullptr) {
    // Semitone mode scoring.
    for (int idx = 0; idx < interval_count; ++idx) {
      int actual = static_cast<int>(pitches[idx + 1]) - static_cast<int>(pitches[idx]);
      int expected = figure.semitone_intervals[idx];
      total_score += scoreSemitoneInterval(actual, expected);
    }
  } else if (figure.degree_intervals != nullptr) {
    // Degree mode scoring.
    for (int idx = 0; idx < interval_count; ++idx) {
      total_score += scoreDegreeInterval(pitches, idx, figure, key, scale);
    }
  } else {
    // No interval data available.
    return 0.0f;
  }

  return total_score / static_cast<float>(interval_count);
}

int findBestFigure(const uint8_t* pitches, int count,
                   const MelodicFigure* const* table, int table_size,
                   Key key, ScaleType scale,
                   float threshold) {
  float best_score = 0.0f;
  int best_index = -1;

  for (int idx = 0; idx < table_size; ++idx) {
    if (table[idx]->note_count != count) continue;

    float score = matchFigure(pitches, count, *table[idx], key, scale);
    if (score > best_score) {
      best_score = score;
      best_index = idx;
    }
  }

  if (best_score >= threshold) return best_index;
  return -1;
}

}  // namespace figure_match
}  // namespace bach
