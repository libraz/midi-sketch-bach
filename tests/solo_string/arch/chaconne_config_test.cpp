// Tests for solo_string/arch/chaconne_config.h -- chaconne configuration and variation plan.

#include "solo_string/arch/chaconne_config.h"

#include <algorithm>
#include <random>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "analysis/fail_report.h"
#include "harmony/key.h"
#include "solo_string/arch/variation_types.h"

namespace bach {
namespace {

// ===========================================================================
// ChaconneConfig defaults
// ===========================================================================

TEST(ChaconneConfigTest, DefaultConfigHasDMinorKey) {
  ChaconneConfig config;
  EXPECT_EQ(config.key.tonic, Key::D);
  EXPECT_TRUE(config.key.is_minor);
}

TEST(ChaconneConfigTest, DefaultConfigHasExpectedBpm) {
  ChaconneConfig config;
  EXPECT_EQ(config.bpm, 60u);
}

TEST(ChaconneConfigTest, DefaultConfigHasAutoSeed) {
  ChaconneConfig config;
  EXPECT_EQ(config.seed, 0u);
}

TEST(ChaconneConfigTest, DefaultConfigHasViolinInstrument) {
  ChaconneConfig config;
  EXPECT_EQ(config.instrument, InstrumentType::Violin);
}

TEST(ChaconneConfigTest, DefaultConfigHasEmptyVariationPlan) {
  ChaconneConfig config;
  EXPECT_TRUE(config.variations.empty());
}

TEST(ChaconneConfigTest, DefaultConfigHasEmptyGroundBassNotes) {
  ChaconneConfig config;
  EXPECT_TRUE(config.ground_bass_notes.empty());
}

TEST(ChaconneConfigTest, DefaultConfigMaxRetries) {
  ChaconneConfig config;
  EXPECT_EQ(config.max_variation_retries, 3);
}

// ===========================================================================
// ClimaxDesign defaults
// ===========================================================================

TEST(ClimaxDesignTest, DefaultAccumulateVariationsIsThree) {
  ClimaxDesign climax;
  EXPECT_EQ(climax.accumulate_variations, 3);
}

TEST(ClimaxDesignTest, DefaultPositionRatioRange) {
  ClimaxDesign climax;
  EXPECT_FLOAT_EQ(climax.position_ratio_min, 0.70f);
  EXPECT_FLOAT_EQ(climax.position_ratio_max, 0.85f);
}

TEST(ClimaxDesignTest, DefaultAllowsFullChords) {
  ClimaxDesign climax;
  EXPECT_TRUE(climax.allow_full_chords);
}

TEST(ClimaxDesignTest, DefaultUnlocksMaxRegister) {
  ClimaxDesign climax;
  EXPECT_TRUE(climax.unlock_max_register);
}

TEST(ClimaxDesignTest, DefaultFixedTextureIsFullChords) {
  ClimaxDesign climax;
  EXPECT_EQ(climax.fixed_texture, TextureType::FullChords);
}

TEST(ClimaxDesignTest, DefaultHarmonicWeightPeak) {
  ClimaxDesign climax;
  EXPECT_FLOAT_EQ(climax.harmonic_weight_peak, 2.0f);
}

TEST(ClimaxDesignTest, DefaultFixedHarmonicDensity) {
  ClimaxDesign climax;
  EXPECT_FLOAT_EQ(climax.fixed_harmonic_density, 0.9f);
}

// ===========================================================================
// MajorSectionConstraints defaults
// ===========================================================================

TEST(MajorSectionConstraintsTest, DefaultDisallowsFullChords) {
  MajorSectionConstraints constraints;
  EXPECT_FALSE(constraints.allow_full_chords);
}

TEST(MajorSectionConstraintsTest, DefaultHasThreeAllowedTextures) {
  MajorSectionConstraints constraints;
  EXPECT_EQ(constraints.allowed_textures.size(), 3u);
  EXPECT_TRUE(constraints.allowed_textures.count(TextureType::SingleLine) > 0);
  EXPECT_TRUE(constraints.allowed_textures.count(TextureType::Arpeggiated) > 0);
  EXPECT_TRUE(constraints.allowed_textures.count(TextureType::Bariolage) > 0);
}

TEST(MajorSectionConstraintsTest, DefaultDoesNotAllowFullChordsTexture) {
  MajorSectionConstraints constraints;
  EXPECT_EQ(constraints.allowed_textures.count(TextureType::FullChords), 0u);
  EXPECT_EQ(constraints.allowed_textures.count(TextureType::ImpliedPolyphony), 0u);
  EXPECT_EQ(constraints.allowed_textures.count(TextureType::ScalePassage), 0u);
}

TEST(MajorSectionConstraintsTest, DefaultRhythmDensityCap) {
  MajorSectionConstraints constraints;
  EXPECT_FLOAT_EQ(constraints.rhythm_density_cap, 0.6f);
}

TEST(MajorSectionConstraintsTest, DefaultRegisterBounds) {
  MajorSectionConstraints constraints;
  EXPECT_EQ(constraints.register_low, 55u);   // G3
  EXPECT_EQ(constraints.register_high, 84u);  // C6
}

// ===========================================================================
// createStandardVariationPlan
// ===========================================================================

TEST(ChaconneConfigTest, StandardPlanCreatesApproximatelyTenVariations) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  EXPECT_EQ(plan.size(), 10u);
}

TEST(ChaconneConfigTest, StandardPlanHasCorrectRoleOrder) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  std::vector<VariationRole> roles;
  for (const auto& var : plan) {
    roles.push_back(var.role);
  }
  EXPECT_TRUE(isRoleOrderValid(roles));
}

