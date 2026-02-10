// Tests for FailReport analysis module.

#include "analysis/fail_report.h"

#include <gtest/gtest.h>

#include <string>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// FailSeverity to string
// ---------------------------------------------------------------------------

TEST(FailSeverityTest, ToStringValues) {
  EXPECT_STREQ(failSeverityToString(FailSeverity::Info), "info");
  EXPECT_STREQ(failSeverityToString(FailSeverity::Warning), "warning");
  EXPECT_STREQ(failSeverityToString(FailSeverity::Critical), "critical");
}

// ---------------------------------------------------------------------------
// FailReport::addIssue
// ---------------------------------------------------------------------------

TEST(FailReportTest, AddIssueIncreasesSize) {
  FailReport report;
  EXPECT_TRUE(report.issues.empty());

  FailIssue issue;
  issue.rule = "test_rule";
  report.addIssue(issue);
  EXPECT_EQ(report.issues.size(), 1u);

  report.addIssue(issue);
  EXPECT_EQ(report.issues.size(), 2u);
}

// ---------------------------------------------------------------------------
// FailReport::summary
// ---------------------------------------------------------------------------

TEST(FailReportTest, SummaryEmpty) {
  FailReport report;
  auto sum = report.summary();
  EXPECT_EQ(sum.total_critical, 0u);
  EXPECT_EQ(sum.total_warning, 0u);
  EXPECT_EQ(sum.total_info, 0u);
}

TEST(FailReportTest, SummaryCountsBySeverity) {
  FailReport report;

  FailIssue critical_issue;
  critical_issue.severity = FailSeverity::Critical;
  report.addIssue(critical_issue);
  report.addIssue(critical_issue);

  FailIssue warning_issue;
  warning_issue.severity = FailSeverity::Warning;
  report.addIssue(warning_issue);

  FailIssue info_issue;
  info_issue.severity = FailSeverity::Info;
  report.addIssue(info_issue);
  report.addIssue(info_issue);
  report.addIssue(info_issue);

  auto sum = report.summary();
  EXPECT_EQ(sum.total_critical, 2u);
  EXPECT_EQ(sum.total_warning, 1u);
  EXPECT_EQ(sum.total_info, 3u);
}

// ---------------------------------------------------------------------------
// FailReport::hasCritical
// ---------------------------------------------------------------------------

TEST(FailReportTest, HasCriticalFalseWhenEmpty) {
  FailReport report;
  EXPECT_FALSE(report.hasCritical());
}

TEST(FailReportTest, HasCriticalFalseWithOnlyWarnings) {
  FailReport report;
  FailIssue issue;
  issue.severity = FailSeverity::Warning;
  report.addIssue(issue);
  EXPECT_FALSE(report.hasCritical());
}

TEST(FailReportTest, HasCriticalTrue) {
  FailReport report;
  FailIssue issue;
  issue.severity = FailSeverity::Critical;
  report.addIssue(issue);
  EXPECT_TRUE(report.hasCritical());
}

// ---------------------------------------------------------------------------
// FailReport::issuesByKind
// ---------------------------------------------------------------------------

TEST(FailReportTest, IssuesByKindFiltersCorrectly) {
  FailReport report;

  FailIssue musical;
  musical.kind = FailKind::MusicalFail;
  musical.rule = "parallel_fifths";
  report.addIssue(musical);
  report.addIssue(musical);

  FailIssue structural;
  structural.kind = FailKind::StructuralFail;
  structural.rule = "phase_order";
  report.addIssue(structural);

  FailIssue config;
  config.kind = FailKind::ConfigFail;
  config.rule = "invalid_voices";
  report.addIssue(config);

  auto musical_issues = report.issuesByKind(FailKind::MusicalFail);
  EXPECT_EQ(musical_issues.size(), 2u);
  EXPECT_EQ(musical_issues[0].rule, "parallel_fifths");

  auto structural_issues = report.issuesByKind(FailKind::StructuralFail);
  EXPECT_EQ(structural_issues.size(), 1u);

  auto config_issues = report.issuesByKind(FailKind::ConfigFail);
  EXPECT_EQ(config_issues.size(), 1u);
}

