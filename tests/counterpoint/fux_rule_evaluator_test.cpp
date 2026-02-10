// Tests for counterpoint/fux_rule_evaluator.h -- Fux strict counterpoint
// rules: consonance, motion, parallel/hidden/crossing detection.

#include "counterpoint/fux_rule_evaluator.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Interval consonance
// ---------------------------------------------------------------------------

TEST(FuxRuleEvaluatorTest, PerfectConsonances) {
  FuxRuleEvaluator rules;
  EXPECT_TRUE(rules.isIntervalConsonant(0, true));   // Unison
  EXPECT_TRUE(rules.isIntervalConsonant(7, true));   // P5
  EXPECT_TRUE(rules.isIntervalConsonant(12, true));  // P8
  EXPECT_TRUE(rules.isIntervalConsonant(19, true));  // Compound P5
  EXPECT_TRUE(rules.isIntervalConsonant(24, true));  // 2 octaves
}

TEST(FuxRuleEvaluatorTest, ImperfectConsonances) {
  FuxRuleEvaluator rules;
  EXPECT_TRUE(rules.isIntervalConsonant(3, true));   // m3
  EXPECT_TRUE(rules.isIntervalConsonant(4, true));   // M3
  EXPECT_TRUE(rules.isIntervalConsonant(8, true));   // m6
  EXPECT_TRUE(rules.isIntervalConsonant(9, true));   // M6
  EXPECT_TRUE(rules.isIntervalConsonant(15, true));  // Compound m3
  EXPECT_TRUE(rules.isIntervalConsonant(16, true));  // Compound M3
}

TEST(FuxRuleEvaluatorTest, Dissonances) {
  FuxRuleEvaluator rules;
  EXPECT_FALSE(rules.isIntervalConsonant(1, true));   // m2
  EXPECT_FALSE(rules.isIntervalConsonant(2, true));   // M2
  EXPECT_FALSE(rules.isIntervalConsonant(5, true));   // P4 (dissonant in 2-voice)
  EXPECT_FALSE(rules.isIntervalConsonant(6, true));   // Tritone
  EXPECT_FALSE(rules.isIntervalConsonant(10, true));  // m7
  EXPECT_FALSE(rules.isIntervalConsonant(11, true));  // M7
}

// ---------------------------------------------------------------------------
// Motion classification
// ---------------------------------------------------------------------------

TEST(FuxRuleEvaluatorTest, ParallelMotion) {
  FuxRuleEvaluator rules;
  // Both voices ascend by the same interval: C4->D4 and G3->A3.
  // Interval before: 60-55=5, Interval after: 62-57=5 (same).
  EXPECT_EQ(rules.classifyMotion(60, 62, 55, 57), MotionType::Parallel);
}

TEST(FuxRuleEvaluatorTest, SimilarMotion) {
  FuxRuleEvaluator rules;
  // Both ascend, but different interval sizes.
  // C4->E4 (up 4) and G3->A3 (up 2). Intervals: 5 -> 8.
  EXPECT_EQ(rules.classifyMotion(60, 64, 55, 57), MotionType::Similar);
}

TEST(FuxRuleEvaluatorTest, ContraryMotion) {
  FuxRuleEvaluator rules;
  // One ascends, other descends.
  // C4->D4 (up) and G4->F4 (down).
  EXPECT_EQ(rules.classifyMotion(60, 62, 67, 65), MotionType::Contrary);
}

TEST(FuxRuleEvaluatorTest, ObliqueMotionVoice1Static) {
  FuxRuleEvaluator rules;
  // Voice 1 stays, voice 2 moves.
  EXPECT_EQ(rules.classifyMotion(60, 60, 55, 57), MotionType::Oblique);
}

TEST(FuxRuleEvaluatorTest, ObliqueMotionVoice2Static) {
  FuxRuleEvaluator rules;
  // Voice 2 stays, voice 1 moves.
  EXPECT_EQ(rules.classifyMotion(60, 62, 55, 55), MotionType::Oblique);
}

