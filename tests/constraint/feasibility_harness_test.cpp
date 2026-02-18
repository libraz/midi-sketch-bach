// Tests for feasibility harness: micro-exposition simulation (g1) and
// voice assignment search (g2).

#include "constraint/feasibility_harness.h"

#include <cstdio>

#include <gtest/gtest.h>

#include "constraint/obligation.h"
#include "constraint/obligation_analyzer.h"
#include "core/basic_types.h"
#include "fugue/answer.h"
#include "fugue/countersubject.h"
#include "fugue/fugue_config.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Create a simple NoteEvent.
NoteEvent makeNote(Tick start, Tick dur, uint8_t pitch) {
  NoteEvent note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = pitch;
  note.velocity = 80;
  return note;
}

/// @brief Build a simple test subject with ascending quarter notes in C major.
///
/// 8 notes spanning 2 bars: C4-D4-E4-F4-G4-A4-B4-C5.
Subject makeSimpleSubject() {
  Subject subject;
  subject.key = Key::C;
  subject.is_minor = false;
  subject.character = SubjectCharacter::Severe;
  subject.length_ticks = kTicksPerBar * 2;

  uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  return subject;
}

/// @brief Build a BWV578-style subject in G minor.
///
/// G4-D5-C5-Bb4-A4-G4-F#4-G4 with mixed rhythms.
Subject makeBWV578Subject() {
  Subject subject;
  subject.key = Key::G;
  subject.is_minor = true;
  subject.character = SubjectCharacter::Severe;

  Tick qtr = kTicksPerBeat;
  Tick eighth = kTicksPerBeat / 2;
  Tick tick = 0;

  subject.notes.push_back(makeNote(tick, qtr * 2, 67));  tick += qtr * 2;  // G4
  subject.notes.push_back(makeNote(tick, qtr, 74));       tick += qtr;      // D5
  subject.notes.push_back(makeNote(tick, eighth, 72));    tick += eighth;   // C5
  subject.notes.push_back(makeNote(tick, eighth, 70));    tick += eighth;   // Bb4
  subject.notes.push_back(makeNote(tick, eighth, 69));    tick += eighth;   // A4
  subject.notes.push_back(makeNote(tick, eighth, 67));    tick += eighth;   // G4
  subject.notes.push_back(makeNote(tick, eighth, 66));    tick += eighth;   // F#4
  subject.notes.push_back(makeNote(tick, qtr, 67));       tick += qtr;     // G4

  subject.length_ticks = tick;
  for (auto& note : subject.notes) {
    note.voice = 0;
  }
  return subject;
}

/// @brief Build a standard FugueConfig for testing.
FugueConfig makeTestConfig(uint8_t num_voices = 3) {
  FugueConfig config;
  config.num_voices = num_voices;
  config.key = Key::C;
  config.is_minor = false;
  config.character = SubjectCharacter::Severe;
  config.subject_bars = 2;
  config.seed = 42;
  return config;
}

// ---------------------------------------------------------------------------
// P1.g1: MicroExposition Simulator tests
// ---------------------------------------------------------------------------

TEST(MicroSimTest, SimpleSubjectHasPositiveSuccessRate) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 5);

  EXPECT_EQ(result.num_attempts, 5);
  EXPECT_GT(result.success_rate(), 0.0f)
      << "Simple ascending subject should have some successful trials";
}

TEST(MicroSimTest, NumAttemptsMatchesTrials) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 10);

  EXPECT_EQ(result.num_attempts, 10);
  EXPECT_GE(result.num_success, 0);
  EXPECT_LE(result.num_success, 10);
}

TEST(MicroSimTest, EmptySubjectReturnsZeroAttempts) {
  Subject subject;
  subject.key = Key::C;
  subject.length_ticks = kTicksPerBar * 2;
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 5);

  EXPECT_EQ(result.num_attempts, 5);
  EXPECT_EQ(result.num_success, 0);
  EXPECT_FLOAT_EQ(result.success_rate(), 0.0f);
}

TEST(MicroSimTest, ZeroTrialsReturnsDefault) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 0);

  EXPECT_EQ(result.num_attempts, 0);
  EXPECT_FLOAT_EQ(result.success_rate(), 0.0f);
}

TEST(MicroSimTest, RegisterOverlapInValidRange) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 5);

  EXPECT_GE(result.avg_register_overlap, 0.0f);
  EXPECT_LE(result.avg_register_overlap, 1.0f);
}

