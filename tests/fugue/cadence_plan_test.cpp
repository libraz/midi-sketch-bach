// Tests for fugue/cadence_plan.h -- cadence plan creation, structural placement,
// cadence type assignment, and timeline application.

#include "fugue/cadence_plan.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {
namespace {

class CadencePlanTest : public ::testing::Test {
 protected:
  FugueStructure buildBasicStructure() {
    FugueStructure structure;
    // Exposition: 0 - 4 bars
    structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                         0, kTicksPerBar * 4, Key::C);
    // Episode 1: 4 - 6 bars
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         kTicksPerBar * 4, kTicksPerBar * 6, Key::G);
    // Middle Entry: 6 - 8 bars
    structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                         kTicksPerBar * 6, kTicksPerBar * 8, Key::G);
    // Episode 2 (return): 8 - 10 bars
    structure.addSection(SectionType::Episode, FuguePhase::Develop,
                         kTicksPerBar * 8, kTicksPerBar * 10, Key::C);
    // Stretto: 10 - 14 bars
    structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                         kTicksPerBar * 10, kTicksPerBar * 14, Key::C);
    // Coda: 14 - 16 bars
    structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                         kTicksPerBar * 14, kTicksPerBar * 16, Key::C);
    return structure;
  }
};

// ---------------------------------------------------------------------------
// createForFugue
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, CreateForFugue_ProducesCadencePoints) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  EXPECT_GE(plan.size(), 4u);  // At least exposition, episodes, coda
}

TEST_F(CadencePlanTest, ExpositionEnd_HasHalfCadence) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  // First cadence should be at exposition end (Half cadence).
  ASSERT_FALSE(plan.points.empty());
  EXPECT_EQ(plan.points[0].type, CadenceType::Half);
}

TEST_F(CadencePlanTest, FinalEpisode_HasPerfectCadence) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  // The final episode should have a Perfect cadence.
  bool found_perfect = false;
  for (const auto& cadence_point : plan.points) {
    if (cadence_point.type == CadenceType::Perfect) {
      found_perfect = true;
      break;
    }
  }
  EXPECT_TRUE(found_perfect);
}

TEST_F(CadencePlanTest, PreStretto_HasDeceptiveCadence) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  bool found_deceptive = false;
  for (const auto& cadence_point : plan.points) {
    if (cadence_point.type == CadenceType::Deceptive) {
      found_deceptive = true;
      break;
    }
  }
  EXPECT_TRUE(found_deceptive);
}

// ---------------------------------------------------------------------------
// Minor key behavior
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, MinorKey_HasPicardyThird) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::A, true};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, true);

  bool found_picardy = false;
  for (const auto& cadence_point : plan.points) {
    if (cadence_point.type == CadenceType::PicardyThird) {
      found_picardy = true;
      break;
    }
  }
  EXPECT_TRUE(found_picardy);
}

// ---------------------------------------------------------------------------
// applyTo
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, ApplyToTimeline_DoesNotCrash) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  HarmonicTimeline timeline = HarmonicTimeline::createStandard(
      home, kTicksPerBar * 16, HarmonicResolution::Bar);

  // Should not crash.
  plan.applyTo(timeline);
  EXPECT_GT(timeline.size(), 0u);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, EmptyStructure_ProducesNoCadences) {
  FugueStructure structure;
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);
  EXPECT_EQ(plan.size(), 0u);
}

}  // namespace
}  // namespace bach
