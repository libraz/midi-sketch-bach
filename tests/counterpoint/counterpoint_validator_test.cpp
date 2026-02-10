// Tests for counterpoint/counterpoint_validator.h -- validation facade,
// compliance rate, JSON/text report generation.

#include "counterpoint/counterpoint_validator.h"

#include <gtest/gtest.h>

#include <string>

#include "counterpoint/counterpoint_state.h"
#include "counterpoint/fux_rule_evaluator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: set up a clean 2-voice counterpoint
// ---------------------------------------------------------------------------

class CounterpointValidatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 36, 96);
    state.registerVoice(1, 36, 96);
  }
  CounterpointState state;
  FuxRuleEvaluator rules;
};

// ---------------------------------------------------------------------------
// Validate full state
// ---------------------------------------------------------------------------

TEST_F(CounterpointValidatorTest, ValidateCleanCounterpoint) {
  // Good counterpoint: imperfect consonances, varied motion.
  state.addNote(0, {0, 480, 64, 80, 0});     // E4
  state.addNote(0, {480, 480, 62, 80, 0});    // D4
  state.addNote(0, {960, 480, 60, 80, 0});    // C4

  state.addNote(1, {0, 480, 55, 80, 1});      // G3 (M6)
  state.addNote(1, {480, 480, 58, 80, 1});    // Bb3 (M3)
  state.addNote(1, {960, 480, 52, 80, 1});    // E3 (m6)

  CounterpointValidator validator(rules);
  auto violations = validator.validate(state);

  // Filter errors only.
  int errors = 0;
  for (const auto& viol : violations) {
    if (viol.severity >= 1) ++errors;
  }
  EXPECT_EQ(errors, 0);
}

TEST_F(CounterpointValidatorTest, ValidateWithViolations) {
  // Parallel fifths: C4/F3 -> D4/G3.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  CounterpointValidator validator(rules);
  auto violations = validator.validate(state);

  EXPECT_GT(violations.size(), 0u);
}

// ---------------------------------------------------------------------------
// Validate with tick range
// ---------------------------------------------------------------------------

TEST_F(CounterpointValidatorTest, ValidateTickRange) {
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(0, {960, 480, 64, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});
  state.addNote(1, {960, 480, 56, 80, 1});

  CounterpointValidator validator(rules);
  // Validate only ticks 0-480 (first beat pair).
  auto violations = validator.validate(state, 0, 480);

  // There should be no parallel perfect at tick 0 (only one beat checked).
  bool found_parallel_at_480 = false;
  for (const auto& viol : violations) {
    if (viol.tick == 480 && viol.rule == "parallel_perfect") {
      found_parallel_at_480 = true;
    }
  }
  // Tick 480 is not in range [0, 480).
  EXPECT_FALSE(found_parallel_at_480);
}

// ---------------------------------------------------------------------------
// Compliance rate
// ---------------------------------------------------------------------------

TEST_F(CounterpointValidatorTest, PerfectComplianceRate) {
  // Good counterpoint with only consonances.
  state.addNote(0, {0, 480, 64, 80, 0});    // E4
  state.addNote(0, {480, 480, 60, 80, 0});   // C4

  state.addNote(1, {0, 480, 55, 80, 1});     // G3 (M6)
  state.addNote(1, {480, 480, 52, 80, 1});   // E3 (m6)

  CounterpointValidator validator(rules);
  float rate = validator.getComplianceRate(state);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST_F(CounterpointValidatorTest, ComplianceRateWithViolations) {
  // Add some violations.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  CounterpointValidator validator(rules);
  float rate = validator.getComplianceRate(state);
  // Should be less than 1.0 due to parallel fifths.
  EXPECT_LT(rate, 1.0f);
  EXPECT_GE(rate, 0.0f);
}

TEST_F(CounterpointValidatorTest, ComplianceRateEmptyState) {
  CounterpointValidator validator(rules);
  EXPECT_FLOAT_EQ(validator.getComplianceRate(state), 1.0f);
}

// ---------------------------------------------------------------------------
// JSON report
// ---------------------------------------------------------------------------

TEST_F(CounterpointValidatorTest, ToJsonFormat) {
  state.addNote(0, {0, 480, 64, 80, 0});
  state.addNote(1, {0, 480, 55, 80, 1});

  CounterpointValidator validator(rules);
  std::string json = validator.toJson(state);

  EXPECT_NE(json.find("\"violations\""), std::string::npos);
  EXPECT_NE(json.find("\"compliance_rate\""), std::string::npos);
}

TEST_F(CounterpointValidatorTest, ToJsonWithViolation) {
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  CounterpointValidator validator(rules);
  std::string json = validator.toJson(state);

  EXPECT_NE(json.find("\"rule\""), std::string::npos);
  EXPECT_NE(json.find("\"tick\""), std::string::npos);
  EXPECT_NE(json.find("\"severity\""), std::string::npos);
}

// ---------------------------------------------------------------------------
// Text report
// ---------------------------------------------------------------------------

TEST_F(CounterpointValidatorTest, GenerateReportContainsHeader) {
  CounterpointValidator validator(rules);
  std::string report = validator.generateReport(state);

  EXPECT_NE(report.find("Counterpoint Validation Report"),
            std::string::npos);
  EXPECT_NE(report.find("Voices:"), std::string::npos);
  EXPECT_NE(report.find("Compliance:"), std::string::npos);
}

TEST_F(CounterpointValidatorTest, GenerateReportShowsViolations) {
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  CounterpointValidator validator(rules);
  std::string report = validator.generateReport(state);

  EXPECT_NE(report.find("ERROR"), std::string::npos);
  EXPECT_NE(report.find("Details:"), std::string::npos);
}

}  // namespace
}  // namespace bach
