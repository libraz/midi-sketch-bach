#include "composer/material.h"

#include <gtest/gtest.h>

namespace bach::composer {
namespace {

MaterialNote note(Tick start, std::uint8_t pitch) {
  MaterialNote n;
  n.start_tick = start;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  return n;
}

}  // namespace

TEST(MaterialTest, AnnotatesSubjectLeadingToneResolution) {
  Material material;
  material.subject = {
      note(0, 67),
      note(kTicksPerBeat, 71),
      note(2 * kTicksPerBeat, 72),
  };

  annotateLeadingToneMarkers(material, 0, false);

  ASSERT_EQ(material.leading_tone_markers.size(), 1u);
  const auto& marker = material.leading_tone_markers.front();
  EXPECT_EQ(marker.fragment, MaterialFragment::Subject);
  EXPECT_EQ(marker.leading_index, 1u);
  EXPECT_EQ(marker.resolution_index, 2u);
  EXPECT_EQ(marker.leading_pitch, 71u);
  EXPECT_EQ(marker.resolution_pitch, 72u);
  EXPECT_EQ(marker.tonic_pc, 0u);
}

TEST(MaterialTest, IgnoresUnresolvedLeadingTone) {
  Material material;
  material.subject = {
      note(0, 71),
      note(kTicksPerBeat, 69),
  };

  annotateLeadingToneMarkers(material, 0, false);

  EXPECT_TRUE(material.leading_tone_markers.empty());
}

TEST(MaterialTest, AnnotatesAnswerLeadingToneResolution) {
  Material material;
  material.answer = {
      note(0, 59),
      note(kTicksPerBeat, 60),
  };

  annotateLeadingToneMarkers(material, 0, false);

  ASSERT_EQ(material.leading_tone_markers.size(), 1u);
  EXPECT_EQ(material.leading_tone_markers.front().fragment, MaterialFragment::Answer);
}

TEST(MaterialTest, AnnotatesCadenceCellsFromHarmonicPlan) {
  Material material;
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  CadenceEvent cadence;
  cadence.tick = 2 * kTicksPerBeat;
  cadence.type = CadenceType::Perfect;
  plan.cadences.push_back(cadence);

  annotateCadenceCells(material, plan);

  ASSERT_EQ(material.cadence_cells.size(), 1u);
  const auto& cell = material.cadence_cells.front();
  EXPECT_EQ(cell.type, CadenceType::Perfect);
  EXPECT_EQ(cell.approach_tick, kTicksPerBeat);
  EXPECT_EQ(cell.cadence_tick, 2 * kTicksPerBeat);
  EXPECT_EQ(cell.soprano_approach_pc, 11u);
  EXPECT_EQ(cell.soprano_cadence_pc, 0u);
  EXPECT_EQ(cell.bass_approach_pc, 7u);
  EXPECT_EQ(cell.bass_cadence_pc, 0u);
}

}  // namespace bach::composer
