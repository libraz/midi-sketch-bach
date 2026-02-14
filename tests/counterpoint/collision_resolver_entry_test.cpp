// Tests for voice reentry detection in collision_resolver.cpp.
//
// Voice reentry occurs when a voice re-enters after a rest gap of at least
// one full bar. On strong beats, reentry notes must be consonant -- the NHT
// exemption (passing tone / neighbor tone) is disabled because there is no
// continuous melodic context to justify a non-harmonic tone.

#include "counterpoint/collision_resolver.h"

#include <gtest/gtest.h>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/fux_rule_evaluator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr Tick kBeat = kTicksPerBeat;  // 480
constexpr Tick kBar = kTicksPerBar;    // 1920

// ---------------------------------------------------------------------------
// Fixture: 2-voice state with FuxRuleEvaluator
// ---------------------------------------------------------------------------

class CollisionResolverEntryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_.registerVoice(0, 48, 84);  // Voice 0: C3-C6
    state_.registerVoice(1, 36, 72);  // Voice 1: C2-C5
  }

  CounterpointState state_;
  FuxRuleEvaluator rules_;
  CollisionResolver resolver_;
};

// ---------------------------------------------------------------------------
// A. Voice reentry forces cascade on strong-beat dissonance
// ---------------------------------------------------------------------------

TEST_F(CollisionResolverEntryTest, ReentryForcesConsonantPitch) {
  // Voice 0 plays C4 sustained from tick 0 across several bars.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 played C3 at tick 0 for one beat, then rests.
  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 48;  // C3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // Voice 1 re-enters at tick kBar*2 (gap = kBar*2 - kBeat > kBar).
  // Try to place F#3 (54) which is a tritone (6 semitones) from C4 -- dissonant.
  Tick reentry_tick = kBar * 2;
  uint8_t dissonant_pitch = 54;  // F#3: tritone from C4

  PlacementResult result = resolver_.findSafePitch(
      state_, rules_, 1, dissonant_pitch, reentry_tick, kBeat);

  // Should be accepted but with a DIFFERENT pitch (cascade rescued).
  EXPECT_TRUE(result.accepted);
  EXPECT_NE(result.pitch, dissonant_pitch)
      << "Dissonant reentry pitch should be replaced by cascade";

  // The rescued pitch should be consonant with voice 0's C4.
  int ivl = absoluteInterval(result.pitch, 60);
  int simple = interval_util::compoundToSimple(ivl);
  EXPECT_TRUE(interval_util::isConsonance(simple))
      << "Rescued pitch " << static_cast<int>(result.pitch)
      << " should be consonant with C4 (simple interval = " << simple << ")";
}

// ---------------------------------------------------------------------------
// B. Voice reentry allows consonant intervals
// ---------------------------------------------------------------------------

TEST_F(CollisionResolverEntryTest, ReentryAllowsConsonant) {
  // Voice 0 plays C4 sustained.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 had a note at tick 0 then rests for a long time.
  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 48;  // C3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // Voice 1 re-enters with E3 (52) -- major 3rd below C4, consonant.
  Tick reentry_tick = kBar * 2;
  uint8_t consonant_pitch = 52;  // E3

  PlacementResult result = resolver_.findSafePitch(
      state_, rules_, 1, consonant_pitch, reentry_tick, kBeat);

  EXPECT_TRUE(result.accepted);
  // Should accept original pitch (consonant, no cascade needed).
  EXPECT_EQ(result.pitch, consonant_pitch)
      << "Consonant reentry pitch should be accepted unchanged";
}

// ---------------------------------------------------------------------------
// C. Normal NHT exemption preserved for non-reentry
// ---------------------------------------------------------------------------

