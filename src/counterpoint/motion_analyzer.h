// Voice-pair motion analysis -- classifies successive intervals and
// computes motion statistics.

#ifndef BACH_COUNTERPOINT_MOTION_ANALYZER_H
#define BACH_COUNTERPOINT_MOTION_ANALYZER_H

#include "core/basic_types.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {

class CounterpointState;

/// @brief Analyzes motion patterns between two counterpoint voices.
///
/// Uses an IRuleEvaluator to classify each successive interval pair, then
/// tallies the results into a MotionStats summary.  A higher contrary
/// motion ratio generally indicates better counterpoint writing.
class MotionAnalyzer {
 public:
  /// @brief Construct an analyzer using the given rule evaluator.
  /// @param rules Reference to the rule evaluator for motion classification.
  explicit MotionAnalyzer(const IRuleEvaluator& rules);

  /// @brief Classify motion between two successive pitch pairs.
  /// Delegates directly to the rule evaluator.
  MotionType classifyMotion(uint8_t prev1, uint8_t curr1,
                            uint8_t prev2, uint8_t curr2) const;

  /// Aggregate motion statistics for a voice pair.
  struct MotionStats {
    int parallel = 0;
    int similar = 0;
    int contrary = 0;
    int oblique = 0;

    /// @brief Total number of classified motion events.
    int total() const { return parallel + similar + contrary + oblique; }

    /// @brief Ratio of contrary motion (higher = better counterpoint).
    /// @return Value in [0.0, 1.0], or 0.0 if total is 0.
    float contraryRatio() const;
  };

  /// @brief Analyze all consecutive tick pairs for two voices.
  /// @param state Counterpoint state containing voice data.
  /// @param voice1 First voice.
  /// @param voice2 Second voice.
  /// @return Aggregate motion statistics.
  MotionStats analyzeVoicePair(const CounterpointState& state,
                               VoiceId voice1, VoiceId voice2) const;

 private:
  const IRuleEvaluator& rules_;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_MOTION_ANALYZER_H
