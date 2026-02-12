// Tests for solo_string/flow/arpeggio_flow_config.h -- GlobalArcConfig validation and defaults.

#include "solo_string/flow/arpeggio_flow_config.h"

#include <gtest/gtest.h>

#include "analysis/fail_report.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// ArpeggioFlowConfig defaults
// ---------------------------------------------------------------------------

TEST(ArpeggioFlowConfigTest, DefaultValues) {
  ArpeggioFlowConfig config;
  EXPECT_EQ(config.key.tonic, Key::D);
  EXPECT_TRUE(config.key.is_minor);
  EXPECT_EQ(config.bpm, 66);
  EXPECT_EQ(config.seed, 0u);
  EXPECT_EQ(config.instrument, InstrumentType::Cello);
  EXPECT_EQ(config.num_sections, 6);
  EXPECT_EQ(config.bars_per_section, 4);
}

TEST(ArpeggioFlowConfigTest, CadenceDefaults) {
  CadenceConfig cadence;
  EXPECT_EQ(cadence.cadence_bars, 8);
  EXPECT_FLOAT_EQ(cadence.open_string_bias, 0.7f);
  EXPECT_TRUE(cadence.restrict_high_register);
  EXPECT_FLOAT_EQ(cadence.rhythm_simplification, 0.3f);
}

// ---------------------------------------------------------------------------
// validateGlobalArcConfig -- valid configs
// ---------------------------------------------------------------------------

TEST(ValidateGlobalArcConfigTest, ValidThreeSections) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Peak},
    {2, ArcPhase::Descent}
  };
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, ValidSixSections) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Ascent},
    {2, ArcPhase::Ascent},
    {3, ArcPhase::Peak},
    {4, ArcPhase::Descent},
    {5, ArcPhase::Descent}
  };
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, ValidMultipleAscentBeforePeak) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Ascent},
    {2, ArcPhase::Peak},
    {3, ArcPhase::Descent}
  };
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

// ---------------------------------------------------------------------------
// validateGlobalArcConfig -- invalid configs
// ---------------------------------------------------------------------------

TEST(ValidateGlobalArcConfigTest, EmptyConfigInvalid) {
  GlobalArcConfig config;
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, NoPeakInvalid) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Ascent},
    {2, ArcPhase::Descent}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, TwoPeaksInvalid) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Peak},
    {2, ArcPhase::Peak},
    {3, ArcPhase::Descent}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, ReversedOrderInvalid) {
  // Descent before Ascent
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Descent},
    {1, ArcPhase::Peak},
    {2, ArcPhase::Ascent}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, DescentBeforePeakInvalid) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Descent},
    {2, ArcPhase::Peak}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, StartsWithPeakInvalid) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Peak},
    {1, ArcPhase::Descent}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

TEST(ValidateGlobalArcConfigTest, AscentAfterDescentInvalid) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent},
    {1, ArcPhase::Peak},
    {2, ArcPhase::Descent},
    {3, ArcPhase::Ascent}
  };
  EXPECT_FALSE(validateGlobalArcConfig(config));
}

// ---------------------------------------------------------------------------
// createDefaultArcConfig
// ---------------------------------------------------------------------------

