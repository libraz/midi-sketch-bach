// Tests for strong-beat suspension preparation-hold-resolution validation.
// Verifies: properly prepared suspensions resolve to consonance, and
// unprepared dissonances on strong beats are rejected.

#include "counterpoint/collision_resolver.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/fux_rule_evaluator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test fixture for suspension resolution tests
// ---------------------------------------------------------------------------

class SuspensionResolutionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state.registerVoice(0, 48, 84);  // Soprano: C3-C6
    state.registerVoice(1, 36, 72);  // Alto: C2-C5
  }
  CounterpointState state;
  FuxRuleEvaluator rules;
  CollisionResolver resolver;
};

// ---------------------------------------------------------------------------
// Properly prepared suspension resolves to consonance
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, PreparedSuspensionResolvesToConsonance) {
  // Setup a 7-6 type suspension:
  //   Previous (tick 0): Voice 0 has A4(69). Not relevant for prep.
  //   Preparation (tick 480): Voice 0 has E4(64), Voice 1 has C4(60).
  //     Interval |64-60| = 4 (M3) = consonant. Preparation OK.
  //   Hold (tick 960): Voice 0 holds E4(64), Voice 1 moves to D4(62).
  //     Interval |64-62| = 2 (M2) = dissonant.
  //   Resolution: E4-2=D4(62) with D4(62) = unison (0) = consonant.
  state.addNote(0, {0, 480, 69, 80, 0});    // Voice 0: A4 at tick 0.
  state.addNote(1, {480, 480, 60, 80, 1});   // Voice 1: C4 at tick 480.
  state.addNote(0, {480, 480, 64, 80, 0});   // Voice 0: E4 at tick 480 (prep).
  state.addNote(1, {960, 480, 62, 80, 1});   // Voice 1: D4 at tick 960.

  auto result = resolver.trySuspension(state, rules, 0, 72, 960, 480);
  EXPECT_TRUE(result.accepted)
      << "A properly prepared suspension should be accepted";
  EXPECT_EQ(result.pitch, 64)  // Held E4.
      << "Suspension should hold the preparation pitch";
  EXPECT_EQ(result.strategy, "suspension");

  // Verify the resolution pitch (E4-2=D4) is consonant with D4(62).
  int resolution_pitch = result.pitch - 2;
  int ivl = std::abs(resolution_pitch - 62);
  EXPECT_TRUE(rules.isIntervalConsonant(ivl, true))
      << "Resolution interval " << ivl << " should be consonant";
}

// ---------------------------------------------------------------------------
// Unprepared dissonance on strong beat is rejected
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, UnpreparedDissonanceOnStrongBeatRejected) {
  // An unprepared dissonance on a strong beat (tick 0, bar start) should
  // be rejected by isSafeToPlace.
  state.addNote(1, {0, 480, 48, 80, 1});  // Voice 1: C3 at tick 0.

  // Voice 0 wants D4(62) at tick 0. Interval |62-48| = 14, mod 12 = 2 (M2).
  // M2 is dissonant on strong beat. No prior context for suspension.
  EXPECT_FALSE(resolver.isSafeToPlace(state, rules, 0, 62, 0, 480))
      << "Unprepared M2 on strong beat (bar start) should be rejected";
}

TEST_F(SuspensionResolutionTest, UnpreparedDissonanceRejectedAtBeatBoundary) {
  // Tick 960 is beat 2 (strong beat in 4/4).
  state.addNote(1, {960, 480, 48, 80, 1});  // Voice 1: C3 at tick 960.

  // Voice 0 wants F#4(66) at tick 960. With C3: |66-48| = 18, mod 12 = 6 (TT).
  // Tritone is dissonant on any strong beat.
  EXPECT_FALSE(resolver.isSafeToPlace(state, rules, 0, 66, 960, 480))
      << "Unprepared tritone on strong beat should be rejected";
}

// ---------------------------------------------------------------------------
// Dissonant preparation fails suspension
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, DissonantPreparationRejectsSuspension) {
  // If the held pitch was dissonant at the preparation beat, the suspension
  // should be rejected (proper suspensions require consonant preparation).
  state.addNote(0, {0, 480, 64, 80, 0});    // Voice 0: E4 at tick 0.
  state.addNote(1, {480, 480, 60, 80, 1});   // Voice 1: C4 at tick 480.
  state.addNote(0, {480, 480, 62, 80, 0});   // Voice 0: D4 (prep: M2 with C4 = dissonant).
  state.addNote(1, {960, 480, 60, 80, 1});   // Voice 1: C4 at tick 960.

  auto result = resolver.trySuspension(state, rules, 0, 67, 960, 480);
  EXPECT_FALSE(result.accepted)
      << "Suspension with dissonant preparation (D4 vs C4 = M2) should be rejected";
}

