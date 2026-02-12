/// @brief Archetype policy: structural strategy constraints for fugue subjects.

#ifndef BACH_FUGUE_ARCHETYPE_POLICY_H
#define BACH_FUGUE_ARCHETYPE_POLICY_H

#include <cstdint>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"

namespace bach {

/// @brief Structural strategy constraints for a fugue archetype.
///
/// Each field constrains or weights a specific aspect of subject generation
/// and evaluation. The policy is immutable once constructed.
struct ArchetypePolicy {
  // --- Basic constraints ---
  int min_range_degrees;       ///< Pitch range lower bound (scale degrees).
  int max_range_degrees;       ///< Pitch range upper bound (scale degrees).
  uint8_t min_subject_bars;    ///< Subject length lower bound (bars).
  uint8_t max_subject_bars;    ///< Subject length upper bound (bars).
  float min_climax_position;   ///< GoalTone position_ratio lower bound.
  float max_climax_position;   ///< GoalTone position_ratio upper bound.
  float min_climax_pitch;      ///< GoalTone pitch_ratio lower bound.
  float max_climax_pitch;      ///< GoalTone pitch_ratio upper bound.
  float dominant_ending_prob;  ///< Dominant ending probability.
  AnswerType preferred_answer; ///< Recommended answer type.
  float min_anacrusis_prob;    ///< Anacrusis probability lower bound.
  float max_anacrusis_prob;    ///< Anacrusis probability upper bound.

  // --- Compact: fragmentability ---
  bool require_fragmentable;         ///< Kopfmotiv decomposition required.
  float fragment_reusability_weight; ///< Score weight for fragment reusability.
  float sequence_potential_weight;   ///< Score weight for sequence potential.

  // --- Cantabile: melodic flow ---
  float max_sixteenth_density;  ///< Maximum sixteenth note density (0.0-1.0).
  float min_step_ratio;         ///< Minimum stepwise motion ratio.

  // --- Invertible: symmetry ---
  bool require_invertible;       ///< Inversion compatibility required.
  bool require_contour_symmetry; ///< Contour mirror symmetry required.
  bool require_axis_stability;   ///< Inversion axis stability required.
  float symmetry_score_weight;   ///< Score weight for symmetry.

  // --- Chromatic: functional harmony ---
  int max_consecutive_chromatic;      ///< Maximum consecutive chromatic steps.
  bool require_functional_resolution; ///< Require functional harmonic resolution.
};

/// @brief Get the archetype policy for a given fugue archetype.
/// @param archetype The fugue archetype.
/// @return Immutable policy for the archetype.
const ArchetypePolicy& getArchetypePolicy(FugueArchetype archetype);

}  // namespace bach

#endif  // BACH_FUGUE_ARCHETYPE_POLICY_H
