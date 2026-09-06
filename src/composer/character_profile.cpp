#include "composer/character_profile.h"

#include <cstddef>

#include "composer/subject_catalog.h"

namespace bach::composer::detail {

namespace {

// Static profile table, indexed by SubjectCharacter enumerator value
// (Severe=0, Playful=1, Noble=2, Restless=3).
constexpr CharacterProfile kProfiles[4] = {
    // Severe: lean, no rhythmic embellishment, no ornaments.
    {-1, false, 0},
    // Playful: ornament-rich.
    {0, false, 2},
    // Noble: dotted, dignified, moderate ornamentation.
    {-1, true, 1},
    // Restless: dense, moderate ornamentation.
    {1, false, 1},
};

// Figuration-idiom preference, indexed by SubjectCharacter enumerator value.
// Each row orders the slots of a form's figuration palette; the builder's seed
// rotation then walks the reordered palette.
//
// Every palette is written plainest-first, so Severe -- the austere reading --
// keeps the order as written, and the other three promote ONE idiom to the
// front rather than reordering wholesale. A character that pushed the whole
// palette away from its plain opening (a full reversal, say) front-loads the
// busiest idiom in every form at once, which costs counterpoint in the forms
// whose palette is deliberately ordered by how much motion each figure adds.
//
// The rows stay distinct once truncated to a three-slot palette. Two slots
// admit only two orders, so there they pair off; a form with that few figures
// separates its characters on density and ornamentation instead.
constexpr std::uint8_t kFigurePreference[4][4] = {
    {0, 1, 2, 3},  // Severe: the palette as written.
    {1, 0, 2, 3},  // Playful: the second idiom first, the plain one still near.
    {2, 0, 1, 3},  // Noble: the long-short cell first -- its dotted figure.
    {1, 2, 0, 3},  // Restless: the driving idioms before the plain one.
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

std::uint8_t figureChoice(SubjectCharacter character, std::uint8_t palette_size,
                          std::uint32_t rotation) {
  if (palette_size <= 1) {
    return 0;
  }
  const std::uint8_t* order = kFigurePreference[static_cast<std::uint8_t>(character)];
  const std::uint32_t position = rotation % palette_size;
  // Walk the preference and count only the slots this palette actually has, so
  // a palette shorter than four figures keeps the character's relative order
  // instead of falling back to the palette's own.
  std::uint32_t seen = 0;
  for (std::size_t idx = 0; idx < 4; ++idx) {
    if (order[idx] >= palette_size) {
      continue;
    }
    if (seen == position) {
      return order[idx];
    }
    ++seen;
  }
  // The preference covers four slots; a wider palette keeps its own order past
  // them, so every slot stays reachable whatever the size.
  for (std::uint8_t slot = 4; slot < palette_size; ++slot) {
    if (seen == position) {
      return slot;
    }
    ++seen;
  }
  return 0;  // unreachable: the loops together offer palette_size candidates.
}

std::uint8_t subjectIndexFor(SubjectCharacter character, bool minor_mode, std::uint32_t seed) {
  const SubjectClass& cls =
      (minor_mode ? kClassesMinor : kClassesMajor)[static_cast<std::uint8_t>(character)];
  const std::size_t pick = static_cast<std::size_t>(seed / 4u) % cls.size;
  return cls.data[pick];
}

}  // namespace bach::composer::detail
