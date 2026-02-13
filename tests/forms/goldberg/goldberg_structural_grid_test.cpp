// Tests for Goldberg Variations 32-bar structural grid.

#include "forms/goldberg/goldberg_structural_grid.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(GoldbergGridTest, Has32Bars) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Access all 32 bars without crash.
  for (int idx = 0; idx < 32; ++idx) {
    grid.getBar(idx);
  }
}

TEST(GoldbergGridTest, Bar1IsTonicG) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_EQ(grid.getStructuralBassPitch(0), 55);  // G3
  EXPECT_EQ(grid.getBar(0).function, HarmonicFunction::Tonic);
}

TEST(GoldbergGridTest, Bar16IsHalfCadence) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto cad = grid.getCadenceType(15);  // 0-indexed
  ASSERT_TRUE(cad.has_value());
  EXPECT_EQ(cad.value(), CadenceType::Half);
}

TEST(GoldbergGridTest, Bar32IsPerfectCadence) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto cad = grid.getCadenceType(31);
  ASSERT_TRUE(cad.has_value());
  EXPECT_EQ(cad.value(), CadenceType::Perfect);
}

TEST(GoldbergGridTest, Bar8IsPerfectCadence) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto cad = grid.getCadenceType(7);
  ASSERT_TRUE(cad.has_value());
  EXPECT_EQ(cad.value(), CadenceType::Perfect);
}

TEST(GoldbergGridTest, NonCadenceBarsHaveNoCadence) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Bar 1 (index 0) has no cadence.
  EXPECT_FALSE(grid.getCadenceType(0).has_value());
  // Bar 5 (index 4) has no cadence.
  EXPECT_FALSE(grid.getCadenceType(4).has_value());
}

TEST(GoldbergGridTest, PhrasePositionCycle) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_EQ(grid.getPhrasePosition(0), PhrasePosition::Opening);
  EXPECT_EQ(grid.getPhrasePosition(1), PhrasePosition::Expansion);
  EXPECT_EQ(grid.getPhrasePosition(2), PhrasePosition::Intensification);
  EXPECT_EQ(grid.getPhrasePosition(3), PhrasePosition::Cadence);
  EXPECT_EQ(grid.getPhrasePosition(4), PhrasePosition::Opening);
}

TEST(GoldbergGridTest, StructuralLevelBar32IsGlobal) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_EQ(grid.getStructuralLevel(31), StructuralLevel::Global32);
}

TEST(GoldbergGridTest, StructuralLevelBar16IsSection) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_EQ(grid.getStructuralLevel(15), StructuralLevel::Section16);
}

TEST(GoldbergGridTest, TensionPeakAtBars29to31) {
  auto grid = GoldbergStructuralGrid::createMajor();
  float t29 = grid.getAggregateTension(28);
  float t30 = grid.getAggregateTension(29);
  float t31 = grid.getAggregateTension(30);
  EXPECT_GT(t29, 0.5f);
  EXPECT_GT(t30, 0.5f);
  EXPECT_GT(t31, 0.5f);
}

TEST(GoldbergGridTest, TensionResolvesAtBar32) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_FLOAT_EQ(grid.getAggregateTension(31), 0.0f);
}

TEST(GoldbergGridTest, BassResolutionAtCadenceBars) {
  auto grid = GoldbergStructuralGrid::createMajor();
  EXPECT_TRUE(grid.hasBassResolution(7));    // Bar 8
  EXPECT_TRUE(grid.hasBassResolution(15));   // Bar 16
  EXPECT_FALSE(grid.hasBassResolution(0));   // Bar 1
}

TEST(GoldbergGridTest, Phrase4ViewCadence) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto phrase0 = grid.getPhrase4(0);
  EXPECT_EQ(phrase0.start_bar, 0);
  ASSERT_TRUE(phrase0.cadence.has_value());
  EXPECT_EQ(phrase0.cadence.value(), CadenceType::Half);
}

TEST(GoldbergGridTest, ToTimelineProducesEvents) {
  auto grid = GoldbergStructuralGrid::createMajor();
  KeySignature key = {Key::G, false};
  TimeSignature time_sig = {3, 4};
  auto timeline = grid.toTimeline(key, time_sig);
  EXPECT_GE(timeline.size(), 32u);
}

TEST(GoldbergGridTest, DescendingScaleBars1to8) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // First 8 bars are descending scale members.
  for (int idx = 0; idx < 8; ++idx) {
    EXPECT_TRUE(grid.getBassMotion(idx).is_descending_scale_member)
        << "Bar " << (idx + 1) << " should be descending scale member";
  }
}

TEST(GoldbergGridTest, NonDescendingScaleAfterBar8) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Bars 9-32 are not descending scale members.
  for (int idx = 8; idx < 32; ++idx) {
    EXPECT_FALSE(grid.getBassMotion(idx).is_descending_scale_member)
        << "Bar " << (idx + 1) << " should NOT be descending scale member";
  }
}

TEST(GoldbergGridTest, MinorGridCreatable) {
  auto grid = GoldbergStructuralGrid::createMinor(MinorModeProfile::MixedBaroqueMinor);
  EXPECT_EQ(grid.getStructuralBassPitch(0), 55);  // Still G
}

TEST(GoldbergGridTest, BarClampingDoesNotCrash) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Negative index clamps to 0.
  EXPECT_EQ(grid.getStructuralBassPitch(-1), grid.getStructuralBassPitch(0));
  // Out-of-range index clamps to 31.
  EXPECT_EQ(grid.getStructuralBassPitch(100), grid.getStructuralBassPitch(31));
}

