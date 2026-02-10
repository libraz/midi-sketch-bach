// Tests for harmony/chord_function.h -- function classification with new enum values,
// secondary dominant targets, and functional progression validation including Applied.

#include "harmony/chord_function.h"

#include <gtest/gtest.h>

#include "harmony/chord_types.h"
#include "harmony/harmonic_function.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// classifyFunction -- all degrees including new ones
// ---------------------------------------------------------------------------

TEST(ChordFunctionTest, ClassifyI_IsTonic) {
  EXPECT_EQ(classifyFunction(ChordDegree::I, false), HarmonicFunction::Tonic);
  EXPECT_EQ(classifyFunction(ChordDegree::I, true), HarmonicFunction::Tonic);
}

TEST(ChordFunctionTest, ClassifyV_IsDominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::V, false), HarmonicFunction::Dominant);
  EXPECT_EQ(classifyFunction(ChordDegree::V, true), HarmonicFunction::Dominant);
}

TEST(ChordFunctionTest, ClassifyIV_IsSubdominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::IV, false), HarmonicFunction::Subdominant);
}

TEST(ChordFunctionTest, Classifyii_IsSubdominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::ii, false), HarmonicFunction::Subdominant);
}

TEST(ChordFunctionTest, ClassifyviiDim_IsDominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::viiDim, false), HarmonicFunction::Dominant);
}

TEST(ChordFunctionTest, Classifyvi_IsTonic) {
  EXPECT_EQ(classifyFunction(ChordDegree::vi, false), HarmonicFunction::Tonic);
}

TEST(ChordFunctionTest, Classifyiii_IsMediant) {
  EXPECT_EQ(classifyFunction(ChordDegree::iii, false), HarmonicFunction::Mediant);
}

TEST(ChordFunctionTest, ClassifybII_IsSubdominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::bII, false), HarmonicFunction::Subdominant);
}

TEST(ChordFunctionTest, ClassifybVI_IsMediant) {
  EXPECT_EQ(classifyFunction(ChordDegree::bVI, false), HarmonicFunction::Mediant);
}

TEST(ChordFunctionTest, ClassifybVII_IsMediant) {
  EXPECT_EQ(classifyFunction(ChordDegree::bVII, false), HarmonicFunction::Mediant);
}

TEST(ChordFunctionTest, ClassifybIII_IsMediant) {
  EXPECT_EQ(classifyFunction(ChordDegree::bIII, false), HarmonicFunction::Mediant);
}

TEST(ChordFunctionTest, ClassifyV_of_V_IsApplied) {
  EXPECT_EQ(classifyFunction(ChordDegree::V_of_V, false), HarmonicFunction::Applied);
}

TEST(ChordFunctionTest, ClassifyV_of_vi_IsApplied) {
  EXPECT_EQ(classifyFunction(ChordDegree::V_of_vi, false), HarmonicFunction::Applied);
}

TEST(ChordFunctionTest, ClassifyV_of_IV_IsApplied) {
  EXPECT_EQ(classifyFunction(ChordDegree::V_of_IV, false), HarmonicFunction::Applied);
}

TEST(ChordFunctionTest, ClassifyV_of_ii_IsApplied) {
  EXPECT_EQ(classifyFunction(ChordDegree::V_of_ii, false), HarmonicFunction::Applied);
}

TEST(ChordFunctionTest, ClassifyV_of_iii_IsApplied) {
  EXPECT_EQ(classifyFunction(ChordDegree::V_of_iii, false), HarmonicFunction::Applied);
}

// ---------------------------------------------------------------------------
// isValidFunctionalProgression
// ---------------------------------------------------------------------------

TEST(ChordFunctionProgressionTest, TonicToDominant_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Tonic, HarmonicFunction::Dominant));
}

TEST(ChordFunctionProgressionTest, DominantToTonic_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Dominant, HarmonicFunction::Tonic));
}

TEST(ChordFunctionProgressionTest, SubdominantToDominant_IsValid) {
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Subdominant, HarmonicFunction::Dominant));
}

TEST(ChordFunctionProgressionTest, DominantToSubdominant_IsInvalid) {
  EXPECT_FALSE(
      isValidFunctionalProgression(HarmonicFunction::Dominant, HarmonicFunction::Subdominant));
}

TEST(ChordFunctionProgressionTest, AppliedToAnything_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Applied, HarmonicFunction::Tonic));
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Applied, HarmonicFunction::Dominant));
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Applied, HarmonicFunction::Subdominant));
}

