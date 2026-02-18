// Tests for Phase 2: ConstraintState, InvariantSet, SectionAccumulator,
// GravityConfig, FeasibilityEstimator.

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "constraint/constraint_state.h"
#include "constraint/feasibility_estimator.h"
#include "constraint/obligation.h"
#include "core/basic_types.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NoteEvent makeNote(Tick start, Tick dur, uint8_t pitch) {
  NoteEvent n;
  n.start_tick = start;
  n.duration = dur;
  n.pitch = pitch;
  n.velocity = 80;
  return n;
}

// ---------------------------------------------------------------------------
// P2.g1: JSD computation tests
// ---------------------------------------------------------------------------

TEST(JSDTest, IdenticalDistributionsYieldZero) {
  float p[] = {0.25f, 0.25f, 0.25f, 0.25f};
  float q[] = {0.25f, 0.25f, 0.25f, 0.25f};
  float jsd = computeJSD(p, q, 4);
  EXPECT_NEAR(jsd, 0.0f, 1e-6f);
}

TEST(JSDTest, DifferentDistributionsPositive) {
  float p[] = {1.0f, 0.0f, 0.0f, 0.0f};
  float q[] = {0.0f, 0.0f, 0.0f, 1.0f};
  float jsd = computeJSD(p, q, 4);
  EXPECT_GT(jsd, 0.5f);
  EXPECT_LE(jsd, 1.0f);
}

TEST(JSDTest, BoundedByOne) {
  float p[] = {1.0f, 0.0f};
  float q[] = {0.0f, 1.0f};
  float jsd = computeJSD(p, q, 2);
  EXPECT_LE(jsd, 1.0f + 1e-6f);
}

// ---------------------------------------------------------------------------
// P2.g1: Normalize distribution tests
// ---------------------------------------------------------------------------

TEST(NormalizeTest, Uint16Normalization) {
  uint16_t counts[] = {100, 200, 300, 400};
  float out[4];
  normalizeDistribution(counts, out, 4);
  EXPECT_NEAR(out[0], 0.1f, 1e-5f);
  EXPECT_NEAR(out[1], 0.2f, 1e-5f);
  EXPECT_NEAR(out[2], 0.3f, 1e-5f);
  EXPECT_NEAR(out[3], 0.4f, 1e-5f);
}

TEST(NormalizeTest, ZeroCountsGiveUniform) {
  uint16_t counts[] = {0, 0, 0};
  float out[3];
  normalizeDistribution(counts, out, 3);
  float expected = 1.0f / 3.0f;
  for (int i = 0; i < 3; ++i) {
    EXPECT_NEAR(out[i], expected, 1e-5f);
  }
}

// ---------------------------------------------------------------------------
// P2.g1: InvariantSet Hard rejection tests
// ---------------------------------------------------------------------------

TEST(InvariantSetTest, RangeViolationIsHard) {
  InvariantSet inv;
  inv.voice_range_lo = 48;  // C3
  inv.voice_range_hi = 84;  // C6

  VerticalSnapshot snap;
  snap.num_voices = 1;
  snap.pitches[0] = 60;

  // Below range.
  auto result = inv.satisfies(30, 0, 0, snap, nullptr, nullptr, nullptr,
                              nullptr, 0);
  EXPECT_GT(result.hard_violations, 0);
  EXPECT_TRUE(result.range_violation);

  // Within range.
  result = inv.satisfies(60, 0, 0, snap, nullptr, nullptr, nullptr,
                         nullptr, 0);
  EXPECT_EQ(result.hard_violations, 0);
}

TEST(InvariantSetTest, RepeatLimitIsHard) {
  InvariantSet inv;
  inv.hard_repeat_limit = 3;

  VerticalSnapshot snap;
  snap.num_voices = 1;
  snap.pitches[0] = 60;

  uint8_t recent[] = {60, 60, 60};
  auto result = inv.satisfies(60, 0, 0, snap, nullptr, nullptr, nullptr,
                              recent, 3);
  EXPECT_GT(result.hard_violations, 0);
  EXPECT_TRUE(result.repeat_violation);

  // Different pitch is OK.
  result = inv.satisfies(62, 0, 0, snap, nullptr, nullptr, nullptr,
                         recent, 3);
  EXPECT_FALSE(result.repeat_violation);
}

// ---------------------------------------------------------------------------
// P2.g1: Soft violation -> recovery obligation test
// ---------------------------------------------------------------------------

