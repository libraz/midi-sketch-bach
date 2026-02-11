// Collision resolver -- finds rule-safe pitches when the desired pitch
// would cause a counterpoint violation.

#ifndef BACH_COUNTERPOINT_COLLISION_RESOLVER_H
#define BACH_COUNTERPOINT_COLLISION_RESOLVER_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

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

/// @brief Resolves counterpoint collisions via a 6-stage strategy cascade.
///
/// When a desired pitch would violate counterpoint rules, the resolver
/// tries progressively more invasive strategies to find a valid pitch:
///
///   1. "original"     -- use the desired pitch as-is
///   2. "chord_tone"   -- nearest consonant interval with all sounding voices
///   3. "suspension"   -- hold previous pitch (preparation-dissonance-resolution)
///   4. "step_shift"   -- try +/-1, +/-2 semitones from desired pitch
///   5. "octave_shift" -- try +/-12 semitones
///   6. "rest"         -- give up; return penalty=1.0
///
/// Each stage calls isSafeToPlace() to validate the candidate.
class CollisionResolver {
 public:
  /// @brief Check if a pitch can be placed without rule violations.
  ///
  /// When next_pitch is provided (non-zero), dissonant pitches on strong beats
  /// may still be allowed if they form a valid passing tone or neighbor tone
  /// pattern with the surrounding pitches.
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator to check against.
  /// @param voice_id Voice that would receive the note.
  /// @param pitch Candidate MIDI pitch.
  /// @param tick Start tick of the note.
  /// @param duration Duration in ticks.
  /// @param next_pitch Next pitch in the voice (0 if unknown, enables non-harmonic
  ///        tone classification when provided).
  /// @return True if no violations would result.
  bool isSafeToPlace(const CounterpointState& state,
                     const IRuleEvaluator& rules,
                     VoiceId voice_id, uint8_t pitch,
                     Tick tick, Tick duration,
                     uint8_t next_pitch = 0) const;

  /// @brief Find a safe pitch using the 6-stage strategy cascade.
  ///
  /// Strategies tried in order:
  ///   1. "original"    -- use the desired pitch as-is
  ///   2. "chord_tone"  -- nearest consonant interval with all sounding voices
  ///   3. "suspension"  -- hold previous pitch if it creates valid suspension
  ///   4. "step_shift"  -- try +/-1, +/-2 semitones from desired pitch
  ///   5. "octave_shift" -- try +/-12 semitones
  ///   6. "rest"        -- give up; return penalty=1.0
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator to check against.
  /// @param voice_id Target voice.
  /// @param desired_pitch Preferred MIDI pitch.
  /// @param tick Start tick.
  /// @param duration Duration in ticks.
  /// @param next_pitch Next pitch in the voice (0 if unknown, enables
  ///        non-harmonic tone classification in isSafeToPlace).
  /// @return PlacementResult with the best available pitch and strategy.
  PlacementResult findSafePitch(const CounterpointState& state,
                                const IRuleEvaluator& rules,
                                VoiceId voice_id, uint8_t desired_pitch,
                                Tick tick, Tick duration,
                                uint8_t next_pitch = 0) const;

  /// @brief Find a safe pitch respecting the source's protection level.
  ///
  /// Immutable sources (subject, cantus, ground bass) only try "original";
  /// if that fails, the note is rejected rather than altered.
  /// Structural sources (answer, countersubject) allow octave shift only.
  /// Flexible sources use the full cascade.
  ///
  /// @param source The note source (determines allowed strategies).
  /// @param next_pitch Next pitch in the voice (0 if unknown, enables
  ///        non-harmonic tone classification in isSafeToPlace).
  PlacementResult findSafePitch(const CounterpointState& state,
                                const IRuleEvaluator& rules,
                                VoiceId voice_id, uint8_t desired_pitch,
                                Tick tick, Tick duration,
                                BachNoteSource source,
                                uint8_t next_pitch = 0) const;