// ---------------------------------------------------------------------------
// Consonant placement on strong beat always accepted
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, ConsonantOnStrongBeatAccepted) {
  state.addNote(1, {0, 480, 48, 80, 1});  // Voice 1: C3 at tick 0.

  // Voice 0 wants E4(64) at tick 0. Interval |64-48| = 16, mod 12 = 4 (M3).
  // M3 is an imperfect consonance -- always allowed.
  EXPECT_TRUE(resolver.isSafeToPlace(state, rules, 0, 64, 0, 480))
      << "Consonant M3 on strong beat should be accepted";

  // P5 is also consonant.
  EXPECT_TRUE(resolver.isSafeToPlace(state, rules, 0, 55, 0, 480))
      << "Consonant P5 on strong beat should be accepted";
}

// ---------------------------------------------------------------------------
// Suspension at tick 0 (no previous beat) is rejected
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, SuspensionAtTickZeroRejected) {
  state.addNote(1, {0, 480, 60, 80, 1});

  // Cannot have a suspension at tick 0: there is no previous beat to prepare.
  auto result = resolver.trySuspension(state, rules, 0, 62, 0, 480);
  EXPECT_FALSE(result.accepted)
      << "Suspension at tick 0 should be rejected (no preparation possible)";
}

// ---------------------------------------------------------------------------
// Suspension with no other voice at preparation beat rejected
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, NoHarmonicContextAtPrepRejected) {
  state.addNote(0, {0, 480, 60, 80, 0});    // Voice 0: C4 at tick 0.
  state.addNote(0, {480, 480, 64, 80, 0});   // Voice 0: E4 at tick 480.
  // Voice 1 only enters at tick 960 -- no harmonic context at preparation.
  state.addNote(1, {960, 480, 62, 80, 1});   // Voice 1: D4 at tick 960.

  auto result = resolver.trySuspension(state, rules, 0, 67, 960, 480);
  EXPECT_FALSE(result.accepted)
      << "Suspension without harmonic context at preparation should be rejected";
}

// ---------------------------------------------------------------------------
// Proper 4-3 suspension (descending resolution)
// ---------------------------------------------------------------------------

TEST_F(SuspensionResolutionTest, Proper43SuspensionAccepted) {
  // Classic 4-3 suspension:
  //   Prep: Voice 0 = G4(67), Voice 1 = C4(60). |67-60| = 7 (P5) = consonant.
  //   Hold: Voice 0 holds G4(67), Voice 1 moves to E4(64).
  //     |67-64| = 3 (m3) = consonant! Not actually dissonant on hold.
  // For a true 4-3, we need:
  //   Prep: Voice 0 = F4(65), Voice 1 = C4(60). |65-60| = 5 (P4) = dissonant in Fux 2-voice.
  // Use instead:
  //   Prep: Voice 0 = E4(64), Voice 1 = A3(57). |64-57| = 7 (P5) = consonant.
  //   Hold: Voice 0 holds E4(64), Voice 1 moves to B3(59).
  //     |64-59| = 5 (P4) = dissonant.
  //   Resolution: E4-1 = Eb4(63) with B3(59): |63-59| = 4 (M3) = consonant.
  //   Or E4-2 = D4(62) with B3(59): |62-59| = 3 (m3) = consonant.
  state.addNote(0, {0, 480, 60, 80, 0});    // Voice 0: C4 at tick 0.
  state.addNote(1, {480, 480, 57, 80, 1});   // Voice 1: A3 at tick 480.
  state.addNote(0, {480, 480, 64, 80, 0});   // Voice 0: E4 at tick 480 (prep).
  state.addNote(1, {960, 480, 59, 80, 1});   // Voice 1: B3 at tick 960.

  auto result = resolver.trySuspension(state, rules, 0, 72, 960, 480);
  EXPECT_TRUE(result.accepted)
      << "Properly prepared 4-3 type suspension should be accepted";
  EXPECT_EQ(result.pitch, 64)
      << "Should hold the preparation pitch E4";
}

}  // namespace
}  // namespace bach
