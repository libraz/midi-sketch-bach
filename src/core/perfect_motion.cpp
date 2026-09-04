#include <cstdlib>

#include "core/pitch_utils.h"

namespace bach {

PerfectMotionKind classifyPerfectMotion(int upper_prev, int upper_curr, int lower_prev,
                                        int lower_curr) {
  const int upper_motion = upper_curr - upper_prev;
  const int lower_motion = lower_curr - lower_prev;
  if (upper_motion == 0 || lower_motion == 0 || (upper_motion > 0) != (lower_motion > 0))
    return PerfectMotionKind::None;

  const int current_class = std::abs(upper_curr - lower_curr) % 12;
  if (current_class != interval::kUnison && current_class != interval::kPerfect5th)
    return PerfectMotionKind::None;

  const int previous_class = std::abs(upper_prev - lower_prev) % 12;
  if (previous_class == current_class) {
    return current_class == interval::kPerfect5th ? PerfectMotionKind::ParallelFifth
                                                  : PerfectMotionKind::ParallelOctave;
  }
  if (std::abs(upper_motion) <= interval::kMajor2nd)
    return PerfectMotionKind::None;
  return current_class == interval::kPerfect5th ? PerfectMotionKind::HiddenFifth
                                                : PerfectMotionKind::HiddenOctave;
}

bool isParallelPerfectMotion(int upper_prev, int upper_curr, int lower_prev, int lower_curr) {
  const PerfectMotionKind kind =
      classifyPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr);
  return kind == PerfectMotionKind::ParallelFifth || kind == PerfectMotionKind::ParallelOctave;
}

bool isForbiddenPerfectMotion(int upper_prev, int upper_curr, int lower_prev, int lower_curr) {
  return classifyPerfectMotion(upper_prev, upper_curr, lower_prev, lower_curr) !=
         PerfectMotionKind::None;
}

bool isBattutaMotion(int upper_prev, int upper_curr, int lower_prev, int lower_curr) {
  const int upper_motion = upper_curr - upper_prev;
  const int lower_motion = lower_curr - lower_prev;
  // Oblique motion has no second line to converge with, and similar motion into
  // an octave is the hidden-perfect case rather than this one.
  if (upper_motion == 0 || lower_motion == 0 || (upper_motion > 0) == (lower_motion > 0))
    return false;

  if (std::abs(upper_curr - lower_curr) % 12 != interval::kUnison)
    return false;
  // Octave to octave by contrary motion is an anti-parallel, already classified
  // elsewhere; the fault here is arriving at a class that was not there before.
  if (std::abs(upper_prev - lower_prev) % 12 == interval::kUnison)
    return false;

  const int arriving_upper_motion = upper_curr >= lower_curr ? upper_motion : lower_motion;
  return arriving_upper_motion < -interval::kMajor2nd;
}

}  // namespace bach
