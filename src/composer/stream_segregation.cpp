#include "composer/stream_segregation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bach::composer::stream_segregation {
namespace {

int sign(int value) {
  if (value > 0)
    return 1;
  if (value < 0)
    return -1;
  return 0;
}

int medianAbsInterval(const std::vector<int>& intervals, std::size_t current) {
  std::vector<int> window;
  const std::size_t begin =
      current > kNeighborhoodRadius ? current - kNeighborhoodRadius : std::size_t{0};
  const std::size_t end = std::min(intervals.size(), current + kNeighborhoodRadius + 1);
  for (std::size_t i = begin; i < end; ++i) {
    if (i == current)
      continue;
    window.push_back(std::abs(intervals[i]));
  }
  if (window.empty())
    return std::abs(intervals[current]);
  std::sort(window.begin(), window.end());
  return window[window.size() / 2];
}

int streamThresholdForIoi(Tick ioi) {
  if (ioi <= kSixteenthIoiTicks)
    return kSixteenthStreamThresholdSemitones;
  if (ioi <= kEighthIoiTicks)
    return kEighthStreamThresholdSemitones;
  return 128;
}

}  // namespace

StreamSegregationSpan analyzeSpan(const std::vector<MaterialNote>& input, SpanId span_id) {
  StreamSegregationSpan result;
  result.span_id = span_id;
  result.detected_stream_count = 1;

  if (input.size() < 3)
    return result;

  std::vector<MaterialNote> notes = input;
  std::sort(notes.begin(), notes.end(), [](const MaterialNote& a, const MaterialNote& b) {
    return a.start_tick < b.start_tick;
  });

  std::vector<int> intervals;
  intervals.reserve(notes.size() - 1);
  for (std::size_t i = 1; i < notes.size(); ++i) {
    intervals.push_back(static_cast<int>(notes[i].pitch) - static_cast<int>(notes[i - 1].pitch));
  }

  int low_max = -1;
  int high_min = 128;
  for (std::size_t i = 1; i < intervals.size(); ++i) {
    const int current = intervals[i];
    const int abs_current = std::abs(current);
    const bool large_interval = abs_current >= kMinTransitionIntervalSemitones;
    const bool contour_reversal = sign(intervals[i - 1]) != 0 && sign(current) != 0 &&
                                  sign(intervals[i - 1]) != sign(current);
    const int neighborhood_median = medianAbsInterval(intervals, i);
    const bool local_contrast = abs_current >= 2 * std::max(1, neighborhood_median);
    const Tick ioi = notes[i + 1].start_tick - notes[i].start_tick;
    const bool ioi_strength = abs_current >= streamThresholdForIoi(ioi);
    if (!(large_interval && contour_reversal && local_contrast && ioi_strength))
      continue;

    result.transition_note_indices.push_back(static_cast<int>(i + 1));
    const int a = static_cast<int>(notes[i].pitch);
    const int b = static_cast<int>(notes[i + 1].pitch);
    low_max = std::max(low_max, std::min(a, b));
    high_min = std::min(high_min, std::max(a, b));
  }

  if (!result.transition_note_indices.empty()) {
    result.detected_stream_count = 2;
    result.stream_separation_semitones = std::max(0, high_min - low_max);
  }
  return result;
}

}  // namespace bach::composer::stream_segregation