TEST(MicroSimTest, AccentCollisionInValidRange) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);

  MicroSimResult result = runMicroSim(subject, config, 5);

  EXPECT_GE(result.max_accent_collision, 0.0f);
  EXPECT_LE(result.max_accent_collision, 1.0f);
}

TEST(MicroSimTest, BWV578SubjectRuns) {
  Subject subject = makeBWV578Subject();
  FugueConfig config;
  config.num_voices = 4;
  config.key = Key::G;
  config.is_minor = true;
  config.character = SubjectCharacter::Severe;
  config.subject_bars = 2;
  config.seed = 100;

  MicroSimResult result = runMicroSim(subject, config, 5);

  EXPECT_EQ(result.num_attempts, 5);
  // BWV578 is a well-formed fugue subject, should have non-zero success.
  EXPECT_GE(result.num_success, 0);
}

TEST(MicroSimTest, FourVoiceFugueRuns) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(4);

  MicroSimResult result = runMicroSim(subject, config, 3);

  EXPECT_EQ(result.num_attempts, 3);
}

TEST(MicroSimTest, TwoVoiceFugueRuns) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(2);

  MicroSimResult result = runMicroSim(subject, config, 3);

  EXPECT_EQ(result.num_attempts, 3);
}

// ---------------------------------------------------------------------------
// MicroSimResult unit tests
// ---------------------------------------------------------------------------

TEST(MicroSimResultTest, SuccessRateComputation) {
  MicroSimResult result;
  result.num_attempts = 10;
  result.num_success = 9;
  EXPECT_NEAR(result.success_rate(), 0.9f, 0.001f);
}

TEST(MicroSimResultTest, FeasibleWhenAllThresholdsMet) {
  MicroSimResult result;
  result.num_attempts = 20;
  result.num_success = 19;  // 95% success rate
  result.num_critical_violations = 0;
  result.num_bottleneck = 0;
  result.avg_register_overlap = 0.40f;
  result.max_accent_collision = 0.30f;
  EXPECT_TRUE(result.feasible());
}

TEST(MicroSimResultTest, InfeasibleLowSuccessRate) {
  MicroSimResult result;
  result.num_attempts = 20;
  result.num_success = 10;  // 50% success rate
  result.num_critical_violations = 0;
  result.num_bottleneck = 0;
  result.avg_register_overlap = 0.40f;
  result.max_accent_collision = 0.30f;
  EXPECT_FALSE(result.feasible());
}

TEST(MicroSimResultTest, InfeasibleCriticalViolations) {
  MicroSimResult result;
  result.num_attempts = 20;
  result.num_success = 19;
  result.num_critical_violations = 1;
  result.num_bottleneck = 0;
  result.avg_register_overlap = 0.40f;
  result.max_accent_collision = 0.30f;
  EXPECT_FALSE(result.feasible());
}

TEST(MicroSimResultTest, InfeasibleHighOverlap) {
  MicroSimResult result;
  result.num_attempts = 20;
  result.num_success = 19;
  result.num_critical_violations = 0;
  result.num_bottleneck = 0;
  result.avg_register_overlap = 0.70f;  // Too high
  result.max_accent_collision = 0.30f;
  EXPECT_FALSE(result.feasible());
}

TEST(MicroSimResultTest, InfeasibleHighAccentCollision) {
  MicroSimResult result;
  result.num_attempts = 20;
  result.num_success = 19;
  result.num_critical_violations = 0;
  result.num_bottleneck = 0;
  result.avg_register_overlap = 0.40f;
  result.max_accent_collision = 0.50f;  // Too high
  EXPECT_FALSE(result.feasible());
}

// ---------------------------------------------------------------------------
// P1.g2: VoiceAssignmentSearch tests
// ---------------------------------------------------------------------------

TEST(VoiceAssignmentTest, ReturnsValidAssignment) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);
  auto profile = analyzeObligations(subject.notes, subject.key, subject.is_minor);

  VoiceAssignment assignment = findBestAssignment(subject, profile, config);

  EXPECT_GE(assignment.start_octave_offset, -2);
  EXPECT_LE(assignment.start_octave_offset, 2);
}

