// Bach-style counterpoint rule evaluator -- relaxed rules reflecting
// J.S. Bach's actual compositional practice rather than strict Fux species.

#ifndef BACH_COUNTERPOINT_BACH_RULE_EVALUATOR_H
#define BACH_COUNTERPOINT_BACH_RULE_EVALUATOR_H

#include <cstdint>

#include "counterpoint/i_rule_evaluator.h"

namespace bach {

/// @brief Bach-style counterpoint rules with context-aware relaxations.
///
/// Differs from FuxRuleEvaluator in several key ways:
///
/// - **P4 consonance**: Perfect 4th (5 semitones) is consonant when the
///   evaluator is configured for 3+ voices (6/4 chord context).
/// - **Hidden perfects**: Allowed when *either* voice approaches by step
///   (Fux only allows when the upper voice steps).
/// - **Temporary voice crossing**: Crossings lasting 1 beat or less that
///   resolve to proper order are permitted.
/// - **Weak-beat dissonance**: In free counterpoint mode, consonances on
///   weak beats pass immediately. Dissonances on weak beats are rejected
///   by isIntervalConsonant so that the CollisionResolver's NHT check
///   (passing tone / neighbor tone) can evaluate with next_pitch context.
class BachRuleEvaluator : public IRuleEvaluator {
 public:
  /// @brief Construct a Bach rule evaluator with voice count context.
  /// @param num_voices Number of active voices (affects P4 consonance).
  explicit BachRuleEvaluator(uint8_t num_voices);

  /// @brief Enable or disable free counterpoint (5th species) mode.
  /// @param enabled When true, weak-beat dissonances are always allowed.
  void setFreeCounterpoint(bool enabled);

  /// @brief Check if free counterpoint mode is active.
  /// @return True if free counterpoint mode is enabled.
  bool isFreeCounterpoint() const;

  /// @brief Get the configured voice count.
  /// @return Number of voices this evaluator was constructed with.
  uint8_t numVoices() const;

  /// @brief Check if an interval is consonant under Bach rules.
  /// @param semitones Absolute interval in semitones.
  /// @param is_strong_beat True on beats 1 and 3 (4/4).
  /// @return True if the interval is consonant.
  /// @note P4 is consonant with 3+ voices. In free counterpoint mode,
  ///       weak-beat consonances pass; weak-beat dissonances return false
  ///       to allow CollisionResolver NHT evaluation with next_pitch.
  bool isIntervalConsonant(int semitones,
                           bool is_strong_beat) const override;

  /// @brief Classify the motion between two successive pitch pairs.
  /// @param prev1 Previous pitch in voice 1.
  /// @param curr1 Current pitch in voice 1.
  /// @param prev2 Previous pitch in voice 2.
  /// @param curr2 Current pitch in voice 2.
  /// @return Classified motion type.
  MotionType classifyMotion(uint8_t prev1, uint8_t curr1,
                            uint8_t prev2, uint8_t curr2) const override;

  /// @brief Detect parallel perfect consonances (P5, P8) at a tick.
  /// @param state Counterpoint state to query.
  /// @param voice1 First voice identifier.
  /// @param voice2 Second voice identifier.
  /// @param tick Tick position to check.
  /// @return True if parallel perfect consonances are detected.
  bool hasParallelPerfect(const CounterpointState& state,
                          VoiceId voice1, VoiceId voice2,
                          Tick tick) const override;

  /// @brief Detect hidden (direct) fifths/octaves at a tick.
  /// @param state Counterpoint state to query.
  /// @param voice1 First voice identifier.
  /// @param voice2 Second voice identifier.
  /// @param tick Tick position to check.
  /// @return True if hidden perfects are detected. More lenient than Fux:
  ///         allowed when either voice approaches by step.
  bool hasHiddenPerfect(const CounterpointState& state,
                        VoiceId voice1, VoiceId voice2,
                        Tick tick) const override;

  /// @brief Detect voice crossing at a tick.
  /// @param state Counterpoint state to query.
  /// @param voice1 First voice identifier (expected higher pitch).
  /// @param voice2 Second voice identifier (expected lower pitch).
  /// @param tick Tick position to check.
  /// @return True if a persistent voice crossing is detected. Temporary
  ///         crossings (resolving within 1 beat) are allowed.
  bool hasVoiceCrossing(const CounterpointState& state,
                        VoiceId voice1, VoiceId voice2,
                        Tick tick) const override;

  /// @brief Validate all voice pairs across a tick range.
  /// @param state The counterpoint state to check.
  /// @param from_tick Start of validation window (inclusive).
  /// @param to_tick End of validation window (exclusive).
  /// @return Vector of all violations found.
  std::vector<RuleViolation> validate(
      const CounterpointState& state,
      Tick from_tick, Tick to_tick) const override;

  /// @brief Bach allows closer voice spacing (soft penalty, not rejection).
  bool isStrictSpacing() const override { return false; }

 private:
  uint8_t num_voices_;
  bool free_counterpoint_ = false;

  /// @brief Check if an interval (mod 12) is a perfect consonance.
  static bool isPerfectConsonance(int semitones);

  /// @brief Get the previous note before the given tick for a voice.
  static const NoteEvent* getPreviousNote(const CounterpointState& state,
                                          VoiceId voice_id, Tick tick);

  /// @brief Check if a voice crossing at the given tick resolves by the next beat.
  /// @param state Counterpoint state to query.
  /// @param voice1 Higher voice (by convention).
  /// @param voice2 Lower voice (by convention).
  /// @param tick Tick position where crossing was detected.
  /// @return True if the crossing resolves within 1 beat (temporary).
  bool isCrossingTemporary(const CounterpointState& state,
                           VoiceId voice1, VoiceId voice2,
                           Tick tick) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_BACH_RULE_EVALUATOR_H
