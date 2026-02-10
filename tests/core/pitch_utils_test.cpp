// Tests for core/pitch_utils.h -- interval classification, pitch utilities,
// counterpoint parallel detection, scale/degree conversion.

#include "core/pitch_utils.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Interval constants
// ---------------------------------------------------------------------------

TEST(IntervalConstantsTest, Values) {
  EXPECT_EQ(interval::kUnison, 0);
  EXPECT_EQ(interval::kMinor2nd, 1);
  EXPECT_EQ(interval::kMajor2nd, 2);
  EXPECT_EQ(interval::kMinor3rd, 3);
  EXPECT_EQ(interval::kMajor3rd, 4);
  EXPECT_EQ(interval::kPerfect4th, 5);
  EXPECT_EQ(interval::kTritone, 6);
  EXPECT_EQ(interval::kPerfect5th, 7);
  EXPECT_EQ(interval::kMinor6th, 8);
  EXPECT_EQ(interval::kMajor6th, 9);
  EXPECT_EQ(interval::kMinor7th, 10);
  EXPECT_EQ(interval::kMajor7th, 11);
  EXPECT_EQ(interval::kOctave, 12);
}

// ---------------------------------------------------------------------------
// Interval classification
// ---------------------------------------------------------------------------

TEST(ClassifyIntervalTest, PerfectConsonances) {
  EXPECT_EQ(classifyInterval(0), IntervalQuality::PerfectConsonance);   // Unison
  EXPECT_EQ(classifyInterval(7), IntervalQuality::PerfectConsonance);   // Perfect 5th
  EXPECT_EQ(classifyInterval(12), IntervalQuality::PerfectConsonance);  // Octave (= unison mod 12)
  EXPECT_EQ(classifyInterval(19), IntervalQuality::PerfectConsonance);  // Compound 5th
}

TEST(ClassifyIntervalTest, ImperfectConsonances) {
  EXPECT_EQ(classifyInterval(3), IntervalQuality::ImperfectConsonance);   // Minor 3rd
  EXPECT_EQ(classifyInterval(4), IntervalQuality::ImperfectConsonance);   // Major 3rd
  EXPECT_EQ(classifyInterval(8), IntervalQuality::ImperfectConsonance);   // Minor 6th
  EXPECT_EQ(classifyInterval(9), IntervalQuality::ImperfectConsonance);   // Major 6th
  EXPECT_EQ(classifyInterval(15), IntervalQuality::ImperfectConsonance);  // Compound minor 3rd
  EXPECT_EQ(classifyInterval(16), IntervalQuality::ImperfectConsonance);  // Compound major 3rd
}

TEST(ClassifyIntervalTest, Dissonances) {
  EXPECT_EQ(classifyInterval(1), IntervalQuality::Dissonance);   // Minor 2nd
  EXPECT_EQ(classifyInterval(2), IntervalQuality::Dissonance);   // Major 2nd
  EXPECT_EQ(classifyInterval(5), IntervalQuality::Dissonance);   // Perfect 4th (dissonant in 2-voice)
  EXPECT_EQ(classifyInterval(6), IntervalQuality::Dissonance);   // Tritone
  EXPECT_EQ(classifyInterval(10), IntervalQuality::Dissonance);  // Minor 7th
  EXPECT_EQ(classifyInterval(11), IntervalQuality::Dissonance);  // Major 7th
}

TEST(ClassifyIntervalTest, NegativeIntervals) {
  // Negative intervals should be treated by absolute value
  EXPECT_EQ(classifyInterval(-7), IntervalQuality::PerfectConsonance);
  EXPECT_EQ(classifyInterval(-3), IntervalQuality::ImperfectConsonance);
  EXPECT_EQ(classifyInterval(-1), IntervalQuality::Dissonance);
}

// ---------------------------------------------------------------------------
// Parallel detection
// ---------------------------------------------------------------------------

