// Collision resolver -- finds rule-safe pitches when the desired pitch
// would cause a counterpoint violation.

#ifndef BACH_COUNTERPOINT_COLLISION_RESOLVER_H
#define BACH_COUNTERPOINT_COLLISION_RESOLVER_H

#include <cstdint>
#include <string>

#include "core/basic_types.h"

namespace bach {

class CounterpointState;
class IRuleEvaluator;

/// @brief Result of a collision resolution attempt.
struct PlacementResult {
  uint8_t pitch = 0;        ///< Final pitch chosen.
  float penalty = 0.0f;     ///< 0.0 = ideal, 1.0 = rejection threshold.
  std::string strategy;     ///< Strategy that succeeded ("original", "chord_tone", etc.).
  bool accepted = false;    ///< True if a safe pitch was found.
};

/// @brief Resolves counterpoint collisions via a 5-stage strategy cascade.
///
/// When a desired pitch would violate counterpoint rules, the resolver
/// tries progressively more invasive strategies to find a valid pitch:
///
///   1. "original"    -- use the desired pitch as-is
///   2. "chord_tone"  -- nearest consonant interval with all sounding voices
///   3. "step_shift"  -- try +/-1, +/-2 semitones from desired pitch
///   4. "octave_shift" -- try +/-12 semitones
///   5. "rest"        -- give up; return penalty=1.0
///
/// Each stage calls isSafeToPlace() to validate the candidate.
class CollisionResolver {
 public:
  /// @brief Check if a pitch can be placed without rule violations.
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator to check against.
  /// @param voice_id Voice that would receive the note.
  /// @param pitch Candidate MIDI pitch.
  /// @param tick Start tick of the note.
  /// @param duration Duration in ticks.
  /// @return True if no violations would result.
  bool isSafeToPlace(const CounterpointState& state,
                     const IRuleEvaluator& rules,
                     VoiceId voice_id, uint8_t pitch,
                     Tick tick, Tick duration) const;

  /// @brief Find a safe pitch using the 5-stage strategy cascade.
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator to check against.
  /// @param voice_id Target voice.
  /// @param desired_pitch Preferred MIDI pitch.
  /// @param tick Start tick.
  /// @param duration Duration in ticks.
  /// @return PlacementResult with the best available pitch and strategy.
  PlacementResult findSafePitch(const CounterpointState& state,
                                const IRuleEvaluator& rules,
                                VoiceId voice_id, uint8_t desired_pitch,
                                Tick tick, Tick duration) const;

  /// @brief Find a safe pitch with pedal-range awareness.
  ///
  /// Same as findSafePitch() but also considers the voice range penalty
  /// when scoring candidates, preferring pitches that stay within the
  /// registered voice range.
  PlacementResult resolvePedal(const CounterpointState& state,
                               const IRuleEvaluator& rules,
                               VoiceId voice_id, uint8_t desired_pitch,
                               Tick tick, Tick duration) const;

  /// @brief Set the maximum search range (in semitones) for step_shift.
  /// @param semitones Maximum distance to search (default: 12).
  void setMaxSearchRange(int semitones);

 private:
  int max_search_range_ = 12;

  /// @brief Attempt a specific resolution strategy.
  PlacementResult tryStrategy(const CounterpointState& state,
                              const IRuleEvaluator& rules,
                              VoiceId voice_id, uint8_t desired_pitch,
                              Tick tick, Tick duration,
                              const std::string& strategy) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_COLLISION_RESOLVER_H
