// Tests for fugue structure analysis module.

#include "analysis/fugue_analyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/basic_types.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

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

/// @brief Create a subject: C4-D4-E4-F4 (stepwise ascending, 4 quarter notes).
std::vector<NoteEvent> makeSubject() {
  return {
      qn(0, 60, 0),              // C4
      qn(kTicksPerBeat, 62, 0),  // D4
      qn(kTicksPerBeat * 2, 64, 0),  // E4
      qn(kTicksPerBeat * 3, 65, 0),  // F4
  };
}

/// @brief Create a 3-voice fugue with proper exposition.
/// Voice 0: subject at tick 0.
/// Voice 1: answer at P5 (subject + 7 semitones) starting at bar 1.
/// Voice 2: subject (octave lower) at bar 2.
/// Then some post-exposition material using subject fragments.
std::vector<NoteEvent> makeProperFugue() {
  std::vector<NoteEvent> notes;

  // Voice 0: subject C4-D4-E4-F4 at bar 0.
  notes.push_back(qn(0, 60, 0));
  notes.push_back(qn(kTicksPerBeat, 62, 0));
  notes.push_back(qn(kTicksPerBeat * 2, 64, 0));
  notes.push_back(qn(kTicksPerBeat * 3, 65, 0));

  // Voice 0: free counterpoint during voice 1 entry (bar 1).
  notes.push_back(qn(kTicksPerBar, 72, 0));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat, 71, 0));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 2, 69, 0));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 3, 67, 0));

  // Voice 1: answer at P5 (G4-A4-B4-C5) at bar 1.
  notes.push_back(qn(kTicksPerBar, 67, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat, 69, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 2, 71, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 3, 72, 1));

  // Voice 2: subject at octave lower (C3-D3-E3-F3) at bar 2.
  notes.push_back(qn(kTicksPerBar * 2, 48, 2));
  notes.push_back(qn(kTicksPerBar * 2 + kTicksPerBeat, 50, 2));
  notes.push_back(qn(kTicksPerBar * 2 + kTicksPerBeat * 2, 52, 2));
  notes.push_back(qn(kTicksPerBar * 2 + kTicksPerBeat * 3, 53, 2));

  // Post-exposition (bar 3-5): episode material with subject fragments.
  // Voice 0: uses subject interval pattern transposed.
  notes.push_back(qn(kTicksPerBar * 3, 65, 0));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat, 67, 0));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 2, 69, 0));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 3, 70, 0));

  // Voice 1: different tonal center (Bb area).
  notes.push_back(qn(kTicksPerBar * 3, 58, 1));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat, 60, 1));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 2, 62, 1));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 3, 63, 1));

  // Voice 2: bass line.
  notes.push_back(qn(kTicksPerBar * 3, 46, 2));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat, 48, 2));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 2, 50, 2));
  notes.push_back(qn(kTicksPerBar * 3 + kTicksPerBeat * 3, 51, 2));

  // Bar 4-5: more development, different tonal area.
  notes.push_back(qn(kTicksPerBar * 4, 67, 0));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat, 69, 0));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 2, 71, 0));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 3, 72, 0));

  notes.push_back(qn(kTicksPerBar * 4, 55, 1));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat, 57, 1));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 2, 59, 1));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 3, 60, 1));

  notes.push_back(qn(kTicksPerBar * 4, 43, 2));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat, 45, 2));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 2, 47, 2));
  notes.push_back(qn(kTicksPerBar * 4 + kTicksPerBeat * 3, 48, 2));

  // Bar 5: return to tonic area.
  notes.push_back(qn(kTicksPerBar * 5, 60, 0));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat, 62, 0));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 2, 64, 0));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 3, 65, 0));

  notes.push_back(qn(kTicksPerBar * 5, 48, 1));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat, 50, 1));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 2, 52, 1));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 3, 53, 1));

  notes.push_back(qn(kTicksPerBar * 5, 36, 2));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat, 38, 2));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 2, 40, 2));
  notes.push_back(qn(kTicksPerBar * 5 + kTicksPerBeat * 3, 41, 2));

  return notes;
}

