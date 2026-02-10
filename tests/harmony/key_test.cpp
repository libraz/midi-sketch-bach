// Tests for harmony/key.h -- key relationships, circle of fifths, parsing.

#include "harmony/key.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getDominant
// ---------------------------------------------------------------------------

TEST(GetDominantTest, CMajor) {
  KeySignature c_major = {Key::C, false};
  KeySignature result = getDominant(c_major);
  EXPECT_EQ(result.tonic, Key::G);
  EXPECT_FALSE(result.is_minor);
}

TEST(GetDominantTest, GMinor) {
  KeySignature g_minor = {Key::G, true};
  KeySignature result = getDominant(g_minor);
  EXPECT_EQ(result.tonic, Key::D);
  EXPECT_TRUE(result.is_minor);  // Same mode.
}

TEST(GetDominantTest, FMajor) {
  KeySignature f_major = {Key::F, false};
  KeySignature result = getDominant(f_major);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_FALSE(result.is_minor);
}

TEST(GetDominantTest, BbMajor) {
  KeySignature bb_major = {Key::Bb, false};
  KeySignature result = getDominant(bb_major);
  EXPECT_EQ(result.tonic, Key::F);
  EXPECT_FALSE(result.is_minor);
}

// ---------------------------------------------------------------------------
// getSubdominant
// ---------------------------------------------------------------------------

TEST(GetSubdominantTest, CMajor) {
  KeySignature c_major = {Key::C, false};
  KeySignature result = getSubdominant(c_major);
  EXPECT_EQ(result.tonic, Key::F);
  EXPECT_FALSE(result.is_minor);
}

TEST(GetSubdominantTest, DMinor) {
  KeySignature d_minor = {Key::D, true};
  KeySignature result = getSubdominant(d_minor);
  EXPECT_EQ(result.tonic, Key::G);
  EXPECT_TRUE(result.is_minor);
}

TEST(GetSubdominantTest, GMajor) {
  KeySignature g_major = {Key::G, false};
  KeySignature result = getSubdominant(g_major);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_FALSE(result.is_minor);
}

// ---------------------------------------------------------------------------
// getRelative
// ---------------------------------------------------------------------------

TEST(GetRelativeTest, CMajorRelativeMinor) {
  KeySignature c_major = {Key::C, false};
  KeySignature result = getRelative(c_major);
  EXPECT_EQ(result.tonic, Key::A);
  EXPECT_TRUE(result.is_minor);
}

TEST(GetRelativeTest, AMinorRelativeMajor) {
  KeySignature a_minor = {Key::A, true};
  KeySignature result = getRelative(a_minor);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_FALSE(result.is_minor);
}

TEST(GetRelativeTest, EbMajorRelativeMinor) {
  // Eb major -> C minor (3 semitones down from Eb(3) = C? No: +9 mod 12 = 0 -> C)
  KeySignature eb_major = {Key::Eb, false};
  KeySignature result = getRelative(eb_major);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_TRUE(result.is_minor);
}

TEST(GetRelativeTest, DMinorRelativeMajor) {
  // D minor -> F major (3 semitones up from D(2) = 5 -> F)
  KeySignature d_minor = {Key::D, true};
  KeySignature result = getRelative(d_minor);
  EXPECT_EQ(result.tonic, Key::F);
  EXPECT_FALSE(result.is_minor);
}

// ---------------------------------------------------------------------------
// getParallel
// ---------------------------------------------------------------------------

TEST(GetParallelTest, CMajorToMinor) {
  KeySignature c_major = {Key::C, false};
  KeySignature result = getParallel(c_major);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_TRUE(result.is_minor);
}

TEST(GetParallelTest, CMinorToMajor) {
  KeySignature c_minor = {Key::C, true};
  KeySignature result = getParallel(c_minor);
  EXPECT_EQ(result.tonic, Key::C);
  EXPECT_FALSE(result.is_minor);
}

TEST(GetParallelTest, RoundTrip) {
  KeySignature g_major = {Key::G, false};
  KeySignature result = getParallel(getParallel(g_major));
  EXPECT_EQ(result, g_major);
}

// ---------------------------------------------------------------------------
// isCloselyRelated
// ---------------------------------------------------------------------------

TEST(IsCloselyRelatedTest, SameKey) {
  KeySignature c_major = {Key::C, false};
  EXPECT_TRUE(isCloselyRelated(c_major, c_major));
}

TEST(IsCloselyRelatedTest, DominantIsClose) {
  KeySignature c_major = {Key::C, false};
  KeySignature g_major = {Key::G, false};
  EXPECT_TRUE(isCloselyRelated(c_major, g_major));
}

TEST(IsCloselyRelatedTest, SubdominantIsClose) {
  KeySignature c_major = {Key::C, false};
  KeySignature f_major = {Key::F, false};
  EXPECT_TRUE(isCloselyRelated(c_major, f_major));
}

