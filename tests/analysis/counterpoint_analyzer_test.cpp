// Tests for counterpoint quality analysis module.

#include "analysis/counterpoint_analyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "analysis/fail_report.h"
#include "core/basic_types.h"

namespace bach {
namespace {

constexpr float kEpsilon = 0.01f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Create a quarter-note NoteEvent.
NoteEvent qn(Tick tick, uint8_t pitch, VoiceId voice) {
  return {tick, kTicksPerBeat, pitch, 80, voice};
}

/// @brief Create two voices moving in parallel 5ths: C-G, D-A, E-B, F-C.
std::vector<NoteEvent> makeParallelFifths() {
  return {
      qn(0, 72, 0),                  // C5
      qn(kTicksPerBeat, 74, 0),      // D5
      qn(kTicksPerBeat * 2, 76, 0),  // E5
      qn(kTicksPerBeat * 3, 77, 0),  // F5
      qn(0, 65, 1),                  // F4 (P5 below C5)
      qn(kTicksPerBeat, 67, 1),      // G4 (P5 below D5)
      qn(kTicksPerBeat * 2, 69, 1),  // A4 (P5 below E5)
      qn(kTicksPerBeat * 3, 70, 1),  // Bb4 (P5 below F5)
  };
}

/// @brief Create two voices moving in parallel octaves.
std::vector<NoteEvent> makeParallelOctaves() {
  return {
      qn(0, 72, 0),                  // C5
      qn(kTicksPerBeat, 74, 0),      // D5
      qn(kTicksPerBeat * 2, 76, 0),  // E5
      qn(0, 60, 1),                  // C4 (octave below)
      qn(kTicksPerBeat, 62, 1),      // D4
      qn(kTicksPerBeat * 2, 64, 1),  // E4
  };
}

/// @brief Create two voices with good counterpoint (contrary motion, imperfect consonances).
/// Intervals: m3, M6, m3, M6 -- no parallel perfect intervals.
std::vector<NoteEvent> makeGoodCounterpoint() {
  return {
      // Voice 0 (soprano): ascending
      qn(0, 72, 0),                  // C5
      qn(kTicksPerBeat, 74, 0),      // D5
      qn(kTicksPerBeat * 2, 76, 0),  // E5
      qn(kTicksPerBeat * 3, 77, 0),  // F5
      // Voice 1 (alto): contrary motion, imperfect consonances
      qn(0, 69, 1),                  // A4 (m3 below C5)
      qn(kTicksPerBeat, 65, 1),      // F4 (M6 below D5)
      qn(kTicksPerBeat * 2, 73, 1),  // C#5 (m3 below E5)
      qn(kTicksPerBeat * 3, 69, 1),  // A4 (M6 below F5)
  };
}

/// @brief Create voice crossing: voice 0 goes below voice 1.
std::vector<NoteEvent> makeVoiceCrossing() {
  return {
      qn(0, 72, 0),              // C5 (voice 0 starts higher)
      qn(kTicksPerBeat, 60, 0),  // C4 (voice 0 drops below)
      qn(0, 65, 1),              // F4 (voice 1)
      qn(kTicksPerBeat, 65, 1),  // F4 (voice 1 stays)
  };
}

/// @brief Create notes with a tritone leap in voice 0.
std::vector<NoteEvent> makeTritoneLeap() {
  return {
      qn(0, 60, 0),              // C4
      qn(kTicksPerBeat, 66, 0),  // F#4 (tritone = 6 semitones)
      qn(0, 48, 1),              // C3 (bass, no leap)
      qn(kTicksPerBeat, 48, 1),  // C3
  };
}

// ---------------------------------------------------------------------------
// countParallelPerfect
// ---------------------------------------------------------------------------

TEST(CountParallelPerfectTest, DetectsParallelFifths) {
  auto notes = makeParallelFifths();
  uint32_t count = countParallelPerfect(notes, 2);
  // 3 consecutive parallel 5ths (beats 0->1, 1->2, 2->3).
  EXPECT_GE(count, 2u);
}

TEST(CountParallelPerfectTest, DetectsParallelOctaves) {
  auto notes = makeParallelOctaves();
  uint32_t count = countParallelPerfect(notes, 2);
  EXPECT_GE(count, 1u);
}

TEST(CountParallelPerfectTest, NoViolationsInGoodCounterpoint) {
  auto notes = makeGoodCounterpoint();
  uint32_t count = countParallelPerfect(notes, 2);
  EXPECT_EQ(count, 0u);
}

TEST(CountParallelPerfectTest, EmptyNotesReturnsZero) {
  std::vector<NoteEvent> empty;
  EXPECT_EQ(countParallelPerfect(empty, 2), 0u);
}

TEST(CountParallelPerfectTest, SingleVoiceReturnsZero) {
  std::vector<NoteEvent> notes = {qn(0, 60, 0), qn(kTicksPerBeat, 67, 0)};
  EXPECT_EQ(countParallelPerfect(notes, 1), 0u);
}

// ---------------------------------------------------------------------------
// countHiddenPerfect
// ---------------------------------------------------------------------------

TEST(CountHiddenPerfectTest, DetectsHiddenFifthWithLeap) {
  // Voice 0 leaps up to form a 5th with voice 1 which also moves up.
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),              // C5
      qn(kTicksPerBeat, 79, 0),  // G5 (leaps up 7 semitones)
      qn(0, 64, 1),              // E4
      qn(kTicksPerBeat, 72, 1),  // C5 (moves up 8, arriving at P5 below G5)
  };
  uint32_t count = countHiddenPerfect(notes, 2);
  EXPECT_GE(count, 1u);
}

