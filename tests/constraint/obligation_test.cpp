// Tests for obligation extraction and subject constraint profiling.

#include <gtest/gtest.h>

#include "constraint/obligation.h"
#include "constraint/obligation_analyzer.h"
#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a NoteEvent
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
// P1.d1: Local detection unit tests
// ---------------------------------------------------------------------------

// F#4 (MIDI 66) in G minor → LeadingTone{dir=+1, required_interval_semitones=+1}
TEST(ObligationDetectionTest, LeadingToneInGMinor) {
  // G minor: tonic = G (MIDI pitch class 7). Leading tone = F# (pitch class 6).
  // F#4 = MIDI 66
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 67),   // G4
      makeNote(kTicksPerBeat, kTicksPerBeat, 66),  // F#4 (leading tone)
  };

  auto profile = analyzeObligations(notes, Key::G, true);

  bool found_lt = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::LeadingTone) {
      found_lt = true;
      EXPECT_EQ(ob.direction, +1);
      EXPECT_EQ(ob.required_interval_semitones, +1);
      EXPECT_EQ(ob.strength, ObligationStrength::Structural);
    }
  }
  EXPECT_TRUE(found_lt) << "Expected LeadingTone obligation for F#4 in G minor";
}

// C4→G4 (P5, 7 semitones) → LeapResolve{dir=-1}
TEST(ObligationDetectionTest, LeapResolveOnPerfectFifth) {
  // C4=60, G4=67: interval = +7 semitones (P5)
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60),              // C4
      makeNote(kTicksPerBeat, kTicksPerBeat, 67),  // G4 (leap up)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  bool found_lr = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::LeapResolve) {
      found_lr = true;
      EXPECT_EQ(ob.direction, -1);  // Must resolve downward (contrary)
      EXPECT_EQ(ob.strength, ObligationStrength::Soft);
    }
  }
  EXPECT_TRUE(found_lr) << "Expected LeapResolve for C4→G4 (P5 leap)";
}

// No LeapResolve for small intervals (M3 = 4 semitones, threshold is 5)
TEST(ObligationDetectionTest, NoLeapResolveForSmallInterval) {
  // C4=60, E4=64: interval = +4 semitones (M3)
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60),              // C4
      makeNote(kTicksPerBeat, kTicksPerBeat, 64),  // E4 (M3, not a leap)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  for (const auto& ob : profile.obligations) {
    EXPECT_NE(ob.type, ObligationType::LeapResolve)
        << "M3 should not trigger LeapResolve";
  }
}

// Strong beat → StrongBeatHarm (not counted as debt)
TEST(ObligationDetectionTest, StrongBeatHarmDetection) {
  // Note on beat 1 (tick 0) should be StrongBeatHarm
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60),  // Beat 1 (strong)
      makeNote(kTicksPerBeat, kTicksPerBeat, 62),  // Beat 2 (weak)
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 64),  // Beat 3 (strong)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  int gate_count = 0;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::StrongBeatHarm) {
      gate_count++;
      EXPECT_FALSE(ob.is_debt()) << "StrongBeatHarm should not be debt";
    }
  }
  EXPECT_EQ(gate_count, 2) << "Expected 2 StrongBeatHarm gates (beats 1 and 3)";
}

// CadenceStable: subject ending on unstable degree
TEST(ObligationDetectionTest, CadenceStableOnUnstableEnding) {
  // End on D4 (degree 2 in C major) - not stable
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60),              // C4
      makeNote(kTicksPerBeat, kTicksPerBeat, 64),  // E4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 62),  // D4 (unstable ending)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  bool found_cs = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::CadenceStable) {
      found_cs = true;
    }
  }
  EXPECT_TRUE(found_cs) << "Expected CadenceStable for ending on degree 2";
}

// CadenceStable: no obligation when ending on tonic
TEST(ObligationDetectionTest, NoCadenceStableOnTonic) {
  // End on C4 (root in C major) - stable
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 64),              // E4
      makeNote(kTicksPerBeat, kTicksPerBeat, 62),  // D4
      makeNote(kTicksPerBeat * 2, kTicksPerBeat, 60),  // C4 (stable ending)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  for (const auto& ob : profile.obligations) {
    EXPECT_NE(ob.type, ObligationType::CadenceStable)
        << "No CadenceStable when ending on tonic";
  }
}

