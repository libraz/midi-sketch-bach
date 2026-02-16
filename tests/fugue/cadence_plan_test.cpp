// Tests for fugue/cadence_plan.h -- cadence plan creation, structural placement,
// cadence type assignment, and timeline application.
// Tests for fugue/cadence_vocabulary.h -- approach formulas, inner voice guidance.

#include "fugue/cadence_plan.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/cadence_insertion.h"
#include "fugue/cadence_vocabulary.h"
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

// ---------------------------------------------------------------------------
// Context rules match structural positions
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, ContextRulesMatchStructuralPositions) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  ASSERT_GE(plan.size(), 3u);

  // Exposition end gets a Half cadence (Bach practice: drives into development).
  // The first cadence point corresponds to the exposition boundary.
  EXPECT_EQ(plan.points[0].type, CadenceType::Half);

  // At least one episode end has a cadence point.
  // Episodes span bars 4-6 (Episode 1) and bars 8-10 (Episode 2).
  Tick episode1_end = kTicksPerBar * 6;
  Tick episode2_end = kTicksPerBar * 10;
  bool found_episode_cadence = false;
  for (const auto& cadence_point : plan.points) {
    // Cadence points are placed at section.end_tick - kTicksPerBar or at
    // section boundaries, so check within the episode tick ranges.
    if ((cadence_point.tick >= kTicksPerBar * 4 &&
         cadence_point.tick <= episode1_end) ||
        (cadence_point.tick >= kTicksPerBar * 8 &&
         cadence_point.tick <= episode2_end)) {
      found_episode_cadence = true;
      break;
    }
  }
  EXPECT_TRUE(found_episode_cadence) << "Expected at least one cadence at an episode end";

  // The final section (Coda) must have a Perfect or PicardyThird cadence.
  // Coda cadence is placed at the coda start_tick (bar 14).
  bool found_final_cadence = false;
  for (const auto& cadence_point : plan.points) {
    if (cadence_point.tick >= kTicksPerBar * 14 &&
        (cadence_point.type == CadenceType::Perfect ||
         cadence_point.type == CadenceType::PicardyThird)) {
      found_final_cadence = true;
      break;
    }
  }
  EXPECT_TRUE(found_final_cadence)
      << "Expected Perfect or PicardyThird cadence in the final section";
}

// ---------------------------------------------------------------------------
// extractCadenceTicks
// ---------------------------------------------------------------------------

TEST_F(CadencePlanTest, ExtractCadenceTicks) {
  FugueStructure structure = buildBasicStructure();
  KeySignature home{Key::C, false};
  CadencePlan plan = CadencePlan::createForFugue(structure, home, false);

  ASSERT_GE(plan.size(), 2u);

  std::vector<Tick> ticks = extractCadenceTicks(plan);

  // extractCadenceTicks must return the same number of ticks as plan points.
  EXPECT_EQ(ticks.size(), plan.points.size());

  // Ticks must be sorted in non-decreasing order.
  EXPECT_TRUE(std::is_sorted(ticks.begin(), ticks.end()))
      << "extractCadenceTicks must return sorted ticks";

  // Every tick in the result must correspond to a plan point's tick.
  for (const auto& tick : ticks) {
    bool found_in_plan = false;
    for (const auto& cadence_point : plan.points) {
      if (cadence_point.tick == tick) {
        found_in_plan = true;
        break;
      }
    }
    EXPECT_TRUE(found_in_plan)
        << "Tick " << tick << " from extractCadenceTicks not found in plan points";
  }
}

// ---------------------------------------------------------------------------
// CadenceVocabularyTest -- approach formulas and inner voice guidance
// ---------------------------------------------------------------------------

class CadenceVocabularyTest : public ::testing::Test {};

TEST_F(CadenceVocabularyTest, GetApproachesForAllTypes) {
  // Perfect, Half, Deceptive, Phrygian must all have at least one approach.
  const CadenceType types_with_approaches[] = {
      CadenceType::Perfect,
      CadenceType::Half,
      CadenceType::Deceptive,
      CadenceType::Phrygian,
  };

  for (CadenceType cad_type : types_with_approaches) {
    auto [approaches, count] = getCadenceApproaches(cad_type);
    EXPECT_GT(count, 0u) << "Expected approaches for cadence type "
                         << static_cast<int>(cad_type);
    ASSERT_NE(approaches, nullptr)
        << "Null pointer for cadence type " << static_cast<int>(cad_type);

    // Every returned approach must have the correct type field.
    for (size_t idx = 0; idx < count; ++idx) {
      EXPECT_EQ(approaches[idx].type, cad_type)
          << "Approach " << idx << " (\"" << approaches[idx].name
          << "\") has wrong type for cadence type " << static_cast<int>(cad_type);
    }
  }
}

TEST_F(CadenceVocabularyTest, InnerVoiceGuidanceDefaults) {
  // Perfect cadence: strict inner voice constraints.
  CadenceInnerVoiceGuidance perfect_guidance = getInnerVoiceGuidance(CadenceType::Perfect);
  EXPECT_LE(perfect_guidance.max_leap_semitones, 4)
      << "Perfect cadence inner voices should have max leap <= 4 semitones (M3)";
  EXPECT_TRUE(perfect_guidance.prefer_4_3_resolution)
      << "Perfect cadence should prefer 4->3 (sus4->3) resolution";

  // Half cadence: slightly relaxed constraints.
  CadenceInnerVoiceGuidance half_guidance = getInnerVoiceGuidance(CadenceType::Half);
  EXPECT_GE(half_guidance.max_leap_semitones, 4)
      << "Half cadence inner voices should allow at least 4 semitones leap";

  // Verify that Half is at least as relaxed as Perfect in leap allowance.
  EXPECT_GE(half_guidance.max_leap_semitones, perfect_guidance.max_leap_semitones)
      << "Half cadence should be at least as relaxed as Perfect for max leap";
}

}  // namespace
}  // namespace bach
