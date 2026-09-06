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

// --- Key context ------------------------------------------------------------

TEST(MaterialKeyContext, DegreeOfAPitchIsMeasuredFromTheKeysOwnTonic) {
  const KeyContext c_major{0, false};
  const KeyContext g_major{7, false};
  EXPECT_EQ(degreeInKey(60, c_major), 0);
  EXPECT_EQ(degreeInKey(67, c_major), 4);
  EXPECT_EQ(degreeInKey(67, g_major), 0);
  // F natural is the seventh degree of C but chromatic in G.
  EXPECT_EQ(degreeInKey(65, c_major), 3);
  EXPECT_EQ(degreeInKey(65, g_major), -1);
}

TEST(MaterialKeyContext, MinorIsTheNaturalCollection) {
  const KeyContext c_minor{0, true};
  EXPECT_TRUE(inKey(63, c_minor));   // Eb
  EXPECT_TRUE(inKey(70, c_minor));   // Bb
  EXPECT_FALSE(inKey(71, c_minor));  // B natural belongs to the dominant, not the key
}

TEST(MaterialKeyContext, TransposingASubjectIntoTheDominantSpellsItsAccidental) {
  const KeyContext c_major{0, false};
  const KeyContext g_major{7, false};
  // The full C major octave restated in G has to produce F sharp; a degree
  // shift inside the home collection would leave F natural and no modulation
  // would be heard.
  const int scale[8] = {60, 62, 64, 65, 67, 69, 71, 72};
  const int expected[8] = {67, 69, 71, 72, 74, 76, 78, 79};
  for (int idx = 0; idx < 8; ++idx) {
    EXPECT_EQ(transposeIntoKey(scale[idx], c_major, g_major), expected[idx]) << "degree " << idx;
  }
}

TEST(MaterialKeyContext, TransposingBelowTheTonicKeepsTheDegree) {
  const KeyContext c_major{0, false};
  const KeyContext g_major{7, false};
  // B3 is the leading tone below C4; in G it is the leading tone below G4.
  EXPECT_EQ(transposeIntoKey(59, c_major, g_major), 66);
}

TEST(MaterialKeyContext, TransposingCarriesAChromaticInflection) {
  const KeyContext c_major{0, false};
  const KeyContext g_major{7, false};
  // C sharp is the raised first degree; in G that is G sharp.
  EXPECT_EQ(transposeIntoKey(61, c_major, g_major), 68);
}

TEST(MaterialKeyContext, TransposingBetweenModesTakesTheTargetsThird) {
  const KeyContext c_major{0, false};
  const KeyContext a_minor{9, true};
  // The third degree of C is E; the third degree of A minor is C.
  EXPECT_EQ(transposeIntoKey(64, c_major, a_minor), 72);
}

TEST(MaterialKeyContext, BendingRaisesAToneThatContradictsTheLocalKey) {
  const KeyContext g_major{7, false};
  EXPECT_EQ(bendIntoKey(65, g_major), 66);  // F natural -> F sharp
  EXPECT_EQ(bendIntoKey(67, g_major), 67);  // already in the key
}

TEST(MaterialKeyContext, LocalKeyFollowsTheModulationBoundaries) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = false;
  plan.modulations.push_back({4 * kTicksPerBar, 0, 7, false, false, ModulationType::Pivot});
  plan.modulations.push_back({8 * kTicksPerBar, 7, 0, false, false, ModulationType::Phrase});

  EXPECT_EQ(localKeyAt(plan, 0).tonic_pc, 0u);
  EXPECT_EQ(localKeyAt(plan, 4 * kTicksPerBar).tonic_pc, 7u);
  EXPECT_EQ(localKeyAt(plan, 6 * kTicksPerBar).tonic_pc, 7u);
  EXPECT_EQ(localKeyAt(plan, 8 * kTicksPerBar).tonic_pc, 0u);
}

TEST(MaterialKeyContext, LocalKeyIsTheHomeKeyWhenNothingModulates) {
  HarmonicPlan plan;
  plan.tonic_pc = 0;
  plan.is_minor = true;
  const KeyContext key = localKeyAt(plan, 40 * kTicksPerBar);
  EXPECT_EQ(key.tonic_pc, 0u);
  EXPECT_TRUE(key.is_minor);
}

}  // namespace bach::composer