TEST(ConstraintStateTest, SoftViolationGeneratesRecovery) {
  ConstraintState cs;
  EXPECT_EQ(cs.soft_violation_count, 0);

  cs.addRecoveryObligation(ObligationType::InvariantRecovery, 0, 480, 0);
  EXPECT_EQ(cs.soft_violation_count, 1);
  EXPECT_EQ(cs.active_obligations.size(), 1u);
  EXPECT_EQ(cs.active_obligations[0].strength, ObligationStrength::Soft);
}

TEST(ConstraintStateTest, RecoveryObligationExpires) {
  ConstraintState cs;
  cs.addRecoveryObligation(ObligationType::InvariantRecovery, 0, 480, 0);
  EXPECT_EQ(cs.active_obligations.size(), 1u);

  // Advance past deadline.
  cs.advance(960, 60, 0);
  EXPECT_EQ(cs.active_obligations.size(), 0u);
}

// ---------------------------------------------------------------------------
// P2.g1: SoftViolation 5-10% tolerance test
// ---------------------------------------------------------------------------

TEST(ConstraintStateTest, SoftViolationRatio) {
  ConstraintState cs;
  cs.total_note_count = 100;
  cs.soft_violation_count = 7;
  EXPECT_NEAR(cs.soft_violation_ratio(), 0.07f, 1e-6f);
  EXPECT_FALSE(cs.is_dead());  // 7% is within tolerance.
}

TEST(ConstraintStateTest, IsDeadOnExcessiveSoftViolations) {
  ConstraintState cs;
  cs.total_note_count = 100;
  cs.soft_violation_count = 20;  // 20% > 15% threshold
  EXPECT_TRUE(cs.is_dead());
}

// ---------------------------------------------------------------------------
// P2.g2: SectionAccumulator rhythm/harmony JSD tests
// ---------------------------------------------------------------------------

TEST(SectionAccumulatorTest, InitiallyUniform) {
  SectionAccumulator accum;
  // With zero counts, normalization yields uniform; JSD vs reference > 0.
  float jsd = accum.rhythm_jsd();
  EXPECT_GE(jsd, 0.0f);
}

TEST(SectionAccumulatorTest, ConvergesWithData) {
  SectionAccumulator accum;
  // Feed data matching reference distribution roughly.
  // kRhythmTarget: {1020, 6137, 2069, 581, 141, 31, 21}
  // Predominantly 16th notes (index 1).
  for (int i = 0; i < 100; ++i) {
    accum.recordNote(120, 0);  // 16th note, degree 0
  }
  for (int i = 0; i < 30; ++i) {
    accum.recordNote(240, 2);  // 8th note, degree 2
  }
  for (int i = 0; i < 15; ++i) {
    accum.recordNote(60, 4);   // 32nd note, degree 4
  }

  float jsd_rhythm = accum.rhythm_jsd();
  EXPECT_LT(jsd_rhythm, 0.3f) << "Rhythm JSD should be low with reference-like data";
}

TEST(SectionAccumulatorTest, ResetClearsAll) {
  SectionAccumulator accum;
  accum.recordNote(120, 0);
  accum.reset();
  EXPECT_EQ(accum.total_rhythm, 0);
  EXPECT_EQ(accum.total_harmony, 0);
}

// ---------------------------------------------------------------------------
// P2.g2: jsd_decay_factor tests
// ---------------------------------------------------------------------------

TEST(JsdDecayTest, NormalPositionNoDecay) {
  std::vector<Tick> cadences = {9600};
  float factor = jsd_decay_factor(4800, 19200, cadences, 0.5f);
  EXPECT_NEAR(factor, 1.0f, 0.01f);
}

TEST(JsdDecayTest, CadenceZoneDecays) {
  std::vector<Tick> cadences = {9600};
  // Within 2 beats (960 ticks) of cadence.
  float factor = jsd_decay_factor(9200, 19200, cadences, 0.5f);
  EXPECT_LT(factor, 0.8f);
}

TEST(JsdDecayTest, HighEnergyDecays) {
  std::vector<Tick> cadences;
  float factor = jsd_decay_factor(4800, 19200, cadences, 0.95f);
  EXPECT_LT(factor, 1.0f);
}

TEST(JsdDecayTest, FloorAt03) {
  std::vector<Tick> cadences = {4800};
  // Everything stacked: in cadence zone + high energy + near end.
  float factor = jsd_decay_factor(4750, 5000, cadences, 0.99f);
  EXPECT_GE(factor, 0.3f);
}

