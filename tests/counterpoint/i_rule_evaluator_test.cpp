// Tests for counterpoint/i_rule_evaluator.h -- MotionType enum and
// RuleViolation struct.

#include "counterpoint/i_rule_evaluator.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// MotionType to string
// ---------------------------------------------------------------------------

TEST(MotionTypeTest, ToString) {
  EXPECT_STREQ(motionTypeToString(MotionType::Parallel), "parallel");
  EXPECT_STREQ(motionTypeToString(MotionType::Similar), "similar");
  EXPECT_STREQ(motionTypeToString(MotionType::Contrary), "contrary");
  EXPECT_STREQ(motionTypeToString(MotionType::Oblique), "oblique");
}

// ---------------------------------------------------------------------------
// RuleViolation defaults
// ---------------------------------------------------------------------------

TEST(RuleViolationTest, DefaultValues) {
  RuleViolation viol;
  EXPECT_EQ(viol.voice1, 0);
  EXPECT_EQ(viol.voice2, 0);
  EXPECT_EQ(viol.tick, 0u);
  EXPECT_TRUE(viol.rule.empty());
  EXPECT_EQ(viol.severity, 1);
}

TEST(RuleViolationTest, CanSetFields) {
  RuleViolation viol;
  viol.voice1 = 0;
  viol.voice2 = 1;
  viol.tick = 1920;
  viol.rule = "parallel_fifths";
  viol.severity = 1;

  EXPECT_EQ(viol.voice1, 0);
  EXPECT_EQ(viol.voice2, 1);
  EXPECT_EQ(viol.tick, 1920u);
  EXPECT_EQ(viol.rule, "parallel_fifths");
  EXPECT_EQ(viol.severity, 1);
}

}  // namespace
}  // namespace bach
