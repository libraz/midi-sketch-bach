#include "composer/chord_voicing.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "composer/harmonic_plan.h"

namespace bach::composer {
namespace {

ChordEvent chord(std::uint8_t root_pc, ChordQuality q, ChordInversion inv = ChordInversion::Root) {
  ChordEvent c;
  c.root_pc = root_pc;
  c.quality = q;
  c.inversion = inv;
  c.has_degree = true;
  return c;
}

}  // namespace

TEST(ChordVoicingTest, MajorTriadHasThreeChordTones) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(0, ChordQuality::Major), &count);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(pcs[0], 0);
  EXPECT_EQ(pcs[1], 4);
  EXPECT_EQ(pcs[2], 7);
}

TEST(ChordVoicingTest, MinorTriadFlattensThird) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(2, ChordQuality::Minor), &count);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(pcs[0], 2);
  EXPECT_EQ(pcs[1], 5);  // D + 3 = F
  EXPECT_EQ(pcs[2], 9);  // D + 7 = A
}

TEST(ChordVoicingTest, DiminishedTriadFlattensFifth) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(11, ChordQuality::Diminished), &count);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(pcs[0], 11);
  EXPECT_EQ(pcs[1], 2);  // B + 3 = D
  EXPECT_EQ(pcs[2], 5);  // B + 6 = F
}

TEST(ChordVoicingTest, AugmentedTriadRaisesFifth) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(0, ChordQuality::Augmented), &count);
  EXPECT_EQ(count, 3u);
  EXPECT_EQ(pcs[2], 8);
}

TEST(ChordVoicingTest, Dominant7AddsMinorSeventh) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(7, ChordQuality::Dominant7), &count);
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(pcs[0], 7);
  EXPECT_EQ(pcs[3], 5);  // G + 10 = F
}

TEST(ChordVoicingTest, Major7AddsMajorSeventh) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(0, ChordQuality::Major7), &count);
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(pcs[3], 11);
}

TEST(ChordVoicingTest, Diminished7FullyDiminishedSeventh) {
  std::size_t count = 0;
  const auto pcs = chordPitchClasses(chord(11, ChordQuality::Diminished7), &count);
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(pcs[3], 8);  // B + 9 = G# (Ab)
}

TEST(ChordVoicingTest, BassPcForRootInversionEqualsRoot) {
  const auto c = chord(5, ChordQuality::Major, ChordInversion::Root);
  EXPECT_EQ(bassPitchClassFor(c), 5);
}

TEST(ChordVoicingTest, BassPcForFirstInversionEqualsThird) {
  const auto c = chord(0, ChordQuality::Major, ChordInversion::First);
  EXPECT_EQ(bassPitchClassFor(c), 4);
}

TEST(ChordVoicingTest, BassPcForSecondInversionEqualsFifth) {
  const auto c = chord(0, ChordQuality::Major, ChordInversion::Second);
  EXPECT_EQ(bassPitchClassFor(c), 7);
}

TEST(ChordVoicingTest, BassPcForThirdInversionEqualsSeventh) {
  const auto c = chord(7, ChordQuality::Dominant7, ChordInversion::Third);
  EXPECT_EQ(bassPitchClassFor(c), 5);
}

TEST(ChordVoicingTest, LeadingTonePcForCMajorIsB) {
  EXPECT_EQ(leadingTonePitchClass(0), 11);
}

TEST(ChordVoicingTest, LeadingTonePcForGMajorIsFSharp) {
  EXPECT_EQ(leadingTonePitchClass(7), 6);
}

TEST(ChordVoicingTest, IsLeadingTonePcIgnoresOctaveBit) {
  EXPECT_TRUE(isLeadingTonePc(83, 0));  // B5
  EXPECT_TRUE(isLeadingTonePc(11, 0));
  EXPECT_FALSE(isLeadingTonePc(67, 0));  // G5
}

TEST(ChordVoicingTest, ClassifySixFourCadentialWhenNextIsDominant) {
  // I 6/4 → V resolution. Next chord is V (degree V), so Cadential.
  const auto i64 = chord(0, ChordQuality::Major, ChordInversion::Second);
  ChordEvent v = chord(7, ChordQuality::Major);
  v.degree = RomanNumeral::V;
  EXPECT_EQ(classifySixFour(/*prev=*/nullptr, i64, &v), SixFourType::Cadential);
}

TEST(ChordVoicingTest, ClassifySixFourPassingWhenBassStepsThrough) {
  // I → V6/4 → I6: bass C → D → E, all step motion. V6/4 in C major has
  // bass = D (V's fifth = D).
  const ChordEvent i_root = chord(0, ChordQuality::Major, ChordInversion::Root);
  ChordEvent v64 = chord(7, ChordQuality::Major, ChordInversion::Second);
  v64.degree = RomanNumeral::V;
  const ChordEvent i_first = chord(0, ChordQuality::Major, ChordInversion::First);
  EXPECT_EQ(classifySixFour(&i_root, v64, &i_first), SixFourType::Passing);
}

TEST(ChordVoicingTest, ClassifySixFourNeighboringWhenBassStays) {
  // Pedal: bass C through I → IV6/4 → I. Each chord's bass pc is the
  // same: I bass = C, IV6/4 bass = (F + 7) = C, I bass = C.
  const ChordEvent i_root = chord(0, ChordQuality::Major, ChordInversion::Root);
  ChordEvent iv64 = chord(5, ChordQuality::Major, ChordInversion::Second);
  iv64.degree = RomanNumeral::IV;
  const ChordEvent i_root2 = chord(0, ChordQuality::Major, ChordInversion::Root);
  EXPECT_EQ(classifySixFour(&i_root, iv64, &i_root2), SixFourType::Neighboring);
}

}  // namespace bach::composer
