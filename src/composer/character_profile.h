#ifndef BACH_COMPOSER_CHARACTER_PROFILE_H
#define BACH_COMPOSER_CHARACTER_PROFILE_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach::composer::detail {

// Per-character composition profile: a density bias applied to the arc
// density tier plus a small set of rhythmic / ornamental preferences the
// per-form builders and the ornament pass consume. Which subjects a character
// may draw from lives in the qualified subject catalog's per-character class
// arrays (subject_catalog.inc), consumed by subjectIndexFor below.
struct CharacterProfile {
  std::int8_t density_bias;       // -1/0/+1 applied to the arc density tier (clamped 0..3).
  bool prefer_dotted;             // Noble: dotted figures.
  bool prefer_syncopation;        // Playful: off-beat onsets.
  bool prefer_chromatic_runs;     // Restless: chromatic passing-tone density in figuration.
  std::uint8_t ornament_density;  // 0..2, consumed by the upcoming ornament pass.
};

/**
 * @brief Look up the immutable composition profile for a character.
 * @param character The subject character.
 * @return Reference to the static CharacterProfile for the character.
 */
const CharacterProfile& characterProfile(SubjectCharacter character);

/**
 * @brief Deterministically pick a qualified-catalog subject index for a character.
 *
 * Mirrors the existing (seed/4)%N subject-derivation style: the choice is
 * (seed / 4) % class_size mapped through the character's feature-class index
 * list (subject_catalog.inc), so the result is always one of the character's
 * allowed catalog indices and is stable across the 4-seed block. Each class
 * contains the character's two legacy slots plus every synthesized entry
 * whose contour / leap / density features fit the character.
 *
 * @param character The subject character (selects the feature class).
 * @param minor_mode True selects the minor-catalog class lists.
 * @param seed The piece seed.
 * @return An index into kSubjectCatalogMajor / kSubjectCatalogMinor.
 */
std::uint8_t subjectIndexFor(SubjectCharacter character, bool minor_mode, std::uint32_t seed);

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_CHARACTER_PROFILE_H
