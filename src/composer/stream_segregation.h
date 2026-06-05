#ifndef BACH_COMPOSER_STREAM_SEGREGATION_H
#define BACH_COMPOSER_STREAM_SEGREGATION_H

#include <vector>

#include "composer/material.h"
#include "composer/validation.h"

namespace bach::composer::stream_segregation {

// Davis-style transition cues. Fixed info-level thresholds that drive the
// stream-count analysis (no search; values are tuned once and held constant).
constexpr int kMinTransitionIntervalSemitones = 6;  // interval must be > P4.
constexpr int kNeighborhoodRadius = 4;
constexpr int kSixteenthIoiTicks = kTicksPerBeat / 4;
constexpr int kEighthIoiTicks = kTicksPerBeat / 2;
constexpr int kSixteenthStreamThresholdSemitones = 5;
constexpr int kEighthStreamThresholdSemitones = 7;

StreamSegregationSpan analyzeSpan(const std::vector<MaterialNote>& notes, SpanId span_id);

}  // namespace bach::composer::stream_segregation

#endif  // BACH_COMPOSER_STREAM_SEGREGATION_H
