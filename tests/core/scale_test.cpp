// Tests for core/scale.h -- scale membership, nearest tone, degree lookup.

#include "core/scale.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// scaleDegreeCount
// ---------------------------------------------------------------------------

TEST(ScaleDegreeCountTest, AlwaysSeven) {
  EXPECT_EQ(scale_util::scaleDegreeCount(), 7);
}

// ---------------------------------------------------------------------------
// isScaleTone -- C Major
// ---------------------------------------------------------------------------

TEST(IsScaleToneTest, CMajorDiatonicTones) {
  // C4 (60), D4 (62), E4 (64), F4 (65), G4 (67), A4 (69), B4 (71)
  EXPECT_TRUE(scale_util::isScaleTone(60, Key::C, ScaleType::Major));  // C
  EXPECT_TRUE(scale_util::isScaleTone(62, Key::C, ScaleType::Major));  // D
  EXPECT_TRUE(scale_util::isScaleTone(64, Key::C, ScaleType::Major));  // E
  EXPECT_TRUE(scale_util::isScaleTone(65, Key::C, ScaleType::Major));  // F
  EXPECT_TRUE(scale_util::isScaleTone(67, Key::C, ScaleType::Major));  // G
  EXPECT_TRUE(scale_util::isScaleTone(69, Key::C, ScaleType::Major));  // A
  EXPECT_TRUE(scale_util::isScaleTone(71, Key::C, ScaleType::Major));  // B
}

TEST(IsScaleToneTest, CMajorNonDiatonicTones) {
  EXPECT_FALSE(scale_util::isScaleTone(61, Key::C, ScaleType::Major));  // C#
  EXPECT_FALSE(scale_util::isScaleTone(63, Key::C, ScaleType::Major));  // Eb
  EXPECT_FALSE(scale_util::isScaleTone(66, Key::C, ScaleType::Major));  // F#
  EXPECT_FALSE(scale_util::isScaleTone(68, Key::C, ScaleType::Major));  // Ab
  EXPECT_FALSE(scale_util::isScaleTone(70, Key::C, ScaleType::Major));  // Bb
}

// ---------------------------------------------------------------------------
// isScaleTone -- G Major
// ---------------------------------------------------------------------------

TEST(IsScaleToneTest, GMajorDiatonicTones) {
  // G Major: G A B C D E F#
  // G4=67, A4=69, B4=71, C5=72, D5=74, E5=76, F#5=78
  EXPECT_TRUE(scale_util::isScaleTone(67, Key::G, ScaleType::Major));   // G
  EXPECT_TRUE(scale_util::isScaleTone(69, Key::G, ScaleType::Major));   // A
  EXPECT_TRUE(scale_util::isScaleTone(71, Key::G, ScaleType::Major));   // B
  EXPECT_TRUE(scale_util::isScaleTone(72, Key::G, ScaleType::Major));   // C
  EXPECT_TRUE(scale_util::isScaleTone(74, Key::G, ScaleType::Major));   // D
  EXPECT_TRUE(scale_util::isScaleTone(76, Key::G, ScaleType::Major));   // E
  EXPECT_TRUE(scale_util::isScaleTone(78, Key::G, ScaleType::Major));   // F#
  EXPECT_FALSE(scale_util::isScaleTone(77, Key::G, ScaleType::Major));  // F natural
}

// ---------------------------------------------------------------------------
// isScaleTone -- D Harmonic Minor
// ---------------------------------------------------------------------------

TEST(IsScaleToneTest, DHarmonicMinor) {
  // D harmonic minor: D E F G A Bb C#
  // Key::D = 2 semitones. Intervals: 0 2 3 5 7 8 11
  // Pitch classes: 2 4 5 7 9 10 1
  EXPECT_TRUE(scale_util::isScaleTone(62, Key::D, ScaleType::HarmonicMinor));   // D
  EXPECT_TRUE(scale_util::isScaleTone(64, Key::D, ScaleType::HarmonicMinor));   // E
  EXPECT_TRUE(scale_util::isScaleTone(65, Key::D, ScaleType::HarmonicMinor));   // F
  EXPECT_TRUE(scale_util::isScaleTone(67, Key::D, ScaleType::HarmonicMinor));   // G
  EXPECT_TRUE(scale_util::isScaleTone(69, Key::D, ScaleType::HarmonicMinor));   // A
  EXPECT_TRUE(scale_util::isScaleTone(70, Key::D, ScaleType::HarmonicMinor));   // Bb
  EXPECT_TRUE(scale_util::isScaleTone(73, Key::D, ScaleType::HarmonicMinor));   // C#
  EXPECT_FALSE(scale_util::isScaleTone(72, Key::D, ScaleType::HarmonicMinor));  // C natural
}

// ---------------------------------------------------------------------------
// isScaleTone -- Different octaves
// ---------------------------------------------------------------------------

TEST(IsScaleToneTest, WorksAcrossOctaves) {
  // C is a scale tone in C Major regardless of octave.
  EXPECT_TRUE(scale_util::isScaleTone(24, Key::C, ScaleType::Major));  // C1
  EXPECT_TRUE(scale_util::isScaleTone(48, Key::C, ScaleType::Major));  // C3
  EXPECT_TRUE(scale_util::isScaleTone(96, Key::C, ScaleType::Major));  // C7
}