// ---------------------------------------------------------------------------
// expositionCompletenessScore
// ---------------------------------------------------------------------------

TEST(ExpositionCompletenessTest, AllVoicesEnterWithSubject) {
  auto fugue = makeProperFugue();
  auto subject = makeSubject();
  float score = expositionCompletenessScore(fugue, 3, subject);
  // All 3 voices have the subject interval pattern.
  EXPECT_GT(score, 0.6f);
}

TEST(ExpositionCompletenessTest, SingleVoiceOnlyPartial) {
  // Only voice 0 has the subject.
  std::vector<NoteEvent> notes = {
      qn(0, 60, 0),
      qn(kTicksPerBeat, 62, 0),
      qn(kTicksPerBeat * 2, 64, 0),
      qn(kTicksPerBeat * 3, 65, 0),
      // Voice 1: random notes, not matching subject pattern.
      qn(kTicksPerBar, 72, 1),
      qn(kTicksPerBar + kTicksPerBeat, 68, 1),
      qn(kTicksPerBar + kTicksPerBeat * 2, 75, 1),
      qn(kTicksPerBar + kTicksPerBeat * 3, 60, 1),
  };
  auto subject = makeSubject();
  float score = expositionCompletenessScore(notes, 2, subject);
  // Only 1 of 2 voices matches.
  EXPECT_LE(score, 0.6f);
}

TEST(ExpositionCompletenessTest, EmptyNotesReturnsZero) {
  std::vector<NoteEvent> empty;
  auto subject = makeSubject();
  EXPECT_NEAR(expositionCompletenessScore(empty, 3, subject), 0.0f, kEpsilon);
}

TEST(ExpositionCompletenessTest, EmptySubjectReturnsZero) {
  auto notes = makeProperFugue();
  std::vector<NoteEvent> empty_subject;
  EXPECT_NEAR(expositionCompletenessScore(notes, 3, empty_subject), 0.0f, kEpsilon);
}

TEST(ExpositionCompletenessTest, SingleNoteSubjectReturnsZero) {
  auto notes = makeProperFugue();
  std::vector<NoteEvent> single_note = {qn(0, 60, 0)};
  EXPECT_NEAR(expositionCompletenessScore(notes, 3, single_note), 0.0f, kEpsilon);
}

