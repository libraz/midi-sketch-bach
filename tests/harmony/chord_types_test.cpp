// Tests for harmony/chord_types.h -- diatonic quality, degree semitones, string conversion.

#include "harmony/chord_types.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// majorKeyQuality -- all 7 diatonic degrees
// ---------------------------------------------------------------------------

TEST(MajorKeyQualityTest, AllDegrees) {
  // I=Maj, ii=min, iii=min, IV=Maj, V=Maj, vi=min, vii=dim
  EXPECT_EQ(majorKeyQuality(ChordDegree::I), ChordQuality::Major);
  EXPECT_EQ(majorKeyQuality(ChordDegree::ii), ChordQuality::Minor);
  EXPECT_EQ(majorKeyQuality(ChordDegree::iii), ChordQuality::Minor);
  EXPECT_EQ(majorKeyQuality(ChordDegree::IV), ChordQuality::Major);
  EXPECT_EQ(majorKeyQuality(ChordDegree::V), ChordQuality::Major);
  EXPECT_EQ(majorKeyQuality(ChordDegree::vi), ChordQuality::Minor);
  EXPECT_EQ(majorKeyQuality(ChordDegree::viiDim), ChordQuality::Diminished);
}

// ---------------------------------------------------------------------------
// minorKeyQuality -- natural minor, all 7 diatonic degrees
// ---------------------------------------------------------------------------

TEST(MinorKeyQualityTest, AllDegrees) {
  // i=min, ii=dim, III=Maj, iv=min, v=min, VI=Maj, VII=Maj
  EXPECT_EQ(minorKeyQuality(ChordDegree::I), ChordQuality::Minor);
  EXPECT_EQ(minorKeyQuality(ChordDegree::ii), ChordQuality::Diminished);
  EXPECT_EQ(minorKeyQuality(ChordDegree::iii), ChordQuality::Major);
  EXPECT_EQ(minorKeyQuality(ChordDegree::IV), ChordQuality::Minor);
  EXPECT_EQ(minorKeyQuality(ChordDegree::V), ChordQuality::Minor);
  EXPECT_EQ(minorKeyQuality(ChordDegree::vi), ChordQuality::Major);
  EXPECT_EQ(minorKeyQuality(ChordDegree::viiDim), ChordQuality::Major);
}

// ---------------------------------------------------------------------------
// degreeSemitones -- major scale offsets
// ---------------------------------------------------------------------------

TEST(DegreeSemitonesTest, MajorScaleOffsets) {
  // C D E F G A B = 0 2 4 5 7 9 11
  EXPECT_EQ(degreeSemitones(ChordDegree::I), 0);
  EXPECT_EQ(degreeSemitones(ChordDegree::ii), 2);
  EXPECT_EQ(degreeSemitones(ChordDegree::iii), 4);
  EXPECT_EQ(degreeSemitones(ChordDegree::IV), 5);
  EXPECT_EQ(degreeSemitones(ChordDegree::V), 7);
  EXPECT_EQ(degreeSemitones(ChordDegree::vi), 9);
  EXPECT_EQ(degreeSemitones(ChordDegree::viiDim), 11);
}

// ---------------------------------------------------------------------------
// degreeMinorSemitones -- natural minor scale offsets
// ---------------------------------------------------------------------------

TEST(DegreeMinorSemitonesTest, NaturalMinorScaleOffsets) {
  // Natural minor: 0 2 3 5 7 8 10
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::I), 0);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::ii), 2);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::iii), 3);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::IV), 5);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::V), 7);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::vi), 8);
  EXPECT_EQ(degreeMinorSemitones(ChordDegree::viiDim), 10);
}

// ---------------------------------------------------------------------------
// String conversions
// ---------------------------------------------------------------------------

TEST(ChordQualityToStringTest, AllQualities) {
  EXPECT_STREQ(chordQualityToString(ChordQuality::Major), "Major");
  EXPECT_STREQ(chordQualityToString(ChordQuality::Minor), "Minor");
  EXPECT_STREQ(chordQualityToString(ChordQuality::Diminished), "Diminished");
  EXPECT_STREQ(chordQualityToString(ChordQuality::Augmented), "Augmented");
  EXPECT_STREQ(chordQualityToString(ChordQuality::Dominant7), "Dominant7");
  EXPECT_STREQ(chordQualityToString(ChordQuality::Minor7), "Minor7");
  EXPECT_STREQ(chordQualityToString(ChordQuality::MajorMajor7), "MajorMajor7");
}

TEST(ChordDegreeToStringTest, AllDegrees) {
  EXPECT_STREQ(chordDegreeToString(ChordDegree::I), "I");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::ii), "ii");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::iii), "iii");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::IV), "IV");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::V), "V");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::vi), "vi");
  EXPECT_STREQ(chordDegreeToString(ChordDegree::viiDim), "viiDim");
}

// ---------------------------------------------------------------------------
// Chord struct defaults
// ---------------------------------------------------------------------------

TEST(ChordStructTest, DefaultValues) {
  Chord chord;
  EXPECT_EQ(chord.degree, ChordDegree::I);
  EXPECT_EQ(chord.quality, ChordQuality::Major);
  EXPECT_EQ(chord.root_pitch, 0);
  EXPECT_EQ(chord.inversion, 0);
}

TEST(ChordStructTest, CustomValues) {
  Chord chord;
  chord.degree = ChordDegree::V;
  chord.quality = ChordQuality::Dominant7;
  chord.root_pitch = 67;  // G4
  chord.inversion = 1;

  EXPECT_EQ(chord.degree, ChordDegree::V);
  EXPECT_EQ(chord.quality, ChordQuality::Dominant7);
  EXPECT_EQ(chord.root_pitch, 67);
  EXPECT_EQ(chord.inversion, 1);
}

}  // namespace
}  // namespace bach
