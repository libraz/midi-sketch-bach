#ifndef BACH_COMPOSER_CHARACTER_PROFILE_H
#define BACH_COMPOSER_CHARACTER_PROFILE_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach::composer::detail {

// Per-character composition profile. A SubjectCharacter selects (a) which two
// subject slots of the 5-slot subject catalogs (kSubjectPatterns /
// kSubjectsMinor) it may draw from, (b) a density bias applied to the arc
// density tier, and (c) a small set of rhythmic / ornamental preferences the
// upcoming per-form builders and the ornament pass consume. Pure data; no
// behaviour beyond the deterministic slot pick below.
struct CharacterProfile {
  std::uint8_t subject_slots[2];  // allowed indices into the 5-slot subject catalogs.
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
 * @brief Deterministically pick one of the character's two subject slots.
 *
 * Mirrors the existing (seed/4)%N subject-derivation style: the choice is
 * (seed / 4) % 2 mapped through the character's `subject_slots`, so the result
 * is always one of the two allowed slot indices and is stable per seed.
 *
 * @param character The subject character (selects the 2-slot pair).
 * @param seed The piece seed.
 * @return One of the character's two subject-catalog slot indices (0..4).
 */
std::uint8_t subjectSlotFor(SubjectCharacter character, std::uint32_t seed);

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_CHARACTER_PROFILE_H
