// Tests for extended chord types -- augmented 6th variants, borrowed chords,
// and new ChordDegree/ChordQuality enum values.

#include <gtest/gtest.h>

#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// ChordQuality string representation -- new aug 6th variants
// ---------------------------------------------------------------------------

TEST(ExtendedChordQualityStringTest, AugSixthItalian) {
  EXPECT_STREQ(chordQualityToString(ChordQuality::AugSixthItalian), "AugSixthItalian");
}

TEST(ExtendedChordQualityStringTest, AugSixthFrench) {
  EXPECT_STREQ(chordQualityToString(ChordQuality::AugSixthFrench), "AugSixthFrench");
}

TEST(ExtendedChordQualityStringTest, AugSixthGerman) {
  EXPECT_STREQ(chordQualityToString(ChordQuality::AugSixthGerman), "AugSixthGerman");
}

// ---------------------------------------------------------------------------
// ChordDegree string representation -- new borrowed chords
// ---------------------------------------------------------------------------

TEST(ExtendedChordDegreeStringTest, bVI) {
  EXPECT_STREQ(chordDegreeToString(ChordDegree::bVI), "bVI");
}

TEST(ExtendedChordDegreeStringTest, bVII) {
  EXPECT_STREQ(chordDegreeToString(ChordDegree::bVII), "bVII");
}

TEST(ExtendedChordDegreeStringTest, bIII) {
  EXPECT_STREQ(chordDegreeToString(ChordDegree::bIII), "bIII");
}

TEST(ExtendedChordDegreeStringTest, V_of_iii) {
  EXPECT_STREQ(chordDegreeToString(ChordDegree::V_of_iii), "V/iii");
}

// ---------------------------------------------------------------------------
// isChordTone -- Italian augmented 6th
// ---------------------------------------------------------------------------

TEST(AugSixthChordToneTest, Italian_RootIsChordTone) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4 (pitch class 8)
  event.chord.quality = ChordQuality::AugSixthItalian;
  // It+6 in C: b6 (Ab), 1 (C), #4 (F#) -> semitones 0, 4, 10 from root
  EXPECT_TRUE(isChordTone(68, event));  // Ab4 (root)
}

TEST(AugSixthChordToneTest, Italian_ThirdIsChordTone) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthItalian;
  // Ab + 4 = C (pitch class 0)
  EXPECT_TRUE(isChordTone(60, event));  // C4 (third)
  EXPECT_TRUE(isChordTone(72, event));  // C5 (third, octave above)
}

TEST(AugSixthChordToneTest, Italian_AugSixthIsChordTone) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthItalian;
  // Ab + 10 = F# (pitch class 6) -- the augmented sixth above the bass
  EXPECT_TRUE(isChordTone(66, event));  // F#4
}

TEST(AugSixthChordToneTest, Italian_NonChordToneRejected) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthItalian;
  EXPECT_FALSE(isChordTone(62, event));  // D4 belongs to the French sixth only
  EXPECT_FALSE(isChordTone(63, event));  // Eb4 belongs to the German sixth only
  EXPECT_FALSE(isChordTone(64, event));  // E4
  EXPECT_FALSE(isChordTone(67, event));  // G4
}

// ---------------------------------------------------------------------------
// isChordTone -- French augmented 6th
// ---------------------------------------------------------------------------

TEST(AugSixthChordToneTest, French_AllTonesPresent) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthFrench;
  // Fr+6 in C: b6 (Ab), 1 (C), 2 (D), #4 (F#) -> semitones 0, 4, 6, 10
  EXPECT_TRUE(isChordTone(68, event));  // Ab4 (root)
  EXPECT_TRUE(isChordTone(60, event));  // C4 (+4)
  EXPECT_TRUE(isChordTone(62, event));  // D4 (+6)
  EXPECT_TRUE(isChordTone(66, event));  // F#4 (+10)
}

TEST(AugSixthChordToneTest, French_NonChordToneRejected) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthFrench;
  EXPECT_FALSE(isChordTone(64, event));  // E4
  EXPECT_FALSE(isChordTone(67, event));  // G4
  EXPECT_FALSE(isChordTone(65, event));  // F4
}

// ---------------------------------------------------------------------------
// isChordTone -- German augmented 6th
// ---------------------------------------------------------------------------