TEST(ParallelDetectionTest, ParallelFifths) {
  EXPECT_TRUE(isParallelFifths(7, 7));    // Both perfect 5ths
  EXPECT_TRUE(isParallelFifths(19, 7));   // Compound 5th to 5th
  EXPECT_TRUE(isParallelFifths(7, 19));   // 5th to compound 5th
  EXPECT_FALSE(isParallelFifths(7, 3));   // 5th to 3rd
  EXPECT_FALSE(isParallelFifths(0, 0));   // Unisons, not 5ths
  EXPECT_FALSE(isParallelFifths(12, 12)); // Octaves, not 5ths
}

TEST(ParallelDetectionTest, ParallelOctaves) {
  EXPECT_TRUE(isParallelOctaves(12, 12));   // Both octaves
  EXPECT_TRUE(isParallelOctaves(0, 0));     // Both unisons
  EXPECT_TRUE(isParallelOctaves(0, 12));    // Unison to octave
  EXPECT_TRUE(isParallelOctaves(24, 12));   // Double octave to octave
  EXPECT_FALSE(isParallelOctaves(7, 7));    // 5ths, not octaves
  EXPECT_FALSE(isParallelOctaves(12, 7));   // Octave to 5th
}

// ---------------------------------------------------------------------------
// Pitch class and octave
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, GetPitchClass) {
  EXPECT_EQ(getPitchClass(60), 0);    // C4
  EXPECT_EQ(getPitchClass(61), 1);    // C#4
  EXPECT_EQ(getPitchClass(62), 2);    // D4
  EXPECT_EQ(getPitchClass(69), 9);    // A4
  EXPECT_EQ(getPitchClass(72), 0);    // C5
  EXPECT_EQ(getPitchClass(0), 0);     // Lowest MIDI
  EXPECT_EQ(getPitchClass(127), 7);   // Highest MIDI = G9
}

TEST(PitchUtilsTest, GetOctave) {
  EXPECT_EQ(getOctave(60), 4);   // C4
  EXPECT_EQ(getOctave(72), 5);   // C5
  EXPECT_EQ(getOctave(48), 3);   // C3
  EXPECT_EQ(getOctave(24), 1);   // C1
  EXPECT_EQ(getOctave(0), -1);   // C-1
}

// ---------------------------------------------------------------------------
// Diatonic check
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, IsDiatonic) {
  // C major scale: C D E F G A B
  EXPECT_TRUE(isDiatonic(60));   // C
  EXPECT_TRUE(isDiatonic(62));   // D
  EXPECT_TRUE(isDiatonic(64));   // E
  EXPECT_TRUE(isDiatonic(65));   // F
  EXPECT_TRUE(isDiatonic(67));   // G
  EXPECT_TRUE(isDiatonic(69));   // A
  EXPECT_TRUE(isDiatonic(71));   // B

  // Non-diatonic (accidentals)
  EXPECT_FALSE(isDiatonic(61));  // C#
  EXPECT_FALSE(isDiatonic(63));  // D#/Eb
  EXPECT_FALSE(isDiatonic(66));  // F#
  EXPECT_FALSE(isDiatonic(68));  // G#/Ab
  EXPECT_FALSE(isDiatonic(70));  // A#/Bb
}

TEST(PitchUtilsTest, IsDiatonicDifferentOctaves) {
  // Diatonic check should work across octaves
  EXPECT_TRUE(isDiatonic(48));   // C3
  EXPECT_TRUE(isDiatonic(36));   // C2
  EXPECT_FALSE(isDiatonic(49)); // C#3
}

// ---------------------------------------------------------------------------
// Clamp pitch
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, ClampPitch) {
  // Within range
  EXPECT_EQ(clampPitch(60, 36, 96), 60);

  // Below range
  EXPECT_EQ(clampPitch(20, 36, 96), 36);

  // Above range
  EXPECT_EQ(clampPitch(100, 36, 96), 96);

  // At boundaries
  EXPECT_EQ(clampPitch(36, 36, 96), 36);
  EXPECT_EQ(clampPitch(96, 36, 96), 96);

  // Negative input
  EXPECT_EQ(clampPitch(-5, 24, 96), 24);
}

