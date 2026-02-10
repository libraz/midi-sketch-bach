// Tests for counterpoint/motion_analyzer.h -- motion classification
// and aggregate statistics.

#include "counterpoint/motion_analyzer.h"

#include <gtest/gtest.h>

#include "counterpoint/counterpoint_state.h"
#include "counterpoint/fux_rule_evaluator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// MotionStats
// ---------------------------------------------------------------------------

TEST(MotionStatsTest, TotalAndRatio) {
  MotionAnalyzer::MotionStats stats;
  stats.parallel = 2;
  stats.similar = 3;
  stats.contrary = 4;
  stats.oblique = 1;

  EXPECT_EQ(stats.total(), 10);
  EXPECT_FLOAT_EQ(stats.contraryRatio(), 0.4f);
}

TEST(MotionStatsTest, EmptyStatsZeroRatio) {
  MotionAnalyzer::MotionStats stats;
  EXPECT_EQ(stats.total(), 0);
  EXPECT_FLOAT_EQ(stats.contraryRatio(), 0.0f);
}

// ---------------------------------------------------------------------------
// MotionAnalyzer delegation
// ---------------------------------------------------------------------------

TEST(MotionAnalyzerTest, ClassifiesDelegation) {
  FuxRuleEvaluator rules;
  MotionAnalyzer analyzer(rules);

  // Contrary motion.
  EXPECT_EQ(analyzer.classifyMotion(60, 62, 67, 65), MotionType::Contrary);

  // Parallel motion.
  EXPECT_EQ(analyzer.classifyMotion(60, 62, 55, 57), MotionType::Parallel);
}

// ---------------------------------------------------------------------------
// Analyze voice pair
// ---------------------------------------------------------------------------

TEST(MotionAnalyzerTest, AnalyzeVoicePair) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);

  // 4 notes each => 3 motion events.
  // Beat 0: C4/E3 -> Beat 1: D4/D3 -> Beat 2: E4/C3 -> Beat 3: F4/B2
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(0, {960, 480, 64, 80, 0});
  state.addNote(0, {1440, 480, 65, 80, 0});

  state.addNote(1, {0, 480, 52, 80, 1});
  state.addNote(1, {480, 480, 50, 80, 1});
  state.addNote(1, {960, 480, 48, 80, 1});
  state.addNote(1, {1440, 480, 47, 80, 1});

  FuxRuleEvaluator rules;
  MotionAnalyzer analyzer(rules);

  auto stats = analyzer.analyzeVoicePair(state, 0, 1);

  // All transitions: voice 0 ascending, voice 1 descending => contrary.
  EXPECT_EQ(stats.contrary, 3);
  EXPECT_EQ(stats.total(), 3);
  EXPECT_FLOAT_EQ(stats.contraryRatio(), 1.0f);
}

TEST(MotionAnalyzerTest, AnalyzeVoicePairTooFewNotes) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);

  // Only one note per voice -- not enough to analyze motion.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(1, {0, 480, 48, 80, 1});

  FuxRuleEvaluator rules;
  MotionAnalyzer analyzer(rules);

  auto stats = analyzer.analyzeVoicePair(state, 0, 1);
  EXPECT_EQ(stats.total(), 0);
}

TEST(MotionAnalyzerTest, AnalyzeMixedMotion) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);

  // Beat 0: C4(60)/G3(55)
  // Beat 1: D4(62)/A3(57) -- parallel (both up, same interval)
  // Beat 2: E4(64)/A3(57) -- oblique (voice 1 static)
  // Beat 3: D4(62)/B3(59) -- contrary (voice 0 down, voice 1 up)
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(0, {960, 480, 64, 80, 0});
  state.addNote(0, {1440, 480, 62, 80, 0});

  state.addNote(1, {0, 480, 55, 80, 1});
  state.addNote(1, {480, 480, 57, 80, 1});
  state.addNote(1, {960, 480, 57, 80, 1});
  state.addNote(1, {1440, 480, 59, 80, 1});

  FuxRuleEvaluator rules;
  MotionAnalyzer analyzer(rules);

  auto stats = analyzer.analyzeVoicePair(state, 0, 1);
  EXPECT_EQ(stats.parallel, 1);
  EXPECT_EQ(stats.oblique, 1);
  EXPECT_EQ(stats.contrary, 1);
  EXPECT_EQ(stats.total(), 3);
}

}  // namespace
}  // namespace bach