TEST(ChordFunctionProgressionTest, AnythingToApplied_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Tonic, HarmonicFunction::Applied));
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Subdominant, HarmonicFunction::Applied));
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Dominant, HarmonicFunction::Applied));
}

TEST(ChordFunctionProgressionTest, MediantToAnything_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Mediant, HarmonicFunction::Dominant));
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Mediant, HarmonicFunction::Subdominant));
}

// ---------------------------------------------------------------------------
// isValidDegreeProgression
// ---------------------------------------------------------------------------

TEST(DegreeProgressionTest, I_to_V_IsValid) {
  EXPECT_TRUE(isValidDegreeProgression(ChordDegree::I, ChordDegree::V));
}

TEST(DegreeProgressionTest, V_to_I_IsValid) {
  EXPECT_TRUE(isValidDegreeProgression(ChordDegree::V, ChordDegree::I));
}

TEST(DegreeProgressionTest, V_to_IV_IsInvalid) {
  EXPECT_FALSE(isValidDegreeProgression(ChordDegree::V, ChordDegree::IV));
}

TEST(DegreeProgressionTest, I_to_bVI_IsValid) {
  // bVI is Mediant, I is Tonic -- T->M is valid
  EXPECT_TRUE(isValidDegreeProgression(ChordDegree::I, ChordDegree::bVI));
}

TEST(DegreeProgressionTest, I_to_V_of_V_IsValid) {
  // V/V is Applied, I is Tonic -- T->Applied is valid
  EXPECT_TRUE(isValidDegreeProgression(ChordDegree::I, ChordDegree::V_of_V));
}

TEST(DegreeProgressionTest, ii_to_V_IsValid) {
  EXPECT_TRUE(isValidDegreeProgression(ChordDegree::ii, ChordDegree::V));
}

// ---------------------------------------------------------------------------
// getSecondaryDominantTarget
// ---------------------------------------------------------------------------

TEST(SecondaryDominantTargetTest, V_of_V_ResolvesToV) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::V_of_V), ChordDegree::V);
}

TEST(SecondaryDominantTargetTest, V_of_vi_ResolvesTovi) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::V_of_vi), ChordDegree::vi);
}

TEST(SecondaryDominantTargetTest, V_of_IV_ResolvesToIV) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::V_of_IV), ChordDegree::IV);
}

TEST(SecondaryDominantTargetTest, V_of_ii_ResolvesToii) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::V_of_ii), ChordDegree::ii);
}

TEST(SecondaryDominantTargetTest, V_of_iii_ResolvesToiii) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::V_of_iii), ChordDegree::iii);
}

TEST(SecondaryDominantTargetTest, NonSecondaryDominant_ReturnsI) {
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::I), ChordDegree::I);
  EXPECT_EQ(getSecondaryDominantTarget(ChordDegree::IV), ChordDegree::I);
}

// ---------------------------------------------------------------------------
// isSecondaryDominant
// ---------------------------------------------------------------------------

TEST(IsSecondaryDominantTest, AllSecondaryDominants) {
  EXPECT_TRUE(isSecondaryDominant(ChordDegree::V_of_V));
  EXPECT_TRUE(isSecondaryDominant(ChordDegree::V_of_vi));
  EXPECT_TRUE(isSecondaryDominant(ChordDegree::V_of_IV));
  EXPECT_TRUE(isSecondaryDominant(ChordDegree::V_of_ii));
  EXPECT_TRUE(isSecondaryDominant(ChordDegree::V_of_iii));
}

TEST(IsSecondaryDominantTest, NonSecondaryDominants) {
  EXPECT_FALSE(isSecondaryDominant(ChordDegree::I));
  EXPECT_FALSE(isSecondaryDominant(ChordDegree::V));
  EXPECT_FALSE(isSecondaryDominant(ChordDegree::IV));
  EXPECT_FALSE(isSecondaryDominant(ChordDegree::bVI));
  EXPECT_FALSE(isSecondaryDominant(ChordDegree::bII));
}

// ---------------------------------------------------------------------------
// harmonicFunctionToString -- verify Applied string
// ---------------------------------------------------------------------------

TEST(ChordFunctionStringTest, AppliedToString) {
  EXPECT_STREQ(harmonicFunctionToString(HarmonicFunction::Applied), "Applied");
}

}  // namespace
}  // namespace bach
