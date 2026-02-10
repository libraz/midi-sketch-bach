/// @file
/// @brief Implementation of IRuleEvaluator utilities - MotionType string conversion.

#include "counterpoint/i_rule_evaluator.h"

namespace bach {

/// @brief Convert a MotionType enum value to its string representation.
/// @param type The motion type to convert.
/// @return A C-string name for the motion type (e.g. "parallel", "contrary").
const char* motionTypeToString(MotionType type) {
  switch (type) {
    case MotionType::Parallel: return "parallel";
    case MotionType::Similar:  return "similar";
    case MotionType::Contrary: return "contrary";
    case MotionType::Oblique:  return "oblique";
  }
  return "unknown";
}

}  // namespace bach
