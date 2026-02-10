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

// ---------------------------------------------------------------------------
// pitchToAbsoluteDegree / absoluteDegreeToPitch -- Round-trip
// ---------------------------------------------------------------------------

TEST(PitchToAbsoluteDegreeTest, RoundTripCMajorDiatonic) {
  // All C major diatonic pitches in the octave 4-5 range (60..83).
  // C major pitch classes: {0, 2, 4, 5, 7, 9, 11}.
  const uint8_t diatonic_pitches[] = {
      60, 62, 64, 65, 67, 69, 71,  // C4 D4 E4 F4 G4 A4 B4
      72, 74, 76, 77, 79, 81, 83   // C5 D5 E5 F5 G5 A5 B5
  };

  for (uint8_t pitch : diatonic_pitches) {
    int abs_deg = scale_util::pitchToAbsoluteDegree(pitch, Key::C, ScaleType::Major);
    uint8_t recovered = scale_util::absoluteDegreeToPitch(abs_deg, Key::C, ScaleType::Major);
    EXPECT_EQ(recovered, pitch)
        << "Round-trip failed for MIDI pitch " << static_cast<int>(pitch);
  }
}

// ---------------------------------------------------------------------------
// pitchToAbsoluteDegree -- Octave difference
// ---------------------------------------------------------------------------

TEST(PitchToAbsoluteDegreeTest, OctaveDifference) {
  // C3 (48) and C5 (72) are 2 octaves apart = 14 scale degrees.
  int deg_c3 = scale_util::pitchToAbsoluteDegree(48, Key::C, ScaleType::Major);
  int deg_c5 = scale_util::pitchToAbsoluteDegree(72, Key::C, ScaleType::Major);
  EXPECT_EQ(deg_c5 - deg_c3, 14);
}

// ---------------------------------------------------------------------------
// pitchToAbsoluteDegree -- Non-scale tone snapping
// ---------------------------------------------------------------------------

TEST(PitchToAbsoluteDegreeTest, NonScaleToneSnap) {
  // C#4 (61) is not in C major. After snapping and round-trip, the result
  // must be a valid C major scale tone.
  int abs_deg = scale_util::pitchToAbsoluteDegree(61, Key::C, ScaleType::Major);
  uint8_t recovered = scale_util::absoluteDegreeToPitch(abs_deg, Key::C, ScaleType::Major);
  EXPECT_TRUE(scale_util::isScaleTone(recovered, Key::C, ScaleType::Major))
      << "Round-trip of non-scale tone should produce a scale tone, got "
      << static_cast<int>(recovered);
}

// ---------------------------------------------------------------------------
// absoluteDegreeToPitch -- Cross-octave arithmetic
// ---------------------------------------------------------------------------

TEST(AbsoluteDegreeToPitchTest, CrossOctaveArithmetic) {
  // Adding 7 degrees to C4 (60) should yield C5 (72).
  int deg_c4 = scale_util::pitchToAbsoluteDegree(60, Key::C, ScaleType::Major);
  uint8_t one_octave_up = scale_util::absoluteDegreeToPitch(deg_c4 + 7, Key::C, ScaleType::Major);
  EXPECT_EQ(one_octave_up, 72);
}

// ---------------------------------------------------------------------------
// pitchToAbsoluteDegree -- G Major
// ---------------------------------------------------------------------------

TEST(PitchToAbsoluteDegreeTest, GMajorAbsoluteDegree) {
  // G4 (67) in G major: octave = 67/12 = 5, scale degree = 0 (root).
  // Absolute degree = 5*7 + 0 = 35.
  int deg_g4 = scale_util::pitchToAbsoluteDegree(67, Key::G, ScaleType::Major);
  EXPECT_EQ(deg_g4, 35);

  // A4 (69) in G major: octave = 69/12 = 5, scale degree = 1 (2nd).
  // Absolute degree = 5*7 + 1 = 36.
  int deg_a4 = scale_util::pitchToAbsoluteDegree(69, Key::G, ScaleType::Major);
  EXPECT_EQ(deg_a4, 36);

  // Round-trip works for tones within the same MIDI octave as the tonic:
  // G4=67, A4=69, B4=71 all have MIDI octave 5, same as G's position.
  const uint8_t same_octave_pitches[] = {67, 69, 71};
  for (uint8_t pitch : same_octave_pitches) {
    int abs_deg = scale_util::pitchToAbsoluteDegree(pitch, Key::G, ScaleType::Major);
    uint8_t recovered = scale_util::absoluteDegreeToPitch(abs_deg, Key::G, ScaleType::Major);
    EXPECT_EQ(recovered, pitch)
        << "G major round-trip failed for MIDI pitch " << static_cast<int>(pitch);
  }

  // Degree arithmetic: G4 + 7 degrees = G5 (one octave up).
  uint8_t octave_up = scale_util::absoluteDegreeToPitch(deg_g4 + 7, Key::G, ScaleType::Major);
  EXPECT_EQ(octave_up, 79);  // G5 = 79
}

// ---------------------------------------------------------------------------
// absoluteDegreeToPitch -- Low pitch round-trip
// ---------------------------------------------------------------------------

TEST(AbsoluteDegreeToPitchTest, LowPitchRoundTrip) {
  // C2 (36) should survive a round-trip through absolute degree conversion.
  int abs_deg = scale_util::pitchToAbsoluteDegree(36, Key::C, ScaleType::Major);
  uint8_t recovered = scale_util::absoluteDegreeToPitch(abs_deg, Key::C, ScaleType::Major);
  EXPECT_EQ(recovered, 36);
}

}  // namespace
}  // namespace bach