// ---------------------------------------------------------------------------
// Scale intervals
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, GetScaleIntervals) {
  const int* major = getScaleIntervals(ScaleType::Major);
  EXPECT_EQ(major[0], 0);
  EXPECT_EQ(major[1], 2);
  EXPECT_EQ(major[2], 4);
  EXPECT_EQ(major[3], 5);
  EXPECT_EQ(major[4], 7);
  EXPECT_EQ(major[5], 9);
  EXPECT_EQ(major[6], 11);

  const int* harmonic_minor = getScaleIntervals(ScaleType::HarmonicMinor);
  EXPECT_EQ(harmonic_minor[0], 0);
  EXPECT_EQ(harmonic_minor[2], 3);   // Minor 3rd
  EXPECT_EQ(harmonic_minor[6], 11);  // Raised 7th

  const int* natural_minor = getScaleIntervals(ScaleType::NaturalMinor);
  EXPECT_EQ(natural_minor[6], 10);   // Flat 7th

  const int* dorian = getScaleIntervals(ScaleType::Dorian);
  EXPECT_EQ(dorian[2], 3);   // Minor 3rd
  EXPECT_EQ(dorian[5], 9);   // Major 6th (distinguishes from natural minor)
}

// ---------------------------------------------------------------------------
// Degree to pitch
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, DegreeToPitchMajorScale) {
  // C major from C4 (MIDI 60)
  EXPECT_EQ(degreeToPitch(0, 60, 0, ScaleType::Major), 60);  // C4
  EXPECT_EQ(degreeToPitch(1, 60, 0, ScaleType::Major), 62);  // D4
  EXPECT_EQ(degreeToPitch(2, 60, 0, ScaleType::Major), 64);  // E4
  EXPECT_EQ(degreeToPitch(3, 60, 0, ScaleType::Major), 65);  // F4
  EXPECT_EQ(degreeToPitch(4, 60, 0, ScaleType::Major), 67);  // G4
  EXPECT_EQ(degreeToPitch(5, 60, 0, ScaleType::Major), 69);  // A4
  EXPECT_EQ(degreeToPitch(6, 60, 0, ScaleType::Major), 71);  // B4
}

TEST(PitchUtilsTest, DegreeToPitchOctaveSpan) {
  // Degree 7 should wrap to next octave
  EXPECT_EQ(degreeToPitch(7, 60, 0, ScaleType::Major), 72);   // C5
  EXPECT_EQ(degreeToPitch(8, 60, 0, ScaleType::Major), 74);   // D5
  EXPECT_EQ(degreeToPitch(14, 60, 0, ScaleType::Major), 84);  // C6
}

TEST(PitchUtilsTest, DegreeToPitchNegativeDegree) {
  // Negative degrees go below the base note
  EXPECT_EQ(degreeToPitch(-1, 60, 0, ScaleType::Major), 59);  // B3
  EXPECT_EQ(degreeToPitch(-7, 60, 0, ScaleType::Major), 48);  // C3
}

TEST(PitchUtilsTest, DegreeToPitchWithKeyOffset) {
  // G major: key_offset = 7 (G is 7 semitones above C)
  EXPECT_EQ(degreeToPitch(0, 60, 7, ScaleType::Major), 67);  // G4
  EXPECT_EQ(degreeToPitch(1, 60, 7, ScaleType::Major), 69);  // A4
  EXPECT_EQ(degreeToPitch(4, 60, 7, ScaleType::Major), 74);  // D5
}

TEST(PitchUtilsTest, DegreeToPitchMinorScale) {
  // C harmonic minor from C4
  EXPECT_EQ(degreeToPitch(0, 60, 0, ScaleType::HarmonicMinor), 60);  // C4
  EXPECT_EQ(degreeToPitch(2, 60, 0, ScaleType::HarmonicMinor), 63);  // Eb4
  EXPECT_EQ(degreeToPitch(6, 60, 0, ScaleType::HarmonicMinor), 71);  // B4 (raised 7th)
}

