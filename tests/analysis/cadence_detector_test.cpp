// Tests for analysis/cadence_detector.h -- read-only cadence detection
// from harmonic timelines.

#include "analysis/cadence_detector.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// ===========================================================================
// Helpers
// ===========================================================================

/// @brief Create a harmonic event with the given parameters.
HarmonicEvent makeEvent(Tick tick, Tick end_tick, ChordDegree degree,
                        ChordQuality quality = ChordQuality::Major,
                        bool is_minor = false, uint8_t inversion = 0) {
  HarmonicEvent evt;
  evt.tick = tick;
  evt.end_tick = end_tick;
  evt.key = Key::C;
  evt.is_minor = is_minor;
  evt.chord.degree = degree;
  evt.chord.quality = quality;
  evt.chord.root_pitch = 60;
  evt.chord.inversion = inversion;
  evt.weight = 1.0f;
  return evt;
}

/// @brief Build a timeline with a V -> I progression (perfect cadence).
HarmonicTimeline makeV_I_Timeline() {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V,
                        ChordQuality::Dominant7));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::I));
  return tl;
}

/// @brief Build a timeline with a V -> vi progression (deceptive cadence).
HarmonicTimeline makeV_vi_Timeline() {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V,
                        ChordQuality::Dominant7));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::vi,
                        ChordQuality::Minor));
  return tl;
}

/// @brief Build a timeline with a IV -> V progression (half cadence).
HarmonicTimeline makeIV_V_Timeline() {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::IV));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::V));
  return tl;
}

// ===========================================================================
// detectCadences -- Perfect cadence
// ===========================================================================

TEST(CadenceDetectorTest, DetectPerfectCadence) {
  auto tl = makeV_I_Timeline();
  auto cadences = detectCadences(tl);

  // Should detect V7->I as perfect cadence.
  ASSERT_GE(cadences.size(), 1u);

  bool found_perfect = false;
  for (const auto& cad : cadences) {
    if (cad.type == CadenceType::Perfect) {
      found_perfect = true;
      EXPECT_EQ(cad.tick, kTicksPerBar * 2);
      EXPECT_GE(cad.confidence, 0.9f);
    }
  }
  EXPECT_TRUE(found_perfect) << "Perfect cadence V7->I not detected";
}

TEST(CadenceDetectorTest, PerfectCadenceWithoutSeventh) {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::V,
                        ChordQuality::Major));  // V without 7th
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I));
  auto cadences = detectCadences(tl);

  ASSERT_GE(cadences.size(), 1u);
  EXPECT_EQ(cadences[0].type, CadenceType::Perfect);
  // Confidence should be slightly lower without the 7th.
  EXPECT_GE(cadences[0].confidence, 0.9f);
  EXPECT_LT(cadences[0].confidence, 0.95f);
}

// ===========================================================================
// detectCadences -- Deceptive cadence
// ===========================================================================

TEST(CadenceDetectorTest, DetectDeceptiveCadence) {
  auto tl = makeV_vi_Timeline();
  auto cadences = detectCadences(tl);

  bool found_deceptive = false;
  for (const auto& cad : cadences) {
    if (cad.type == CadenceType::Deceptive) {
      found_deceptive = true;
      EXPECT_EQ(cad.tick, kTicksPerBar * 2);
      EXPECT_FLOAT_EQ(cad.confidence, 0.8f);
    }
  }
  EXPECT_TRUE(found_deceptive) << "Deceptive cadence V->vi not detected";
}

// ===========================================================================
// detectCadences -- Half cadence
// ===========================================================================

TEST(CadenceDetectorTest, DetectHalfCadence) {
  auto tl = makeIV_V_Timeline();
  auto cadences = detectCadences(tl);

  bool found_half = false;
  for (const auto& cad : cadences) {
    if (cad.type == CadenceType::Half) {
      found_half = true;
      EXPECT_EQ(cad.tick, kTicksPerBar * 2);
      EXPECT_FLOAT_EQ(cad.confidence, 0.7f);
    }
  }
  EXPECT_TRUE(found_half) << "Half cadence *->V not detected";
}

// ===========================================================================
// detectCadences -- Phrygian cadence
// ===========================================================================

TEST(CadenceDetectorTest, DetectPhrygianCadence) {
  HarmonicTimeline tl;
  // iv6 -> V in minor key.
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::IV,
                        ChordQuality::Minor, /*is_minor=*/true,
                        /*inversion=*/1));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V,
                        ChordQuality::Major, /*is_minor=*/true));
  auto cadences = detectCadences(tl);

  ASSERT_GE(cadences.size(), 1u);
  EXPECT_EQ(cadences[0].type, CadenceType::Phrygian);
  EXPECT_FLOAT_EQ(cadences[0].confidence, 0.85f);
}

// ===========================================================================
// detectCadences -- Plagal cadence
// ===========================================================================

TEST(CadenceDetectorTest, DetectPlagalCadence) {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::IV));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::I));

  auto cadences = detectCadences(tl);

  bool found_plagal = false;
  for (const auto& cad : cadences) {
    if (cad.type == CadenceType::Plagal) {
      found_plagal = true;
      EXPECT_EQ(cad.tick, kTicksPerBar * 2);
      EXPECT_FLOAT_EQ(cad.confidence, 0.8f);
    }
  }
  EXPECT_TRUE(found_plagal) << "Plagal cadence IV->I not detected";
}