// ---------------------------------------------------------------------------
// nearestScaleTone -- C Major
// ---------------------------------------------------------------------------

TEST(NearestScaleToneTest, AlreadyOnScale) {
  // If already a scale tone, return as-is.
  EXPECT_EQ(scale_util::nearestScaleTone(60, Key::C, ScaleType::Major), 60);  // C4
  EXPECT_EQ(scale_util::nearestScaleTone(67, Key::C, ScaleType::Major), 67);  // G4
}

TEST(NearestScaleToneTest, SnapToNearerTone) {
  // C#4 (61) is between C4 (60) and D4 (62) -- equidistant, prefer lower.
  EXPECT_EQ(scale_util::nearestScaleTone(61, Key::C, ScaleType::Major), 60);

  // Eb4 (63) is between D4 (62) and E4 (64) -- equidistant, prefer lower.
  EXPECT_EQ(scale_util::nearestScaleTone(63, Key::C, ScaleType::Major), 62);

  // F#4 (66) is between F4 (65) and G4 (67) -- equidistant, prefer lower.
  EXPECT_EQ(scale_util::nearestScaleTone(66, Key::C, ScaleType::Major), 65);

  // Ab4 (68) is between G4 (67) and A4 (69) -- equidistant, prefer lower.
  EXPECT_EQ(scale_util::nearestScaleTone(68, Key::C, ScaleType::Major), 67);

  // Bb4 (70) is between A4 (69) and B4 (71) -- equidistant, prefer lower.
  EXPECT_EQ(scale_util::nearestScaleTone(70, Key::C, ScaleType::Major), 69);
}

// ---------------------------------------------------------------------------
// nearestScaleTone -- G Major
// ---------------------------------------------------------------------------

TEST(NearestScaleToneTest, GMajorSnapping) {
  // F4 (65) is not in G major. Nearest: E4 (64, 1 away) vs F#4 (66, 1 away).
  // Prefer lower -> E4.
  EXPECT_EQ(scale_util::nearestScaleTone(65, Key::G, ScaleType::Major), 64);
}

// ---------------------------------------------------------------------------
// pitchToScaleDegree
// ---------------------------------------------------------------------------

TEST(PitchToScaleDegreeTest, CMajorDegrees) {
  int degree = -1;

  EXPECT_TRUE(scale_util::pitchToScaleDegree(60, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 0);  // C = root

  EXPECT_TRUE(scale_util::pitchToScaleDegree(62, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 1);  // D = 2nd

  EXPECT_TRUE(scale_util::pitchToScaleDegree(64, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 2);  // E = 3rd

  EXPECT_TRUE(scale_util::pitchToScaleDegree(65, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 3);  // F = 4th

  EXPECT_TRUE(scale_util::pitchToScaleDegree(67, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 4);  // G = 5th

  EXPECT_TRUE(scale_util::pitchToScaleDegree(69, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 5);  // A = 6th

  EXPECT_TRUE(scale_util::pitchToScaleDegree(71, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 6);  // B = 7th
}

TEST(PitchToScaleDegreeTest, NonScaleToneReturnsFalse) {
  int degree = -1;
  EXPECT_FALSE(scale_util::pitchToScaleDegree(61, Key::C, ScaleType::Major, degree));
  // degree should remain unchanged.
  EXPECT_EQ(degree, -1);
}

TEST(PitchToScaleDegreeTest, GMajorDegrees) {
  int degree = -1;

  // G is the root of G Major.
  EXPECT_TRUE(scale_util::pitchToScaleDegree(67, Key::G, ScaleType::Major, degree));
  EXPECT_EQ(degree, 0);  // G = root

  // F# is the 7th of G Major.
  EXPECT_TRUE(scale_util::pitchToScaleDegree(66, Key::G, ScaleType::Major, degree));
  EXPECT_EQ(degree, 6);  // F# = 7th
}

TEST(PitchToScaleDegreeTest, DifferentOctaves) {
  int degree = -1;
  // C in any octave is the root of C Major.
  EXPECT_TRUE(scale_util::pitchToScaleDegree(48, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 0);

  EXPECT_TRUE(scale_util::pitchToScaleDegree(84, Key::C, ScaleType::Major, degree));
  EXPECT_EQ(degree, 0);
}

TEST(PitchToScaleDegreeTest, HarmonicMinorDegrees) {
  int degree = -1;

  // In C harmonic minor, B (71) is the raised 7th (degree 6).
  EXPECT_TRUE(scale_util::pitchToScaleDegree(71, Key::C, ScaleType::HarmonicMinor, degree));
  EXPECT_EQ(degree, 6);

  // Eb (63) is the minor 3rd (degree 2).
  EXPECT_TRUE(scale_util::pitchToScaleDegree(63, Key::C, ScaleType::HarmonicMinor, degree));
  EXPECT_EQ(degree, 2);
}

}  // namespace
}  // namespace bach
