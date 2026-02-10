// Tests for solo_string/arch/variation_types.h -- variation role/type utilities.

#include "solo_string/arch/variation_types.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

// ===========================================================================
// getAllowedTypes
// ===========================================================================

TEST(VariationTypesTest, EstablishAllowsThemeAndLyrical) {
  auto types = getAllowedTypes(VariationRole::Establish);
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], VariationType::Theme);
  EXPECT_EQ(types[1], VariationType::Lyrical);
}

TEST(VariationTypesTest, DevelopAllowsRhythmicAndLyrical) {
  auto types = getAllowedTypes(VariationRole::Develop);
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], VariationType::Rhythmic);
  EXPECT_EQ(types[1], VariationType::Lyrical);
}

TEST(VariationTypesTest, DestabilizeAllowsVirtuosicAndRhythmic) {
  auto types = getAllowedTypes(VariationRole::Destabilize);
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], VariationType::Virtuosic);
  EXPECT_EQ(types[1], VariationType::Rhythmic);
}

TEST(VariationTypesTest, IlluminateAllowsLyricalAndChordal) {
  auto types = getAllowedTypes(VariationRole::Illuminate);
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], VariationType::Lyrical);
  EXPECT_EQ(types[1], VariationType::Chordal);
}

TEST(VariationTypesTest, AccumulateAllowsVirtuosicAndChordal) {
  auto types = getAllowedTypes(VariationRole::Accumulate);
  ASSERT_EQ(types.size(), 2u);
  EXPECT_EQ(types[0], VariationType::Virtuosic);
  EXPECT_EQ(types[1], VariationType::Chordal);
}

TEST(VariationTypesTest, ResolveAllowsOnlyTheme) {
  auto types = getAllowedTypes(VariationRole::Resolve);
  ASSERT_EQ(types.size(), 1u);
  EXPECT_EQ(types[0], VariationType::Theme);
}

// ===========================================================================
// isTypeAllowedForRole -- positive cases
// ===========================================================================

TEST(VariationTypesTest, ThemeAllowedForEstablish) {
  EXPECT_TRUE(isTypeAllowedForRole(VariationType::Theme, VariationRole::Establish));
}

TEST(VariationTypesTest, LyricalAllowedForEstablish) {
  EXPECT_TRUE(isTypeAllowedForRole(VariationType::Lyrical, VariationRole::Establish));
}

TEST(VariationTypesTest, VirtuosicAllowedForAccumulate) {
  EXPECT_TRUE(isTypeAllowedForRole(VariationType::Virtuosic, VariationRole::Accumulate));
}

TEST(VariationTypesTest, ChordalAllowedForAccumulate) {
  EXPECT_TRUE(isTypeAllowedForRole(VariationType::Chordal, VariationRole::Accumulate));
}

TEST(VariationTypesTest, ThemeAllowedForResolve) {
  EXPECT_TRUE(isTypeAllowedForRole(VariationType::Theme, VariationRole::Resolve));
}

// ===========================================================================
// isTypeAllowedForRole -- negative cases
// ===========================================================================

TEST(VariationTypesTest, VirtuosicNotAllowedForEstablish) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Virtuosic, VariationRole::Establish));
}

TEST(VariationTypesTest, ChordalNotAllowedForEstablish) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Chordal, VariationRole::Establish));
}

TEST(VariationTypesTest, RhythmicNotAllowedForEstablish) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Rhythmic, VariationRole::Establish));
}

TEST(VariationTypesTest, LyricalNotAllowedForResolve) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Lyrical, VariationRole::Resolve));
}

TEST(VariationTypesTest, VirtuosicNotAllowedForResolve) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Virtuosic, VariationRole::Resolve));
}

TEST(VariationTypesTest, RhythmicNotAllowedForResolve) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Rhythmic, VariationRole::Resolve));
}

TEST(VariationTypesTest, ChordalNotAllowedForResolve) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Chordal, VariationRole::Resolve));
}

TEST(VariationTypesTest, ThemeNotAllowedForDevelop) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Theme, VariationRole::Develop));
}

TEST(VariationTypesTest, ThemeNotAllowedForAccumulate) {
  EXPECT_FALSE(isTypeAllowedForRole(VariationType::Theme, VariationRole::Accumulate));
}

// ===========================================================================
// String conversions
// ===========================================================================

TEST(VariationTypesTest, TextureTypeToStringCoversAll) {
  EXPECT_STREQ(textureTypeToString(TextureType::SingleLine), "SingleLine");
  EXPECT_STREQ(textureTypeToString(TextureType::ImpliedPolyphony), "ImpliedPolyphony");
  EXPECT_STREQ(textureTypeToString(TextureType::FullChords), "FullChords");
  EXPECT_STREQ(textureTypeToString(TextureType::Arpeggiated), "Arpeggiated");
  EXPECT_STREQ(textureTypeToString(TextureType::ScalePassage), "ScalePassage");
  EXPECT_STREQ(textureTypeToString(TextureType::Bariolage), "Bariolage");
}

TEST(VariationTypesTest, VariationTypeToStringCoversAll) {
  EXPECT_STREQ(variationTypeToString(VariationType::Theme), "Theme");
  EXPECT_STREQ(variationTypeToString(VariationType::Lyrical), "Lyrical");
  EXPECT_STREQ(variationTypeToString(VariationType::Rhythmic), "Rhythmic");
  EXPECT_STREQ(variationTypeToString(VariationType::Virtuosic), "Virtuosic");
  EXPECT_STREQ(variationTypeToString(VariationType::Chordal), "Chordal");
}

// ===========================================================================
// isRoleOrderValid -- valid sequences
// ===========================================================================

TEST(VariationTypesTest, StandardOrderIsValid) {
  // The standard chaconne order:
  // Establish, Develop, Destabilize, Illuminate, Illuminate,
  // Destabilize, Accumulate, Accumulate, Accumulate, Resolve
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_TRUE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, MinimalValidOrder) {
  // Minimum valid: Establish, Develop, Destabilize, Illuminate,
  //                Destabilize, Accumulate x3, Resolve
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_TRUE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, DestabilizeAllowedBeforeAndAfterIlluminate) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Destabilize,   // Two before Illuminate
      VariationRole::Illuminate,
      VariationRole::Destabilize,   // One after Illuminate
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_TRUE(isRoleOrderValid(roles));
}

// ===========================================================================
// isRoleOrderValid -- invalid sequences
// ===========================================================================

TEST(VariationTypesTest, ReversedOrderIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Resolve,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Develop,
      VariationRole::Establish};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, EmptyRolesIsInvalid) {
  std::vector<VariationRole> empty;
  EXPECT_FALSE(isRoleOrderValid(empty));
}

TEST(VariationTypesTest, TwoAccumulateIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, FourAccumulateIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, ZeroAccumulateIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Resolve};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, ResolveNotAtEndIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Resolve,  // Resolve in wrong position
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, DevelopBeforeEstablishIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Develop,      // Should come after Establish
      VariationRole::Establish,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, AccumulateBeforeIlluminateIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Accumulate,   // Too early
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Resolve};
  EXPECT_FALSE(isRoleOrderValid(roles));
}

TEST(VariationTypesTest, MultipleResolveIsInvalid) {
  std::vector<VariationRole> roles = {
      VariationRole::Establish,
      VariationRole::Develop,
      VariationRole::Destabilize,
      VariationRole::Illuminate,
      VariationRole::Destabilize,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Accumulate,
      VariationRole::Resolve,
      VariationRole::Resolve};  // Two Resolves
  EXPECT_FALSE(isRoleOrderValid(roles));
}

}  // namespace
}  // namespace bach