TEST(IsCloselyRelatedTest, RelativeIsClose) {
  KeySignature c_major = {Key::C, false};
  KeySignature a_minor = {Key::A, true};
  EXPECT_TRUE(isCloselyRelated(c_major, a_minor));
}

TEST(IsCloselyRelatedTest, DistantKeyIsNotClose) {
  KeySignature c_major = {Key::C, false};
  KeySignature fs_major = {Key::Fs, false};
  EXPECT_FALSE(isCloselyRelated(c_major, fs_major));
}

TEST(IsCloselyRelatedTest, Symmetry) {
  KeySignature c_major = {Key::C, false};
  KeySignature g_major = {Key::G, false};
  EXPECT_EQ(isCloselyRelated(c_major, g_major), isCloselyRelated(g_major, c_major));
}

// ---------------------------------------------------------------------------
// circleOfFifthsDistance
// ---------------------------------------------------------------------------

TEST(CircleOfFifthsDistanceTest, SameKey) {
  KeySignature c_major = {Key::C, false};
  EXPECT_EQ(circleOfFifthsDistance(c_major, c_major), 0);
}

TEST(CircleOfFifthsDistanceTest, OneStepApart) {
  KeySignature c_major = {Key::C, false};
  KeySignature g_major = {Key::G, false};
  EXPECT_EQ(circleOfFifthsDistance(c_major, g_major), 1);
}

TEST(CircleOfFifthsDistanceTest, TwoStepsApart) {
  KeySignature c_major = {Key::C, false};
  KeySignature d_major = {Key::D, false};
  EXPECT_EQ(circleOfFifthsDistance(c_major, d_major), 2);
}

TEST(CircleOfFifthsDistanceTest, MaxDistance) {
  // C to F# is 6 steps on the circle of fifths (or 6 going either way).
  KeySignature c_major = {Key::C, false};
  KeySignature fs_major = {Key::Fs, false};
  EXPECT_EQ(circleOfFifthsDistance(c_major, fs_major), 6);
}

TEST(CircleOfFifthsDistanceTest, MixedModes) {
  // C major to A minor: A minor's relative is C major, so distance = 0.
  KeySignature c_major = {Key::C, false};
  KeySignature a_minor = {Key::A, true};
  EXPECT_EQ(circleOfFifthsDistance(c_major, a_minor), 0);
}

TEST(CircleOfFifthsDistanceTest, MixedModesOneStep) {
  // C major to E minor: E minor's relative is G major. G major is 1 step from C major.
  KeySignature c_major = {Key::C, false};
  KeySignature e_minor = {Key::E, true};
  EXPECT_EQ(circleOfFifthsDistance(c_major, e_minor), 1);
}

TEST(CircleOfFifthsDistanceTest, Symmetry) {
  KeySignature c_major = {Key::C, false};
  KeySignature d_major = {Key::D, false};
  EXPECT_EQ(circleOfFifthsDistance(c_major, d_major),
            circleOfFifthsDistance(d_major, c_major));
}

// ---------------------------------------------------------------------------
// getCloselyRelatedKeys
// ---------------------------------------------------------------------------

TEST(GetCloselyRelatedKeysTest, CMajorIncludes) {
  KeySignature c_major = {Key::C, false};
  auto keys = getCloselyRelatedKeys(c_major);

  // Should include at least: C major, G major, F major, A minor.
  auto has_key = [&](Key tonic, bool minor) {
    return std::any_of(keys.begin(), keys.end(), [&](const KeySignature& ks) {
      return ks.tonic == tonic && ks.is_minor == minor;
    });
  };

  EXPECT_TRUE(has_key(Key::C, false));   // Self
  EXPECT_TRUE(has_key(Key::G, false));   // Dominant
  EXPECT_TRUE(has_key(Key::F, false));   // Subdominant
  EXPECT_TRUE(has_key(Key::A, true));    // Relative minor
}

TEST(GetCloselyRelatedKeysTest, IncludesParallel) {
  KeySignature c_major = {Key::C, false};
  auto keys = getCloselyRelatedKeys(c_major);

  auto has_key = [&](Key tonic, bool minor) {
    return std::any_of(keys.begin(), keys.end(), [&](const KeySignature& ks) {
      return ks.tonic == tonic && ks.is_minor == minor;
    });
  };

  EXPECT_TRUE(has_key(Key::C, true));  // Parallel minor
}

TEST(GetCloselyRelatedKeysTest, MinorKeyIncludes) {
  KeySignature a_minor = {Key::A, true};
  auto keys = getCloselyRelatedKeys(a_minor);

  auto has_key = [&](Key tonic, bool minor) {
    return std::any_of(keys.begin(), keys.end(), [&](const KeySignature& ks) {
      return ks.tonic == tonic && ks.is_minor == minor;
    });
  };

  EXPECT_TRUE(has_key(Key::A, true));   // Self
  EXPECT_TRUE(has_key(Key::E, true));   // Dominant (minor)
  EXPECT_TRUE(has_key(Key::D, true));   // Subdominant (minor)
  EXPECT_TRUE(has_key(Key::C, false));  // Relative major
}