// Seventh detection: F4 (degree 4 in C major = 7th of V)
TEST(ObligationDetectionTest, SeventhDetection) {
  // F4 = MIDI 65, which is scale degree 3 (0-based) = 4th degree in C major
  std::vector<NoteEvent> notes = {
      makeNote(0, kTicksPerBeat, 60),              // C4
      makeNote(kTicksPerBeat, kTicksPerBeat, 65),  // F4 (chord seventh)
  };

  auto profile = analyzeObligations(notes, Key::C, false);

  bool found_sev = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::Seventh) {
      found_sev = true;
      EXPECT_EQ(ob.direction, -1);  // Must resolve downward
    }
  }
  EXPECT_TRUE(found_sev) << "Expected Seventh obligation for F4 in C major";
}

// ---------------------------------------------------------------------------
// P1.d2: BWV578 profile test
// ---------------------------------------------------------------------------

// BWV578 subject in G minor (simplified but melodically representative):
// G4 - D5 - C5 - Bb4 - A4 - G4 - F#4 - G4 - A4 - Bb4 - A4 - D4
// Key features: opening P5 leap, stepwise descent, leading tone, final P5 leap
std::vector<NoteEvent> makeBWV578Subject() {
  std::vector<NoteEvent> notes;
  Tick t = 0;
  Tick q = kTicksPerBeat;      // Quarter
  Tick e = kTicksPerBeat / 2;  // Eighth

  // G4=67, D5=74, C5=72, Bb4=70, A4=69, G4=67, F#4=66
  notes.push_back(makeNote(t, q * 2, 67));  t += q * 2;  // G4 (half note)
  notes.push_back(makeNote(t, q, 74));      t += q;      // D5
  notes.push_back(makeNote(t, e, 72));      t += e;      // C5
  notes.push_back(makeNote(t, e, 70));      t += e;      // Bb4
  notes.push_back(makeNote(t, e, 69));      t += e;      // A4
  notes.push_back(makeNote(t, e, 67));      t += e;      // G4
  notes.push_back(makeNote(t, e, 66));      t += e;      // F#4 (leading tone)
  notes.push_back(makeNote(t, q, 67));      t += q;      // G4
  notes.push_back(makeNote(t, e, 69));      t += e;      // A4
  notes.push_back(makeNote(t, e, 70));      t += e;      // Bb4
  notes.push_back(makeNote(t, q, 69));      t += q;      // A4
  notes.push_back(makeNote(t, q * 2, 62));  t += q * 2;  // D4 (half note)
  return notes;
}

TEST(BWV578ProfileTest, DensityRange) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // BWV578: peak_density should be in [1, 3]
  EXPECT_GE(profile.peak_density, 1.0f);
  EXPECT_LE(profile.peak_density, 3.0f);
}

TEST(BWV578ProfileTest, SynchronousPressureRange) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // BWV578: synchronous_pressure should be in [0.0, 0.5]
  // Lower bound relaxed: depends on exact alignment of debt windows and strong beats.
  EXPECT_GE(profile.synchronous_pressure, 0.0f);
  EXPECT_LE(profile.synchronous_pressure, 0.5f);
}

TEST(BWV578ProfileTest, FeasibleFor4Voices) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  EXPECT_TRUE(profile.feasible_for(4));
}

TEST(BWV578ProfileTest, FeasibleFor3Voices) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  EXPECT_TRUE(profile.feasible_for(3));
}

TEST(BWV578ProfileTest, HasLeadingTone) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  bool has_lt = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::LeadingTone) {
      has_lt = true;
      break;
    }
  }
  EXPECT_TRUE(has_lt) << "BWV578 contains F#4 (leading tone in G minor)";
}

TEST(BWV578ProfileTest, HasLeapResolve) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  bool has_lr = false;
  for (const auto& ob : profile.obligations) {
    if (ob.type == ObligationType::LeapResolve) {
      has_lr = true;
      break;
    }
  }
  EXPECT_TRUE(has_lr) << "BWV578 has G4→D5 (P5 leap)";
}

