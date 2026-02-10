// Tests for counterpoint/bach_rule_evaluator.h -- Bach-style counterpoint
// rules: P4 consonance with 3+ voices, lenient hidden perfects, temporary
// voice crossing, and weak-beat dissonance tolerance.

#include "counterpoint/bach_rule_evaluator.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// P4 consonance context
// ---------------------------------------------------------------------------

TEST(BachRuleEvaluatorTest, P4ConsonantWithThreeOrMoreVoices) {
  BachRuleEvaluator rules(3);
  // P4 = 5 semitones. Should be consonant with 3+ voices.
  EXPECT_TRUE(rules.isIntervalConsonant(5, true));
  EXPECT_TRUE(rules.isIntervalConsonant(5, false));

  // Also check compound P4 (5 + 12 = 17).
  EXPECT_TRUE(rules.isIntervalConsonant(17, true));
}

TEST(BachRuleEvaluatorTest, P4DissonantWithTwoVoices) {
  BachRuleEvaluator rules(2);
  // P4 should remain dissonant in 2-voice context (same as Fux).
  EXPECT_FALSE(rules.isIntervalConsonant(5, true));
  EXPECT_FALSE(rules.isIntervalConsonant(5, false));
}

// ---------------------------------------------------------------------------
// Other consonances/dissonances unchanged
// ---------------------------------------------------------------------------

TEST(BachRuleEvaluatorTest, PerfectConsonancesStillWork) {
  BachRuleEvaluator rules(3);
  EXPECT_TRUE(rules.isIntervalConsonant(0, true));   // Unison
  EXPECT_TRUE(rules.isIntervalConsonant(7, true));   // P5
  EXPECT_TRUE(rules.isIntervalConsonant(12, true));  // P8
}

TEST(BachRuleEvaluatorTest, ImperfectConsonancesStillWork) {
  BachRuleEvaluator rules(3);
  EXPECT_TRUE(rules.isIntervalConsonant(3, true));   // m3
  EXPECT_TRUE(rules.isIntervalConsonant(4, true));   // M3
  EXPECT_TRUE(rules.isIntervalConsonant(8, true));   // m6
  EXPECT_TRUE(rules.isIntervalConsonant(9, true));   // M6
}

TEST(BachRuleEvaluatorTest, DissonancesStillFlagged) {
  BachRuleEvaluator rules(3);
  EXPECT_FALSE(rules.isIntervalConsonant(1, true));   // m2
  EXPECT_FALSE(rules.isIntervalConsonant(2, true));   // M2
  EXPECT_FALSE(rules.isIntervalConsonant(6, true));   // Tritone
  EXPECT_FALSE(rules.isIntervalConsonant(10, true));  // m7
  EXPECT_FALSE(rules.isIntervalConsonant(11, true));  // M7
}

// ---------------------------------------------------------------------------
// Hidden perfect -- lower voice step relaxation
// ---------------------------------------------------------------------------

class BachHiddenPerfectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 36, 96);  // Upper voice
    state.registerVoice(1, 36, 96);  // Lower voice
  }
  CounterpointState state;
  BachRuleEvaluator rules{3};
};

TEST_F(BachHiddenPerfectTest, HiddenPerfectAllowedWhenLowerVoiceSteps) {
  // Both voices ascend, arriving at P5, upper voice leaps but lower steps.
  // Beat 1: E4(64) and Bb3(58) -> interval 6 (tritone)
  // Beat 2: A4(69) and D4(62) -> interval 7 (P5)
  // Upper voice: 64->69 = leap of 5. Lower voice: 58->62 = step of 4.
  // Wait, 4 semitones is a major third, not a step. Use 2 semitones for step.
  //
  // Beat 1: E4(64) and A3(57) -> interval 7... that's already P5.
  // Need: prev interval != P5, arrive at P5, similar motion,
  // upper leaps (>2), lower steps (<=2).
  //
  // Beat 1: voice0=E4(64), voice1=C4(60) -> interval 4 (M3)
  // Beat 2: voice0=A4(69), voice1=D4(62) -> interval 7 (P5)
  // Upper: 64->69 = +5 (leap). Lower: 60->62 = +2 (step).
  // Both ascend -> similar motion. Arriving at P5.
  // Fux would flag this (upper leaps). Bach allows (lower steps).
  state.addNote(0, {0, 480, 64, 80, 0});
  state.addNote(0, {480, 480, 69, 80, 0});
  state.addNote(1, {0, 480, 60, 80, 1});
  state.addNote(1, {480, 480, 62, 80, 1});

  EXPECT_FALSE(rules.hasHiddenPerfect(state, 0, 1, 480));
}