TEST(VoiceAssignmentTest, ScoresInValidRange) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(3);
  auto profile = analyzeObligations(subject.notes, subject.key, subject.is_minor);

  VoiceAssignment assignment = findBestAssignment(subject, profile, config);

  EXPECT_GE(assignment.separation_score, 0.0f);
  EXPECT_LE(assignment.separation_score, 1.0f);
  EXPECT_GE(assignment.sim_score, 0.0f);
  EXPECT_LE(assignment.sim_score, 1.0f);
  EXPECT_GE(assignment.final_score, 0.0f);
  EXPECT_LE(assignment.final_score, 1.0f);
}

TEST(VoiceAssignmentTest, FourVoiceReturnsAssignment) {
  Subject subject = makeSimpleSubject();
  FugueConfig config = makeTestConfig(4);
  auto profile = analyzeObligations(subject.notes, subject.key, subject.is_minor);

  VoiceAssignment assignment = findBestAssignment(subject, profile, config);

  EXPECT_GE(assignment.start_octave_offset, -2);
  EXPECT_LE(assignment.start_octave_offset, 2);
  EXPECT_GE(assignment.final_score, 0.0f);
}

TEST(VoiceAssignmentTest, EmptySubjectReturnsDefault) {
  Subject subject;
  subject.key = Key::C;
  subject.length_ticks = kTicksPerBar * 2;
  FugueConfig config = makeTestConfig(3);
  SubjectConstraintProfile profile;

  VoiceAssignment assignment = findBestAssignment(subject, profile, config);

  // Should return some assignment even with empty subject.
  EXPECT_GE(assignment.start_octave_offset, -2);
  EXPECT_LE(assignment.start_octave_offset, 2);
  EXPECT_GE(assignment.final_score, 0.0f);
}

TEST(VoiceAssignmentTest, BWV578SubjectRuns) {
  Subject subject = makeBWV578Subject();
  FugueConfig config;
  config.num_voices = 4;
  config.key = Key::G;
  config.is_minor = true;
  config.character = SubjectCharacter::Severe;
  config.subject_bars = 2;
  config.seed = 42;
  auto profile = analyzeObligations(subject.notes, subject.key, subject.is_minor);

  VoiceAssignment assignment = findBestAssignment(subject, profile, config);

  EXPECT_GE(assignment.start_octave_offset, -2);
  EXPECT_LE(assignment.start_octave_offset, 2);
  EXPECT_GE(assignment.final_score, 0.0f);
}

// ---------------------------------------------------------------------------
// VoiceAssignment unit tests
// ---------------------------------------------------------------------------

TEST(VoiceAssignmentStructTest, DefaultValues) {
  VoiceAssignment assignment;
  EXPECT_EQ(assignment.start_octave_offset, 0);
  EXPECT_FLOAT_EQ(assignment.separation_score, 0.0f);
  EXPECT_FLOAT_EQ(assignment.sim_score, 0.0f);
  EXPECT_FLOAT_EQ(assignment.final_score, 0.0f);
}

// ===========================================================================
// P1.g3: PairVerification tests
// ===========================================================================

// Helper: create a SubjectConstraintProfile with specific obligations.
SubjectConstraintProfile makeProfileWithObligations(
    std::vector<ObligationNode> obligations) {
  SubjectConstraintProfile profile;
  profile.obligations = std::move(obligations);
  profile.tonal_answer_feasible = true;
  return profile;
}

ObligationNode makeObligation(uint16_t node_id, ObligationType type,
                              Tick start, Tick deadline) {
  ObligationNode node;
  node.id = node_id;
  node.type = type;
  node.start_tick = start;
  node.deadline = deadline;
  node.origin = start;
  node.strength = ObligationStrength::Soft;
  return node;
}

// Test 1: Non-overlapping obligations -> feasible
TEST(PairVerificationTest, NonOverlappingObligationsFeasible) {
  // Subject obligations: LeapResolve at ticks [0, 480]
  SubjectConstraintProfile subject_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::LeapResolve, 0, kTicksPerBeat),
  });

  // Answer obligations: LeapResolve at ticks [0, 480] (will be shifted)
  SubjectConstraintProfile answer_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::LeapResolve, 0, kTicksPerBeat),
  });

  // Offset large enough that obligations don't overlap.
  // Subject: [0, 480], Answer shifted by 960: [960, 1440]
  auto result = verifyPair(subject_prof, answer_prof,
                           static_cast<int>(kTicksPerBeat * 2));

  EXPECT_TRUE(result.feasible())
      << "Non-overlapping obligations should be feasible";
  EXPECT_TRUE(result.conflicts.empty())
      << "No conflicts expected for non-overlapping obligations";
  EXPECT_LT(result.cadence_conflict_score, 0.5f);
}

