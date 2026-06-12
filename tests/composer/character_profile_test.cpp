#include "composer/character_profile.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <set>

#include "composer/subject_catalog.h"
#include "core/basic_types.h"

namespace bach::composer::detail {
namespace {

constexpr SubjectCharacter kCharacters[4] = {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                             SubjectCharacter::Noble, SubjectCharacter::Restless};

TEST(CharacterProfileTest, SevereTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Severe);
  EXPECT_EQ(prof.density_bias, -1);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 0);
}

TEST(CharacterProfileTest, PlayfulTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Playful);
  EXPECT_EQ(prof.density_bias, 0);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_TRUE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 2);
}

TEST(CharacterProfileTest, NobleTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Noble);
  EXPECT_EQ(prof.density_bias, -1);
  EXPECT_TRUE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 1);
}

TEST(CharacterProfileTest, RestlessTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Restless);
  EXPECT_EQ(prof.density_bias, 1);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_TRUE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 1);
}

// Collect every index a character reaches across one full cycle of seed
// blocks (the pick is (seed/4) % class_size, so 4 * catalog_size seeds cover
// any class list at least once).
std::set<std::uint8_t> reachableIndices(SubjectCharacter character, bool minor_mode) {
  std::set<std::uint8_t> indices;
  const std::uint32_t max_seed =
      4u * static_cast<std::uint32_t>(tables::kSubjectCatalogMajor.size());
  for (std::uint32_t seed = 0; seed < max_seed; ++seed) {
    indices.insert(subjectIndexFor(character, minor_mode, seed));
  }
  return indices;
}

TEST(CharacterProfileTest, SubjectIndexForStaysInCatalogRange) {
  for (SubjectCharacter chr : kCharacters) {
    for (bool minor_mode : {false, true}) {
      const std::size_t catalog_size =
          minor_mode ? tables::kSubjectCatalogMinor.size() : tables::kSubjectCatalogMajor.size();
      for (const std::uint8_t index : reachableIndices(chr, minor_mode)) {
        EXPECT_LT(index, catalog_size) << "minor=" << minor_mode;
      }
    }
  }
}

TEST(CharacterProfileTest, LegacyAnchorSlotsRemainReachable) {
  // Each character's feature class contains its legacy two-slot pair, so the
  // shipped subject pairing stays reachable after the catalog generalization.
  const std::uint8_t legacy_pairs[4][2] = {{0, 2}, {1, 4}, {0, 3}, {2, 4}};
  for (int c = 0; c < 4; ++c) {
    for (bool minor_mode : {false, true}) {
      const std::set<std::uint8_t> indices = reachableIndices(kCharacters[c], minor_mode);
      EXPECT_TRUE(indices.count(legacy_pairs[c][0])) << "character " << c;
      EXPECT_TRUE(indices.count(legacy_pairs[c][1])) << "character " << c;
      // At minimum the legacy pair (the catalog may carry only the shipped
      // anchors when the synthesized entries are retired).
      EXPECT_GE(indices.size(), 2u) << "character " << c;
    }
  }
}

TEST(CharacterProfileTest, SubjectIndexForIsStableWithinSeedBlock) {
  // (seed/4) blocks: seeds 4k..4k+3 share one pick, and consecutive blocks
  // walk the class list deterministically.
  for (SubjectCharacter chr : kCharacters) {
    for (bool minor_mode : {false, true}) {
      for (std::uint32_t block = 0; block < 16; ++block) {
        const std::uint8_t first = subjectIndexFor(chr, minor_mode, block * 4u);
        for (std::uint32_t offset = 1; offset < 4; ++offset) {
          EXPECT_EQ(subjectIndexFor(chr, minor_mode, block * 4u + offset), first)
              << "block " << block;
        }
      }
      EXPECT_EQ(subjectIndexFor(chr, minor_mode, 99), subjectIndexFor(chr, minor_mode, 99));
    }
  }
}

}  // namespace
}  // namespace bach::composer::detail