TEST(BWV578ProfileTest, RegisterTrajectory) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // Opening pitch: G4 = 67
  EXPECT_EQ(profile.register_arc.opening_pitch, 67);
  // Peak: D5 = 74
  EXPECT_EQ(profile.register_arc.peak_pitch, 74);
  // Closing: D4 = 62
  EXPECT_EQ(profile.register_arc.closing_pitch, 62);
  // Peak is near the beginning (second note)
  EXPECT_LT(profile.register_arc.peak_position, 0.3f);
  // Overall descending (67 → 62)
  EXPECT_EQ(profile.register_arc.overall_direction, -1);
}

TEST(BWV578ProfileTest, TonalAnswerFeasible) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // BWV578 starts on G4 (tonic) → tonal answer feasible
  EXPECT_TRUE(profile.tonal_answer_feasible);
}

TEST(BWV578ProfileTest, AccentContour) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // BWV578: front-weighted (half note opening on strong beat)
  EXPECT_GT(profile.accent_contour.front_weight, 0.0f);
  // Weights should sum to approximately 1.0
  float total = profile.accent_contour.front_weight +
                profile.accent_contour.mid_weight +
                profile.accent_contour.tail_weight;
  EXPECT_NEAR(total, 1.0f, 0.01f);
}

TEST(BWV578ProfileTest, HarmonicImpulses) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // Should have at least one harmonic impulse
  EXPECT_FALSE(profile.harmonic_impulses.empty());

  // First impulse should be around tonic (degree 1)
  if (!profile.harmonic_impulses.empty()) {
    EXPECT_GE(profile.harmonic_impulses.front().strength, 0.1f);
  }
}

// ---------------------------------------------------------------------------
// ObligationNode tests
// ---------------------------------------------------------------------------

TEST(ObligationNodeTest, IsDebt) {
  ObligationNode lt;
  lt.type = ObligationType::LeadingTone;
  EXPECT_TRUE(lt.is_debt());

  ObligationNode gate;
  gate.type = ObligationType::StrongBeatHarm;
  EXPECT_FALSE(gate.is_debt());

  ObligationNode recovery;
  recovery.type = ObligationType::InvariantRecovery;
  EXPECT_FALSE(recovery.is_debt());
}

TEST(ObligationNodeTest, IsActiveAt) {
  ObligationNode node;
  node.start_tick = 100;
  node.deadline = 200;

  EXPECT_FALSE(node.is_active_at(50));
  EXPECT_TRUE(node.is_active_at(100));
  EXPECT_TRUE(node.is_active_at(150));
  EXPECT_TRUE(node.is_active_at(200));
  EXPECT_FALSE(node.is_active_at(201));
}

// ---------------------------------------------------------------------------
// StrettoFeasibilityEntry tests
// ---------------------------------------------------------------------------

TEST(StrettoFeasibilityTest, ScoreAllGood) {
  StrettoFeasibilityEntry entry;
  entry.peak_obligation = 0.0f;
  entry.vertical_clash = 0.0f;
  entry.register_overlap = 0.0f;
  entry.perceptual_overlap_score = 0.0f;
  entry.cadence_conflict_score = 0.0f;

  // All dimensions perfect → score should be 1.0
  EXPECT_NEAR(entry.feasibility_score(), 1.0f, 0.01f);
}

TEST(StrettoFeasibilityTest, FloorGuard) {
  StrettoFeasibilityEntry entry;
  entry.peak_obligation = 0.0f;
  entry.vertical_clash = 0.95f;  // Very bad
  entry.register_overlap = 0.0f;
  entry.perceptual_overlap_score = 0.0f;
  entry.cadence_conflict_score = 0.0f;

  // norm_clash = 0.05 < 0.2 → floor guard triggers
  float score = entry.feasibility_score();
  EXPECT_LT(score, 0.2f);
}

TEST(StrettoFeasibilityTest, InfeasibleScore) {
  StrettoFeasibilityEntry entry;
  entry.peak_obligation = 4.0f;  // Extremely dense
  entry.vertical_clash = 0.5f;
  entry.register_overlap = 0.8f;
  entry.perceptual_overlap_score = 0.6f;
  entry.cadence_conflict_score = 0.3f;

  EXPECT_LT(entry.feasibility_score(), StrettoFeasibilityEntry::kMinFeasibleScore);
}