TEST(CreateDefaultArcConfigTest, ThreeSections) {
  auto config = createDefaultArcConfig(3);
  ASSERT_EQ(config.phase_assignment.size(), 3u);
  EXPECT_EQ(config.phase_assignment[0].second, ArcPhase::Ascent);
  EXPECT_EQ(config.phase_assignment[1].second, ArcPhase::Peak);
  EXPECT_EQ(config.phase_assignment[2].second, ArcPhase::Descent);
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(CreateDefaultArcConfigTest, SixSections) {
  auto config = createDefaultArcConfig(6);
  ASSERT_EQ(config.phase_assignment.size(), 6u);

  // Peak should be at approximately 65% position.
  // ceil(6 * 0.65) - 1 = ceil(3.9) - 1 = 4 - 1 = 3 (0-based).
  int peak_count = 0;
  int peak_index = -1;
  for (size_t idx = 0; idx < config.phase_assignment.size(); ++idx) {
    if (config.phase_assignment[idx].second == ArcPhase::Peak) {
      ++peak_count;
      peak_index = static_cast<int>(idx);
    }
  }
  EXPECT_EQ(peak_count, 1);
  EXPECT_EQ(peak_index, 3);
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(CreateDefaultArcConfigTest, EightSections) {
  auto config = createDefaultArcConfig(8);
  ASSERT_EQ(config.phase_assignment.size(), 8u);

  // Peak at ceil(8 * 0.65) - 1 = ceil(5.2) - 1 = 6 - 1 = 5 (0-based).
  int peak_count = 0;
  int peak_index = -1;
  for (size_t idx = 0; idx < config.phase_assignment.size(); ++idx) {
    if (config.phase_assignment[idx].second == ArcPhase::Peak) {
      ++peak_count;
      peak_index = static_cast<int>(idx);
    }
  }
  EXPECT_EQ(peak_count, 1);
  EXPECT_EQ(peak_index, 5);
  EXPECT_TRUE(validateGlobalArcConfig(config));
}

TEST(CreateDefaultArcConfigTest, TooFewSectionsReturnsEmpty) {
  auto config_two = createDefaultArcConfig(2);
  EXPECT_TRUE(config_two.phase_assignment.empty());

  auto config_one = createDefaultArcConfig(1);
  EXPECT_TRUE(config_one.phase_assignment.empty());

  auto config_zero = createDefaultArcConfig(0);
  EXPECT_TRUE(config_zero.phase_assignment.empty());
}

TEST(CreateDefaultArcConfigTest, AlwaysProducesValidConfig) {
  // Test a range of section counts to ensure all produce valid configs.
  for (int num = 3; num <= 12; ++num) {
    auto config = createDefaultArcConfig(num);
    EXPECT_TRUE(validateGlobalArcConfig(config))
        << "Invalid config for num_sections=" << num;
  }
}

TEST(CreateDefaultArcConfigTest, SectionIdsAreSequential) {
  auto config = createDefaultArcConfig(6);
  for (size_t idx = 0; idx < config.phase_assignment.size(); ++idx) {
    EXPECT_EQ(config.phase_assignment[idx].first, static_cast<SectionId>(idx));
  }
}


// ---------------------------------------------------------------------------
// validateGlobalArcConfigReport
// ---------------------------------------------------------------------------

TEST(ValidateGlobalArcConfigTest, ReportEmptyConfig) {
  GlobalArcConfig config;
  auto report = validateGlobalArcConfigReport(config);
  EXPECT_TRUE(report.hasCritical());
  ASSERT_EQ(report.issues.size(), 1u);
  EXPECT_EQ(report.issues[0].rule, "empty_phases");
  EXPECT_EQ(report.issues[0].kind, FailKind::ConfigFail);
}

TEST(ValidateGlobalArcConfigTest, ReportValidConfigHasNoIssues) {
  GlobalArcConfig config;
  config.phase_assignment = {{0, ArcPhase::Ascent}, {1, ArcPhase::Peak}, {2, ArcPhase::Descent}};
  auto report = validateGlobalArcConfigReport(config);
  EXPECT_FALSE(report.hasCritical());
  EXPECT_TRUE(report.issues.empty());
}

TEST(ValidateGlobalArcConfigTest, ReportFirstNotAscent) {
  GlobalArcConfig config;
  config.phase_assignment = {{0, ArcPhase::Peak}, {1, ArcPhase::Descent}};
  auto report = validateGlobalArcConfigReport(config);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "first_not_ascent") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(ValidateGlobalArcConfigTest, ReportPhaseRegression) {
  GlobalArcConfig config;
  config.phase_assignment = {{0, ArcPhase::Ascent}, {1, ArcPhase::Descent}, {2, ArcPhase::Peak}};
  auto report = validateGlobalArcConfigReport(config);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "phase_regression") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

TEST(ValidateGlobalArcConfigTest, ReportWrongPeakCount) {
  GlobalArcConfig config;
  config.phase_assignment = {
    {0, ArcPhase::Ascent}, {1, ArcPhase::Peak}, {2, ArcPhase::Peak}, {3, ArcPhase::Descent}
  };
  auto report = validateGlobalArcConfigReport(config);
  EXPECT_TRUE(report.hasCritical());
  bool found = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "peak_count") { found = true; break; }
  }
  EXPECT_TRUE(found);
}

}  // namespace
}  // namespace bach