TEST_F(BachHiddenPerfectTest, HiddenPerfectFlaggedWhenBothVoicesLeap) {
  // Both voices ascend by leap, arriving at P5.
  // Beat 1: voice0=E4(64), voice1=A3(57) -> interval 7 (P5)
  // That's already P5, would be parallel not hidden. Need different intervals.
  //
  // Beat 1: voice0=D4(62), voice1=Ab3(56) -> interval 6 (tritone)
  // Beat 2: voice0=G4(67), voice1=C4(60) -> interval 7 (P5)
  // Upper: 62->67 = +5 (leap). Lower: 56->60 = +4 (leap).
  // Both leap -> should be flagged even in Bach style.
  state.addNote(0, {0, 480, 62, 80, 0});
  state.addNote(0, {480, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 56, 80, 1});
  state.addNote(1, {480, 480, 60, 80, 1});

  EXPECT_TRUE(rules.hasHiddenPerfect(state, 0, 1, 480));
}

TEST_F(BachHiddenPerfectTest, HiddenPerfectAllowedWhenUpperVoiceSteps) {
  // Upper voice steps, lower leaps -- also allowed in both Fux and Bach.
  // Beat 1: voice0=F#4(66), voice1=Bb3(58) -> interval 8 (m6)
  // Beat 2: voice0=G4(67), voice1=C4(60) -> interval 7 (P5)
  // Upper: 66->67 = +1 (step). Lower: 58->60 = +2 (step).
  // Both step, so of course allowed. Need lower to leap.
  //
  // Beat 1: voice0=F#4(66), voice1=Ab3(56) -> interval 10 (m7)
  // Beat 2: voice0=G4(67), voice1=C4(60) -> interval 7 (P5)
  // Upper: 66->67 = +1 (step). Lower: 56->60 = +4 (leap).
  state.addNote(0, {0, 480, 66, 80, 0});
  state.addNote(0, {480, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 56, 80, 1});
  state.addNote(1, {480, 480, 60, 80, 1});

  EXPECT_FALSE(rules.hasHiddenPerfect(state, 0, 1, 480));
}

// ---------------------------------------------------------------------------
// Temporary voice crossing
// ---------------------------------------------------------------------------

class BachVoiceCrossingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 36, 96);  // Upper voice
    state.registerVoice(1, 36, 96);  // Lower voice
  }
  CounterpointState state;
  BachRuleEvaluator rules{3};
};

TEST_F(BachVoiceCrossingTest, TemporaryCrossingAllowed) {
  // Voice 0 (upper) dips below voice 1 at beat 2, but returns at beat 3.
  // Beat 1 (tick 0):    voice0=G4(67), voice1=C4(60) -- proper order.
  // Beat 2 (tick 480):  voice0=B3(59), voice1=C4(60) -- crossed!
  // Beat 3 (tick 960):  voice0=E4(64), voice1=C4(60) -- resolved.
  state.addNote(0, {0, 480, 67, 80, 0});
  state.addNote(0, {480, 480, 59, 80, 0});    // Crossing here.
  state.addNote(0, {960, 480, 64, 80, 0});     // Resolved.
  state.addNote(1, {0, 1440, 60, 80, 1});      // Held C4 throughout.

  // At tick 480, voice0(59) < voice1(60) = crossed, but resolves at tick 960.
  EXPECT_FALSE(rules.hasVoiceCrossing(state, 0, 1, 480));
}

TEST_F(BachVoiceCrossingTest, PersistentCrossingFlagged) {
  // Voice 0 (upper) drops below voice 1 and stays below.
  // Beat 1 (tick 0):    voice0=G4(67), voice1=C4(60) -- proper order.
  // Beat 2 (tick 480):  voice0=B3(59), voice1=C4(60) -- crossed!
  // Beat 3 (tick 960):  voice0=A3(57), voice1=C4(60) -- still crossed!
  state.addNote(0, {0, 480, 67, 80, 0});
  state.addNote(0, {480, 480, 59, 80, 0});    // Crossing here.
  state.addNote(0, {960, 480, 57, 80, 0});     // Still crossed.
  state.addNote(1, {0, 1440, 60, 80, 1});      // Held C4 throughout.

  // At tick 480, voice0(59) < voice1(60) = crossed, and at 960 still crossed.
  EXPECT_TRUE(rules.hasVoiceCrossing(state, 0, 1, 480));
}

TEST_F(BachVoiceCrossingTest, NoCrossingIsClean) {
  // Voice 0 stays properly above voice 1.
  state.addNote(0, {0, 480, 67, 80, 0});
  state.addNote(1, {0, 480, 60, 80, 1});

  EXPECT_FALSE(rules.hasVoiceCrossing(state, 0, 1, 0));
}

// ---------------------------------------------------------------------------
// Weak-beat dissonance in free counterpoint mode
// ---------------------------------------------------------------------------

