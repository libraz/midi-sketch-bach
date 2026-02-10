// Voice leading planner -- design-value tables for pre-generation hints.

#ifndef BACH_HARMONY_VOICE_LEADING_PLANNER_H
#define BACH_HARMONY_VOICE_LEADING_PLANNER_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Voice leading hints for harmonic timeline construction.
///
/// These are design-value tables (Principle 4: trust design values, don't search).
/// Generated at config time BEFORE any note generation begins.
/// The HarmonicTimeline uses these as hints during initial construction only.
struct VoiceLeadingHints {
  /// @brief Preferred intervals between voices for each fugue phase (in semitones).
  ///   Establish: 3rds, 6ths (stable imperfect consonances)
  ///   Develop: wider variety, including 4ths (more tension)
  ///   Resolve: 3rds, 5ths, octaves (resolution, clarity)
  std::vector<uint8_t> preferred_intervals;

  /// @brief Target ratio of contrary motion between voice pairs [0, 1].
  float contrary_motion_target = 0.5f;

  /// @brief Target suspension density (suspensions per bar).
  float suspension_density = 0.25f;

  /// @brief Target chord tone coverage ratio [0, 1].
  float chord_tone_target = 0.75f;
};

/// @brief Generate voice leading hints based on fugue configuration.
///
/// Returns fixed design-value tables (Principle 4) based on character and phase.
/// These are NOT searched or optimized -- they are predetermined lookup values.
///
/// @param character Subject character (affects tension preferences).
/// @param phase Current fugue phase (affects interval preferences).
/// @param num_voices Number of voices (affects contrary motion targets).
/// @return VoiceLeadingHints for the given configuration.
VoiceLeadingHints planVoiceLeading(SubjectCharacter character, FuguePhase phase,
                                   uint8_t num_voices);

/// @brief Get preferred intervals for a fugue phase.
///
/// Design values:
///   Establish: {3, 4, 8, 9} (3rds and 6ths - stable consonances)
///   Develop:   {3, 4, 5, 7, 8, 9} (wider variety including 4ths and 5ths)
///   Resolve:   {3, 4, 7, 12} (3rds, 5ths, octaves - resolution)
///
/// @param phase The fugue phase.
/// @return Vector of preferred interval sizes in semitones.
std::vector<uint8_t> getPhasePreferredIntervals(FuguePhase phase);

/// @brief Get contrary motion target based on character.
///
/// Design values:
///   Severe:  0.55 (high contrary motion - strict counterpoint)
///   Playful: 0.40 (more parallel/similar - lighter texture)
///   Noble:   0.50 (balanced)
///   Restless: 0.45 (moderate, allows chromatic parallel motion)
///
/// @param character The subject character.
/// @return Target contrary motion ratio.
float getContraryMotionTarget(SubjectCharacter character);

/// @brief Get suspension density based on character and phase.
///
/// Design values (suspensions per bar):
///   Severe/Establish:  0.15 (sparse suspensions)
///   Severe/Develop:    0.30 (more tension)
///   Severe/Resolve:    0.20 (moderate)
///   Playful/*:         0.10 (light)
///   Noble/*:           0.25 (moderate throughout)
///   Restless/*:        0.35 (frequent suspensions)
///
/// @param character The subject character.
/// @param phase The fugue phase.
/// @return Suspensions per bar target.
float getSuspensionDensity(SubjectCharacter character, FuguePhase phase);

}  // namespace bach

#endif  // BACH_HARMONY_VOICE_LEADING_PLANNER_H