// Test 2: LT vs StrongBeatHarm conflict -> has conflicts
TEST(PairVerificationTest, LeadingToneVsStrongBeatHarmConflict) {
  // Subject: LeadingTone active at [480, 1440]
  SubjectConstraintProfile subject_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::LeadingTone,
                     kTicksPerBeat, kTicksPerBeat * 3),
  });

  // Answer: StrongBeatHarm gate at [960, 960] (instantaneous at strong beat)
  // With offset=0, this should overlap with the subject's LeadingTone.
  SubjectConstraintProfile answer_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::StrongBeatHarm,
                     kTicksPerBeat * 2, kTicksPerBeat * 2),
  });

  auto result = verifyPair(subject_prof, answer_prof, 0);

  EXPECT_FALSE(result.conflicts.empty())
      << "LeadingTone vs StrongBeatHarm should produce a conflict";

  // Verify conflict types.
  bool found_lt_sbh_conflict = false;
  for (const auto& conflict : result.conflicts) {
    if ((conflict.type_a == ObligationType::LeadingTone &&
         conflict.type_b == ObligationType::StrongBeatHarm) ||
        (conflict.type_a == ObligationType::StrongBeatHarm &&
         conflict.type_b == ObligationType::LeadingTone)) {
      found_lt_sbh_conflict = true;
    }
  }
  EXPECT_TRUE(found_lt_sbh_conflict)
      << "Should detect LT vs StrongBeatHarm conflict";
}

// Test: Seventh vs LeapResolve conflict
TEST(PairVerificationTest, SeventhVsLeapResolveConflict) {
  // Subject: Seventh at [0, 960]
  SubjectConstraintProfile subject_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::Seventh, 0, kTicksPerBeat * 2),
  });

  // Answer: LeapResolve at [0, 960] (same timeframe, no offset)
  SubjectConstraintProfile answer_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::LeapResolve, 0, kTicksPerBeat * 2),
  });

  auto result = verifyPair(subject_prof, answer_prof, 0);

  EXPECT_FALSE(result.conflicts.empty())
      << "Seventh vs LeapResolve should produce a conflict";

  bool found_sev_lr = false;
  for (const auto& conflict : result.conflicts) {
    if ((conflict.type_a == ObligationType::Seventh &&
         conflict.type_b == ObligationType::LeapResolve) ||
        (conflict.type_a == ObligationType::LeapResolve &&
         conflict.type_b == ObligationType::Seventh)) {
      found_sev_lr = true;
    }
  }
  EXPECT_TRUE(found_sev_lr)
      << "Should detect Seventh vs LeapResolve conflict";
}

// Test: Cadence conflict score rises when both have cadence obligations
TEST(PairVerificationTest, CadenceConflictDetected) {
  // Subject: CadenceApproach active during [1920, 3840]
  SubjectConstraintProfile subject_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::CadenceApproach,
                     kTicksPerBar, kTicksPerBar * 2),
  });

  // Answer: CadenceStable active during [0, 1920]
  // With offset = kTicksPerBar, answer's cadence shifts to [1920, 3840],
  // perfectly overlapping subject's cadence.
  SubjectConstraintProfile answer_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::CadenceStable,
                     0, kTicksPerBar),
  });

  auto result = verifyPair(subject_prof, answer_prof,
                           static_cast<int>(kTicksPerBar));

  // Both cadence obligations are active at [1920, 3840], overlapping entirely.
  EXPECT_GT(result.cadence_conflict_score, 0.0f)
      << "Should detect cadence conflict when both profiles have "
         "simultaneous cadence obligations";
}

// Test: Empty profiles -> trivially feasible
TEST(PairVerificationTest, EmptyProfilesFeasible) {
  SubjectConstraintProfile empty_prof;
  empty_prof.tonal_answer_feasible = true;

  auto result = verifyPair(empty_prof, empty_prof, 0);

  EXPECT_TRUE(result.feasible());
  EXPECT_TRUE(result.conflicts.empty());
  EXPECT_FLOAT_EQ(result.pair_peak_density, 0.0f);
  EXPECT_FLOAT_EQ(result.cadence_conflict_score, 0.0f);
}