// ---------------------------------------------------------------------------
// P2.g2: GravityConfig phase weights test
// ---------------------------------------------------------------------------

TEST(GravityTest, PhaseWeightsSumToOne) {
  for (auto phase : {FuguePhase::Establish, FuguePhase::Develop,
                     FuguePhase::Resolve, FuguePhase::Conclude}) {
    const auto& w = getPhaseWeights(phase);
    float sum = w.melodic + w.vertical + w.rhythm + w.vocabulary;
    EXPECT_NEAR(sum, 1.0f, 1e-5f)
        << "Phase " << fuguePhaseToString(phase) << " weights don't sum to 1";
  }
}

// ---------------------------------------------------------------------------
// P2.g2: GravityConfig score boundedness test
// ---------------------------------------------------------------------------

TEST(GravityTest, ScoreIsBounded) {
  GravityConfig grav;
  grav.energy = 0.5f;
  grav.phase = FuguePhase::Develop;
  // No models set -> scores should be near zero.

  MarkovContext ctx;
  VerticalSnapshot snap;
  SectionAccumulator accum;

  float s = grav.score(60, 480, ctx, snap, accum, 1.0f, 0.0f);
  EXPECT_GT(s, -10.0f);
  EXPECT_LT(s, 10.0f);
}

// ---------------------------------------------------------------------------
// P2.g2: ConstraintState evaluate Hard rejection test
// ---------------------------------------------------------------------------

TEST(ConstraintStateTest, EvaluateRejectsOutOfRange) {
  ConstraintState cs;
  cs.invariants.voice_range_lo = 48;
  cs.invariants.voice_range_hi = 84;

  MarkovContext ctx;
  VerticalSnapshot snap;
  snap.num_voices = 1;
  snap.pitches[0] = 60;

  float score = cs.evaluate(30, 480, 0, 0, ctx, snap,
                            nullptr, nullptr, nullptr,
                            nullptr, 0, 0.0f);
  EXPECT_EQ(score, -std::numeric_limits<float>::infinity());
}

// ---------------------------------------------------------------------------
// P2.g2: CrossingPolicy phase-dependent test
// ---------------------------------------------------------------------------

TEST(InvariantSetTest, CrossingPolicyRejectInResolve) {
  InvariantSet inv;
  inv.crossing_policy = CrossingPolicy::Reject;
  // Without actual counterpoint state, we just verify the struct setting.
  EXPECT_EQ(inv.crossing_policy, CrossingPolicy::Reject);
}

TEST(InvariantSetTest, CrossingPolicyAllowInEstablish) {
  InvariantSet inv;
  inv.crossing_policy = CrossingPolicy::AllowTemporary;
  EXPECT_EQ(inv.crossing_policy, CrossingPolicy::AllowTemporary);
}

// ---------------------------------------------------------------------------
// P2.g2: VerticalSnapshot construction
// ---------------------------------------------------------------------------

TEST(VerticalSnapshotTest, FromCounterpointState) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.registerVoice(1, 36, 72);

  state.addNote(0, makeNote(0, 480, 72));  // C5
  state.addNote(1, makeNote(0, 480, 48));  // C3

  auto snap = VerticalSnapshot::fromState(state, 0);
  EXPECT_EQ(snap.num_voices, 2);
  EXPECT_EQ(snap.pitches[0], 72);
  EXPECT_EQ(snap.pitches[1], 48);
}

TEST(VerticalSnapshotTest, SilentVoiceIsZero) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);
  state.registerVoice(1, 36, 72);

  state.addNote(0, makeNote(0, 480, 72));
  // Voice 1 has no note at tick 0.

  // But we need to check at a tick where voice 1 has no note.
  auto snap = VerticalSnapshot::fromState(state, 960);
  EXPECT_EQ(snap.num_voices, 2);
  EXPECT_EQ(snap.pitches[0], 0);  // No note at tick 960
  EXPECT_EQ(snap.pitches[1], 0);
}

// ---------------------------------------------------------------------------
// P2.g2: FeasibilityEstimator basic test
// ---------------------------------------------------------------------------

TEST(FeasibilityEstimatorTest, EmptyStateHasCandidates) {
  CounterpointState state;
  state.registerVoice(0, 48, 84);

  BachRuleEvaluator rules(1);
  CollisionResolver resolver;
  InvariantSet inv;
  inv.voice_range_lo = 48;
  inv.voice_range_hi = 84;

  VerticalSnapshot snap;
  snap.num_voices = 1;

  FeasibilityEstimator estimator;
  auto result = estimator.estimate(state, rules, resolver, 0, 0, 480,
                                   inv, snap);
  EXPECT_GT(result.min_choices, 0);
}