TEST(GetCloselyRelatedKeysTest, ResultSize) {
  KeySignature c_major = {Key::C, false};
  auto keys = getCloselyRelatedKeys(c_major);
  // Self + dominant + subdominant + relative + parallel + dom_of_relative (if unique).
  EXPECT_GE(keys.size(), 4u);
  EXPECT_LE(keys.size(), 6u);
}

// ---------------------------------------------------------------------------
// tonicPitch
// ---------------------------------------------------------------------------

TEST(TonicPitchTest, C4) {
  EXPECT_EQ(tonicPitch(Key::C, 4), 60);  // C4 = MIDI 60
}

TEST(TonicPitchTest, A4) {
  EXPECT_EQ(tonicPitch(Key::A, 4), 69);  // A4 = MIDI 69
}

TEST(TonicPitchTest, G3) {
  EXPECT_EQ(tonicPitch(Key::G, 3), 55);  // G3 = MIDI 55
}

TEST(TonicPitchTest, DefaultOctave) {
  // Default octave is 4.
  EXPECT_EQ(tonicPitch(Key::C), 60);
}

TEST(TonicPitchTest, LowOctave) {
  EXPECT_EQ(tonicPitch(Key::C, 1), 24);  // C1 = MIDI 24
}

// ---------------------------------------------------------------------------
// keySignatureFromString
// ---------------------------------------------------------------------------

TEST(KeySignatureFromStringTest, CMajor) {
  auto ks = keySignatureFromString("C_major");
  EXPECT_EQ(ks.tonic, Key::C);
  EXPECT_FALSE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, GMinor) {
  auto ks = keySignatureFromString("g_minor");
  EXPECT_EQ(ks.tonic, Key::G);
  EXPECT_TRUE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, DMinor) {
  auto ks = keySignatureFromString("D_minor");
  EXPECT_EQ(ks.tonic, Key::D);
  EXPECT_TRUE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, EbMajor) {
  auto ks = keySignatureFromString("Eb_major");
  EXPECT_EQ(ks.tonic, Key::Eb);
  EXPECT_FALSE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, FsMinor) {
  auto ks = keySignatureFromString("Fs_minor");
  EXPECT_EQ(ks.tonic, Key::Fs);
  EXPECT_TRUE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, BbMinor) {
  auto ks = keySignatureFromString("Bb_minor");
  EXPECT_EQ(ks.tonic, Key::Bb);
  EXPECT_TRUE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, FSharpNotation) {
  // Support F# notation as well as Fs.
  auto ks = keySignatureFromString("F#_minor");
  EXPECT_EQ(ks.tonic, Key::Fs);
  EXPECT_TRUE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, UnrecognizedDefaultsToCMajor) {
  auto ks = keySignatureFromString("xyz");
  EXPECT_EQ(ks.tonic, Key::C);
  EXPECT_FALSE(ks.is_minor);
}

TEST(KeySignatureFromStringTest, CaseInsensitive) {
  auto ks = keySignatureFromString("G_MAJOR");
  EXPECT_EQ(ks.tonic, Key::G);
  EXPECT_FALSE(ks.is_minor);
}

// ---------------------------------------------------------------------------
// keySignatureToString
// ---------------------------------------------------------------------------

TEST(KeySignatureToStringTest, CMajor) {
  KeySignature ks = {Key::C, false};
  EXPECT_EQ(keySignatureToString(ks), "C_major");
}

TEST(KeySignatureToStringTest, GMinor) {
  KeySignature ks = {Key::G, true};
  EXPECT_EQ(keySignatureToString(ks), "G_minor");
}

TEST(KeySignatureToStringTest, FsMajor) {
  KeySignature ks = {Key::Fs, false};
  EXPECT_EQ(keySignatureToString(ks), "F#_major");
}

TEST(KeySignatureToStringTest, BbMinor) {
  KeySignature ks = {Key::Bb, true};
  EXPECT_EQ(keySignatureToString(ks), "Bb_minor");
}

// ---------------------------------------------------------------------------
// Round-trip: fromString -> toString
// ---------------------------------------------------------------------------

TEST(KeySignatureRoundTripTest, MajorKeys) {
  KeySignature ks = {Key::D, false};
  auto str = keySignatureToString(ks);
  auto parsed = keySignatureFromString(str);
  EXPECT_EQ(parsed, ks);
}

TEST(KeySignatureRoundTripTest, MinorKeys) {
  KeySignature ks = {Key::E, true};
  auto str = keySignatureToString(ks);
  auto parsed = keySignatureFromString(str);
  EXPECT_EQ(parsed, ks);
}

}  // namespace
}  // namespace bach