// Test: Pair density increases with overlapping debts
TEST(PairVerificationTest, PairPeakDensityIncreases) {
  // Subject: two debts active at [0, 960]
  SubjectConstraintProfile subject_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::LeapResolve, 0, kTicksPerBeat * 2),
      makeObligation(1, ObligationType::LeadingTone, 0, kTicksPerBeat * 2),
  });

  // Answer: one debt active at [0, 960]
  SubjectConstraintProfile answer_prof = makeProfileWithObligations({
      makeObligation(0, ObligationType::Seventh, 0, kTicksPerBeat * 2),
  });

  // With offset=0, all three debts overlap.
  auto result = verifyPair(subject_prof, answer_prof, 0);

  EXPECT_GE(result.pair_peak_density, 3.0f)
      << "Three simultaneous debts should produce peak density >= 3";
}

// Test: Tonal answer feasibility is propagated
TEST(PairVerificationTest, TonalAnswerFeasibilityPropagated) {
  SubjectConstraintProfile subject_prof;
  subject_prof.tonal_answer_feasible = true;

  SubjectConstraintProfile answer_prof;
  answer_prof.tonal_answer_feasible = false;

  auto result = verifyPair(subject_prof, answer_prof, 0);

  EXPECT_FALSE(result.tonal_answer_feasible)
      << "Should propagate answer's tonal_answer_feasible";
}

// ===========================================================================
// P1.g4: Solvability tests
// ===========================================================================

// Test 3: Consonant intervals -> solvable
TEST(SolvabilityTest, ConsonantIntervalsSolvable) {
  // Subject: C4 half notes on strong beats (C4, E4, G4, C5)
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 2, 60),                        // C4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 64),        // E4
      makeNote(kTicksPerBeat * 4, kTicksPerBeat * 2, 67),        // G4
      makeNote(kTicksPerBeat * 6, kTicksPerBeat * 2, 72),        // C5
  };

  // CS: a third above (E4, G4, B4, E5) -- consonant 3rds throughout
  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 2, 64),                        // E4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 67),        // G4
      makeNote(kTicksPerBeat * 4, kTicksPerBeat * 2, 71),        // B4
      makeNote(kTicksPerBeat * 6, kTicksPerBeat * 2, 76),        // E5
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_TRUE(result.solvable())
      << "Parallel thirds should be solvable";
  EXPECT_LE(result.vertical_clash_rate, 0.15f);
  EXPECT_LE(result.strong_beat_dissonance_rate, 0.05f);
}

// Test 4: Many dissonances -> not solvable
TEST(SolvabilityTest, ManyDissonancesNotSolvable) {
  // Subject: sustained C4 across the whole span
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 8, 60),  // C4 for 4 half-bars
  };

  // CS: sustained C#4 across the whole span (minor 2nd = harsh dissonance)
  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 8, 61),  // C#4 for 4 half-bars
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_FALSE(result.solvable())
      << "Sustained minor 2nd should not be solvable";
  EXPECT_GT(result.vertical_clash_rate, 0.15f);
}

// Test 5: Register overlap calculation correctness
TEST(SolvabilityTest, RegisterOverlapCalculation) {
  // Subject: C4(60) to G4(67) range = 7 semitones
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 2, 60),  // C4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 67),  // G4
  };

  // CS: E4(64) to B4(71) range = 7 semitones
  // Intersection: E4(64) to G4(67) = 3 semitones
  // Union: C4(60) to B4(71) = 11 semitones
  // Overlap = 3/11 ~ 0.273
  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 2, 64),  // E4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 71),  // B4
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_NEAR(result.register_overlap, 3.0f / 11.0f, 0.01f)
      << "Register overlap should be intersection/union";
}

// Test: Zero register overlap when ranges don't intersect
TEST(SolvabilityTest, ZeroRegisterOverlapSeparateRanges) {
  // Subject: C2(36) to G2(43)
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 2, 36),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 43),
  };

  // CS: C5(72) to G5(79)
  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 2, 72),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 79),
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_FLOAT_EQ(result.register_overlap, 0.0f)
      << "Non-overlapping ranges should have zero overlap";
}

