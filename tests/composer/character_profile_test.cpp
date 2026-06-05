#include "composer/character_profile.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "core/basic_types.h"

namespace bach::composer::detail {
namespace {

TEST(CharacterProfileTest, SevereTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Severe);
  EXPECT_EQ(prof.subject_slots[0], 0);
  EXPECT_EQ(prof.subject_slots[1], 2);
  EXPECT_EQ(prof.density_bias, -1);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 0);
}

TEST(CharacterProfileTest, PlayfulTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Playful);
  EXPECT_EQ(prof.subject_slots[0], 1);
  EXPECT_EQ(prof.subject_slots[1], 4);
  EXPECT_EQ(prof.density_bias, 0);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_TRUE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 2);
}

TEST(CharacterProfileTest, NobleTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Noble);
  EXPECT_EQ(prof.subject_slots[0], 0);
  EXPECT_EQ(prof.subject_slots[1], 3);
  EXPECT_EQ(prof.density_bias, -1);
  EXPECT_TRUE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_FALSE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 1);
}

TEST(CharacterProfileTest, RestlessTable) {
  const CharacterProfile& prof = characterProfile(SubjectCharacter::Restless);
  EXPECT_EQ(prof.subject_slots[0], 2);
  EXPECT_EQ(prof.subject_slots[1], 4);
  EXPECT_EQ(prof.density_bias, 1);
  EXPECT_FALSE(prof.prefer_dotted);
  EXPECT_FALSE(prof.prefer_syncopation);
  EXPECT_TRUE(prof.prefer_chromatic_runs);
  EXPECT_EQ(prof.ornament_density, 1);
}

TEST(CharacterProfileTest, AllSlotsInCatalogRange) {
  const SubjectCharacter chars[4] = {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                     SubjectCharacter::Noble, SubjectCharacter::Restless};
  for (SubjectCharacter chr : chars) {
    const CharacterProfile& prof = characterProfile(chr);
    EXPECT_LT(prof.subject_slots[0], 5);
    EXPECT_LT(prof.subject_slots[1], 5);
  }
}

TEST(CharacterProfileTest, SubjectSlotForPicksFromAllowedPair) {
  const SubjectCharacter chars[4] = {SubjectCharacter::Severe, SubjectCharacter::Playful,
                                     SubjectCharacter::Noble, SubjectCharacter::Restless};
  for (SubjectCharacter chr : chars) {
    const CharacterProfile& prof = characterProfile(chr);
    for (std::uint32_t seed = 0; seed < 64; ++seed) {
      const std::uint8_t slot = subjectSlotFor(chr, seed);
      EXPECT_TRUE(slot == prof.subject_slots[0] || slot == prof.subject_slots[1])
          << "seed " << seed;
    }
  }
}

TEST(CharacterProfileTest, SubjectSlotForDeterministicAndSelectsBoth) {
  // (seed/4)%2: seeds 0..3 -> pick 0, seeds 4..7 -> pick 1.
  EXPECT_EQ(subjectSlotFor(SubjectCharacter::Severe, 0), 0);
  EXPECT_EQ(subjectSlotFor(SubjectCharacter::Severe, 3), 0);
  EXPECT_EQ(subjectSlotFor(SubjectCharacter::Severe, 4), 2);
  EXPECT_EQ(subjectSlotFor(SubjectCharacter::Severe, 7), 2);
  // Determinism: repeated calls agree.
  EXPECT_EQ(subjectSlotFor(SubjectCharacter::Noble, 99),
            subjectSlotFor(SubjectCharacter::Noble, 99));
}

}  // namespace
}  // namespace bach::composer::detail