TEST(GoldbergGridTest, AllStructuralBarsAreBar4) {
  auto grid = GoldbergStructuralGrid::createMajor();
  for (int idx = 0; idx < 32; ++idx) {
    const auto& bar_info = grid.getBar(idx);
    if (bar_info.is_structural_bar) {
      EXPECT_EQ(bar_info.bar_in_phrase, 4)
          << "Structural bar at index " << idx << " should have BIP=4";
    }
  }
}

TEST(GoldbergGridTest, EightPhraseGroups) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Verify all 8 phrase groups are represented.
  for (int phrase_idx = 0; phrase_idx < 8; ++phrase_idx) {
    auto view = grid.getPhrase4(phrase_idx);
    EXPECT_EQ(view.start_bar, phrase_idx * 4);
  }
}

TEST(GoldbergGridTest, Phrase8ViewStructure) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto section0 = grid.getPhrase8(0);
  EXPECT_EQ(section0.start_bar, 0);
  ASSERT_TRUE(section0.final_cadence.has_value());
  EXPECT_EQ(section0.final_cadence.value(), CadenceType::Perfect);
  EXPECT_EQ(section0.phrases[0].start_bar, 0);
  EXPECT_EQ(section0.phrases[1].start_bar, 4);
}

TEST(GoldbergGridTest, Section16ViewStructure) {
  auto grid = GoldbergStructuralGrid::createMajor();
  auto half0 = grid.getSection16(0);
  EXPECT_EQ(half0.start_bar, 0);
  ASSERT_TRUE(half0.section_cadence.has_value());
  EXPECT_EQ(half0.section_cadence.value(), CadenceType::Half);
  EXPECT_EQ(half0.phrases[0].start_bar, 0);
  EXPECT_EQ(half0.phrases[1].start_bar, 8);
}

TEST(GoldbergGridTest, SectionBoundaryDetection) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Phrase8 boundaries: bars 8, 24 (indices 7, 23).
  EXPECT_TRUE(grid.isSectionBoundary(7));
  EXPECT_TRUE(grid.isSectionBoundary(23));
  // Section16 boundary: bar 16 (index 15).
  EXPECT_TRUE(grid.isSectionBoundary(15));
  // Global32 boundary: bar 32 (index 31).
  EXPECT_TRUE(grid.isSectionBoundary(31));
  // Non-boundary bars.
  EXPECT_FALSE(grid.isSectionBoundary(0));
  EXPECT_FALSE(grid.isSectionBoundary(5));
}

TEST(GoldbergGridTest, TimelineTickPositions) {
  auto grid = GoldbergStructuralGrid::createMajor();
  KeySignature key = {Key::G, false};
  TimeSignature time_sig = {3, 4};
  auto timeline = grid.toTimeline(key, time_sig);

  // 3/4 time: ticksPerBar = 3 * 480 = 1440
  Tick expected_tpb = 1440;
  const auto& events = timeline.events();
  ASSERT_EQ(events.size(), 32u);
  for (size_t idx = 0; idx < events.size(); ++idx) {
    EXPECT_EQ(events[idx].tick, static_cast<Tick>(idx) * expected_tpb)
        << "Event " << idx << " tick mismatch";
    EXPECT_EQ(events[idx].end_tick, static_cast<Tick>(idx + 1) * expected_tpb)
        << "Event " << idx << " end_tick mismatch";
  }
}

TEST(GoldbergGridTest, TimelineKeyIsG) {
  auto grid = GoldbergStructuralGrid::createMajor();
  KeySignature key = {Key::G, false};
  TimeSignature time_sig = {3, 4};
  auto timeline = grid.toTimeline(key, time_sig);

  for (size_t idx = 0; idx < timeline.size(); ++idx) {
    EXPECT_EQ(timeline.events()[idx].key, Key::G)
        << "Event " << idx << " should be in G";
    EXPECT_FALSE(timeline.events()[idx].is_minor)
        << "Event " << idx << " should be major";
  }
}

TEST(GoldbergGridTest, DescendingScaleIndicesSequential) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Descending scale indices should be 0-7 for bars 1-8.
  for (int idx = 0; idx < 8; ++idx) {
    EXPECT_EQ(grid.getBassMotion(idx).scale_degree_index, static_cast<uint8_t>(idx))
        << "Bar " << (idx + 1) << " scale degree index mismatch";
  }
}

TEST(GoldbergGridTest, BassPitchesFirstHalf) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // Verify the descending bass line for bars 1-8.
  // G(55), F#(54), E(52), D(50), C(48), B(47), A(45), B(47)
  EXPECT_EQ(grid.getStructuralBassPitch(0), 55);
  EXPECT_EQ(grid.getStructuralBassPitch(1), 54);
  EXPECT_EQ(grid.getStructuralBassPitch(2), 52);
  EXPECT_EQ(grid.getStructuralBassPitch(3), 50);
  EXPECT_EQ(grid.getStructuralBassPitch(4), 48);
  EXPECT_EQ(grid.getStructuralBassPitch(5), 47);
  EXPECT_EQ(grid.getStructuralBassPitch(6), 45);
  EXPECT_EQ(grid.getStructuralBassPitch(7), 47);
}

TEST(GoldbergGridTest, CadenceBarsFall4thBarOfPhrase) {
  auto grid = GoldbergStructuralGrid::createMajor();
  // All cadence bars should be the 4th bar of their phrase.
  for (int idx = 0; idx < 32; ++idx) {
    if (grid.isCadenceBar(idx)) {
      EXPECT_EQ(grid.getBarInPhrase(idx), 4)
          << "Cadence at bar " << (idx + 1) << " should be BIP=4";
    }
  }
}

}  // namespace
}  // namespace bach