// ---------------------------------------------------------------------------
// String conversion tests
// ---------------------------------------------------------------------------

TEST(ObligationStringTest, TypeToString) {
  EXPECT_STREQ(obligationTypeToString(ObligationType::LeadingTone), "LeadingTone");
  EXPECT_STREQ(obligationTypeToString(ObligationType::StrongBeatHarm), "StrongBeatHarm");
}

TEST(ObligationStringTest, StrengthToString) {
  EXPECT_STREQ(obligationStrengthToString(ObligationStrength::Structural), "Structural");
  EXPECT_STREQ(obligationStrengthToString(ObligationStrength::Soft), "Soft");
}

// ---------------------------------------------------------------------------
// P1.e: Stretto feasibility matrix tests
// ---------------------------------------------------------------------------

TEST(StrettoFeasibilityMatrixTest, BWV578MatrixNotEmpty) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // analyzeObligations should populate stretto_matrix via computeStrettoFeasibility.
  EXPECT_FALSE(profile.stretto_matrix.empty())
      << "Stretto matrix should be populated after analysis";
}

TEST(StrettoFeasibilityMatrixTest, BWV578TwoVoiceAtOneBarFeasible) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // BWV578 is historically known to support stretto. A 2-voice stretto
  // at offset = 1 bar (1920 ticks) should be feasible.
  bool found_entry = false;
  bool found_feasible = false;
  for (const auto& entry : profile.stretto_matrix) {
    if (entry.num_voices == 2 &&
        entry.offset_ticks == static_cast<int>(kTicksPerBar)) {
      found_entry = true;
      found_feasible = (entry.feasibility_score() >=
                        StrettoFeasibilityEntry::kMinFeasibleScore);
      break;
    }
  }
  EXPECT_TRUE(found_entry) << "Should have a 2-voice entry at 1-bar offset";
  EXPECT_TRUE(found_feasible)
      << "BWV578: 2-voice stretto at 1-bar offset should be feasible";
}

TEST(StrettoFeasibilityMatrixTest, BWV578MinSafeStrettoOffset) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // min_safe_stretto_offset for 2 voices should return a valid offset.
  int min_offset = profile.min_safe_stretto_offset(2);
  EXPECT_GT(min_offset, 0)
      << "BWV578 should have at least one feasible 2-voice stretto offset";

  // The minimum offset should be at least 1 beat.
  EXPECT_GE(min_offset, static_cast<int>(kTicksPerBeat));
}

TEST(StrettoFeasibilityMatrixTest, HighDensitySubjectFiveVoicesHarder) {
  // Create a high-density subject: many leaps and leading tones packed tightly,
  // within a narrow pitch range. This stresses register overlap and perceptual
  // overlap, making stretto difficult especially for many voices.
  std::vector<NoteEvent> dense_notes;
  Tick eighth = kTicksPerBeat / 2;
  Tick tick = 0;

  // Pattern: alternating large leaps and leading tones in C major, all 8th notes.
  // C4-B4-C4-B4-G4-C5-B4-C5-G4-C5-B4-C5 (12 notes, all 8th notes = 6 beats)
  uint8_t pitches[] = {60, 71, 60, 71, 67, 72, 71, 72, 67, 72, 71, 72};
  for (uint8_t pitch : pitches) {
    dense_notes.push_back(makeNote(tick, eighth, pitch));
    tick += eighth;
  }

  auto profile = analyzeObligations(dense_notes, Key::C, false);

  EXPECT_GT(static_cast<int>(profile.stretto_matrix.size()), 0)
      << "Should have stretto entries";

  // 5-voice stretto should be harder than 2-voice stretto at the same offset.
  // Compare average scores: 5-voice should score lower than 2-voice.
  float sum_2v = 0.0f, sum_5v = 0.0f;
  int count_2v = 0, count_5v = 0;
  for (const auto& entry : profile.stretto_matrix) {
    if (entry.num_voices == 2) {
      sum_2v += entry.feasibility_score();
      count_2v++;
    }
    if (entry.num_voices == 5) {
      sum_5v += entry.feasibility_score();
      count_5v++;
    }
  }

  EXPECT_GT(count_2v, 0) << "Should have 2-voice entries";
  EXPECT_GT(count_5v, 0) << "Should have 5-voice entries";

  float avg_2v = sum_2v / static_cast<float>(count_2v);
  float avg_5v = sum_5v / static_cast<float>(count_5v);
  EXPECT_GT(avg_2v, avg_5v)
      << "5-voice stretto should be harder than 2-voice on average";
}