// ---------------------------------------------------------------------------
// P2: Obligation conflicts/satisfies field tests
// ---------------------------------------------------------------------------

TEST(ObligationNodeTest, ConflictsFieldExists) {
  ObligationNode ob;
  ob.conflicts.push_back(42);
  EXPECT_EQ(ob.conflicts.size(), 1u);
  EXPECT_EQ(ob.conflicts[0], 42);
}

TEST(ObligationNodeTest, SatisfiesFieldExists) {
  ObligationNode ob;
  ob.satisfies.push_back(7);
  ob.satisfies.push_back(13);
  EXPECT_EQ(ob.satisfies.size(), 2u);
}

// ---------------------------------------------------------------------------
// P2: FuguePhase::Conclude exists
// ---------------------------------------------------------------------------

TEST(FuguePhaseTest, ConcludeExists) {
  FuguePhase phase = FuguePhase::Conclude;
  EXPECT_STREQ(fuguePhaseToString(phase), "Conclude");
}


// ---------------------------------------------------------------------------
// P4.e4: GravityConfig with models connected
// ---------------------------------------------------------------------------

TEST(GravityConfigTest, GravityScoreWithModel) {
  GravityConfig gravity;
  gravity.melodic_model = &kFugueUpperMarkov;
  gravity.vertical_table = &kFugueVerticalTable;
  gravity.phase = FuguePhase::Develop;
  gravity.energy = 0.6f;

  MarkovContext ctx;
  ctx.prev_pitch = 60;
  ctx.key = Key::C;
  ctx.scale = ScaleType::Major;
  ctx.beat = BeatPos::Bar;
  ctx.prev_dur = DurCategory::Qtr;
  ctx.prev_step = 0;
  ctx.deg_class = DegreeClass::Stable;
  ctx.dir_class = DirIntervalClass::StepUp;

  VerticalSnapshot snap;
  snap.num_voices = 2;
  snap.pitches[0] = 60;
  snap.pitches[1] = 67;

  SectionAccumulator accum;
  float score = gravity.score(62, kTicksPerBeat, ctx, snap, accum, 1.0f, 0.0f);

  // With models connected, score should be non-zero.
  EXPECT_NE(score, 0.0f);
}

TEST(GravityConfigTest, GravityScoreWithoutModel) {
  GravityConfig gravity;
  // melodic_model and vertical_table left as nullptr (default).

  MarkovContext ctx;
  ctx.prev_pitch = 60;
  ctx.key = Key::C;
  ctx.scale = ScaleType::Major;
  ctx.beat = BeatPos::Bar;
  ctx.prev_dur = DurCategory::Qtr;

  VerticalSnapshot snap;
  snap.num_voices = 2;
  snap.pitches[0] = 60;
  snap.pitches[1] = 67;

  SectionAccumulator accum;
  float score = gravity.score(62, kTicksPerBeat, ctx, snap, accum, 1.0f, 0.0f);

  // Without models, melodic and vertical layers produce 0. Only JSD penalty remains.
  // JSD penalty is negative (or zero with empty accumulator), so score should be <= 0.
  EXPECT_LE(score, 0.01f);
}

// ---------------------------------------------------------------------------
// P4.f4: advance() records in SectionAccumulator
// ---------------------------------------------------------------------------

TEST(ConstraintStateTest, AdvanceRecordsInAccumulator) {
  ConstraintState state;
  EXPECT_EQ(state.accumulator.total_rhythm, 0);
  EXPECT_EQ(state.accumulator.total_harmony, 0);

  // Advance with duration > 0 should record in accumulator.
  state.advance(0, 60, 0, kTicksPerBeat, Key::C);
  EXPECT_GT(state.accumulator.total_rhythm, 0);
  EXPECT_GT(state.accumulator.total_harmony, 0);
}

TEST(ConstraintStateTest, AdvanceZeroDurationSkipsRecord) {
  ConstraintState state;
  EXPECT_EQ(state.accumulator.total_rhythm, 0);

  // Advance with duration=0 (default) should NOT record.
  state.advance(0, 60, 0);
  EXPECT_EQ(state.accumulator.total_rhythm, 0);
  EXPECT_EQ(state.accumulator.total_harmony, 0);
}

}  // namespace
}  // namespace bach
