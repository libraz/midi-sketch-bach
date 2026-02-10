// Tests for harmony/harmonic_function.h -- function classification, secondary dominants,
// Neapolitan sixths, and functional progression validation.

#include "harmony/harmonic_function.h"

#include <gtest/gtest.h>

#include "harmony/chord_types.h"
#include "harmony/key.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// classifyFunction
// ---------------------------------------------------------------------------

TEST(HarmonicFunctionTest, ClassifyI_IsTonic) {
  EXPECT_EQ(classifyFunction(ChordDegree::I, false), HarmonicFunction::Tonic);
  EXPECT_EQ(classifyFunction(ChordDegree::I, true), HarmonicFunction::Tonic);
}

TEST(HarmonicFunctionTest, ClassifyV_IsDominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::V, false), HarmonicFunction::Dominant);
}

TEST(HarmonicFunctionTest, ClassifyIV_IsSubdominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::IV, false), HarmonicFunction::Subdominant);
}

TEST(HarmonicFunctionTest, Classifyii_IsSubdominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::ii, false), HarmonicFunction::Subdominant);
}

TEST(HarmonicFunctionTest, Classifyiii_IsMediant) {
  EXPECT_EQ(classifyFunction(ChordDegree::iii, false), HarmonicFunction::Mediant);
}

TEST(HarmonicFunctionTest, Classifyvi_IsTonic) {
  EXPECT_EQ(classifyFunction(ChordDegree::vi, false), HarmonicFunction::Tonic);
}

TEST(HarmonicFunctionTest, ClassifyviiDim_IsDominant) {
  EXPECT_EQ(classifyFunction(ChordDegree::viiDim, false), HarmonicFunction::Dominant);
}

// ---------------------------------------------------------------------------
// harmonicFunctionToString
// ---------------------------------------------------------------------------

TEST(HarmonicFunctionTest, ToStringTonic) {
  EXPECT_STREQ(harmonicFunctionToString(HarmonicFunction::Tonic), "Tonic");
}

TEST(HarmonicFunctionTest, ToStringDominant) {
  EXPECT_STREQ(harmonicFunctionToString(HarmonicFunction::Dominant), "Dominant");
}

// ---------------------------------------------------------------------------
// createSecondaryDominant
// ---------------------------------------------------------------------------

TEST(SecondaryDominantTest, V_of_V_InCMajor) {
  KeySignature key{Key::C, false};
  Chord chord = createSecondaryDominant(ChordDegree::V, key);
  EXPECT_EQ(chord.quality, ChordQuality::Dominant7);
  // V/V in C = D major. D = pitch class 2.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 2);
}

TEST(SecondaryDominantTest, V_of_vi_InCMajor) {
  KeySignature key{Key::C, false};
  Chord chord = createSecondaryDominant(ChordDegree::vi, key);
  EXPECT_EQ(chord.quality, ChordQuality::Dominant7);
  // V/vi in C = E major. E = pitch class 4.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 4);
}

TEST(SecondaryDominantTest, V_of_IV_InCMajor) {
  KeySignature key{Key::C, false};
  Chord chord = createSecondaryDominant(ChordDegree::IV, key);
  EXPECT_EQ(chord.quality, ChordQuality::Dominant7);
  // V/IV in C = C major (root = tonic). C = pitch class 0.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 0);
}

TEST(SecondaryDominantTest, V_of_ii_InCMajor) {
  KeySignature key{Key::C, false};
  Chord chord = createSecondaryDominant(ChordDegree::ii, key);
  EXPECT_EQ(chord.quality, ChordQuality::Dominant7);
  // V/ii in C = A major. A = pitch class 9.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 9);
}

// ---------------------------------------------------------------------------
// createNeapolitanSixth
// ---------------------------------------------------------------------------

TEST(NeapolitanTest, CMajor_bII) {
  KeySignature key{Key::C, false};
  Chord chord = createNeapolitanSixth(key);
  EXPECT_EQ(chord.quality, ChordQuality::Major);
  EXPECT_EQ(chord.inversion, 1);
  // bII in C = Db major. Db = pitch class 1.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 1);
}

TEST(NeapolitanTest, AMinor_bII) {
  KeySignature key{Key::A, true};
  Chord chord = createNeapolitanSixth(key);
  EXPECT_EQ(chord.quality, ChordQuality::Major);
  EXPECT_EQ(chord.inversion, 1);
  // bII in A minor = Bb major. Bb = pitch class 10.
  int root_pc = chord.root_pitch % 12;
  EXPECT_EQ(root_pc, 10);
}

// ---------------------------------------------------------------------------
// isValidFunctionalProgression
// ---------------------------------------------------------------------------

TEST(FunctionalProgressionTest, TonicToDominant_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Tonic, HarmonicFunction::Dominant));
}

TEST(FunctionalProgressionTest, DominantToTonic_IsValid) {
  EXPECT_TRUE(isValidFunctionalProgression(HarmonicFunction::Dominant, HarmonicFunction::Tonic));
}

TEST(FunctionalProgressionTest, DominantToSubdominant_IsInvalid) {
  EXPECT_FALSE(
      isValidFunctionalProgression(HarmonicFunction::Dominant, HarmonicFunction::Subdominant));
}

TEST(FunctionalProgressionTest, MediantToAnything_IsValid) {
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Mediant, HarmonicFunction::Dominant));
  EXPECT_TRUE(
      isValidFunctionalProgression(HarmonicFunction::Mediant, HarmonicFunction::Subdominant));
}

}  // namespace
}  // namespace bach