// ---------------------------------------------------------------------------
// Pitch to note name
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, PitchToNoteName) {
  EXPECT_EQ(pitchToNoteName(60), "C4");
  EXPECT_EQ(pitchToNoteName(61), "C#4");
  EXPECT_EQ(pitchToNoteName(69), "A4");
  EXPECT_EQ(pitchToNoteName(72), "C5");
  EXPECT_EQ(pitchToNoteName(36), "C2");
  EXPECT_EQ(pitchToNoteName(24), "C1");
  EXPECT_EQ(pitchToNoteName(0), "C-1");
}

// ---------------------------------------------------------------------------
// Transpose pitch
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, TransposePitch) {
  // C major -> C major (no transposition)
  EXPECT_EQ(transposePitch(60, Key::C), 60);

  // C major -> G major (+7 semitones)
  EXPECT_EQ(transposePitch(60, Key::G), 67);

  // C major -> D major (+2 semitones)
  EXPECT_EQ(transposePitch(60, Key::D), 62);
}

TEST(PitchUtilsTest, TransposePitchClamping) {
  // Should not exceed MIDI range
  EXPECT_EQ(transposePitch(127, Key::G), 127);  // Clamped at 127
  EXPECT_LE(transposePitch(120, Key::B), 127);
}

// ---------------------------------------------------------------------------
// Absolute and directed intervals
// ---------------------------------------------------------------------------

TEST(PitchUtilsTest, AbsoluteInterval) {
  EXPECT_EQ(absoluteInterval(60, 67), 7);   // C4 to G4 = P5
  EXPECT_EQ(absoluteInterval(67, 60), 7);   // G4 to C4 = P5 (absolute)
  EXPECT_EQ(absoluteInterval(60, 60), 0);   // Unison
  EXPECT_EQ(absoluteInterval(60, 72), 12);  // Octave
}

TEST(PitchUtilsTest, DirectedInterval) {
  EXPECT_EQ(directedInterval(60, 67), 7);    // Ascending P5
  EXPECT_EQ(directedInterval(67, 60), -7);   // Descending P5
  EXPECT_EQ(directedInterval(60, 60), 0);    // Unison
  EXPECT_EQ(directedInterval(60, 72), 12);   // Ascending octave
}

// ---------------------------------------------------------------------------
// Organ and string ranges
// ---------------------------------------------------------------------------

TEST(PitchRangesTest, OrganRanges) {
  // Manual I (Great): C2-C6
  EXPECT_EQ(organ_range::kManual1Low, 36);
  EXPECT_EQ(organ_range::kManual1High, 96);

  // Manual II (Swell): C2-C6
  EXPECT_EQ(organ_range::kManual2Low, 36);
  EXPECT_EQ(organ_range::kManual2High, 96);

  // Manual III (Positiv): C3-C6
  EXPECT_EQ(organ_range::kManual3Low, 48);
  EXPECT_EQ(organ_range::kManual3High, 96);

  // Pedal: C1-D3
  EXPECT_EQ(organ_range::kPedalLow, 24);
  EXPECT_EQ(organ_range::kPedalHigh, 50);
}

TEST(PitchRangesTest, StringRanges) {
  // Cello: C2-A5
  EXPECT_EQ(string_range::kCelloLow, 36);
  EXPECT_EQ(string_range::kCelloHigh, 81);

  // Violin: G3-C7
  EXPECT_EQ(string_range::kViolinLow, 55);
  EXPECT_EQ(string_range::kViolinHigh, 96);

  // Guitar: E2-B5
  EXPECT_EQ(string_range::kGuitarLow, 40);
  EXPECT_EQ(string_range::kGuitarHigh, 83);
}

// ---------------------------------------------------------------------------
// Scale constant arrays
// ---------------------------------------------------------------------------

