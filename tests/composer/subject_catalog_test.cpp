#include "composer/subject_catalog.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>

#include "composer/figuration.h"
#include "composer/minor_material.h"

namespace bach::composer {
namespace {

using tables::kSubjectCatalogMajor;
using tables::kSubjectCatalogMajorRhythms;
using tables::kSubjectCatalogMinor;
using tables::kSubjectCatalogMinorRhythms;

constexpr int kSubjectTicks = 16 * kTicksPerBeat;  // four 4/4 bars
constexpr std::uint8_t kTailLeadingTone = 71;
constexpr std::uint8_t kTailTonic = 72;

// Adjacent-interval whitelist. The synthesized entries use the verified
// minor_material.h contract ({1..5, 7, 8, 12} plus bounded repeats); the
// shipped major catalog additionally walks a diminished fifth into the
// leading-tone tail (77 -> 71 in row 0), so 6 is admitted catalog-wide.
bool intervalAllowed(int magnitude) {
  static const std::set<int> kAllowed = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12};
  return kAllowed.count(magnitude) > 0;
}

bool pitchClassDiatonic(std::uint8_t pitch, bool minor) {
  static const std::set<int> kMajor = {0, 2, 4, 5, 7, 9, 11};
  // C natural minor for the free body; the leading tone (pc 11) is reserved
  // for the fixed cadential tail.
  static const std::set<int> kMinor = {0, 2, 3, 5, 7, 8, 10};
  const int pc = pitch % 12;
  return minor ? kMinor.count(pc) > 0 : kMajor.count(pc) > 0;
}

template <std::size_t N>
void expectSubjectInvariants(const std::array<std::array<std::uint8_t, 16>, N>& pitches,
                             const std::array<std::array<Tick, 16>, N>& rhythms, bool minor,
                             std::uint8_t body_low, std::uint8_t body_high) {
  for (std::size_t row = 0; row < N; ++row) {
    const auto& subject = pitches[row];
    // Fixed leading-tone tail: answer (-5), re-entry (-12), and stretto
    // (-24) transpositions stay valid for every catalog entry.
    EXPECT_EQ(subject[14], kTailLeadingTone) << "row " << row;
    EXPECT_EQ(subject[15], kTailTonic) << "row " << row;
    for (int pos = 0; pos < 14; ++pos) {
      EXPECT_TRUE(pitchClassDiatonic(subject[pos], minor))
          << "row " << row << " pos " << pos << " pitch " << int(subject[pos]);
      EXPECT_GE(subject[pos], body_low) << "row " << row << " pos " << pos;
      EXPECT_LE(subject[pos], body_high) << "row " << row << " pos " << pos;
    }
    for (int pos = 1; pos < 16; ++pos) {
      const int magnitude = subject[pos] > subject[pos - 1] ? subject[pos] - subject[pos - 1]
                                                            : subject[pos - 1] - subject[pos];
      EXPECT_TRUE(intervalAllowed(magnitude))
          << "row " << row << " pos " << pos << " interval " << magnitude;
    }
    const int opening = subject[1] > subject[0] ? subject[1] - subject[0] : subject[0] - subject[1];
    EXPECT_TRUE(opening <= 7 || opening == 12) << "row " << row;
    if (minor) {
      // No augmented second into the leading-tone tail (Ab -> B).
      EXPECT_NE(subject[13] % 12, 8) << "row " << row;
    }

    const auto& rhythm = rhythms[row];
    int total = 0;
    for (int pos = 0; pos < 16; ++pos) {
      EXPECT_GT(rhythm[pos], 0) << "row " << row << " pos " << pos;
      total += static_cast<int>(rhythm[pos]);
    }
    if (row < 5) {
      // The shipped rhythm rows deliberately vary around the nominal 4-bar
      // span (15..17 beats); pin that envelope.
      EXPECT_GE(total, 15 * kTicksPerBeat) << "row " << row;
      EXPECT_LE(total, 17 * kTicksPerBeat) << "row " << row;
    } else {
      // Synthesized entries are sampled to exactly four 4/4 bars.
      EXPECT_EQ(total, kSubjectTicks) << "row " << row;
    }
    EXPECT_GE(rhythm[15], 2 * kTicksPerBeat) << "row " << row;
  }
}

TEST(SubjectCatalog, MajorAnchorsMirrorShippedCatalog) {
  ASSERT_GE(kSubjectCatalogMajor.size(), 5u);
  ASSERT_EQ(kSubjectCatalogMajor.size(), kSubjectCatalogMajorRhythms.size());
  for (std::size_t row = 0; row < 5; ++row) {
    EXPECT_EQ(kSubjectCatalogMajor[row], detail::kFugueCompleteSubjects[row]) << "row " << row;
    EXPECT_EQ(kSubjectCatalogMajorRhythms[row], detail::kFugueCompleteSubjectRhythms[row])
        << "row " << row;
  }
}

TEST(SubjectCatalog, MinorAnchorsMirrorShippedCatalog) {
  ASSERT_GE(kSubjectCatalogMinor.size(), 5u);
  ASSERT_EQ(kSubjectCatalogMinor.size(), kSubjectCatalogMinorRhythms.size());
  for (std::size_t row = 0; row < 5; ++row) {
    EXPECT_EQ(kSubjectCatalogMinor[row], detail::kSubjectsMinor[row]) << "row " << row;
    EXPECT_EQ(kSubjectCatalogMinorRhythms[row], detail::kFugueCompleteSubjectRhythms[row])
        << "row " << row;
  }
}

TEST(SubjectCatalog, MajorEntriesHoldSubjectInvariants) {
  expectSubjectInvariants(kSubjectCatalogMajor, kSubjectCatalogMajorRhythms,
                          /*minor=*/false, /*body_low=*/71, /*body_high=*/81);
}

TEST(SubjectCatalog, MinorEntriesHoldSubjectInvariants) {
  expectSubjectInvariants(kSubjectCatalogMinor, kSubjectCatalogMinorRhythms,
                          /*minor=*/true, /*body_low=*/70, /*body_high=*/82);
}

template <std::size_t N>
void expectClassArrayValid(const std::array<std::uint8_t, N>& indices, std::size_t catalog_size,
                           std::uint8_t anchor_a, std::uint8_t anchor_b) {
  static_assert(N >= 2, "a class must hold at least its legacy slot pair");
  std::set<int> seen;
  for (std::size_t i = 0; i < N; ++i) {
    EXPECT_LT(indices[i], catalog_size) << "position " << i;
    if (i > 0) {
      // Strictly ascending implies unique entries.
      EXPECT_GT(indices[i], indices[i - 1]) << "position " << i;
    }
    seen.insert(indices[i]);
  }
  // The legacy two-slot pair stays reachable for the character.
  EXPECT_TRUE(seen.count(anchor_a));
  EXPECT_TRUE(seen.count(anchor_b));
}

TEST(SubjectCatalog, CharacterClassArraysAreValid) {
  const std::size_t major = kSubjectCatalogMajor.size();
  const std::size_t minor = kSubjectCatalogMinor.size();
  expectClassArrayValid(tables::kSubjectClassSevereMajor, major, 0, 2);
  expectClassArrayValid(tables::kSubjectClassPlayfulMajor, major, 1, 4);
  expectClassArrayValid(tables::kSubjectClassNobleMajor, major, 0, 3);
  expectClassArrayValid(tables::kSubjectClassRestlessMajor, major, 2, 4);
  expectClassArrayValid(tables::kSubjectClassSevereMinor, minor, 0, 2);
  expectClassArrayValid(tables::kSubjectClassPlayfulMinor, minor, 1, 4);
  expectClassArrayValid(tables::kSubjectClassNobleMinor, minor, 0, 3);
  expectClassArrayValid(tables::kSubjectClassRestlessMinor, minor, 2, 4);
}

TEST(SubjectCatalog, EveryCatalogEntryIsReachableBySomeCharacter) {
  // An entry no character can reach would silently shrink the effective
  // catalog (the generator rejects this at render time; pin it here too).
  for (int mode = 0; mode < 2; ++mode) {
    std::set<int> reachable;
    if (mode == 0) {
      for (std::uint8_t v : tables::kSubjectClassSevereMajor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassPlayfulMajor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassNobleMajor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassRestlessMajor)
        reachable.insert(v);
    } else {
      for (std::uint8_t v : tables::kSubjectClassSevereMinor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassPlayfulMinor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassNobleMinor)
        reachable.insert(v);
      for (std::uint8_t v : tables::kSubjectClassRestlessMinor)
        reachable.insert(v);
    }
    const std::size_t size =
        (mode == 0) ? kSubjectCatalogMajor.size() : kSubjectCatalogMinor.size();
    for (std::size_t index = 0; index < size; ++index) {
      EXPECT_TRUE(reachable.count(static_cast<int>(index)))
          << (mode == 0 ? "major" : "minor") << " entry " << index;
    }
  }
}

TEST(SubjectCatalog, NoRepeatedNoteRunsBeyondTwo) {
  for (const auto& subject : kSubjectCatalogMajor) {
    for (int pos = 2; pos < 16; ++pos) {
      EXPECT_FALSE(subject[pos] == subject[pos - 1] && subject[pos - 1] == subject[pos - 2]);
    }
  }
  for (const auto& subject : kSubjectCatalogMinor) {
    for (int pos = 2; pos < 16; ++pos) {
      EXPECT_FALSE(subject[pos] == subject[pos - 1] && subject[pos - 1] == subject[pos - 2]);
    }
  }
}

}  // namespace
}  // namespace bach::composer