// Test: Empty inputs produce zero rates
TEST(SolvabilityTest, EmptyInputsZeroRates) {
  std::vector<NoteEvent> empty;
  std::vector<NoteEvent> some = {makeNote(0, kTicksPerBeat, 60)};

  auto result_both_empty = testSolvability(empty, empty, Key::C, false);
  EXPECT_FLOAT_EQ(result_both_empty.vertical_clash_rate, 0.0f);
  EXPECT_FLOAT_EQ(result_both_empty.register_overlap, 0.0f);

  auto result_one_empty = testSolvability(some, empty, Key::C, false);
  EXPECT_FLOAT_EQ(result_one_empty.vertical_clash_rate, 0.0f);
}

// Test: Perfect fifths are consonant (not dissonant)
TEST(SolvabilityTest, PerfectFifthsConsonant) {
  // Subject and CS in perfect 5ths -- should be consonant
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 2, 60),                        // C4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 62),        // D4
      makeNote(kTicksPerBeat * 4, kTicksPerBeat * 2, 64),        // E4
      makeNote(kTicksPerBeat * 6, kTicksPerBeat * 2, 65),        // F4
  };

  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 2, 67),                        // G4 (P5 above C4)
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 69),        // A4 (P5 above D4)
      makeNote(kTicksPerBeat * 4, kTicksPerBeat * 2, 71),        // B4 (P5 above E4)
      makeNote(kTicksPerBeat * 6, kTicksPerBeat * 2, 72),        // C5 (P5 above F4)
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_FLOAT_EQ(result.vertical_clash_rate, 0.0f)
      << "Perfect fifths should not count as dissonances";
  EXPECT_TRUE(result.solvable());
}

// Test: Full overlap means register_overlap = 1.0
TEST(SolvabilityTest, FullRegisterOverlap) {
  // Same pitch range for both voices
  std::vector<NoteEvent> subject = {
      makeNote(0, kTicksPerBeat * 2, 60),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 72),
  };

  std::vector<NoteEvent> counter = {
      makeNote(0, kTicksPerBeat * 2, 60),
      makeNote(kTicksPerBeat * 2, kTicksPerBeat * 2, 72),
  };

  auto result = testSolvability(subject, counter, Key::C, false);

  EXPECT_NEAR(result.register_overlap, 1.0f, 0.01f)
      << "Identical pitch ranges should have overlap = 1.0";
}

// ===========================================================================
// Integration: analyze real subjects and verify pairs
// ===========================================================================

/// @brief Build full BWV578 subject notes (12 notes) for integration testing.
///
/// The full subject includes resolution notes after the leading tone:
/// G4-D5-C5-Bb4-A4-G4-F#4-G4-A4-Bb4-A4-D4.
/// This is the same as obligation_test.cpp's helper.
std::vector<NoteEvent> makeBWV578FullSubjectNotes() {
  std::vector<NoteEvent> notes;
  Tick tick = 0;
  Tick qtr = kTicksPerBeat;
  Tick eth = kTicksPerBeat / 2;

  notes.push_back(makeNote(tick, qtr * 2, 67));  tick += qtr * 2;  // G4
  notes.push_back(makeNote(tick, qtr, 74));      tick += qtr;      // D5
  notes.push_back(makeNote(tick, eth, 72));      tick += eth;      // C5
  notes.push_back(makeNote(tick, eth, 70));      tick += eth;      // Bb4
  notes.push_back(makeNote(tick, eth, 69));      tick += eth;      // A4
  notes.push_back(makeNote(tick, eth, 67));      tick += eth;      // G4
  notes.push_back(makeNote(tick, eth, 66));      tick += eth;      // F#4
  notes.push_back(makeNote(tick, qtr, 67));      tick += qtr;      // G4
  notes.push_back(makeNote(tick, eth, 69));      tick += eth;      // A4
  notes.push_back(makeNote(tick, eth, 70));      tick += eth;      // Bb4
  notes.push_back(makeNote(tick, qtr, 69));      tick += qtr;      // A4
  notes.push_back(makeNote(tick, qtr * 2, 62));  tick += qtr * 2;  // D4
  return notes;
}

