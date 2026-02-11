// Tests for counterpoint quality analysis module.

#include "analysis/counterpoint_analyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "analysis/fail_report.h"
#include "core/basic_types.h"
#include "core/note_source.h"

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

TEST(CountVoiceCrossingsTest, TemporaryCrossingExcluded) {
  // Voice 0 briefly dips below voice 1 for one beat, then resolves.
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),                  // C5 (voice 0 starts higher)
      qn(kTicksPerBeat, 60, 0),      // C4 (dips below voice 1)
      qn(kTicksPerBeat * 2, 72, 0),  // C5 (resolves back up)
      qn(0, 65, 1),                  // F4
      qn(kTicksPerBeat, 65, 1),      // F4
      qn(kTicksPerBeat * 2, 65, 1),  // F4
  };
  // At beat 1 crossing resolves at beat 2 -> temporary, excluded.
  EXPECT_EQ(countVoiceCrossings(notes, 2), 0u);
}

TEST(CountVoiceCrossingsTest, PersistentCrossingCounted) {
  // Voice 0 crosses below voice 1 and stays crossed.
  std::vector<NoteEvent> notes = {
      qn(0, 72, 0),                  // C5
      qn(kTicksPerBeat, 60, 0),      // C4 (crosses)
      qn(kTicksPerBeat * 2, 58, 0),  // Bb3 (stays crossed)
      qn(0, 65, 1),                  // F4
      qn(kTicksPerBeat, 65, 1),      // F4
      qn(kTicksPerBeat * 2, 65, 1),  // F4
  };
  // Both crossings persist -> counted.
  EXPECT_GE(countVoiceCrossings(notes, 2), 1u);
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

// ---------------------------------------------------------------------------
// bassLineStepwiseRatio
// ---------------------------------------------------------------------------

TEST(BassLineStepwiseRatioTest, AllStepwise) {
  // Bass voice (voice 2 in 3-voice texture) moves by steps only.
  std::vector<NoteEvent> notes;
  NoteEvent n;
  n.voice = 2; n.velocity = 80; n.duration = kTicksPerBeat;
  n.pitch = 48; n.start_tick = 0; notes.push_back(n);
  n.pitch = 50; n.start_tick = kTicksPerBeat; notes.push_back(n);
  n.pitch = 48; n.start_tick = kTicksPerBeat * 2; notes.push_back(n);
  n.pitch = 47; n.start_tick = kTicksPerBeat * 3; notes.push_back(n);
  EXPECT_FLOAT_EQ(bassLineStepwiseRatio(notes, 3), 1.0f);
}

TEST(BassLineStepwiseRatioTest, MixedMotion) {
  std::vector<NoteEvent> notes;
  NoteEvent n;
  n.voice = 1; n.velocity = 80; n.duration = kTicksPerBeat;
  // Voice 1 is bass in 2-voice texture.
  n.pitch = 48; n.start_tick = 0; notes.push_back(n);
  n.pitch = 50; n.start_tick = kTicksPerBeat; notes.push_back(n);  // step (+2)
  n.pitch = 55; n.start_tick = kTicksPerBeat * 2; notes.push_back(n);  // leap (+5)
  n.pitch = 53; n.start_tick = kTicksPerBeat * 3; notes.push_back(n);  // step (-2)
  // 2 steps out of 3 intervals = 0.667
  float ratio = bassLineStepwiseRatio(notes, 2);
  EXPECT_NEAR(ratio, 2.0f / 3.0f, 0.01f);
}

TEST(BassLineStepwiseRatioTest, SingleNote) {
  std::vector<NoteEvent> notes;
  NoteEvent n;
  n.voice = 0; n.velocity = 80; n.duration = kTicksPerBeat;
  n.pitch = 60; n.start_tick = 0; notes.push_back(n);
  EXPECT_FLOAT_EQ(bassLineStepwiseRatio(notes, 1), 1.0f);
}

TEST(BassLineStepwiseRatioTest, NoNotes) {
  std::vector<NoteEvent> notes;
  EXPECT_FLOAT_EQ(bassLineStepwiseRatio(notes, 0), 1.0f);
}

// ---------------------------------------------------------------------------
// voiceLeadingSmoothness
// ---------------------------------------------------------------------------

TEST(VoiceLeadingSmoothnessTest, PureStepwise) {
  // All stepwise motion -> average should be ~1.5.
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 62; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);
  note.pitch = 65; note.start_tick = kTicksPerBeat * 3; notes.push_back(note);

  float smoothness = voiceLeadingSmoothness(notes, 1);
  EXPECT_LE(smoothness, 3.0f);
}

TEST(VoiceLeadingSmoothnessTest, LargeLeaps) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 72; note.start_tick = kTicksPerBeat; notes.push_back(note);      // octave leap
  note.pitch = 60; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);  // octave leap

  float smoothness = voiceLeadingSmoothness(notes, 1);
  EXPECT_GT(smoothness, 3.0f);
}

TEST(VoiceLeadingSmoothnessTest, NoNotes) {
  std::vector<NoteEvent> notes;
  EXPECT_FLOAT_EQ(voiceLeadingSmoothness(notes, 0), 0.0f);
}