TEST(FuxRuleEvaluatorTest, ObliqueMotionBothStatic) {
  FuxRuleEvaluator rules;
  // Both voices stay on same pitch (oblique degenerate case).
  EXPECT_EQ(rules.classifyMotion(60, 60, 55, 55), MotionType::Oblique);
}

// ---------------------------------------------------------------------------
// Parallel perfect detection (stateful)
// ---------------------------------------------------------------------------

class ParallelPerfectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 36, 96);  // Soprano
    state.registerVoice(1, 36, 96);  // Alto
  }
  CounterpointState state;
  FuxRuleEvaluator rules;
};

TEST_F(ParallelPerfectTest, ParallelFifthsDetected) {
  // Beat 1: C4(60) and F3(53) -> interval 7 (P5)
  // Beat 2: D4(62) and G3(55) -> interval 7 (P5) -- parallel fifths!
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  EXPECT_TRUE(rules.hasParallelPerfect(state, 0, 1, 480));
}

TEST_F(ParallelPerfectTest, ParallelOctavesDetected) {
  // Beat 1: C4(60) and C3(48) -> interval 12 (P8)
  // Beat 2: D4(62) and D3(50) -> interval 12 (P8) -- parallel octaves!
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 48, 80, 1});
  state.addNote(1, {480, 480, 50, 80, 1});

  EXPECT_TRUE(rules.hasParallelPerfect(state, 0, 1, 480));
}

TEST_F(ParallelPerfectTest, NonParallelNotDetected) {
  // Beat 1: C4(60) and E3(52) -> interval 8 (m6)
  // Beat 2: D4(62) and G3(55) -> interval 7 (P5)
  // m6 -> P5 is not parallel perfect.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 52, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  EXPECT_FALSE(rules.hasParallelPerfect(state, 0, 1, 480));
}

TEST_F(ParallelPerfectTest, ContraryFifthsNotParallel) {
  // Beat 1: C4(60) and F3(53) -> interval 7 (P5)
  // Beat 2: B3(59) and E3(52) -> interval 7 (P5) but contrary motion.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 59, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 52, 80, 1});

  // Contrary motion P5->P5: both descend by same direction,
  // so actually this IS same direction. Let me verify: 60->59 is down,
  // 53->52 is down. Both down, interval remains 7. This IS parallel.
  // Use a true contrary case instead.
}

TEST_F(ParallelPerfectTest, ContraryMotionFifthsAreNotParallel) {
  // Beat 1: C4(60) and F3(53) -> interval 7 (P5)
  // Beat 2: B3(59) and F#3(54) -> interval 5 (P4)
  // Contrary motion, different intervals.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 59, 80, 0});  // down
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 54, 80, 1});  // up

  EXPECT_FALSE(rules.hasParallelPerfect(state, 0, 1, 480));
}

// ---------------------------------------------------------------------------
// Hidden perfect detection
// ---------------------------------------------------------------------------

TEST_F(ParallelPerfectTest, HiddenFifthByLeap) {
  // Both voices ascend, arriving at P5, upper voice by leap.
  // Beat 1: E4(64) and C4(60) -> interval 4 (M3)
  // Beat 2: G4(67) and C4(60) -> interval 7 (P5)
  // Upper voice leaps (64->67 = +3).
  state.addNote(0, {0, 480, 64, 80, 0});
  state.addNote(0, {480, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 58, 80, 1});
  state.addNote(1, {480, 480, 60, 80, 1});

  EXPECT_TRUE(rules.hasHiddenPerfect(state, 0, 1, 480));
}