TEST(AugSixthChordToneTest, German_AllTonesPresent) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthGerman;
  // Ger+6 in C: b6 (Ab), 1 (C), b3 (Eb), #4 (F#) -> semitones 0, 4, 7, 10
  EXPECT_TRUE(isChordTone(68, event));  // Ab4 (root, pc=8)
  EXPECT_TRUE(isChordTone(60, event));  // C4 (+4, pc=0)
  EXPECT_TRUE(isChordTone(63, event));  // Eb4 (+7, pc=3)
  EXPECT_TRUE(isChordTone(66, event));  // F#4 (+10, pc=6)
}

TEST(AugSixthChordToneTest, German_NonChordToneRejected) {
  HarmonicEvent event;
  event.chord.root_pitch = 68;  // Ab4
  event.chord.quality = ChordQuality::AugSixthGerman;
  EXPECT_FALSE(isChordTone(62, event));  // D4 (pc=2)
  EXPECT_FALSE(isChordTone(61, event));  // Db4 (pc=1)
  EXPECT_FALSE(isChordTone(64, event));  // E4 (pc=4)
  EXPECT_FALSE(isChordTone(67, event));  // G4
}

// ---------------------------------------------------------------------------
// majorKeyQuality -- borrowed chords
// ---------------------------------------------------------------------------

TEST(BorrowedChordQualityTest, MajorKey_bVI_IsMajor) {
  EXPECT_EQ(majorKeyQuality(ChordDegree::bVI), ChordQuality::Major);
}

TEST(BorrowedChordQualityTest, MajorKey_bVII_IsMajor) {
  EXPECT_EQ(majorKeyQuality(ChordDegree::bVII), ChordQuality::Major);
}

TEST(BorrowedChordQualityTest, MajorKey_bIII_IsMajor) {
  EXPECT_EQ(majorKeyQuality(ChordDegree::bIII), ChordQuality::Major);
}

TEST(BorrowedChordQualityTest, MajorKey_V_of_iii_IsDominant7) {
  EXPECT_EQ(majorKeyQuality(ChordDegree::V_of_iii), ChordQuality::Dominant7);
}

// ---------------------------------------------------------------------------
// minorKeyQuality -- borrowed chords (same as natural minor)
// ---------------------------------------------------------------------------

TEST(BorrowedChordQualityTest, MinorKey_bVI_IsMajor) {
  EXPECT_EQ(minorKeyQuality(ChordDegree::bVI), ChordQuality::Major);
}

TEST(BorrowedChordQualityTest, MinorKey_bVII_IsMajor) {
  EXPECT_EQ(minorKeyQuality(ChordDegree::bVII), ChordQuality::Major);
}

TEST(BorrowedChordQualityTest, MinorKey_bIII_IsMajor) {
  EXPECT_EQ(minorKeyQuality(ChordDegree::bIII), ChordQuality::Major);
}

// ---------------------------------------------------------------------------
// degreeSemitones -- new degrees
// ---------------------------------------------------------------------------

TEST(ExtendedDegreeSemitonesTest, bVI_Is8) {
  EXPECT_EQ(degreeSemitones(ChordDegree::bVI), 8);
}

TEST(ExtendedDegreeSemitonesTest, bVII_Is10) {
  EXPECT_EQ(degreeSemitones(ChordDegree::bVII), 10);
}

TEST(ExtendedDegreeSemitonesTest, bIII_Is3) {
  EXPECT_EQ(degreeSemitones(ChordDegree::bIII), 3);
}

TEST(ExtendedDegreeSemitonesTest, V_of_iii_Is11) {
  EXPECT_EQ(degreeSemitones(ChordDegree::V_of_iii), 11);
}

// ---------------------------------------------------------------------------
// degreeMinorSemitones -- new degrees
// ---------------------------------------------------------------------------

TEST(ExtendedDegreeMinorSemitonesTest, bVI_Is8) {
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::bVI), 8);
}

TEST(ExtendedDegreeMinorSemitonesTest, bVII_Is10) {
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::bVII), 10);
}

TEST(ExtendedDegreeMinorSemitonesTest, bIII_Is3) {
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::bIII), 3);
}

TEST(ExtendedDegreeMinorSemitonesTest, V_of_iii_Is10) {
  // In C minor, III is E-flat and its dominant is B-flat.
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::V_of_iii), 10);
}

}  // namespace
}  // namespace bach