TEST(CadenceDetectorTest, PlagalNotConfusedWithPerfect) {
  // IV->I should be Plagal, not Perfect (V->I).
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::IV));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I));

  auto cadences = detectCadences(tl);
  ASSERT_GE(cadences.size(), 1u);
  EXPECT_EQ(cadences[0].type, CadenceType::Plagal);
}

// ===========================================================================
// detectCadences -- Edge cases
// ===========================================================================

TEST(CadenceDetectorTest, EmptyTimeline) {
  HarmonicTimeline tl;
  auto cadences = detectCadences(tl);
  EXPECT_TRUE(cadences.empty());
}

TEST(CadenceDetectorTest, SingleEvent) {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  auto cadences = detectCadences(tl);
  EXPECT_TRUE(cadences.empty());
}

TEST(CadenceDetectorTest, NoCadencePattern) {
  HarmonicTimeline tl;
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::IV));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::ii,
                        ChordQuality::Minor));
  auto cadences = detectCadences(tl);

  // No V->I, V->vi, or *->V pattern. Should be empty.
  EXPECT_TRUE(cadences.empty());
}

TEST(CadenceDetectorTest, MultipleCadences) {
  HarmonicTimeline tl;
  // V -> I at bar 1, then V -> vi at bar 3.
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::V,
                        ChordQuality::Dominant7));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::I));
  tl.addEvent(makeEvent(kTicksPerBar * 2, kTicksPerBar * 3, ChordDegree::V,
                        ChordQuality::Dominant7));
  tl.addEvent(makeEvent(kTicksPerBar * 3, kTicksPerBar * 4, ChordDegree::vi,
                        ChordQuality::Minor));

  auto cadences = detectCadences(tl);
  EXPECT_GE(cadences.size(), 2u);

  // Verify sorted by tick.
  for (size_t idx = 1; idx < cadences.size(); ++idx) {
    EXPECT_GE(cadences[idx].tick, cadences[idx - 1].tick);
  }
}

TEST(CadenceDetectorTest, VtoVNotDetectedAsHalf) {
  HarmonicTimeline tl;
  // V -> V should NOT be detected as a half cadence.
  tl.addEvent(makeEvent(0, kTicksPerBar, ChordDegree::V));
  tl.addEvent(makeEvent(kTicksPerBar, kTicksPerBar * 2, ChordDegree::V));

  auto cadences = detectCadences(tl);
  for (const auto& cad : cadences) {
    EXPECT_NE(cad.type, CadenceType::Half)
        << "V->V should not be detected as half cadence";
  }
}

// ===========================================================================
// cadenceDetectionRate
// ===========================================================================

TEST(CadenceDetectionRateTest, DetectionRateAllMatched) {
  std::vector<DetectedCadence> detected;
  detected.push_back({CadenceType::Perfect, kTicksPerBar * 2, 0.95f});
  detected.push_back({CadenceType::Perfect, kTicksPerBar * 6, 0.9f});

  std::vector<Tick> planned = {kTicksPerBar * 2, kTicksPerBar * 6};

  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(CadenceDetectionRateTest, DetectionRatePartialMatch) {
  std::vector<DetectedCadence> detected;
  detected.push_back({CadenceType::Perfect, kTicksPerBar * 2, 0.9f});

  // Two planned, but only one is near a detected cadence.
  std::vector<Tick> planned = {kTicksPerBar * 2, kTicksPerBar * 10};

  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 0.5f);
}

TEST(CadenceDetectionRateTest, DetectionRateEmptyInput) {
  std::vector<DetectedCadence> detected;
  std::vector<Tick> planned;

  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(CadenceDetectionRateTest, DetectionRateEmptyDetected) {
  std::vector<DetectedCadence> detected;
  std::vector<Tick> planned = {kTicksPerBar, kTicksPerBar * 2};

  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(CadenceDetectionRateTest, ToleranceWindowMatches) {
  std::vector<DetectedCadence> detected;
  // Detected slightly before the planned tick.
  detected.push_back({CadenceType::Perfect,
                      kTicksPerBar * 2 + kTicksPerBeat / 2, 0.9f});

  std::vector<Tick> planned = {kTicksPerBar * 2};

  // Within default tolerance (kTicksPerBeat = 480).
  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(CadenceDetectionRateTest, ToleranceWindowMisses) {
  std::vector<DetectedCadence> detected;
  // Detected far from the planned tick.
  detected.push_back({CadenceType::Perfect, kTicksPerBar * 5, 0.9f});

  std::vector<Tick> planned = {kTicksPerBar * 2};

  float rate = cadenceDetectionRate(detected, planned);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(CadenceDetectionRateTest, CustomTolerance) {
  std::vector<DetectedCadence> detected;
  // Detected 2 beats away from planned.
  detected.push_back({CadenceType::Perfect,
                      kTicksPerBar * 2 + kTicksPerBeat * 2, 0.9f});

  std::vector<Tick> planned = {kTicksPerBar * 2};

  // Default tolerance (1 beat) should miss.
  float rate_default = cadenceDetectionRate(detected, planned, kTicksPerBeat);
  EXPECT_FLOAT_EQ(rate_default, 0.0f);

  // Wider tolerance (3 beats) should match.
  float rate_wide = cadenceDetectionRate(detected, planned, kTicksPerBeat * 3);
  EXPECT_FLOAT_EQ(rate_wide, 1.0f);
}

}  // namespace
}  // namespace bach
