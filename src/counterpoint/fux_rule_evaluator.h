// Fux strict counterpoint rule evaluator -- implements the species
// counterpoint rules of J.J. Fux's "Gradus ad Parnassum".

#ifndef BACH_COUNTERPOINT_FUX_RULE_EVALUATOR_H
#define BACH_COUNTERPOINT_FUX_RULE_EVALUATOR_H

#include "counterpoint/i_rule_evaluator.h"

namespace bach {

/// @brief Strict counterpoint rules based on Fux's treatise.
///
/// Consonance table:
///   Perfect consonance:   P1(0), P5(7), P8(12)
///   Imperfect consonance: m3(3), M3(4), m6(8), M6(9)
///   Dissonance:           m2(1), M2(2), P4(5*), tritone(6), m7(10), M7(11)
///
/// * P4 is treated as dissonant in two-voice counterpoint.  In 3+ voice
///   contexts where the 4th appears above a bass note supporting a 6/4
///   chord, the caller should use a less strict evaluator.
class FuxRuleEvaluator : public IRuleEvaluator {
 public:
  /// @brief Check if an interval is consonant under Fux rules.
  /// @param semitones Absolute interval in semitones.
  /// @param is_strong_beat True on beats 1 and 3 (4/4).
  bool isIntervalConsonant(int semitones,
                           bool is_strong_beat) const override;

  /// @brief Classify the motion between two successive pitch pairs.
  MotionType classifyMotion(uint8_t prev1, uint8_t curr1,
                            uint8_t prev2, uint8_t curr2) const override;

  /// @brief Detect parallel perfect consonances (P5, P8) at a tick.
  bool hasParallelPerfect(const CounterpointState& state,
                          VoiceId voice1, VoiceId voice2,
                          Tick tick) const override;

  /// @brief Detect hidden (direct) fifths/octaves at a tick.
  bool hasHiddenPerfect(const CounterpointState& state,
                        VoiceId voice1, VoiceId voice2,
                        Tick tick) const override;

  /// @brief Detect voice crossing at a tick.
  bool hasVoiceCrossing(const CounterpointState& state,
                        VoiceId voice1, VoiceId voice2,
                        Tick tick) const override;

  /// @brief Validate all voice pairs across a tick range.
  std::vector<RuleViolation> validate(
      const CounterpointState& state,
      Tick from_tick, Tick to_tick) const override;

 private:
  /// @brief Check if an interval (mod 12) is a perfect consonance.
  static bool isPerfectConsonance(int semitones);

  /// @brief Get the previous note before the given tick for a voice.
  static const NoteEvent* getPreviousNote(const CounterpointState& state,
                                          VoiceId voice_id, Tick tick);
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_FUX_RULE_EVALUATOR_H