TEST(ExpositionCompletenessTest, ZeroVoicesReturnsZero) {
  auto notes = makeProperFugue();
  auto subject = makeSubject();
  EXPECT_NEAR(expositionCompletenessScore(notes, 0, subject), 0.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// tonalPlanScore
// ---------------------------------------------------------------------------

TEST(TonalPlanTest, MonotonalLowScore) {
  // All notes are C4 throughout -- single tonal center.
  std::vector<NoteEvent> notes;
  for (Tick bar = 0; bar < 8; ++bar) {
    for (uint8_t beat = 0; beat < 4; ++beat) {
      Tick tick = bar * kTicksPerBar + beat * kTicksPerBeat;
      notes.push_back(qn(tick, 60, 0));
    }
  }
  float score = tonalPlanScore(notes);
  // Only 1 tonal center, should be low (0.25 base).
  EXPECT_LE(score, 0.5f);
}

TEST(TonalPlanTest, VariedTonalCenters) {
  auto notes = makeProperFugue();
  float score = tonalPlanScore(notes);
  // The proper fugue has varied pitch areas.
  EXPECT_GT(score, 0.2f);
}

TEST(TonalPlanTest, EmptyReturnsZero) {
  std::vector<NoteEvent> empty;
  EXPECT_NEAR(tonalPlanScore(empty), 0.0f, kEpsilon);
}

TEST(TonalPlanTest, SingleNoteReturnsPositive) {
  std::vector<NoteEvent> notes = {qn(0, 60, 0)};
  float score = tonalPlanScore(notes);
  // At least one tonal center exists.
  EXPECT_GE(score, 0.0f);
}

TEST(TonalPlanTest, TonalReturnBonus) {
  // First and last quarters use the same tonal center (C), middle uses different.
  std::vector<NoteEvent> notes;
  // Q1: C major area.
  for (uint8_t beat = 0; beat < 4; ++beat) {
    notes.push_back(qn(beat * kTicksPerBeat, 60, 0));  // C4
  }
  // Q2: G area.
  for (uint8_t beat = 0; beat < 4; ++beat) {
    notes.push_back(qn(kTicksPerBar + beat * kTicksPerBeat, 67, 0));  // G4
  }
  // Q3: F area.
  for (uint8_t beat = 0; beat < 4; ++beat) {
    notes.push_back(qn(kTicksPerBar * 2 + beat * kTicksPerBeat, 65, 0));  // F4
  }
  // Q4: return to C.
  for (uint8_t beat = 0; beat < 4; ++beat) {
    notes.push_back(qn(kTicksPerBar * 3 + beat * kTicksPerBeat, 60, 0));  // C4
  }
  float score = tonalPlanScore(notes);
  // 3 distinct centers + tonal return bonus + modulation bonus.
  EXPECT_GT(score, 0.7f);
}

// ---------------------------------------------------------------------------
// analyzeFugue (integration)
// ---------------------------------------------------------------------------

TEST(AnalyzeFugueTest, ProperFugueScoresWell) {
  auto notes = makeProperFugue();
  auto subject = makeSubject();
  auto result = analyzeFugue(notes, 3, subject);

  EXPECT_GT(result.exposition_completeness_score, 0.3f);
  EXPECT_GT(result.tonal_plan_score, 0.2f);

  // All scores should be in [0, 1].
  EXPECT_GE(result.answer_accuracy_score, 0.0f);
  EXPECT_LE(result.answer_accuracy_score, 1.0f);
  EXPECT_GE(result.exposition_completeness_score, 0.0f);
  EXPECT_LE(result.exposition_completeness_score, 1.0f);
  EXPECT_GE(result.episode_motif_usage_rate, 0.0f);
  EXPECT_LE(result.episode_motif_usage_rate, 1.0f);
  EXPECT_GE(result.tonal_plan_score, 0.0f);
  EXPECT_LE(result.tonal_plan_score, 1.0f);
}

TEST(AnalyzeFugueTest, EmptyNotesAllZero) {
  std::vector<NoteEvent> empty;
  auto subject = makeSubject();
  auto result = analyzeFugue(empty, 3, subject);
  EXPECT_NEAR(result.exposition_completeness_score, 0.0f, kEpsilon);
  EXPECT_NEAR(result.tonal_plan_score, 0.0f, kEpsilon);
}

TEST(AnalyzeFugueTest, AnswerAccuracyPerfectFifth) {
  auto notes = makeProperFugue();
  auto subject = makeSubject();
  auto result = analyzeFugue(notes, 3, subject);
  // Voice 1 enters at P5 above (G4 = subject start + 7).
  EXPECT_GT(result.answer_accuracy_score, 0.5f);
}

TEST(AnalyzeFugueTest, AnswerAccuracyWrongTransposition) {
  // Voice 1 enters at M2 instead of P5.
  std::vector<NoteEvent> notes;
  // Voice 0: subject.
  notes.push_back(qn(0, 60, 0));
  notes.push_back(qn(kTicksPerBeat, 62, 0));
  notes.push_back(qn(kTicksPerBeat * 2, 64, 0));
  notes.push_back(qn(kTicksPerBeat * 3, 65, 0));
  // Voice 1: at M2 (D4-E4-F#4-G4) -- wrong transposition level.
  notes.push_back(qn(kTicksPerBar, 62, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat, 64, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 2, 66, 1));
  notes.push_back(qn(kTicksPerBar + kTicksPerBeat * 3, 67, 1));

  auto subject = makeSubject();
  auto result = analyzeFugue(notes, 2, subject);
  // Transposition is M2 (2 semitones), not P5 or P4.
  EXPECT_LT(result.answer_accuracy_score, 0.5f);
}

TEST(AnalyzeFugueTest, EpisodeMotifUsage) {
  auto notes = makeProperFugue();
  auto subject = makeSubject();
  auto result = analyzeFugue(notes, 3, subject);
  // Post-exposition material uses subject-derived intervals.
  EXPECT_GE(result.episode_motif_usage_rate, 0.0f);
  EXPECT_LE(result.episode_motif_usage_rate, 1.0f);
}

TEST(AnalyzeFugueTest, DefaultResultIsZero) {
  FugueAnalysisResult result;
  EXPECT_NEAR(result.answer_accuracy_score, 0.0f, kEpsilon);
  EXPECT_NEAR(result.exposition_completeness_score, 0.0f, kEpsilon);
  EXPECT_NEAR(result.episode_motif_usage_rate, 0.0f, kEpsilon);
  EXPECT_NEAR(result.tonal_plan_score, 0.0f, kEpsilon);
  EXPECT_NEAR(result.cadence_detection_rate, 0.0f, kEpsilon);
  EXPECT_NEAR(result.motivic_unity_score, 0.0f, kEpsilon);
  EXPECT_NEAR(result.tonal_consistency_score, 0.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// Timeline helper for cadence detection tests
// ---------------------------------------------------------------------------

/// @brief Create a harmonic event for test timelines.
HarmonicEvent makeHarmonicEvent(Tick tick, Tick end_tick, ChordDegree degree,
                                ChordQuality quality = ChordQuality::Major) {
  HarmonicEvent evt;
  evt.tick = tick;
  evt.end_tick = end_tick;
  evt.key = Key::C;
  evt.is_minor = false;
  evt.chord.degree = degree;
  evt.chord.quality = quality;
  evt.chord.root_pitch = 60;
  evt.chord.inversion = 0;
  evt.weight = 1.0f;
  return evt;
}

// ---------------------------------------------------------------------------
// computeCadenceDetectionRate
// ---------------------------------------------------------------------------

TEST(CadenceDetectionRateTest, AllDetected_ReturnsOne) {
  // Build a timeline with V7->I cadences at bar 2 and bar 6.
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmonicEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar, kTicksPerBar * 2,
                                      ChordDegree::V, ChordQuality::Dominant7));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 2, kTicksPerBar * 3,
                                      ChordDegree::I));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 3, kTicksPerBar * 4,
                                      ChordDegree::IV));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 4, kTicksPerBar * 5,
                                      ChordDegree::ii, ChordQuality::Minor));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 5, kTicksPerBar * 6,
                                      ChordDegree::V, ChordQuality::Dominant7));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 6, kTicksPerBar * 7,
                                      ChordDegree::I));

  // Planned cadences at the V7->I resolution ticks.
  std::vector<Tick> planned = {kTicksPerBar * 2, kTicksPerBar * 6};

  float rate = computeCadenceDetectionRate(timeline, planned);
  EXPECT_FLOAT_EQ(rate, 1.0f);
}