TEST_F(CollisionResolverEntryTest, NormalNHTExemptionPreserved) {
  // Voice 0 plays C4 sustained.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 plays E3(52) ending right at tick kBeat (continuous, NO gap).
  NoteEvent v1_prev;
  v1_prev.start_tick = 0;
  v1_prev.duration = kBeat;
  v1_prev.pitch = 52;  // E3
  v1_prev.velocity = 80;
  v1_prev.voice = 1;
  state_.addNote(1, v1_prev);

  // At tick kBeat (continuous, not a reentry), try F3(53) with next = E3(52).
  // Pattern: E3 -> F3 -> E3 = neighbor tone.
  // F3(53) with C4(60) = 7 semitones = P5, which is consonant.
  // So let's try a dissonant scenario for NHT:
  // prev=E3(52), current=F#3(54), next=G3(55).
  // E3->F#3->G3 is stepwise ascending passing tone.
  // F#3(54) with C4(60) = 6 semitones = tritone, dissonant.
  // This should be allowed as a passing tone (NHT exemption).
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 54, kBeat, kBeat,
      std::optional<uint8_t>(55));  // next = G3(55)

  // Wait: melodic leap constraint checks tritone in melodic context.
  // prev=E3(52), current=F#3(54): leap = |54-52| = 2 (M2), stepwise, OK.
  // The vertical interval F#3(54) vs C4(60) = 6 = tritone, dissonant on strong beat.
  // But with NHT (passing tone E3->F#3->G3), should be allowed.
  EXPECT_TRUE(safe)
      << "NHT exemption should allow dissonant passing tone for continuous voice";
}

TEST_F(CollisionResolverEntryTest, ReentryDisablesNHTExemption) {
  // Same scenario as above but with a reentry gap -- NHT should NOT work.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 played at tick 0, then has a gap of 2 bars.
  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 52;  // E3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // At tick kBar*2 (reentry), try same dissonant pitch with next_pitch context.
  // F#3(54) with C4(60) = tritone = dissonant.
  // Even with next_pitch=55 (passing tone pattern), NHT should be disabled
  // because this is a reentry.
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 54, kBar * 2, kBeat,
      std::optional<uint8_t>(55));

  EXPECT_FALSE(safe)
      << "NHT exemption should be disabled for voice reentry";
}

// ---------------------------------------------------------------------------
// D. Reentry stats are tracked
// ---------------------------------------------------------------------------

TEST_F(CollisionResolverEntryTest, ReentryStatsTracked) {
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 48;  // C3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // Initial stats should be zero.
  auto [det0, cas0, res0] = resolver_.getReentryStats();
  EXPECT_EQ(det0, 0u);
  EXPECT_EQ(cas0, 0u);
  EXPECT_EQ(res0, 0u);

  // Trigger reentry with dissonant pitch.
  // F#3(54) against C4(60) = tritone = dissonant.
  resolver_.findSafePitch(state_, rules_, 1, 54, kBar * 2, kBeat);

  auto [det1, cas1, res1] = resolver_.getReentryStats();
  EXPECT_GT(det1, 0u) << "Reentry detection should be counted";
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_F(CollisionResolverEntryTest, FirstEntryIsNotReentry) {
  // A voice that has never played before is technically a "first entry",
  // which isVoiceReentry classifies as true (returns true when no prior notes).
  // This means even the first note must be consonant on strong beats, which
  // is correct Baroque counterpoint practice.

  // Voice 0 plays C4.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 has never played. Try dissonant pitch at tick 0 (strong beat).
  // D3(50) with C4(60) = 10 = m7, dissonant.
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 50, 0, kBeat, std::optional<uint8_t>(52));

  EXPECT_FALSE(safe)
      << "First entry should also force consonant placement on strong beat";
}

TEST_F(CollisionResolverEntryTest, ShortGapIsNotReentry) {
  // A gap shorter than one beat does NOT trigger reentry detection.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 played at tick 0 for just under one beat, ending at tick kBeat-1.
  NoteEvent v1_prev;
  v1_prev.start_tick = 0;
  v1_prev.duration = kBeat - 1;  // Ends at tick 479
  v1_prev.pitch = 52;  // E3
  v1_prev.velocity = 80;
  v1_prev.voice = 1;
  state_.addNote(1, v1_prev);

  // At tick kBeat (480), gap = 480 - 479 = 1 tick (< kBeat).
  // This is NOT a reentry.
  // F#3(54) with C4(60) = tritone, dissonant on strong beat.
  // With next_pitch=55, passing tone E3->F#3->G3 should be allowed.
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 54, kBeat, kBeat,
      std::optional<uint8_t>(55));

  EXPECT_TRUE(safe)
      << "Gap < kBeat should NOT trigger reentry; NHT exemption should apply";
}

