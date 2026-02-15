// Implementation of toccata-fugue Kerngestalt affinity scorer.

#include "fugue/toccata_affinity.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace bach {

namespace {

/// Score a single interval pair: direction match + magnitude proximity.
float scoreIntervalPair(int subject_ivl, int toccata_ivl) {
  // Direction match (weight 0.7).
  bool same_sign = (subject_ivl > 0 && toccata_ivl > 0) ||
                   (subject_ivl < 0 && toccata_ivl < 0) ||
                   (subject_ivl == 0 && toccata_ivl == 0);
  float dir_score = same_sign ? 1.0f : 0.0f;

  // Magnitude proximity (weight 0.3).
  int mag_diff = std::abs(std::abs(subject_ivl) - std::abs(toccata_ivl));
  float mag_score = 0.0f;
  if (mag_diff == 0) mag_score = 1.0f;
  else if (mag_diff == 1) mag_score = 0.5f;
  // else 0.0

  return dir_score * 0.7f + mag_score * 0.3f;
}

/// Score a subsequence of subject against a subsequence of toccata variant.
float scoreSubsequence(const std::vector<int>& subject, size_t s_start,
                       const std::vector<int>& variant, size_t v_start,
                       size_t len) {
  float total = 0.0f;
  for (size_t i = 0; i < len; ++i) {
    total += scoreIntervalPair(subject[s_start + i], variant[v_start + i]);
  }
  return total / static_cast<float>(len);
}

/// Build inverted form of intervals (negate all).
std::vector<int> invertIntervals(const std::vector<int>& intervals) {
  std::vector<int> result;
  result.reserve(intervals.size());
  for (int ivl : intervals) {
    result.push_back(-ivl);
  }
  return result;
}

}  // namespace

float computeToccataAffinity(const std::vector<int>& subject_intervals,
                             const std::vector<int>& toccata_intervals) {
  if (subject_intervals.size() < 3 || toccata_intervals.size() < 3) {
    return 0.0f;
  }

  // Limit subject to head (first 8 intervals).
  size_t subject_len = std::min(subject_intervals.size(), static_cast<size_t>(8));

  // Build variants: original + inverted.
  std::vector<std::vector<int>> variants;
  variants.push_back(toccata_intervals);
  variants.push_back(invertIntervals(toccata_intervals));

  float max_score = 0.0f;

  // Try all subsequence lengths 3..6.
  for (size_t sub_len = 3; sub_len <= 6; ++sub_len) {
    if (sub_len > subject_len) break;

    // Slide across subject head.
    for (size_t s_start = 0; s_start + sub_len <= subject_len; ++s_start) {
      // Slide across each variant.
      for (const auto& variant : variants) {
        for (size_t v_start = 0; v_start + sub_len <= variant.size(); ++v_start) {
          float score = scoreSubsequence(subject_intervals, s_start,
                                         variant, v_start, sub_len);
          if (score > max_score) max_score = score;
        }
      }
    }
  }

  return max_score;
}

}  // namespace bach