TEST(CadenceDetectionRateTest, NoneDetected_ReturnsZero) {
  // Timeline with no cadential progressions (I -> IV -> ii only).
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmonicEvent(0, kTicksPerBar, ChordDegree::I));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar, kTicksPerBar * 2,
                                      ChordDegree::IV));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar * 2, kTicksPerBar * 3,
                                      ChordDegree::ii, ChordQuality::Minor));

  std::vector<Tick> planned = {kTicksPerBar * 2};

  float rate = computeCadenceDetectionRate(timeline, planned);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

TEST(CadenceDetectionRateTest, EmptyPlanned_ReturnsZero) {
  HarmonicTimeline timeline;
  timeline.addEvent(makeHarmonicEvent(0, kTicksPerBar, ChordDegree::V,
                                      ChordQuality::Dominant7));
  timeline.addEvent(makeHarmonicEvent(kTicksPerBar, kTicksPerBar * 2,
                                      ChordDegree::I));

  std::vector<Tick> planned;  // No planned cadences.

  float rate = computeCadenceDetectionRate(timeline, planned);
  EXPECT_FLOAT_EQ(rate, 0.0f);
}

// ---------------------------------------------------------------------------
// computeMotivicUnityScore
// ---------------------------------------------------------------------------

TEST(MotivicUnityScoreTest, SubjectPervasive_HighScore) {
  // Subject: C4-D4-E4-F4 (intervals: +2, +2, +1).
  auto subject = makeSubject();

  // Place the subject pattern (transposed) in every voice across 4 time quarters.
  // Piece spans 8 bars = 4 quarters of 2 bars each, 2 voices.
  std::vector<NoteEvent> notes;
  for (int qtr = 0; qtr < 4; ++qtr) {
    Tick base = static_cast<Tick>(qtr) * kTicksPerBar * 2;
    // Voice 0: subject pattern transposed up by quarter index.
    uint8_t offset = static_cast<uint8_t>(qtr * 5);
    notes.push_back(qn(base, 60 + offset, 0));
    notes.push_back(qn(base + kTicksPerBeat, 62 + offset, 0));
    notes.push_back(qn(base + kTicksPerBeat * 2, 64 + offset, 0));
    notes.push_back(qn(base + kTicksPerBeat * 3, 65 + offset, 0));

    // Voice 1: same pattern.
    notes.push_back(qn(base + kTicksPerBar, 48 + offset, 1));
    notes.push_back(qn(base + kTicksPerBar + kTicksPerBeat, 50 + offset, 1));
    notes.push_back(qn(base + kTicksPerBar + kTicksPerBeat * 2, 52 + offset, 1));
    notes.push_back(qn(base + kTicksPerBar + kTicksPerBeat * 3, 53 + offset, 1));
  }

  float score = computeMotivicUnityScore(notes, subject, 2);
  // All 8 cells (4 quarters x 2 voices) should have the motif.
  EXPECT_GT(score, 0.7f);
  EXPECT_LE(score, 1.0f);
}

