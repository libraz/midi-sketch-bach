#include "composer/character_profile.h"

namespace bach::composer::detail {

namespace {

// Static profile table, indexed by SubjectCharacter enumerator value
// (Severe=0, Playful=1, Noble=2, Restless=3).
constexpr CharacterProfile kProfiles[4] = {
    // Severe: lean, no rhythmic embellishment, no ornaments.
    {{0, 2}, -1, false, false, false, 0},
    // Playful: syncopated, ornament-rich.
    {{1, 4}, 0, false, true, false, 2},
    // Noble: dotted, dignified, moderate ornamentation.
    {{0, 3}, -1, true, false, false, 1},
    // Restless: dense, chromatic runs, moderate ornamentation.
    {{2, 4}, 1, false, false, true, 1},
};

}  // namespace

const CharacterProfile& characterProfile(SubjectCharacter character) {
  return kProfiles[static_cast<std::uint8_t>(character)];
}

std::uint8_t subjectSlotFor(SubjectCharacter character, std::uint32_t seed) {
  const CharacterProfile& profile = characterProfile(character);
  const std::uint32_t pick = (seed / 4u) % 2u;
  return profile.subject_slots[pick];
}

}  // namespace bach::composer::detail