TEST_F(CollisionResolverEntryTest, ExactBeatGapIsReentry) {
  // A gap of exactly kBeat (480 ticks) DOES trigger reentry.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 played at tick 0 for one beat, ending at kBeat.
  NoteEvent v1_prev;
  v1_prev.start_tick = 0;
  v1_prev.duration = kBeat;
  v1_prev.pitch = 52;  // E3
  v1_prev.velocity = 80;
  v1_prev.voice = 1;
  state_.addNote(1, v1_prev);

  // At tick kBeat*2 (960), gap = 960 - 480 = 480 = kBeat. This IS reentry.
  // F#3(54) with C4(60) = tritone, dissonant.
  // Even with next_pitch, NHT should be disabled.
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 54, kBeat * 2, kBeat,
      std::optional<uint8_t>(55));

  EXPECT_FALSE(safe)
      << "Gap == kBeat should trigger reentry; NHT exemption disabled";
}

TEST_F(CollisionResolverEntryTest, ContinuousVoiceIsNotReentry) {
  // Gap = 0 (continuous) is never reentry.
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 plays one beat ending exactly at kBeat.
  NoteEvent v1_prev;
  v1_prev.start_tick = 0;
  v1_prev.duration = kBeat;
  v1_prev.pitch = 52;  // E3
  v1_prev.velocity = 80;
  v1_prev.voice = 1;
  state_.addNote(1, v1_prev);

  // At tick kBeat (480), gap = 480 - 480 = 0. Continuous, NOT reentry.
  // F#3(54) dissonant with C4, but NHT exemption should apply.
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 54, kBeat, kBeat,
      std::optional<uint8_t>(55));

  EXPECT_TRUE(safe)
      << "Gap = 0 (continuous) should NOT trigger reentry; NHT applies";
}

TEST_F(CollisionResolverEntryTest, OffbeatPositionIsNotReentry) {
  // Reentry only triggers on beat boundaries (tick % kTicksPerBeat == 0).
  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  // Voice 1 played early, gap > kBar.
  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 48;  // C3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // At tick kBar*2 + 240 (off beat -- 8th note position), not a beat boundary.
  // isSafeToPlace only checks consonance on strong beats (tick % kTicksPerBeat == 0).
  // Since 240 is NOT a beat boundary, consonance is not checked, so dissonance
  // passes regardless of reentry status.
  // Use D3(50) which is M2 from C4(60) = dissonant, but |50-48|=2 (M2)
  // avoids the melodic tritone check that rejects 54 from 48 (|54-48|=6).
  Tick offbeat_tick = kBar * 2 + kBeat / 2;  // 4080 = not beat boundary
  bool safe = resolver_.isSafeToPlace(
      state_, rules_, 1, 50, offbeat_tick, kBeat);

  EXPECT_TRUE(safe)
      << "Off-beat positions should not enforce consonance regardless of gap";
}

TEST_F(CollisionResolverEntryTest, ReentryWithBachRuleEvaluator) {
  // Verify reentry logic also works with BachRuleEvaluator.
  BachRuleEvaluator bach_rules{2};

  NoteEvent v0_note;
  v0_note.start_tick = 0;
  v0_note.duration = kBar * 4;
  v0_note.pitch = 60;  // C4
  v0_note.velocity = 80;
  v0_note.voice = 0;
  state_.addNote(0, v0_note);

  NoteEvent v1_old;
  v1_old.start_tick = 0;
  v1_old.duration = kBeat;
  v1_old.pitch = 48;  // C3
  v1_old.velocity = 80;
  v1_old.voice = 1;
  state_.addNote(1, v1_old);

  // Reentry with dissonant pitch -- should be rejected by isSafeToPlace.
  Tick reentry_tick = kBar * 2;
  bool safe = resolver_.isSafeToPlace(
      state_, bach_rules, 1, 54, reentry_tick, kBeat,
      std::optional<uint8_t>(55));

  EXPECT_FALSE(safe)
      << "Reentry detection should also work with BachRuleEvaluator";

  // But the cascade should rescue it.
  PlacementResult result = resolver_.findSafePitch(
      state_, bach_rules, 1, 54, reentry_tick, kBeat);
  EXPECT_TRUE(result.accepted);
  EXPECT_NE(result.pitch, 54);
}

}  // namespace
}  // namespace bach
