// Melodic context and quality scoring for voice leading improvement.

#ifndef BACH_COUNTERPOINT_MELODIC_CONTEXT_H
#define BACH_COUNTERPOINT_MELODIC_CONTEXT_H

#include <cstdint>

namespace bach {

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

  /// @brief Evaluate melodic quality of a candidate pitch.
  ///
  /// Scoring rules (additive, clamped to [0.0, 1.0]):
  ///   +0.3  Step after leap in opposite direction (Bach's melodic law)
  ///   +0.2  Stepwise motion (1-2 semitones)
  ///   +0.1  Imperfect consonance interval (3rd or 6th)
  ///   -0.2  Same-pitch repetition (3+ consecutive)
  ///   -0.3  Augmented interval leap (tritone)
  ///   -0.5  Leading tone not resolving to tonic (semitone above)
  ///
  /// Returns 0.5 (neutral) when context has no valid previous pitches.
  ///
  /// @param ctx The melodic context to evaluate against.
  /// @param candidate The candidate MIDI pitch to score.
  /// @return Quality score in [0.0, 1.0], higher = better voice leading.
  static float scoreMelodicQuality(const MelodicContext& ctx, uint8_t candidate);
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_MELODIC_CONTEXT_H
