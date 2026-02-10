// Pure abstract interface for counterpoint rule evaluation.
// Concrete implementations: FuxRuleEvaluator (strict), BachRuleEvaluator.

#ifndef BACH_COUNTERPOINT_I_RULE_EVALUATOR_H
#define BACH_COUNTERPOINT_I_RULE_EVALUATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

// Forward declaration -- avoids circular include.
class CounterpointState;

// ---------------------------------------------------------------------------
// Motion classification
// ---------------------------------------------------------------------------

/// @brief Voice-pair motion type between two successive intervals.
enum class MotionType : uint8_t {
  Parallel,  // Same direction, same interval size
  Similar,   // Same direction, different interval size
  Contrary,  // Opposite directions
  Oblique    // One voice stationary
};

/// @brief Convert MotionType to a human-readable string.
/// @param type The motion type value.
/// @return Null-terminated C string (e.g. "parallel").
const char* motionTypeToString(MotionType type);

// ---------------------------------------------------------------------------
// Rule violation descriptor
// ---------------------------------------------------------------------------

/// @brief A single counterpoint rule violation detected during validation.
struct RuleViolation {
  VoiceId voice1 = 0;     ///< First voice involved.
  VoiceId voice2 = 0;     ///< Second voice involved.
  Tick tick = 0;           ///< Tick position of the violation.
  std::string rule;        ///< Rule name (e.g. "parallel_fifths").
  uint8_t severity = 1;   ///< 0 = warning, 1 = error.
};

// ---------------------------------------------------------------------------
// Abstract interface
// ---------------------------------------------------------------------------

/// @brief Abstract interface for counterpoint rule evaluation.
///
/// Implementations define what intervals are consonant, how motion is
/// classified, and which voice-leading patterns are forbidden.  The
/// validate() method runs a full sweep over a tick range and returns all
/// violations found.
class IRuleEvaluator {
 public:
  virtual ~IRuleEvaluator() = default;

  /// @brief Check whether an interval is consonant.
  /// @param semitones Absolute interval in semitones.
  /// @param is_strong_beat True if the interval occurs on a strong beat.
  /// @return True if the interval is consonant under this ruleset.
  virtual bool isIntervalConsonant(int semitones,
                                   bool is_strong_beat) const = 0;

  /// @brief Classify the motion between two successive pitch pairs.
  /// @param prev1 Previous pitch in voice 1.
  /// @param curr1 Current pitch in voice 1.
  /// @param prev2 Previous pitch in voice 2.
  /// @param curr2 Current pitch in voice 2.
  /// @return Classified motion type.
  virtual MotionType classifyMotion(uint8_t prev1, uint8_t curr1,
                                    uint8_t prev2, uint8_t curr2) const = 0;

  /// @brief Detect parallel perfect fifths/octaves at a given tick.
  virtual bool hasParallelPerfect(const CounterpointState& state,
                                  VoiceId voice1, VoiceId voice2,
                                  Tick tick) const = 0;

  /// @brief Detect hidden (direct) fifths/octaves at a given tick.
  virtual bool hasHiddenPerfect(const CounterpointState& state,
                                VoiceId voice1, VoiceId voice2,
                                Tick tick) const = 0;

  /// @brief Detect voice crossing at a given tick.
  virtual bool hasVoiceCrossing(const CounterpointState& state,
                                VoiceId voice1, VoiceId voice2,
                                Tick tick) const = 0;

  /// @brief Validate all voice pairs in a tick range.
  /// @param state The counterpoint state to check.
  /// @param from_tick Start of validation window (inclusive).
  /// @param to_tick End of validation window (exclusive).
  /// @return Vector of all violations found.
  virtual std::vector<RuleViolation> validate(
      const CounterpointState& state,
      Tick from_tick, Tick to_tick) const = 0;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_I_RULE_EVALUATOR_H