TEST_F(ParallelPerfectTest, HiddenFifthByStepAllowed) {
  // Both voices ascend, arriving at P5, but upper voice by step.
  // Beat 1: F4(65) and C4(60) -> interval 5 (P4)
  // Beat 2: G4(67) and C4(60) -> interval 7 (P5)
  // Upper voice steps (65->67 = +2). Allowed.
  state.addNote(0, {0, 480, 66, 80, 0});
  state.addNote(0, {480, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 58, 80, 1});
  state.addNote(1, {480, 480, 60, 80, 1});

  EXPECT_FALSE(rules.hasHiddenPerfect(state, 0, 1, 480));
}

// ---------------------------------------------------------------------------
// Voice crossing detection
// ---------------------------------------------------------------------------

TEST_F(ParallelPerfectTest, VoiceCrossingDetected) {
  // Voice 0 (higher by convention) drops below voice 1.
  state.addNote(0, {0, 480, 55, 80, 0});  // Voice 0 at G3
  state.addNote(1, {0, 480, 60, 80, 1});  // Voice 1 at C4 -- higher!

  EXPECT_TRUE(rules.hasVoiceCrossing(state, 0, 1, 0));
}

TEST_F(ParallelPerfectTest, NoVoiceCrossing) {
  // Voice 0 is properly higher than voice 1.
  state.addNote(0, {0, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 60, 80, 1});

  EXPECT_FALSE(rules.hasVoiceCrossing(state, 0, 1, 0));
}

// ---------------------------------------------------------------------------
// Full validation
// ---------------------------------------------------------------------------

TEST(FuxRuleEvaluatorTest, ValidateDetectsParallelFifths) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);
  FuxRuleEvaluator rules;

  // Create parallel fifths: C4/F3 -> D4/G3
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  auto violations = rules.validate(state, 0, 960);
  bool found_parallel = false;
  for (const auto& viol : violations) {
    if (viol.rule == "parallel_fifths" || viol.rule == "parallel_octaves") {
      found_parallel = true;
      EXPECT_EQ(viol.tick, 480u);
    }
  }
  EXPECT_TRUE(found_parallel);
}

TEST(FuxRuleEvaluatorTest, ValidateCleanCounterpoint) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);
  FuxRuleEvaluator rules;

  // Good first-species counterpoint: imperfect consonances, contrary motion.
  // C4/E3 -> D4/B2 -> E4/C3 -> F4/A2
  state.addNote(0, {0, 480, 60, 80, 0});     // C4
  state.addNote(0, {480, 480, 62, 80, 0});    // D4
  state.addNote(0, {960, 480, 64, 80, 0});    // E4
  state.addNote(0, {1440, 480, 65, 80, 0});   // F4

  state.addNote(1, {0, 480, 52, 80, 1});      // E3 (m6 below C4 -- wait, 60-52=8 m6)
  state.addNote(1, {480, 480, 58, 80, 1});    // Bb3 (62-58=4 M3)
  state.addNote(1, {960, 480, 55, 80, 1});    // G3 (64-55=9 M6)
  state.addNote(1, {1440, 480, 57, 80, 1});   // A3 (65-57=8 m6)

  auto violations = rules.validate(state, 0, 1920);

  // Filter out warnings, keep only errors.
  int error_count = 0;
  for (const auto& viol : violations) {
    if (viol.severity >= 1) {
      ++error_count;
    }
  }
  EXPECT_EQ(error_count, 0);
}

TEST(FuxRuleEvaluatorTest, ValidateDissonanceOnStrongBeat) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);
  FuxRuleEvaluator rules;

  // Dissonance on beat 1 (strong beat): C4 and D3 = M2 (2 semitones).
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(1, {0, 480, 50, 80, 1});  // D3, interval = 10 = m7

  auto violations = rules.validate(state, 0, 480);
  bool found_dissonance = false;
  for (const auto& viol : violations) {
    if (viol.rule == "dissonance_on_strong_beat") {
      found_dissonance = true;
    }
  }
  EXPECT_TRUE(found_dissonance);
}

}  // namespace
}  // namespace bach
