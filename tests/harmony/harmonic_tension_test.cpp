// Tests for harmonic tension computation.

#include "harmony/harmonic_tension.h"

#include <gtest/gtest.h>

#include "harmony/harmonic_event.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Tests for computeHarmonicTension(ChordDegree, ChordQuality, int)
// ---------------------------------------------------------------------------

TEST(HarmonicTensionTest, TonicReturnsZero) {
  EXPECT_FLOAT_EQ(0.0f, computeHarmonicTension(ChordDegree::I, ChordQuality::Major));
}

TEST(HarmonicTensionTest, SubmediantReturnsZero) {
  EXPECT_FLOAT_EQ(0.0f, computeHarmonicTension(ChordDegree::vi, ChordQuality::Minor));
}

TEST(HarmonicTensionTest, MediantReturnsLowTension) {
  EXPECT_FLOAT_EQ(0.2f, computeHarmonicTension(ChordDegree::iii, ChordQuality::Minor));
}

TEST(HarmonicTensionTest, SubdominantReturnsModerateTension) {
  EXPECT_FLOAT_EQ(0.3f, computeHarmonicTension(ChordDegree::IV, ChordQuality::Major));
}

TEST(HarmonicTensionTest, SupertonicReturnsModerateTension) {
  EXPECT_FLOAT_EQ(0.3f, computeHarmonicTension(ChordDegree::ii, ChordQuality::Minor));
}

TEST(HarmonicTensionTest, DominantTriadReturns06) {
  EXPECT_FLOAT_EQ(0.6f, computeHarmonicTension(ChordDegree::V, ChordQuality::Major));
}

TEST(HarmonicTensionTest, Dominant7RootPositionReturns08) {
  EXPECT_FLOAT_EQ(0.8f, computeHarmonicTension(ChordDegree::V, ChordQuality::Dominant7, 0));
}

TEST(HarmonicTensionTest, Dominant7FirstInversionReturns07) {
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V, ChordQuality::Dominant7, 1));
}

TEST(HarmonicTensionTest, Dominant7ThirdInversionReturns09) {
  EXPECT_FLOAT_EQ(0.9f, computeHarmonicTension(ChordDegree::V, ChordQuality::Dominant7, 3));
}

TEST(HarmonicTensionTest, Dominant7SecondInversionDefaultsTo08) {
  EXPECT_FLOAT_EQ(0.8f, computeHarmonicTension(ChordDegree::V, ChordQuality::Dominant7, 2));
}

TEST(HarmonicTensionTest, SecondaryDominantsReturn07) {
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V_of_V, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V_of_vi, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V_of_IV, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V_of_ii, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.7f, computeHarmonicTension(ChordDegree::V_of_iii, ChordQuality::Major));
}

TEST(HarmonicTensionTest, DiminishedTriadReturns09) {
  EXPECT_FLOAT_EQ(0.9f, computeHarmonicTension(ChordDegree::viiDim, ChordQuality::Diminished));
}

TEST(HarmonicTensionTest, FullyDiminished7Returns10) {
  EXPECT_FLOAT_EQ(1.0f,
                  computeHarmonicTension(ChordDegree::viiDim, ChordQuality::Diminished7));
}

TEST(HarmonicTensionTest, NeapolitanReturns06) {
  EXPECT_FLOAT_EQ(0.6f, computeHarmonicTension(ChordDegree::bII, ChordQuality::Major));
}

TEST(HarmonicTensionTest, BorrowedChordsReturn04) {
  EXPECT_FLOAT_EQ(0.4f, computeHarmonicTension(ChordDegree::bVI, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.4f, computeHarmonicTension(ChordDegree::bVII, ChordQuality::Major));
  EXPECT_FLOAT_EQ(0.4f, computeHarmonicTension(ChordDegree::bIII, ChordQuality::Major));
}

TEST(HarmonicTensionTest, DefaultInversionIsZero) {
  // Without explicit inversion, V7 should behave as root position (0.8).
  EXPECT_FLOAT_EQ(0.8f, computeHarmonicTension(ChordDegree::V, ChordQuality::Dominant7));
}

// ---------------------------------------------------------------------------
// Tests for computeHarmonicTension(const HarmonicEvent&)
// ---------------------------------------------------------------------------

TEST(HarmonicTensionTest, EventOverloadDelegatesToDegreeOverload) {
  HarmonicEvent harm;
  harm.chord.degree = ChordDegree::V;
  harm.chord.quality = ChordQuality::Dominant7;
  harm.chord.inversion = 3;

  EXPECT_FLOAT_EQ(0.9f, computeHarmonicTension(harm));
}

TEST(HarmonicTensionTest, EventOverloadTonicChord) {
  HarmonicEvent harm;
  harm.chord.degree = ChordDegree::I;
  harm.chord.quality = ChordQuality::Major;
  harm.chord.inversion = 0;

  EXPECT_FLOAT_EQ(0.0f, computeHarmonicTension(harm));
}

TEST(HarmonicTensionTest, EventOverloadDominantTriad) {
  HarmonicEvent harm;
  harm.chord.degree = ChordDegree::V;
  harm.chord.quality = ChordQuality::Major;
  harm.chord.inversion = 0;

  EXPECT_FLOAT_EQ(0.6f, computeHarmonicTension(harm));
}

TEST(HarmonicTensionTest, EventOverloadDiminished7) {
  HarmonicEvent harm;
  harm.chord.degree = ChordDegree::viiDim;
  harm.chord.quality = ChordQuality::Diminished7;
  harm.chord.inversion = 0;

  EXPECT_FLOAT_EQ(1.0f, computeHarmonicTension(harm));
}

// ---------------------------------------------------------------------------
// Range validation
// ---------------------------------------------------------------------------

TEST(HarmonicTensionTest, AllResultsInUnitRange) {
  // Test all ChordDegree values with a few qualities to ensure [0.0, 1.0].
  ChordDegree degrees[] = {
      ChordDegree::I,      ChordDegree::ii,      ChordDegree::iii,
      ChordDegree::IV,     ChordDegree::V,       ChordDegree::vi,
      ChordDegree::viiDim, ChordDegree::bII,     ChordDegree::V_of_V,
      ChordDegree::V_of_vi, ChordDegree::V_of_IV, ChordDegree::V_of_ii,
      ChordDegree::bVI,    ChordDegree::bVII,    ChordDegree::bIII,
      ChordDegree::V_of_iii};
  ChordQuality qualities[] = {ChordQuality::Major, ChordQuality::Minor,
                               ChordQuality::Dominant7, ChordQuality::Diminished7};

  for (auto deg : degrees) {
    for (auto qual : qualities) {
      for (int inv = 0; inv <= 3; ++inv) {
        float tension = computeHarmonicTension(deg, qual, inv);
        EXPECT_GE(tension, 0.0f)
            << "degree=" << static_cast<int>(deg)
            << " quality=" << static_cast<int>(qual) << " inv=" << inv;
        EXPECT_LE(tension, 1.0f)
            << "degree=" << static_cast<int>(deg)
            << " quality=" << static_cast<int>(qual) << " inv=" << inv;
      }
    }
  }
}

}  // namespace
}  // namespace bach