TEST(CountHiddenPerfectTest, NoHiddenWhenContraryMotion) {
  // Contrary motion cannot produce hidden perfects.
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),              // C5
      qn(kTicksPerBeat, 79, 0),  // G5 (up)
      qn(0, 65, 1),              // F4
      qn(kTicksPerBeat, 60, 1),  // C4 (down)
  };
  uint32_t count = countHiddenPerfect(notes, 2);
  EXPECT_EQ(count, 0u);
}

TEST(CountHiddenPerfectTest, EmptyReturnsZero) {
  std::vector<NoteEvent> empty;
  EXPECT_EQ(countHiddenPerfect(empty, 2), 0u);
}

// ---------------------------------------------------------------------------
// countVoiceCrossings
// ---------------------------------------------------------------------------

TEST(CountVoiceCrossingsTest, DetectsCrossing) {
  auto notes = makeVoiceCrossing();
  uint32_t count = countVoiceCrossings(notes, 2);
  // At beat 1, voice 0 (60) < voice 1 (65) -> crossing.
  EXPECT_GE(count, 1u);
}

TEST(CountVoiceCrossingsTest, NoCrossingWhenProperRegisters) {
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),              // Soprano
      qn(kTicksPerBeat, 74, 0),
      qn(0, 60, 1),              // Alto (below soprano)
      qn(kTicksPerBeat, 62, 1),
  };
  EXPECT_EQ(countVoiceCrossings(notes, 2), 0u);
}

TEST(CountVoiceCrossingsTest, EqualPitchIsNotCrossing) {
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(0, 60, 1),
  };
  // Equal pitch: not a crossing (upper >= lower).
  EXPECT_EQ(countVoiceCrossings(notes, 2), 0u);
}

TEST(CountVoiceCrossingsTest, EmptyReturnsZero) {
  std::vector<NoteEvent> empty;
  EXPECT_EQ(countVoiceCrossings(empty, 2), 0u);
}

TEST(CountVoiceCrossingsTest, ThreeVoices) {
  std::vector<NoteEvent> notes = {
      qn(0, 48, 0),  // Voice 0 very low
      qn(0, 60, 1),  // Voice 1 middle
      qn(0, 72, 2),  // Voice 2 high
  };
  // Voice 0 (48) < voice 1 (60) -> crossing. Voice 0 (48) < voice 2 (72) -> crossing.
  EXPECT_GE(countVoiceCrossings(notes, 3), 2u);
}