TEST(MotivicUnityScoreTest, NoMotif_ReturnsZero) {
  auto subject = makeSubject();  // Intervals: +2, +2, +1.

  // Create notes with completely different interval pattern (large jumps).
  std::vector<NoteEvent> notes;
  for (int qtr = 0; qtr < 4; ++qtr) {
    Tick base = static_cast<Tick>(qtr) * kTicksPerBar * 2;
    // Voice 0: large jumps (+12, -12, +12) -- no match to +2,+2,+1.
    notes.push_back(qn(base, 60, 0));
    notes.push_back(qn(base + kTicksPerBeat, 72, 0));
    notes.push_back(qn(base + kTicksPerBeat * 2, 60, 0));
    notes.push_back(qn(base + kTicksPerBeat * 3, 72, 0));
  }

  float score = computeMotivicUnityScore(notes, subject, 1);
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// computeTonalConsistencyScore
// ---------------------------------------------------------------------------

TEST(TonalConsistencyScoreTest, AllOnScale_ReturnsHigh) {
  // All notes are C major scale tones (C, D, E, F, G, A, B).
  std::vector<NoteEvent> notes;
  uint8_t scale_pitches[] = {60, 62, 64, 65, 67, 69, 71};  // C4 D4 E4 F4 G4 A4 B4
  for (int idx = 0; idx < 7; ++idx) {
    notes.push_back(qn(static_cast<Tick>(idx) * kTicksPerBeat, scale_pitches[idx], 0));
  }
  // Add extra C notes to make tonic most frequent.
  notes.push_back(qn(7 * kTicksPerBeat, 60, 0));
  notes.push_back(qn(8 * kTicksPerBeat, 60, 0));

  float score = computeTonalConsistencyScore(notes, Key::C, false);
  // All notes on scale => base ~1.0, plus tonic bonus +0.1 => clamped to 1.0.
  EXPECT_GT(score, 0.9f);
  EXPECT_LE(score, 1.0f);
}

TEST(TonalConsistencyScoreTest, AllChromatic_ReturnsLow) {
  // All notes are exclusively off-scale chromatic tones for C major.
  // C major scale: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  // Off-scale: Cs(1), Eb(3), Fs(6), Ab(8), Bb(10)
  std::vector<NoteEvent> notes;
  uint8_t chromatic_pitches[] = {61, 63, 66, 68, 70};  // Cs4 Eb4 Fs4 Ab4 Bb4
  for (int idx = 0; idx < 5; ++idx) {
    notes.push_back(qn(static_cast<Tick>(idx) * kTicksPerBeat, chromatic_pitches[idx], 0));
  }

  float score = computeTonalConsistencyScore(notes, Key::C, false);
  // All notes off-scale => 0.0 base, no tonic bonus.
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

TEST(TonalConsistencyScoreTest, TonicMostFrequent_BonusApplied) {
  // Mix of on-scale and chromatic, but tonic (C) is most frequent.
  std::vector<NoteEvent> notes;
  // 5 C notes (on scale, tonic).
  for (int idx = 0; idx < 5; ++idx) {
    notes.push_back(qn(static_cast<Tick>(idx) * kTicksPerBeat, 60, 0));
  }
  // 3 D notes (on scale, not tonic).
  for (int idx = 0; idx < 3; ++idx) {
    notes.push_back(qn(static_cast<Tick>(5 + idx) * kTicksPerBeat, 62, 0));
  }
  // 2 Cs notes (off scale).
  notes.push_back(qn(8 * kTicksPerBeat, 61, 0));
  notes.push_back(qn(9 * kTicksPerBeat, 61, 0));

  float score_with_tonic = computeTonalConsistencyScore(notes, Key::C, false);
  // 8/10 on scale = 0.8 base, tonic is most frequent => +0.1 = 0.9.
  EXPECT_NEAR(score_with_tonic, 0.9f, 0.05f);

  // Same notes but evaluated against D major (tonic D) -- C is NOT the D tonic.
  // D major scale: D(2), E(4), Fs(6), G(7), A(9), B(11), Cs(1)
  // Of our 10 notes: 5xC(0)=off, 3xD(2)=on, 2xCs(1)=on => 5/10 = 0.5 base.
  // D count=3, C count=5 => D is not most frequent => no bonus.
  float score_d_major = computeTonalConsistencyScore(notes, Key::D, false);
  EXPECT_LT(score_d_major, score_with_tonic);
}

// ---------------------------------------------------------------------------
// invertibleCounterpointScore
// ---------------------------------------------------------------------------

TEST(InvertibleCounterpointScoreTest, NoCountersubject) {
  auto subject = makeSubject();
  std::vector<NoteEvent> empty_counter;
  float score = invertibleCounterpointScore(subject, empty_counter, 2);
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

TEST(InvertibleCounterpointScoreTest, SimpleConsonance) {
  // Subject: C4-D4-E4-F4, Counter: A3-B3-C4-D4 (3rds below).
  // Inversion swaps: counter goes to upper, subject down an octave.
  // The inverted intervals should remain mostly consonant (3rds/6ths).
  auto subject = makeSubject();
  std::vector<NoteEvent> counter = {
      qn(0, 57, 1),              // A3
      qn(kTicksPerBeat, 59, 1),  // B3
      qn(kTicksPerBeat * 2, 60, 1),  // C4
      qn(kTicksPerBeat * 3, 62, 1),  // D4
  };
  float score = invertibleCounterpointScore(subject, counter, 2);
  // Should be near 1.0 since parallel 3rds invert to parallel 6ths (no forbidden parallels).
  EXPECT_GT(score, 0.8f);
}

}  // namespace
}  // namespace bach