TEST(ChaconneConfigTest, StandardPlanHasExactlyThreeAccumulate) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  int accumulate_count = 0;
  for (const auto& var : plan) {
    if (var.role == VariationRole::Accumulate) {
      ++accumulate_count;
    }
  }
  EXPECT_EQ(accumulate_count, 3);
}

TEST(ChaconneConfigTest, StandardPlanResolveIsThemeOnly) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  ASSERT_FALSE(plan.empty());

  // Last variation must be Resolve with Theme type.
  const auto& last = plan.back();
  EXPECT_EQ(last.role, VariationRole::Resolve);
  EXPECT_EQ(last.type, VariationType::Theme);
}

TEST(ChaconneConfigTest, StandardPlanFirstIsEstablishTheme) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  ASSERT_FALSE(plan.empty());

  EXPECT_EQ(plan[0].role, VariationRole::Establish);
  EXPECT_EQ(plan[0].type, VariationType::Theme);
}

TEST(ChaconneConfigTest, StandardPlanHasMajorSectionVariations) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  int major_count = 0;
  for (const auto& var : plan) {
    if (var.is_major_section) {
      ++major_count;
    }
  }
  EXPECT_GT(major_count, 0);
}

TEST(ChaconneConfigTest, StandardPlanMajorSectionUsesParallelMajor) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  KeySignature d_major = getParallel(d_minor);

  for (const auto& var : plan) {
    if (var.is_major_section) {
      EXPECT_EQ(var.key, d_major)
          << "Major section variation should use parallel major key";
    }
  }
}

TEST(ChaconneConfigTest, StandardPlanMajorSectionRolesAreIlluminate) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  for (const auto& var : plan) {
    if (var.is_major_section) {
      EXPECT_EQ(var.role, VariationRole::Illuminate)
          << "Major section variations should have Illuminate role";
    }
  }
}

TEST(ChaconneConfigTest, StandardPlanVariationNumbersAreSequential) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  for (size_t idx = 0; idx < plan.size(); ++idx) {
    EXPECT_EQ(plan[idx].variation_number, static_cast<int>(idx))
        << "Variation number mismatch at index " << idx;
  }
}

TEST(ChaconneConfigTest, StandardPlanUsesMultipleTextures) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  std::set<TextureType> textures;
  for (const auto& var : plan) {
    textures.insert(var.primary_texture);
  }
  // Expect at least 3 distinct texture types
  EXPECT_GE(textures.size(), 3u);
}

// ===========================================================================
// createStandardVariationPlan with different keys
// ===========================================================================

TEST(ChaconneConfigTest, StandardPlanWorksWithCMinor) {
  KeySignature c_minor = {Key::C, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(c_minor, rng);
  EXPECT_EQ(plan.size(), 10u);
  EXPECT_TRUE(validateVariationPlan(plan));
}

TEST(ChaconneConfigTest, StandardPlanWorksWithGMinor) {
  KeySignature g_minor = {Key::G, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(g_minor, rng);
  EXPECT_EQ(plan.size(), 10u);
  EXPECT_TRUE(validateVariationPlan(plan));
}

// ===========================================================================
// validateVariationPlan
// ===========================================================================

TEST(ChaconneConfigTest, ValidateAcceptsStandardPlan) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  EXPECT_TRUE(validateVariationPlan(plan));
}

TEST(ChaconneConfigTest, ValidateRejectsEmptyPlan) {
  std::vector<ChaconneVariation> empty_plan;
  EXPECT_FALSE(validateVariationPlan(empty_plan));
}

TEST(ChaconneConfigTest, ValidateRejectsWrongAccumulateCount) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  // Remove one Accumulate
  auto iter = std::find_if(plan.begin(), plan.end(),
                           [](const ChaconneVariation& var) {
                             return var.role == VariationRole::Accumulate;
                           });
  ASSERT_NE(iter, plan.end());
  plan.erase(iter);

  EXPECT_FALSE(validateVariationPlan(plan));
}

TEST(ChaconneConfigTest, ValidateRejectsInvalidRoleOrder) {
  // Create a plan with reversed role order.
  std::vector<ChaconneVariation> bad_plan;
  bad_plan.push_back({0, VariationRole::Resolve, VariationType::Theme,
                      TextureType::SingleLine, {Key::D, true}, false});
  bad_plan.push_back({1, VariationRole::Establish, VariationType::Theme,
                      TextureType::SingleLine, {Key::D, true}, false});
  EXPECT_FALSE(validateVariationPlan(bad_plan));
}