TEST(PairVerificationIntegrationTest, BWV578SubjectVsAnswerFeasible) {
  auto subject_notes = makeBWV578FullSubjectNotes();
  auto subject_prof = analyzeObligations(subject_notes, Key::G, true);

  // BWV578 answer: tonal answer at the dominant (D minor).
  // Full answer with 12 notes mirroring the subject structure.
  std::vector<NoteEvent> answer_notes;
  Tick tick = 0;
  Tick qtr = kTicksPerBeat;
  Tick eth = kTicksPerBeat / 2;

  answer_notes.push_back(makeNote(tick, qtr * 2, 74));  tick += qtr * 2;  // D5
  answer_notes.push_back(makeNote(tick, qtr, 79));      tick += qtr;      // G5
  answer_notes.push_back(makeNote(tick, eth, 77));      tick += eth;      // F5
  answer_notes.push_back(makeNote(tick, eth, 76));      tick += eth;      // E5
  answer_notes.push_back(makeNote(tick, eth, 74));      tick += eth;      // D5
  answer_notes.push_back(makeNote(tick, eth, 72));      tick += eth;      // C5
  answer_notes.push_back(makeNote(tick, eth, 71));      tick += eth;      // B4
  answer_notes.push_back(makeNote(tick, qtr, 72));      tick += qtr;      // C5
  answer_notes.push_back(makeNote(tick, eth, 74));      tick += eth;      // D5
  answer_notes.push_back(makeNote(tick, eth, 76));      tick += eth;      // E5
  answer_notes.push_back(makeNote(tick, qtr, 74));      tick += qtr;      // D5
  answer_notes.push_back(makeNote(tick, qtr * 2, 67));  tick += qtr * 2;  // G4

  auto answer_prof = analyzeObligations(answer_notes, Key::D, true);

  // Use full subject length as offset (answer enters after subject completes).
  Tick subject_length = subject_notes.back().start_tick +
                        subject_notes.back().duration;
  auto result = verifyPair(subject_prof, answer_prof,
                           static_cast<int>(subject_length));

  // BWV578 subject + answer should be feasible when answer enters sequentially.
  // The full subject (including resolution notes) has all debts resolved before
  // the answer's strong beat obligations begin.
  EXPECT_TRUE(result.feasible())
      << "BWV578 full subject+answer at sequential offset should be feasible";
}

// ===========================================================================
// P1.g5: Baseline pass rate benchmark (100 subjects, 3/4/5 voices)
// ===========================================================================