// ---------------------------------------------------------------------------
// dissonanceResolutionRate
// ---------------------------------------------------------------------------

TEST(DissonanceResolutionTest, AllResolved) {
  // Beat 0: m2 between voices (dissonant), Beat 1: m3 (consonant).
  // Voice 1 resolves by step (1 semitone).
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),              // C5
      qn(kTicksPerBeat, 72, 0),  // C5 (stays)
      qn(0, 71, 1),              // B4 (m2 below, dissonant)
      qn(kTicksPerBeat, 69, 1),  // A4 (resolves down by step to m3 below C5)
  };
  float rate = dissonanceResolutionRate(notes, 2);
  EXPECT_NEAR(rate, 1.0f, kEpsilon);
}

TEST(DissonanceResolutionTest, NoDissonancesReturnsOne) {
  // Only consonant intervals.
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),              // C5
      qn(kTicksPerBeat, 74, 0),  // D5
      qn(0, 69, 1),              // A4 (m3 below C5)
      qn(kTicksPerBeat, 71, 1),  // B4 (m3 below D5)
  };
  float rate = dissonanceResolutionRate(notes, 2);
  EXPECT_NEAR(rate, 1.0f, kEpsilon);
}

TEST(DissonanceResolutionTest, SingleVoiceReturnsOne) {
  std::vector<NoteEvent> notes = {qn(0, 60, 0)};
  EXPECT_NEAR(dissonanceResolutionRate(notes, 1), 1.0f, kEpsilon);
}

TEST(DissonanceResolutionTest, EmptyReturnsOne) {
  std::vector<NoteEvent> empty;
  EXPECT_NEAR(dissonanceResolutionRate(empty, 2), 1.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// countAugmentedLeaps
// ---------------------------------------------------------------------------

TEST(CountAugmentedLeapsTest, DetectsTritone) {
  auto notes = makeTritoneLeap();
  uint32_t count = countAugmentedLeaps(notes, 2);
  EXPECT_GE(count, 1u);
}

TEST(CountAugmentedLeapsTest, NoLeapInStepwiseMotion) {
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(kTicksPerBeat, 62, 0),  // M2 step
      qn(kTicksPerBeat * 2, 64, 0),  // M2 step
  };
  EXPECT_EQ(countAugmentedLeaps(notes, 1), 0u);
}

TEST(CountAugmentedLeapsTest, PerfectFifthIsNotAugmented) {
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(kTicksPerBeat, 67, 0),  // P5 = 7 semitones (not tritone)
  };
  EXPECT_EQ(countAugmentedLeaps(notes, 1), 0u);
}

TEST(CountAugmentedLeapsTest, EmptyReturnsZero) {
  std::vector<NoteEvent> empty;
  EXPECT_EQ(countAugmentedLeaps(empty, 1), 0u);
}

// ---------------------------------------------------------------------------
// analyzeCounterpoint
// ---------------------------------------------------------------------------

TEST(AnalyzeCounterpointTest, GoodCounterpointHighCompliance) {
  auto notes = makeGoodCounterpoint();
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_GT(result.overall_compliance_rate, 0.5f);
  EXPECT_EQ(result.parallel_perfect_count, 0u);
}

TEST(AnalyzeCounterpointTest, ParallelFifthsLowerCompliance) {
  auto notes = makeParallelFifths();
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_GT(result.parallel_perfect_count, 0u);
  EXPECT_LT(result.overall_compliance_rate, 1.0f);
}

TEST(AnalyzeCounterpointTest, EmptyNotesDefaultResult) {
  std::vector<NoteEvent> empty;
  auto result = analyzeCounterpoint(empty, 2);
  EXPECT_EQ(result.parallel_perfect_count, 0u);
  EXPECT_EQ(result.voice_crossing_count, 0u);
  EXPECT_NEAR(result.overall_compliance_rate, 1.0f, kEpsilon);
}