TEST(BachRuleEvaluatorTest, WeakBeatDissonanceAllowedInFreeCounterpoint) {
  BachRuleEvaluator rules(3);
  rules.setFreeCounterpoint(true);

  // All intervals should be consonant on weak beats.
  EXPECT_TRUE(rules.isIntervalConsonant(1, false));   // m2 on weak beat
  EXPECT_TRUE(rules.isIntervalConsonant(2, false));   // M2 on weak beat
  EXPECT_TRUE(rules.isIntervalConsonant(6, false));   // Tritone on weak beat
  EXPECT_TRUE(rules.isIntervalConsonant(10, false));  // m7 on weak beat
  EXPECT_TRUE(rules.isIntervalConsonant(11, false));  // M7 on weak beat
}

TEST(BachRuleEvaluatorTest, StrongBeatDissonanceStillFlaggedInFreeCounterpoint) {
  BachRuleEvaluator rules(3);
  rules.setFreeCounterpoint(true);

  // Strong-beat dissonances are still flagged, even in free counterpoint.
  EXPECT_FALSE(rules.isIntervalConsonant(1, true));   // m2 on strong beat
  EXPECT_FALSE(rules.isIntervalConsonant(2, true));   // M2 on strong beat
  EXPECT_FALSE(rules.isIntervalConsonant(6, true));   // Tritone on strong beat
  EXPECT_FALSE(rules.isIntervalConsonant(10, true));  // m7 on strong beat
  EXPECT_FALSE(rules.isIntervalConsonant(11, true));  // M7 on strong beat
}

TEST(BachRuleEvaluatorTest, WeakBeatDissonanceFlaggedWithoutFreeCounterpoint) {
  BachRuleEvaluator rules(3);
  // Default: free counterpoint is off.
  EXPECT_FALSE(rules.isFreeCounterpoint());

  // Dissonances on weak beats should NOT be allowed without free counterpoint.
  EXPECT_FALSE(rules.isIntervalConsonant(1, false));   // m2
  EXPECT_FALSE(rules.isIntervalConsonant(6, false));   // Tritone
  EXPECT_FALSE(rules.isIntervalConsonant(11, false));  // M7
}

// ---------------------------------------------------------------------------
// Parallel fifths still detected (inherited logic)
// ---------------------------------------------------------------------------

class BachParallelPerfectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 36, 96);  // Upper voice
    state.registerVoice(1, 36, 96);  // Lower voice
  }
  CounterpointState state;
  BachRuleEvaluator rules{3};
};

TEST_F(BachParallelPerfectTest, ParallelFifthsStillDetected) {
  // Beat 1: C4(60) and F3(53) -> interval 7 (P5)
  // Beat 2: D4(62) and G3(55) -> interval 7 (P5) -- parallel fifths!
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 53, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  EXPECT_TRUE(rules.hasParallelPerfect(state, 0, 1, 480));
}

TEST_F(BachParallelPerfectTest, ParallelOctavesStillDetected) {
  // Beat 1: C4(60) and C3(48) -> interval 12 (P8)
  // Beat 2: D4(62) and D3(50) -> interval 12 (P8) -- parallel octaves!
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 48, 80, 1});
  state.addNote(1, {480, 480, 50, 80, 1});

  EXPECT_TRUE(rules.hasParallelPerfect(state, 0, 1, 480));
}

TEST_F(BachParallelPerfectTest, NonParallelNotFlagged) {
  // Beat 1: C4(60) and E3(52) -> interval 8 (m6)
  // Beat 2: D4(62) and G3(55) -> interval 7 (P5)
  // m6 -> P5 is not parallel.
  state.addNote(0, {0, 480, 60, 80, 0});
  state.addNote(0, {480, 480, 62, 80, 0});
  state.addNote(1, {0, 480, 52, 80, 1});
  state.addNote(1, {480, 480, 55, 80, 1});

  EXPECT_FALSE(rules.hasParallelPerfect(state, 0, 1, 480));
}

// ---------------------------------------------------------------------------
// Full validation integration
// ---------------------------------------------------------------------------

TEST(BachRuleEvaluatorTest, ValidateDetectsParallelFifths) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 36, 96);
  BachRuleEvaluator rules(3);

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

TEST(BachRuleEvaluatorTest, NumVoicesAccessor) {
  BachRuleEvaluator rules2(2);
  BachRuleEvaluator rules4(4);
  EXPECT_EQ(rules2.numVoices(), 2);
  EXPECT_EQ(rules4.numVoices(), 4);
}

TEST(BachRuleEvaluatorTest, FreeCounterpointToggle) {
  BachRuleEvaluator rules(3);
  EXPECT_FALSE(rules.isFreeCounterpoint());
  rules.setFreeCounterpoint(true);
  EXPECT_TRUE(rules.isFreeCounterpoint());
  rules.setFreeCounterpoint(false);
  EXPECT_FALSE(rules.isFreeCounterpoint());
}

}  // namespace
}  // namespace bach
