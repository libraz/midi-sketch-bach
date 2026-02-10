// High-level counterpoint validation and reporting.

#ifndef BACH_COUNTERPOINT_COUNTERPOINT_VALIDATOR_H
#define BACH_COUNTERPOINT_COUNTERPOINT_VALIDATOR_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {

class CounterpointState;

/// @brief High-level validation facade for counterpoint state.
///
/// Wraps an IRuleEvaluator to provide convenience methods: full-state
/// validation, compliance rate calculation, and human-readable reports.
/// The validator itself holds no state beyond the evaluator reference.
class CounterpointValidator {
 public:
  /// @brief Construct with a rule evaluator reference.
  /// @param rules The rule evaluator to delegate to (must outlive this).
  explicit CounterpointValidator(const IRuleEvaluator& rules);

  /// @brief Validate the entire state (all ticks, all voice pairs).
  /// @param state Counterpoint state to validate.
  /// @return All violations found.
  std::vector<RuleViolation> validate(const CounterpointState& state) const;

  /// @brief Validate a specific tick range.
  /// @param state Counterpoint state to validate.
  /// @param from_tick Start of range (inclusive).
  /// @param to_tick End of range (exclusive).
  /// @return Violations found within the range.
  std::vector<RuleViolation> validate(const CounterpointState& state,
                                      Tick from_tick,
                                      Tick to_tick) const;

  /// @brief Calculate the compliance rate for the entire state.
  ///
  /// Compliance = 1.0 - (violations / total_intervals).  A value of
  /// 1.0 means no violations were found.  Returns 1.0 when there are
  /// no intervals to check (e.g. single voice or no notes).
  ///
  /// @param state Counterpoint state to evaluate.
  /// @return Compliance rate in [0.0, 1.0].
  float getComplianceRate(const CounterpointState& state) const;

  /// @brief Generate a JSON report of violations.
  /// @param state Counterpoint state to evaluate.
  /// @return JSON string (minified).
  std::string toJson(const CounterpointState& state) const;

  /// @brief Generate a human-readable text report.
  /// @param state Counterpoint state to evaluate.
  /// @return Multi-line report string.
  std::string generateReport(const CounterpointState& state) const;

 private:
  const IRuleEvaluator& rules_;

  /// @brief Estimate the total number of interval checks performed.
  size_t estimateIntervalCount(const CounterpointState& state) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_COUNTERPOINT_VALIDATOR_H
