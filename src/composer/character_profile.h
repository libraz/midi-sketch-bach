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
  std::uint8_t ornament_density;  // 0..2, consumed by the ornament pass.
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

/**
 * @brief Pick which figure of a form's figuration palette a cycle reaches for.
 *
 * The per-form builders hold a small palette of figuration idioms and walk it
 * with a seed-driven rotation, which varies the piece across seeds but leaves
 * every character on the same idiom sequence. This orders the palette by the
 * character's idiom preference first, so the character decides which figure a
 * cycle takes while the seed still decides the sequence. The four preferences
 * are distinct permutations and stay distinct once truncated to a three-figure
 * palette, so no two characters walk the same idiom sequence; a two-figure
 * palette only admits two orders and necessarily pairs them. Severe's
 * preference is the palette's own order, which makes it the plain reference.
 *
 * @param character The subject character (selects the preference order).
 * @param palette_size Number of figures in the form's palette (>= 1).
 * @param rotation The builder's seed-driven rotation counter.
 * @return An index in [0, palette_size).
 */
std::uint8_t figureChoice(SubjectCharacter character, std::uint8_t palette_size,
                          std::uint32_t rotation);

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_CHARACTER_PROFILE_H