TEST(ScaleArraysTest, MajorScale) {
  EXPECT_EQ(kScaleMajor[0], 0);
  EXPECT_EQ(kScaleMajor[6], 11);
}

TEST(ScaleArraysTest, HarmonicMinorHasRaisedSeventh) {
  EXPECT_EQ(kScaleHarmonicMinor[6], 11);  // Raised 7th
  EXPECT_EQ(kScaleNaturalMinor[6], 10);   // Natural 7th
}

TEST(ScaleArraysTest, MelodicMinorHasRaisedSixthAndSeventh) {
  EXPECT_EQ(kScaleMelodicMinor[5], 9);   // Raised 6th
  EXPECT_EQ(kScaleMelodicMinor[6], 11);  // Raised 7th
}

TEST(ScaleArraysTest, DorianHasRaisedSixth) {
  EXPECT_EQ(kScaleDorian[2], 3);  // Minor 3rd (like natural minor)
  EXPECT_EQ(kScaleDorian[5], 9);  // Major 6th (unlike natural minor's 8)
}

TEST(ScaleArraysTest, MixolydianHasFlatSeventh) {
  EXPECT_EQ(kScaleMixolydian[2], 4);   // Major 3rd (like major)
  EXPECT_EQ(kScaleMixolydian[6], 10);  // Flat 7th (unlike major's 11)
}

// ---------------------------------------------------------------------------
// isDiatonicInKey -- minor scale union (natural + harmonic + melodic)
// ---------------------------------------------------------------------------

TEST(IsDiatonicInKeyTest, MinorKey_RaisedSeventhIsDiatonic) {
  // A minor: raised 7th = G# (pitch class 8, offset from A=9 is (8-9+12)%12=11)
  // G#3 = MIDI 56
  EXPECT_TRUE(isDiatonicInKey(56, Key::A, true));
  // G#4 = MIDI 68
  EXPECT_TRUE(isDiatonicInKey(68, Key::A, true));
}

TEST(IsDiatonicInKeyTest, MinorKey_RaisedSixthIsDiatonic) {
  // A minor: raised 6th = F# (pitch class 6, offset from A=9 is (6-9+12)%12=9)
  // F#4 = MIDI 66
  EXPECT_TRUE(isDiatonicInKey(66, Key::A, true));
}

TEST(IsDiatonicInKeyTest, MinorKey_NaturalSeventhAlsoDiatonic) {
  // A minor: natural 7th = G (pitch class 7)
  // G4 = MIDI 67
  EXPECT_TRUE(isDiatonicInKey(67, Key::A, true));
}

TEST(IsDiatonicInKeyTest, MinorKey_NaturalSixthAlsoDiatonic) {
  // A minor: natural 6th = F (pitch class 5)
  // F4 = MIDI 65
  EXPECT_TRUE(isDiatonicInKey(65, Key::A, true));
}

TEST(IsDiatonicInKeyTest, MinorKey_ChromaticStillDetected) {
  // A minor: D# is not in any minor scale form
  // D#4 = MIDI 63
  EXPECT_FALSE(isDiatonicInKey(63, Key::A, true));
}

TEST(IsDiatonicInKeyTest, DMinor_RaisedSeventhIsDiatonic) {
  // D minor: raised 7th = C# (pitch class 1)
  // C#4 = MIDI 61
  EXPECT_TRUE(isDiatonicInKey(61, Key::D, true));
}

TEST(IsDiatonicInKeyTest, DMinor_RaisedSixthIsDiatonic) {
  // D minor: raised 6th = B natural (pitch class 11)
  // B3 = MIDI 59
  EXPECT_TRUE(isDiatonicInKey(59, Key::D, true));
}

TEST(IsDiatonicInKeyTest, MajorKeyUnchanged) {
  // C major: F# should NOT be diatonic
  EXPECT_FALSE(isDiatonicInKey(66, Key::C, false));
  // C major: C should be diatonic
  EXPECT_TRUE(isDiatonicInKey(60, Key::C, false));
}

}  // namespace
}  // namespace bach
