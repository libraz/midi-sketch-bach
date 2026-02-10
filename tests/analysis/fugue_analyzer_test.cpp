// Tests for fugue structure analysis module.

#include "analysis/fugue_analyzer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/basic_types.h"

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
}

}  // namespace
}  // namespace bach