TEST(AnalyzeCounterpointTest, AllMetricsPopulated) {
  auto notes = makeParallelFifths();
  auto result = analyzeCounterpoint(notes, 2);

  // Just verify all fields are populated (not NaN).
  EXPECT_FALSE(std::isnan(result.dissonance_resolution_rate));
  EXPECT_FALSE(std::isnan(result.overall_compliance_rate));
  EXPECT_GE(result.overall_compliance_rate, 0.0f);
  EXPECT_LE(result.overall_compliance_rate, 1.0f);
}

// ---------------------------------------------------------------------------
// buildCounterpointReport
// ---------------------------------------------------------------------------

TEST(BuildCounterpointReportTest, ReportsParallelFifths) {
  auto notes = makeParallelFifths();
  auto report = buildCounterpointReport(notes, 2);
  EXPECT_FALSE(report.issues.empty());

  bool found_parallel = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "parallel_fifths") {
      found_parallel = true;
      EXPECT_EQ(issue.severity, FailSeverity::Critical);
      EXPECT_EQ(issue.kind, FailKind::MusicalFail);
    }
  }
  EXPECT_TRUE(found_parallel);
}

TEST(BuildCounterpointReportTest, ReportsVoiceCrossing) {
  auto notes = makeVoiceCrossing();
  auto report = buildCounterpointReport(notes, 2);

  bool found_crossing = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "voice_crossing") {
      found_crossing = true;
      EXPECT_EQ(issue.severity, FailSeverity::Critical);
    }
  }
  EXPECT_TRUE(found_crossing);
}

TEST(BuildCounterpointReportTest, ReportsAugmentedLeap) {
  auto notes = makeTritoneLeap();
  auto report = buildCounterpointReport(notes, 2);

  bool found_leap = false;
  for (const auto& issue : report.issues) {
    if (issue.rule == "augmented_leap") {
      found_leap = true;
      EXPECT_EQ(issue.severity, FailSeverity::Critical);
    }
  }
  EXPECT_TRUE(found_leap);
}

TEST(BuildCounterpointReportTest, CleanReportForGoodCounterpoint) {
  auto notes = makeGoodCounterpoint();
  auto report = buildCounterpointReport(notes, 2);

  // Should have no critical parallel or crossing issues.
  for (const auto& issue : report.issues) {
    if (issue.rule == "parallel_fifths" || issue.rule == "parallel_octaves") {
      FAIL() << "Unexpected parallel perfect issue in good counterpoint";
    }
  }
}

TEST(BuildCounterpointReportTest, EmptyReportForSingleVoice) {
  std::vector<NoteEvent> notes = {qn(0, 60, 0)};
  auto report = buildCounterpointReport(notes, 1);
  EXPECT_TRUE(report.issues.empty());
}

TEST(BuildCounterpointReportTest, IssueBarBeatPopulated) {
  auto notes = makeVoiceCrossing();
  auto report = buildCounterpointReport(notes, 2);

  for (const auto& issue : report.issues) {
    if (issue.rule == "voice_crossing") {
      // The crossing is at beat 1 (tick = kTicksPerBeat).
      EXPECT_EQ(issue.bar, 0u);
      EXPECT_EQ(issue.beat, 1u);
    }
  }
}

TEST(BuildCounterpointReportTest, JsonContainsAllIssues) {
  auto notes = makeParallelFifths();
  auto report = buildCounterpointReport(notes, 2);

  // Convert to JSON and verify it contains the issues.
  std::string json = report.toJson();
  EXPECT_NE(json.find("\"issues\""), std::string::npos);
  if (!report.issues.empty()) {
    EXPECT_NE(json.find("\"rule\""), std::string::npos);
  }
}

}  // namespace
}  // namespace bach