  /// @brief Find a safe pitch with pedal-range awareness.
  ///
  /// Same as findSafePitch() but also considers the voice range penalty
  /// when scoring candidates, preferring pitches that stay within the
  /// registered voice range.
  PlacementResult resolvePedal(const CounterpointState& state,
                               const IRuleEvaluator& rules,
                               VoiceId voice_id, uint8_t desired_pitch,
                               Tick tick, Tick duration) const;

  /// @brief Try to create a suspension pattern (preparation-suspension-resolution).
  ///
  /// A suspension holds the previous pitch into a new beat where it becomes
  /// dissonant, then resolves downward by step to a consonant interval.
  /// This method checks if the previous pitch for the given voice would
  /// create a valid suspension at the current tick.
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator.
  /// @param voice_id Voice placing the note.
  /// @param desired_pitch Desired pitch (used only for context, not placed).
  /// @param tick Current tick position.
  /// @param duration Note duration.
  /// @return PlacementResult with suspension if viable, otherwise not accepted.
  PlacementResult trySuspension(const CounterpointState& state,
                                const IRuleEvaluator& rules,
                                VoiceId voice_id, uint8_t desired_pitch,
                                Tick tick, Tick duration) const;

  /// @brief Find a safe pitch using 2-beat lookahead scoring.
  ///
  /// For each candidate pitch, evaluates the quality of placement at the current
  /// tick AND the potential quality at the next beat. Score = current_penalty +
  /// 0.5 * next_beat_penalty. Selects the candidate with the lowest total score.
  ///
  /// @param state Current counterpoint state.
  /// @param rules Rule evaluator.
  /// @param voice_id Voice placing the note.
  /// @param desired_pitch Ideal pitch.
  /// @param tick Current tick position.
  /// @param duration Note duration.
  /// @param next_desired_pitch Desired pitch for the next beat (0 if unknown).
  /// @return PlacementResult with the best lookahead pitch.
  PlacementResult findSafePitchWithLookahead(const CounterpointState& state,
                                              const IRuleEvaluator& rules,
                                              VoiceId voice_id,
                                              uint8_t desired_pitch, Tick tick,
                                              Tick duration,
                                              uint8_t next_desired_pitch) const;

  /// @brief Set the maximum search range (in semitones) for step_shift.
  /// @param semitones Maximum distance to search (default: 12).
  void setMaxSearchRange(int semitones);

  /// @brief Set the voice range tolerance (in semitones).
  ///
  /// Notes placed more than this many semitones outside their voice's
  /// registered range are rejected by isSafeToPlace().
  ///
  /// @param semitones Maximum allowed excursion outside range (default: 6).
  void setRangeTolerance(int semitones);

  /// @brief Set cadence tick positions for voice-leading enhancement.
  ///
  /// At cadence ticks, leading tone -> tonic resolution is prioritized.
  /// When the current tick is within kTicksPerBeat of a cadence tick and the
  /// previous pitch is the leading tone (pitch % 12 == 11 in C major context),
  /// resolution upward by semitone receives a penalty bonus (-0.3).
  ///
  /// @param ticks Sorted vector of cadence tick positions.
  void setCadenceTicks(const std::vector<Tick>& ticks);

 private:
  int max_search_range_ = 12;
  int range_tolerance_ = 3;
  std::vector<Tick> cadence_ticks_;

  /// @brief Attempt a specific resolution strategy.
  PlacementResult tryStrategy(const CounterpointState& state,
                              const IRuleEvaluator& rules,
                              VoiceId voice_id, uint8_t desired_pitch,
                              Tick tick, Tick duration,
                              const std::string& strategy,
                              uint8_t next_pitch = 0) const;

  /// @brief Check if a pitch would cross an adjacent voice.
  /// @return True if the candidate pitch crosses above a higher voice
  ///         or below a lower voice at the given tick.
  bool wouldCrossVoice(const CounterpointState& state,
                       VoiceId voice_id, uint8_t pitch,
                       Tick tick) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_COLLISION_RESOLVER_H