TEST(VoiceLeadingSmoothnessTest, MultipleVoicesAveraged) {
  // Voice 0: stepwise (avg 2.0), Voice 1: larger leaps (avg 5.0).
  // Overall average: (2.0 + 5.0) / 2 = 3.5.
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  // Voice 0: 60 -> 62 -> 64 (intervals: 2, 2; avg = 2.0).
  note.voice = 0;
  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 62; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);

  // Voice 1: 48 -> 53 -> 58 (intervals: 5, 5; avg = 5.0).
  note.voice = 1;
  note.pitch = 48; note.start_tick = 0; notes.push_back(note);
  note.pitch = 53; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 58; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);

  float smoothness = voiceLeadingSmoothness(notes, 2);
  EXPECT_NEAR(smoothness, 3.5f, kEpsilon);
}

TEST(VoiceLeadingSmoothnessTest, SingleNoteReturnsZero) {
  std::vector<NoteEvent> notes = {qn(0, 60, 0)};
  EXPECT_FLOAT_EQ(voiceLeadingSmoothness(notes, 1), 0.0f);
}

// ---------------------------------------------------------------------------
// contraryMotionRate
// ---------------------------------------------------------------------------

TEST(ContraryMotionRateTest, AllContrary) {
  // Two voices always moving in opposite directions.
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  // Voice 0 goes up.
  note.voice = 0;
  note.pitch = 72; note.start_tick = 0; notes.push_back(note);
  note.pitch = 74; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 76; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);

  // Voice 1 goes down.
  note.voice = 1;
  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 58; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 56; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);

  float rate = contraryMotionRate(notes, 2);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(ContraryMotionRateTest, AllParallel) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.voice = 0;
  note.pitch = 72; note.start_tick = 0; notes.push_back(note);
  note.pitch = 74; note.start_tick = kTicksPerBeat; notes.push_back(note);

  note.voice = 1;
  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 62; note.start_tick = kTicksPerBeat; notes.push_back(note);

  float rate = contraryMotionRate(notes, 2);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(ContraryMotionRateTest, SingleVoice) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;
  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  EXPECT_FLOAT_EQ(contraryMotionRate(notes, 1), 0.0f);
}

TEST(ContraryMotionRateTest, ObliqueMotionNotCounted) {
  // One voice stays still while the other moves -- not contrary, not parallel.
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.voice = 0;
  note.pitch = 72; note.start_tick = 0; notes.push_back(note);
  note.pitch = 72; note.start_tick = kTicksPerBeat; notes.push_back(note);  // stays

  note.voice = 1;
  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 58; note.start_tick = kTicksPerBeat; notes.push_back(note);  // moves down

  // Oblique motion: motion_a == 0, so this transition is not counted.
  float rate = contraryMotionRate(notes, 2);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

// ---------------------------------------------------------------------------
// leapResolutionRate
// ---------------------------------------------------------------------------

TEST(LeapResolutionRateTest, AllResolved) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 67; note.start_tick = kTicksPerBeat; notes.push_back(note);      // leap up +7
  note.pitch = 65; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);  // step down -2

  float rate = leapResolutionRate(notes, 1);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(LeapResolutionRateTest, NoneResolved) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 67; note.start_tick = kTicksPerBeat; notes.push_back(note);      // leap up +7
  note.pitch = 72; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);  // continues up

  float rate = leapResolutionRate(notes, 1);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(LeapResolutionRateTest, NoLeaps) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 62; note.start_tick = kTicksPerBeat; notes.push_back(note);
  note.pitch = 64; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);

  EXPECT_FLOAT_EQ(leapResolutionRate(notes, 1), 1.0f);
}