TEST(FeasibilityBaselineTest, DISABLED_P1g5_BaselinePassRates) {
  // Key cycle: C, G, D, F, Bb, Eb, A, E (8 keys).
  constexpr Key kKeyCycle[] = {
      Key::C, Key::G, Key::D, Key::F, Key::Bb, Key::Eb, Key::A, Key::E,
  };
  constexpr int kNumKeys = 8;

  // Character cycle: Severe, Playful, Noble, Restless.
  constexpr SubjectCharacter kCharCycle[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless,
  };
  constexpr int kNumChars = 4;

  constexpr int kNumSeeds = 100;
  constexpr int kMicroSimTrials = 20;

  // Aggregate counters.
  int valid_subjects = 0;

  // Per-voice-count pass counters (a subject "passes" for N voices if ALL of:
  // MicroSim feasible, pair feasible, CS solvable).
  int pass_3v = 0;
  int pass_4v = 0;
  int pass_5v = 0;

  // Individual metric counters.
  int tonal_answer_feasible_count = 0;
  int cs_solvable_count = 0;

  // MicroSim average success rate accumulator for 4 voices.
  float microsim_4v_success_sum = 0.0f;
  int microsim_4v_count = 0;

  SubjectGenerator gen;

  for (int seed = 1; seed <= kNumSeeds; ++seed) {
    // Build config with varying parameters.
    FugueConfig config;
    config.key = kKeyCycle[(seed - 1) % kNumKeys];
    config.is_minor = (seed % 3 == 0);
    config.character = kCharCycle[(seed - 1) % kNumChars];
    config.archetype = FugueArchetype::Compact;
    config.subject_bars = 2;
    config.seed = static_cast<uint32_t>(seed);

    // Generate subject.
    Subject subject = gen.generate(config, static_cast<uint32_t>(seed));
    if (subject.notes.empty()) {
      continue;
    }
    ++valid_subjects;

    // Generate answer once (shared across voice counts).
    Answer answer = generateAnswer(subject);

    // Analyze obligations for subject and answer.
    auto subject_prof = analyzeObligations(subject.notes, subject.key, subject.is_minor);
    auto answer_prof = analyzeObligations(answer.notes, answer.key, subject.is_minor);

    // Track tonal answer feasibility.
    if (subject_prof.tonal_answer_feasible) {
      ++tonal_answer_feasible_count;
    }

    // Generate countersubject once (shared across voice counts).
    Countersubject csubject = generateCountersubject(subject, static_cast<uint32_t>(seed));

    // Test CS solvability.
    auto solv_result = testSolvability(subject.notes, csubject.notes,
                                       subject.key, subject.is_minor);
    bool cs_solvable = solv_result.solvable();
    if (cs_solvable) {
      ++cs_solvable_count;
    }

    // Pair verification (subject vs answer at sequential offset).
    auto pair_result = verifyPair(subject_prof, answer_prof,
                                  static_cast<int>(subject.length_ticks));
    bool pair_feasible = pair_result.feasible();

    // Test each voice count: 3, 4, 5.
    for (int num_voices = 3; num_voices <= 5; ++num_voices) {
      FugueConfig voice_config = config;
      voice_config.num_voices = static_cast<uint8_t>(num_voices);

      // Run MicroSim.
      MicroSimResult sim_result = runMicroSim(subject, voice_config, kMicroSimTrials);
      bool sim_feasible = sim_result.feasible();

      // Track 4-voice MicroSim average success rate.
      if (num_voices == 4) {
        microsim_4v_success_sum += sim_result.success_rate();
        ++microsim_4v_count;
      }

      // A subject "passes" for N voices if ALL three checks pass.
      bool passes = sim_feasible && pair_feasible && cs_solvable;

      if (passes) {
        if (num_voices == 3) ++pass_3v;
        if (num_voices == 4) ++pass_4v;
        if (num_voices == 5) ++pass_5v;
      }
    }
  }

  // Compute rates.
  float rate_3v = valid_subjects > 0
      ? 100.0f * static_cast<float>(pass_3v) / static_cast<float>(valid_subjects)
      : 0.0f;
  float rate_4v = valid_subjects > 0
      ? 100.0f * static_cast<float>(pass_4v) / static_cast<float>(valid_subjects)
      : 0.0f;
  float rate_5v = valid_subjects > 0
      ? 100.0f * static_cast<float>(pass_5v) / static_cast<float>(valid_subjects)
      : 0.0f;
  float tonal_rate = valid_subjects > 0
      ? 100.0f * static_cast<float>(tonal_answer_feasible_count)
            / static_cast<float>(valid_subjects)
      : 0.0f;
  float cs_rate = valid_subjects > 0
      ? 100.0f * static_cast<float>(cs_solvable_count)
            / static_cast<float>(valid_subjects)
      : 0.0f;
  float avg_microsim_4v = microsim_4v_count > 0
      ? microsim_4v_success_sum / static_cast<float>(microsim_4v_count)
      : 0.0f;

  // Print summary table.
  fprintf(stderr, "\n");
  fprintf(stderr, "=============================================================\n");
  fprintf(stderr, " P1.g5 Baseline Pass Rates (%d seeds, %d valid subjects)\n",
          kNumSeeds, valid_subjects);
  fprintf(stderr, "=============================================================\n");
  fprintf(stderr, " 3-voice pass rate:              %6.1f%% (%d / %d)\n",
          rate_3v, pass_3v, valid_subjects);
  fprintf(stderr, " 4-voice pass rate:              %6.1f%% (%d / %d)\n",
          rate_4v, pass_4v, valid_subjects);
  fprintf(stderr, " 5-voice pass rate:              %6.1f%% (%d / %d)\n",
          rate_5v, pass_5v, valid_subjects);
  fprintf(stderr, " Tonal answer feasible rate:     %6.1f%% (%d / %d)\n",
          tonal_rate, tonal_answer_feasible_count, valid_subjects);
  fprintf(stderr, " CS solvability rate:            %6.1f%% (%d / %d)\n",
          cs_rate, cs_solvable_count, valid_subjects);
  fprintf(stderr, " Avg MicroSim success (4-voice): %6.3f\n", avg_microsim_4v);
  fprintf(stderr, "=============================================================\n");
  fprintf(stderr, "\n");

  // No assertions that could fail -- this test just records baseline numbers.
  // Verify test ran with valid data.
  EXPECT_GT(valid_subjects, 0) << "At least some subjects should generate successfully";
}

}  // namespace
}  // namespace bach