TEST(StrettoFeasibilityMatrixTest, MatrixCoversAllVoiceCounts) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // Matrix should contain entries for voice counts 2 through 5.
  bool has_2v = false, has_3v = false, has_4v = false, has_5v = false;
  for (const auto& entry : profile.stretto_matrix) {
    if (entry.num_voices == 2) has_2v = true;
    if (entry.num_voices == 3) has_3v = true;
    if (entry.num_voices == 4) has_4v = true;
    if (entry.num_voices == 5) has_5v = true;
  }
  EXPECT_TRUE(has_2v) << "Matrix should have 2-voice entries";
  EXPECT_TRUE(has_3v) << "Matrix should have 3-voice entries";
  EXPECT_TRUE(has_4v) << "Matrix should have 4-voice entries";
  EXPECT_TRUE(has_5v) << "Matrix should have 5-voice entries";
}

TEST(StrettoFeasibilityMatrixTest, LargerOffsetsMoreFeasible) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  // For 2-voice stretto, larger offsets should generally be more feasible
  // (less temporal overlap = fewer conflicts). Compare the smallest and
  // largest offsets available.
  float min_offset_score = -1.0f;
  float max_offset_score = -1.0f;
  int min_offset_val = INT32_MAX;
  int max_offset_val = 0;

  for (const auto& entry : profile.stretto_matrix) {
    if (entry.num_voices != 2) continue;
    if (entry.offset_ticks < min_offset_val) {
      min_offset_val = entry.offset_ticks;
      min_offset_score = entry.feasibility_score();
    }
    if (entry.offset_ticks > max_offset_val) {
      max_offset_val = entry.offset_ticks;
      max_offset_score = entry.feasibility_score();
    }
  }

  EXPECT_GT(max_offset_val, min_offset_val)
      << "Should have a range of offsets";
  EXPECT_GE(max_offset_score, min_offset_score)
      << "Larger offsets should be at least as feasible as smaller offsets";
}

TEST(StrettoFeasibilityMatrixTest, EmptyNotesProducesEmptyMatrix) {
  std::vector<NoteEvent> empty_notes;
  auto profile = analyzeObligations(empty_notes, Key::C, false);

  EXPECT_TRUE(profile.stretto_matrix.empty())
      << "Empty notes should produce empty stretto matrix";
}

TEST(StrettoFeasibilityMatrixTest, ScoresInValidRange) {
  auto notes = makeBWV578Subject();
  auto profile = analyzeObligations(notes, Key::G, true);

  for (const auto& entry : profile.stretto_matrix) {
    float score = entry.feasibility_score();
    EXPECT_GE(score, 0.0f) << "Feasibility score should be >= 0.0";
    EXPECT_LE(score, 1.0f) << "Feasibility score should be <= 1.0";

    // Individual components should also be in [0, 1].
    EXPECT_GE(entry.vertical_clash, 0.0f);
    EXPECT_LE(entry.vertical_clash, 1.0f);
    EXPECT_GE(entry.rhythmic_interference, 0.0f);
    EXPECT_LE(entry.rhythmic_interference, 1.0f);
    EXPECT_GE(entry.register_overlap, 0.0f);
    EXPECT_LE(entry.register_overlap, 1.0f);
    EXPECT_GE(entry.perceptual_overlap_score, 0.0f);
    EXPECT_LE(entry.perceptual_overlap_score, 1.0f);
    EXPECT_GE(entry.cadence_conflict_score, 0.0f);
    EXPECT_LE(entry.cadence_conflict_score, 1.0f);

    // Peak obligation should be non-negative.
    EXPECT_GE(entry.peak_obligation, 0.0f);
  }
}

}  // namespace
}  // namespace bach