TEST(LeapResolutionRateTest, LeapAtEndUnresolved) {
  // A leap as the last interval has no following note to resolve it.
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 60; note.start_tick = 0; notes.push_back(note);
  note.pitch = 62; note.start_tick = kTicksPerBeat; notes.push_back(note);      // step
  note.pitch = 70; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);  // leap up +8

  // The leap has no following note, so it counts as unresolved.
  float rate = leapResolutionRate(notes, 1);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(LeapResolutionRateTest, DownwardLeapResolvedUpward) {
  std::vector<NoteEvent> notes;
  NoteEvent note;
  note.voice = 0;
  note.velocity = 80;
  note.duration = kTicksPerBeat;

  note.pitch = 72; note.start_tick = 0; notes.push_back(note);
  note.pitch = 65; note.start_tick = kTicksPerBeat; notes.push_back(note);      // leap down -7
  note.pitch = 67; note.start_tick = kTicksPerBeat * 2; notes.push_back(note);  // step up +2

  float rate = leapResolutionRate(notes, 1);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(LeapResolutionRateTest, EmptyReturnsOne) {
  std::vector<NoteEvent> notes;
  EXPECT_FLOAT_EQ(leapResolutionRate(notes, 0), 1.0f);
}

// ---------------------------------------------------------------------------
// rhythmDiversityScore
// ---------------------------------------------------------------------------

TEST(RhythmDiversityScoreTest, AllSameDuration) {
  // All notes are quarter notes -- max_ratio = 1.0 -> score = 0.0.
  std::vector<NoteEvent> notes;
  for (int idx = 0; idx < 10; ++idx) {
    notes.push_back(qn(static_cast<Tick>(idx) * kTicksPerBeat, 60, 0));
  }
  float score = rhythmDiversityScore(notes, 1);
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

TEST(RhythmDiversityScoreTest, VeryDiverse) {
  // Each note has a unique duration. Max ratio = 1/N, which is << 0.3 for N >= 4.
  std::vector<NoteEvent> notes;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = static_cast<Tick>((idx + 1) * 100);  // 100, 200, ..., 800.
    note.pitch = 60;
    note.velocity = 80;
    note.voice = 0;
    notes.push_back(note);
  }
  float score = rhythmDiversityScore(notes, 1);
  EXPECT_GT(score, 0.9f);
}

TEST(RhythmDiversityScoreTest, Empty) {
  std::vector<NoteEvent> empty;
  EXPECT_NEAR(rhythmDiversityScore(empty, 1), 1.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// textureDensityVariance
// ---------------------------------------------------------------------------

TEST(TextureDensityVarianceTest, UniformDensity) {
  // Two voices, each with one note per beat spanning the entire piece.
  // At every beat, exactly 2 notes are sounding -> variance = 0.
  std::vector<NoteEvent> notes;
  for (int idx = 0; idx < 4; ++idx) {
    Tick tick = static_cast<Tick>(idx) * kTicksPerBeat;
    notes.push_back(qn(tick, 72, 0));
    notes.push_back(qn(tick, 60, 1));
  }
  float var = textureDensityVariance(notes, 2);
  EXPECT_NEAR(var, 0.0f, kEpsilon);
}

TEST(TextureDensityVarianceTest, VaryingDensity) {
  // Beat 0: 2 notes sounding, Beat 1: 1 note sounding -> variance > 0.
  std::vector<NoteEvent> notes;
  notes.push_back(qn(0, 72, 0));                  // beat 0, voice 0
  notes.push_back(qn(0, 60, 1));                   // beat 0, voice 1
  notes.push_back(qn(kTicksPerBeat, 74, 0));       // beat 1, voice 0 only
  float var = textureDensityVariance(notes, 2);
  EXPECT_GT(var, 0.0f);
}

// ---------------------------------------------------------------------------
// Structural parallel classification
// ---------------------------------------------------------------------------

/// @brief Create a quarter-note NoteEvent with source.
NoteEvent qns(Tick tick, uint8_t pitch, VoiceId voice, BachNoteSource source) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = kTicksPerBeat;
  n.pitch = pitch;
  n.velocity = 80;
  n.voice = voice;
  n.source = source;
  return n;
}

TEST(StructuralParallelTest, BothStructuralCountedAsStructural) {
  // Parallel 5ths where both voices are structural (subject + answer).
  std::vector<NoteEvent> notes = {
      qns(0, 72, 0, BachNoteSource::FugueSubject),
      qns(kTicksPerBeat, 74, 0, BachNoteSource::FugueSubject),
      qns(0, 65, 1, BachNoteSource::FugueAnswer),
      qns(kTicksPerBeat, 67, 1, BachNoteSource::FugueAnswer),
  };
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_GT(result.parallel_perfect_count, 0u);
  EXPECT_EQ(result.structural_parallel_count, result.parallel_perfect_count);
}

TEST(StructuralParallelTest, NonStructuralNotCountedAsStructural) {
  // Parallel 5ths where both voices are non-structural (free counterpoint).
  std::vector<NoteEvent> notes = {
      qns(0, 72, 0, BachNoteSource::FreeCounterpoint),
      qns(kTicksPerBeat, 74, 0, BachNoteSource::FreeCounterpoint),
      qns(0, 65, 1, BachNoteSource::EpisodeMaterial),
      qns(kTicksPerBeat, 67, 1, BachNoteSource::EpisodeMaterial),
  };
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_GT(result.parallel_perfect_count, 0u);
  EXPECT_EQ(result.structural_parallel_count, 0u);
}

TEST(StructuralParallelTest, MixedSourcesNotStructural) {
  // Parallel 5ths with one structural and one non-structural voice.
  std::vector<NoteEvent> notes = {
      qns(0, 72, 0, BachNoteSource::FugueSubject),
      qns(kTicksPerBeat, 74, 0, BachNoteSource::FugueSubject),
      qns(0, 65, 1, BachNoteSource::FreeCounterpoint),
      qns(kTicksPerBeat, 67, 1, BachNoteSource::FreeCounterpoint),
  };
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_GT(result.parallel_perfect_count, 0u);
  EXPECT_EQ(result.structural_parallel_count, 0u);
}

TEST(StructuralParallelTest, ZeroParallelsZeroStructural) {
  // Good counterpoint: no parallels at all.
  auto notes = makeGoodCounterpoint();
  auto result = analyzeCounterpoint(notes, 2);
  EXPECT_EQ(result.parallel_perfect_count, 0u);
  EXPECT_EQ(result.structural_parallel_count, 0u);
}

}  // namespace
}  // namespace bach