TEST(ChaconneConfigTest, ValidateRejectsInvalidTypeForRole) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  // Corrupt: assign Virtuosic to Establish (not allowed).
  plan[0].type = VariationType::Virtuosic;
  EXPECT_FALSE(validateVariationPlan(plan));
}

TEST(ChaconneConfigTest, ValidateRejectsResolveNotTheme) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);

  // Corrupt: Resolve should be Theme, not Lyrical.
  plan.back().type = VariationType::Lyrical;
  EXPECT_FALSE(validateVariationPlan(plan));
}

TEST(ChaconneConfigTest, ValidateRejectsResolveNotFinal) {
  // Plan where Resolve is not the last variation.
  std::vector<ChaconneVariation> plan;
  plan.push_back({0, VariationRole::Establish, VariationType::Theme,
                  TextureType::SingleLine, {Key::D, true}, false});
  plan.push_back({1, VariationRole::Develop, VariationType::Rhythmic,
                  TextureType::ImpliedPolyphony, {Key::D, true}, false});
  plan.push_back({2, VariationRole::Destabilize, VariationType::Virtuosic,
                  TextureType::ScalePassage, {Key::D, true}, false});
  plan.push_back({3, VariationRole::Illuminate, VariationType::Lyrical,
                  TextureType::SingleLine, {Key::D, false}, true});
  plan.push_back({4, VariationRole::Destabilize, VariationType::Virtuosic,
                  TextureType::ScalePassage, {Key::D, true}, false});
  plan.push_back({5, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::FullChords, {Key::D, true}, false});
  plan.push_back({6, VariationRole::Accumulate, VariationType::Chordal,
                  TextureType::FullChords, {Key::D, true}, false});
  plan.push_back({7, VariationRole::Accumulate, VariationType::Virtuosic,
                  TextureType::FullChords, {Key::D, true}, false});
  plan.push_back({8, VariationRole::Resolve, VariationType::Theme,
                  TextureType::SingleLine, {Key::D, true}, false});
  // Extra variation after Resolve breaks the constraint.
  plan.push_back({9, VariationRole::Resolve, VariationType::Theme,
                  TextureType::SingleLine, {Key::D, true}, false});

  EXPECT_FALSE(validateVariationPlan(plan));
}

// ===========================================================================
// ChaconneVariation defaults
// ===========================================================================

TEST(ChaconneVariationTest, DefaultValues) {
  ChaconneVariation var;
  EXPECT_EQ(var.variation_number, 0);
  EXPECT_EQ(var.role, VariationRole::Establish);
  EXPECT_EQ(var.type, VariationType::Theme);
  EXPECT_EQ(var.primary_texture, TextureType::SingleLine);
  EXPECT_EQ(var.key.tonic, Key::D);
  EXPECT_TRUE(var.key.is_minor);
  EXPECT_FALSE(var.is_major_section);
}


// ===========================================================================
// validateVariationPlanReport
// ===========================================================================

TEST(ChaconneConfigTest, ReportEmptyPlan) {
  auto report = validateVariationPlanReport({});
  EXPECT_TRUE(report.hasCritical());
  ASSERT_EQ(report.issues.size(), 1u);
  EXPECT_EQ(report.issues[0].rule, "empty_plan");
  EXPECT_EQ(report.issues[0].kind, FailKind::ConfigFail);
  EXPECT_EQ(report.issues[0].severity, FailSeverity::Critical);
}

TEST(ChaconneConfigTest, ReportValidPlanHasNoIssues) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  auto report = validateVariationPlanReport(plan);
  EXPECT_FALSE(report.hasCritical());
  EXPECT_TRUE(report.issues.empty());
}

TEST(ChaconneConfigTest, ReportInvalidTypeForRole) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  plan[0].type = VariationType::Virtuosic;  // Not allowed for Establish
  auto report = validateVariationPlanReport(plan);
  EXPECT_TRUE(report.hasCritical());
  // Should have an issue with rule "invalid_type_for_role"
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "invalid_type_for_role") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(ChaconneConfigTest, ReportWrongAccumulateCount) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  // Remove an Accumulate variation
  auto iter = std::find_if(plan.begin(), plan.end(),
      [](const ChaconneVariation& v) { return v.role == VariationRole::Accumulate; });
  if (iter != plan.end()) plan.erase(iter);
  auto report = validateVariationPlanReport(plan);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "accumulate_count") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(ChaconneConfigTest, ReportFinalNotResolve) {
  KeySignature d_minor = {Key::D, true};
  std::mt19937 rng(42);
  auto plan = createStandardVariationPlan(d_minor, rng);
  plan.back().type = VariationType::Lyrical;  // Should be Theme
  auto report = validateVariationPlanReport(plan);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "final_not_resolve") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

}  // namespace
}  // namespace bach
