/// @brief Archetype policy definitions for fugue subject generation.

#include "fugue/archetype_policy.h"

namespace bach {

namespace {

// NOLINTBEGIN(readability-magic-numbers): policy constants are design values
const ArchetypePolicy kCompactPolicy = {
    3,                  // min_range_degrees
    7,                  // max_range_degrees
    1,                  // min_subject_bars
    2,                  // max_subject_bars
    0.50f,              // min_climax_position
    0.80f,              // max_climax_position
    0.70f,              // min_climax_pitch
    1.00f,              // max_climax_pitch
    0.80f,              // dominant_ending_prob
    AnswerType::Auto,   // preferred_answer
    0.10f,              // min_anacrusis_prob
    0.40f,              // max_anacrusis_prob
    true,               // require_fragmentable
    0.3f,               // fragment_reusability_weight
    0.2f,               // sequence_potential_weight
    0.5f,               // max_sixteenth_density
    0.4f,               // min_step_ratio
    false,              // require_invertible
    false,              // require_contour_symmetry
    false,              // require_axis_stability
    0.0f,               // symmetry_score_weight
    2,                  // max_consecutive_chromatic
    false,              // require_functional_resolution
    6,                  // path_candidates
    0.60f               // base_quality_weight
};

const ArchetypePolicy kCantabilePolicy = {
    5,                  // min_range_degrees
    9,                  // max_range_degrees
    2,                  // min_subject_bars
    4,                  // max_subject_bars
    0.40f,              // min_climax_position
    0.70f,              // max_climax_position
    0.60f,              // min_climax_pitch
    0.95f,              // max_climax_pitch
    0.60f,              // dominant_ending_prob
    AnswerType::Auto,   // preferred_answer
    0.40f,              // min_anacrusis_prob
    0.80f,              // max_anacrusis_prob
    false,              // require_fragmentable
    0.0f,               // fragment_reusability_weight
    0.0f,               // sequence_potential_weight
    0.40f,              // max_sixteenth_density (raised: Bach cantabile still uses 16ths)
    0.6f,               // min_step_ratio
    false,              // require_invertible
    false,              // require_contour_symmetry
    false,              // require_axis_stability
    0.0f,               // symmetry_score_weight
    2,                  // max_consecutive_chromatic
    false,              // require_functional_resolution
    8,                  // path_candidates
    0.75f               // base_quality_weight
};

const ArchetypePolicy kInvertiblePolicy = {
    4,                  // min_range_degrees
    7,                  // max_range_degrees
    2,                  // min_subject_bars
    3,                  // max_subject_bars
    0.40f,              // min_climax_position
    0.80f,              // max_climax_position
    0.65f,              // min_climax_pitch
    0.95f,              // max_climax_pitch
    0.70f,              // dominant_ending_prob
    AnswerType::Real,   // preferred_answer
    0.20f,              // min_anacrusis_prob
    0.50f,              // max_anacrusis_prob
    false,              // require_fragmentable
    0.0f,               // fragment_reusability_weight
    0.1f,               // sequence_potential_weight
    0.3f,               // max_sixteenth_density
    0.5f,               // min_step_ratio
    true,               // require_invertible
    true,               // require_contour_symmetry
    true,               // require_axis_stability
    0.4f,               // symmetry_score_weight
    2,                  // max_consecutive_chromatic
    false,              // require_functional_resolution
    12,                 // path_candidates
    0.50f               // base_quality_weight
};

const ArchetypePolicy kChromaticPolicy = {
    4,                  // min_range_degrees
    8,                  // max_range_degrees
    2,                  // min_subject_bars
    4,                  // max_subject_bars
    0.50f,              // min_climax_position
    0.90f,              // max_climax_position
    0.60f,              // min_climax_pitch
    1.00f,              // max_climax_pitch
    0.55f,              // dominant_ending_prob
    AnswerType::Real,   // preferred_answer
    0.30f,              // min_anacrusis_prob
    0.60f,              // max_anacrusis_prob
    false,              // require_fragmentable
    0.0f,               // fragment_reusability_weight
    0.0f,               // sequence_potential_weight
    0.2f,               // max_sixteenth_density
    0.4f,               // min_step_ratio
    false,              // require_invertible
    false,              // require_contour_symmetry
    false,              // require_axis_stability
    0.0f,               // symmetry_score_weight
    4,                  // max_consecutive_chromatic
    true,               // require_functional_resolution
    10,                 // path_candidates
    0.60f               // base_quality_weight
};
// NOLINTEND(readability-magic-numbers)

}  // namespace

const ArchetypePolicy& getArchetypePolicy(FugueArchetype archetype) {
  switch (archetype) {
    case FugueArchetype::Compact:    return kCompactPolicy;
    case FugueArchetype::Cantabile:  return kCantabilePolicy;
    case FugueArchetype::Invertible: return kInvertiblePolicy;
    case FugueArchetype::Chromatic:  return kChromaticPolicy;
  }
  return kCompactPolicy;
}

}  // namespace bach
