#include "composer/character_profile.h"

#include <cstddef>

#include "composer/subject_catalog.h"

namespace bach::composer::detail {

namespace {

// Static profile table, indexed by SubjectCharacter enumerator value
// (Severe=0, Playful=1, Noble=2, Restless=3).
constexpr CharacterProfile kProfiles[4] = {
    // Severe: lean, no rhythmic embellishment, no ornaments.
    {-1, false, false, false, 0},
    // Playful: syncopated, ornament-rich.
    {0, false, true, false, 2},
    // Noble: dotted, dignified, moderate ornamentation.
    {-1, true, false, false, 1},
    // Restless: dense, chromatic runs, moderate ornamentation.
    {1, false, false, true, 1},
};

// View over one character's catalog-index class array (subject_catalog.inc).
struct SubjectClass {
  const std::uint8_t* data;
  std::size_t size;
};

template <std::size_t N>
constexpr SubjectClass classOf(const std::array<std::uint8_t, N>& indices) {
  return {indices.data(), N};
}

// Indexed by SubjectCharacter enumerator value, one table per mode.
constexpr SubjectClass kClassesMajor[4] = {
    classOf(tables::kSubjectClassSevereMajor),
    classOf(tables::kSubjectClassPlayfulMajor),
    classOf(tables::kSubjectClassNobleMajor),
    classOf(tables::kSubjectClassRestlessMajor),
};
constexpr SubjectClass kClassesMinor[4] = {
    classOf(tables::kSubjectClassSevereMinor),
    classOf(tables::kSubjectClassPlayfulMinor),
    classOf(tables::kSubjectClassNobleMinor),
    classOf(tables::kSubjectClassRestlessMinor),
};

}  // namespace

const CharacterProfile& characterProfile(SubjectCharacter character) {
  return kProfiles[static_cast<std::uint8_t>(character)];
}

std::uint8_t subjectIndexFor(SubjectCharacter character, bool minor_mode, std::uint32_t seed) {
  const SubjectClass& cls =
      (minor_mode ? kClassesMinor : kClassesMajor)[static_cast<std::uint8_t>(character)];
  const std::size_t pick = static_cast<std::size_t>(seed / 4u) % cls.size;
  return cls.data[pick];
}

}  // namespace bach::composer::detail
