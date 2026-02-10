// Motif template system for organic subject generation.
//
// Each SubjectCharacter maps to a fixed pair of MotifTemplates (A and B).
// Motif A drives toward the GoalTone (climax); Motif B descends from it.
// This replaces random-walk note generation with structured motivic patterns.

#ifndef BACH_FUGUE_MOTIF_TEMPLATE_H
#define BACH_FUGUE_MOTIF_TEMPLATE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Types of motivic patterns for subject construction.
enum class MotifType : uint8_t {
  Scale,      ///< Stepwise motion (2-3 notes in scale degrees).
  Leap,       ///< Leap + contrary step (3rd+ jump then stepwise return).
  Rhythmic,   ///< Rhythmic pattern (repeated pitch with characteristic rhythm).
  Chromatic,  ///< Chromatic motion (half-step crawl, 2-3 notes).
  Sustain     ///< Sustained note (single long-value note).
};

/// @brief A fixed motivic pattern template.
///
/// Each template defines a sequence of degree offsets from the starting degree
/// and a corresponding rhythm pattern. Templates are design values (Principle 4)
/// and are never generated randomly.
struct MotifTemplate {
  MotifType type;
  std::vector<int> degree_offsets;  ///< Relative scale degrees from start (0-based).
  std::vector<Tick> durations;      ///< Duration for each note.
};

/// @brief Goal tone position and pitch for subject climax.
///
/// Design values (Principle 4): fixed per character, not searched.
/// position_ratio: where in the subject the climax falls [0.0, 1.0].
/// pitch_ratio: how high in the available range [0.0, 1.0].
struct GoalTone {
  float position_ratio;  ///< Climax position as fraction of subject length.
  float pitch_ratio;     ///< Climax pitch as fraction of available range.
};

/// @brief Get the GoalTone design values for a given character.
/// @param character Subject character type.
/// @return GoalTone with fixed position and pitch ratios.
GoalTone goalToneForCharacter(SubjectCharacter character);

/// @brief Get the pair of MotifTemplates (A and B) for a given character.
///
/// Each character has 4 template pairs (16 total). Use template_idx to select
/// among the variants; values are taken modulo 4. This expands subject diversity
/// while keeping fixed design values (Principle 3: reduce generation).
///
/// Motif A is used for the ascending portion (toward the goal tone).
/// Motif B is used for the descending portion (away from the goal tone).
///
/// @param character Subject character type.
/// @param template_idx Index to select among 4 template pairs (taken mod 4).
/// @return Pair of templates: first = Motif A (ascending), second = Motif B (descending).
std::pair<MotifTemplate, MotifTemplate> motifTemplatesForCharacter(
    SubjectCharacter character, uint32_t template_idx = 0);

}  // namespace bach

#endif  // BACH_FUGUE_MOTIF_TEMPLATE_H
