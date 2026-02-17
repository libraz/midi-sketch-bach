// Melodic context and quality scoring for voice leading improvement.

#ifndef BACH_COUNTERPOINT_MELODIC_CONTEXT_H
#define BACH_COUNTERPOINT_MELODIC_CONTEXT_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

/// @brief Goal tone for phrase-level melodic direction.
///
/// Represents a cadence tone or characteristic interval target that the melodic
/// line should approach over time. The target_pitch is a DESIGN VALUE derived
/// deterministically from the cadence tone and subject characteristic intervals
/// (Principle 4: Trust Design Values -- not searched).
///
/// When set, the scoring system awards a bonus proportional to how close the
/// candidate pitch and current tick are to the goal, encouraging melodic lines
/// to arc toward structurally significant tones.
struct PhraseGoal {
  uint8_t target_pitch = 0;  ///< Goal pitch (cadence tone or characteristic interval).
  Tick target_tick = 0;      ///< When the goal should be reached.
  float bonus = 0.3f;        ///< Maximum bonus weight for approaching the goal.
};

/// @brief Melodic context for evaluating voice-leading quality.
///
/// Tracks the recent pitch history of a voice to enable melodic quality
/// scoring when the CollisionResolver has multiple candidate pitches.
/// All fields have safe defaults so that an uninitialized context
/// produces neutral scores (no behavioral change from prior code).
struct MelodicContext {
  uint8_t prev_pitches[3] = {0, 0, 0};  ///< Last 3 pitches (0=unknown), [0]=most recent.
  uint8_t prev_count = 0;                ///< Number of valid previous pitches (0-3).
  int8_t prev_direction = 0;             ///< -1=descending, 0=unknown, 1=ascending.
  bool is_leading_tone = false;          ///< True if prev pitch is the leading tone.
  bool leap_needs_resolution = false;    ///< True if prev interval was a leap (>= 5 semitones).
  int8_t consecutive_same_dir = 0;       ///< Count of consecutive same-direction stepwise motions.

  /// @brief Evaluate melodic quality of a candidate pitch.
  ///
  /// Scoring rules (additive, clamped to [0.0, 1.0]):
  ///   +0.3  Step after leap in opposite direction (Bach's melodic law)
  ///   +0.2  Stepwise motion (1-2 semitones)
  ///   +0.1  Imperfect consonance interval (3rd or 6th)
  ///   -0.1  Same-pitch repetition (any, mild)
  ///   -0.2  3 consecutive same pitch (additional, total -0.3)
  ///   -0.2  4 consecutive same pitch (additional, total -0.5)
  ///   -0.3  Augmented interval leap (tritone)
  ///   -0.5  Leading tone not resolving to tonic (semitone above)
  ///   +0.4/-0.4  Unresolved leap penalty / resolution bonus (Rule 8)
  ///   -0.15 Long same-direction stepwise run (>= 5 consecutive, Rule 9)
  ///
  /// Returns 0.5 (neutral) when context has no valid previous pitches.
  ///
  /// @param ctx The melodic context to evaluate against.
  /// @param candidate The candidate MIDI pitch to score.
  /// @param goal Optional phrase goal. When non-null, an additive bonus is
  ///        applied based on proximity to the goal pitch and tick.
  /// @param current_tick Tick of the candidate note. Used only when goal is set.
  /// @return Quality score in [0.0, 1.0], higher = better voice leading.
  static float scoreMelodicQuality(const MelodicContext& ctx, uint8_t candidate,
                                   const PhraseGoal* goal = nullptr,
                                   Tick current_tick = 0);
};

/// @brief Compute the goal approach bonus for a candidate pitch at a given tick.
///
/// The bonus is proportional to both pitch proximity (closer to goal pitch = more
/// bonus) and temporal proximity (closer to goal tick = more bonus). This
/// encourages melodic lines to converge toward structurally important tones
/// at phrase boundaries.
///
/// Pitch proximity uses an inverse relationship: bonus decreases as the
/// semitone distance increases. Beyond 12 semitones (an octave), the pitch
/// factor is zero.
///
/// Temporal proximity ramps linearly from 0.0 (at tick 0 or before any context)
/// to 1.0 (at or past the target tick).
///
/// @param current_pitch The candidate MIDI pitch to evaluate.
/// @param current_tick The tick position of the candidate note.
/// @param goal The phrase goal containing target pitch, tick, and max bonus.
/// @return Bonus in [0.0, goal.bonus], additive to the melodic quality score.
float computeGoalApproachBonus(uint8_t current_pitch, Tick current_tick,
                               const PhraseGoal& goal);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_MELODIC_CONTEXT_H