TEST(FailReportTest, IssuesByKindEmptyResult) {
  FailReport report;
  FailIssue issue;
  issue.kind = FailKind::MusicalFail;
  report.addIssue(issue);

  auto result = report.issuesByKind(FailKind::StructuralFail);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// FailReport::issuesBySeverity
// ---------------------------------------------------------------------------

TEST(FailReportTest, IssuesBySeverityFiltersCorrectly) {
  FailReport report;

  FailIssue critical;
  critical.severity = FailSeverity::Critical;
  report.addIssue(critical);

  FailIssue warning;
  warning.severity = FailSeverity::Warning;
  report.addIssue(warning);
  report.addIssue(warning);

  auto criticals = report.issuesBySeverity(FailSeverity::Critical);
  EXPECT_EQ(criticals.size(), 1u);

  auto warnings = report.issuesBySeverity(FailSeverity::Warning);
  EXPECT_EQ(warnings.size(), 2u);

  auto infos = report.issuesBySeverity(FailSeverity::Info);
  EXPECT_TRUE(infos.empty());
}

// ---------------------------------------------------------------------------
// FailReport::toJson
// ---------------------------------------------------------------------------

TEST(FailReportTest, ToJsonEmptyReport) {
  FailReport report;
  std::string json = report.toJson();
  EXPECT_NE(json.find("\"summary\""), std::string::npos);
  EXPECT_NE(json.find("\"critical\": 0"), std::string::npos);
  EXPECT_NE(json.find("\"warning\": 0"), std::string::npos);
  EXPECT_NE(json.find("\"info\": 0"), std::string::npos);
  EXPECT_NE(json.find("\"issues\""), std::string::npos);
}

TEST(FailReportTest, ToJsonWithIssues) {
  FailReport report;

  FailIssue issue;
  issue.kind = FailKind::MusicalFail;
  issue.severity = FailSeverity::Critical;
  issue.tick = kTicksPerBar * 5;
  issue.bar = static_cast<uint8_t>(tickToBar(issue.tick));
  issue.beat = beatInBar(issue.tick);
  issue.voice_a = 0;
  issue.voice_b = 1;
  issue.rule = "parallel_fifths";
  issue.description = "Parallel 5ths between voices 0 and 1";
  report.addIssue(issue);

  std::string json = report.toJson();
  EXPECT_NE(json.find("\"critical\": 1"), std::string::npos);
  EXPECT_NE(json.find("\"musical\""), std::string::npos);
  EXPECT_NE(json.find("\"parallel_fifths\""), std::string::npos);
  EXPECT_NE(json.find("\"bar\": 5"), std::string::npos);
}

TEST(FailReportTest, ToJsonMultipleIssues) {
  FailReport report;

  FailIssue issue_a;
  issue_a.severity = FailSeverity::Critical;
  issue_a.rule = "voice_crossing";
  report.addIssue(issue_a);

  FailIssue issue_b;
  issue_b.severity = FailSeverity::Warning;
  issue_b.rule = "hidden_perfect";
  report.addIssue(issue_b);

  std::string json = report.toJson();
  EXPECT_NE(json.find("\"critical\": 1"), std::string::npos);
  EXPECT_NE(json.find("\"warning\": 1"), std::string::npos);
  EXPECT_NE(json.find("\"voice_crossing\""), std::string::npos);
  EXPECT_NE(json.find("\"hidden_perfect\""), std::string::npos);
}

// ---------------------------------------------------------------------------
// FailIssue defaults
// ---------------------------------------------------------------------------

TEST(FailIssueTest, DefaultValues) {
  FailIssue issue;
  EXPECT_EQ(issue.kind, FailKind::MusicalFail);
  EXPECT_EQ(issue.severity, FailSeverity::Warning);
  EXPECT_EQ(issue.tick, 0u);
  EXPECT_EQ(issue.bar, 0u);
  EXPECT_EQ(issue.beat, 0u);
  EXPECT_EQ(issue.voice_a, 0u);
  EXPECT_EQ(issue.voice_b, 0u);
  EXPECT_TRUE(issue.rule.empty());
  EXPECT_TRUE(issue.description.empty());
}

// ---------------------------------------------------------------------------
// FailIssue bar/beat from tick
// ---------------------------------------------------------------------------

TEST(FailIssueTest, BarBeatFromTick) {
  FailIssue issue;
  issue.tick = kTicksPerBar * 3 + kTicksPerBeat * 2;  // Bar 3, beat 2.
  issue.bar = static_cast<uint8_t>(tickToBar(issue.tick));
  issue.beat = beatInBar(issue.tick);
  EXPECT_EQ(issue.bar, 3u);
  EXPECT_EQ(issue.beat, 2u);
}

}  // namespace
}  // namespace bach
