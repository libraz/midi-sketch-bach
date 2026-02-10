// Tests for voice independence analysis module.

#include "analysis/voice_independence.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a simple ascending quarter-note line on the given voice.
/// Pitches: base, base+2, base+4, base+6 (whole-tone ascent).
std::vector<NoteEvent> makeAscendingLine(VoiceId voice, uint8_t base_pitch,
                                         Tick start = 0) {
  return {
      {start, kTicksPerBeat, base_pitch, 80, voice},
      {start + kTicksPerBeat, kTicksPerBeat, static_cast<uint8_t>(base_pitch + 2), 80, voice},
      {start + kTicksPerBeat * 2, kTicksPerBeat, static_cast<uint8_t>(base_pitch + 4), 80,
       voice},
      {start + kTicksPerBeat * 3, kTicksPerBeat, static_cast<uint8_t>(base_pitch + 6), 80,
       voice},
  };
}

/// Create a descending quarter-note line on the given voice.
/// Pitches: base, base-2, base-4, base-6.
std::vector<NoteEvent> makeDescendingLine(VoiceId voice, uint8_t base_pitch,
                                          Tick start = 0) {
  return {
      {start, kTicksPerBeat, base_pitch, 80, voice},
      {start + kTicksPerBeat, kTicksPerBeat, static_cast<uint8_t>(base_pitch - 2), 80, voice},
      {start + kTicksPerBeat * 2, kTicksPerBeat, static_cast<uint8_t>(base_pitch - 4), 80,
       voice},
      {start + kTicksPerBeat * 3, kTicksPerBeat, static_cast<uint8_t>(base_pitch - 6), 80,
       voice},
  };
}

/// Merge two note vectors into one.
std::vector<NoteEvent> merge(const std::vector<NoteEvent>& vec_a,
                             const std::vector<NoteEvent>& vec_b) {
  std::vector<NoteEvent> result;
  result.reserve(vec_a.size() + vec_b.size());
  result.insert(result.end(), vec_a.begin(), vec_a.end());
  result.insert(result.end(), vec_b.begin(), vec_b.end());
  return result;
}

constexpr float kEpsilon = 0.01f;

// ---------------------------------------------------------------------------
// VoiceIndependenceScore: composite and meetsTrioStandard
// ---------------------------------------------------------------------------

TEST(VoiceIndependenceScoreTest, CompositeWeights) {
  VoiceIndependenceScore score;
  score.rhythm_independence = 1.0f;
  score.contour_independence = 1.0f;
  score.register_separation = 1.0f;
  EXPECT_NEAR(score.composite(), 1.0f, kEpsilon);
}

TEST(VoiceIndependenceScoreTest, CompositeZero) {
  VoiceIndependenceScore score;
  // Default-initialized to 0.
  EXPECT_NEAR(score.composite(), 0.0f, kEpsilon);
}

TEST(VoiceIndependenceScoreTest, CompositePartial) {
  VoiceIndependenceScore score;
  score.rhythm_independence = 0.5f;   // 0.5 * 0.4 = 0.20
  score.contour_independence = 0.8f;  // 0.8 * 0.3 = 0.24
  score.register_separation = 0.6f;   // 0.6 * 0.3 = 0.18
  // Total: 0.62
  EXPECT_NEAR(score.composite(), 0.62f, kEpsilon);
}

TEST(VoiceIndependenceScoreTest, MeetsTrioStandardTrue) {
  VoiceIndependenceScore score;
  score.rhythm_independence = 0.8f;
  score.contour_independence = 0.7f;
  score.register_separation = 0.6f;
  // composite = 0.32 + 0.21 + 0.18 = 0.71
  EXPECT_TRUE(score.meetsTrioStandard());
}

TEST(VoiceIndependenceScoreTest, MeetsTrioStandardFalse) {
  VoiceIndependenceScore score;
  score.rhythm_independence = 0.3f;
  score.contour_independence = 0.2f;
  score.register_separation = 0.1f;
  // composite = 0.12 + 0.06 + 0.03 = 0.21
  EXPECT_FALSE(score.meetsTrioStandard());
}

TEST(VoiceIndependenceScoreTest, MeetsTrioStandardBoundary) {
  VoiceIndependenceScore score;
  score.rhythm_independence = 0.6f;
  score.contour_independence = 0.6f;
  score.register_separation = 0.6f;
  // composite = 0.24 + 0.18 + 0.18 = 0.60 -> exactly threshold
  EXPECT_TRUE(score.meetsTrioStandard());
}

// ---------------------------------------------------------------------------
// Rhythm independence
// ---------------------------------------------------------------------------

TEST(RhythmIndependenceTest, IdenticalOnsets) {
  // Two voices with identical onset patterns -> low independence.
  auto voice_a = makeAscendingLine(0, 60);
  auto voice_b = makeAscendingLine(1, 72);
  float score = calculateRhythmIndependence(voice_a, voice_b);
  // All 4 onsets match. 4 simultaneous / 4 total = 1.0 ratio -> score = 0.0.
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

TEST(RhythmIndependenceTest, CompletelyDifferentOnsets) {
  // Voice A on beats 0, 1, 2, 3.  Voice B on off-beats.
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 62, 80, 0},
      {960, 480, 64, 80, 0},
      {1440, 480, 65, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {240, 240, 72, 80, 1},
      {720, 240, 74, 80, 1},
      {1200, 240, 76, 80, 1},
      {1680, 240, 77, 80, 1},
  };
  float score = calculateRhythmIndependence(voice_a, voice_b);
  // All 8 onsets are unique, 0 simultaneous. Score = 1.0.
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

TEST(RhythmIndependenceTest, PartialOverlap) {
  // Voice A: beats 0, 1.  Voice B: beats 1, 2.  Beat 1 overlaps.
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 62, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {480, 480, 72, 80, 1},
      {960, 480, 74, 80, 1},
  };
  float score = calculateRhythmIndependence(voice_a, voice_b);
  // 3 unique onsets (0, 480, 960). 1 simultaneous (480). Score = 1 - 1/3 = 0.667.
  EXPECT_NEAR(score, 0.667f, 0.02f);
}

TEST(RhythmIndependenceTest, EmptyBothVoices) {
  std::vector<NoteEvent> empty_a;
  std::vector<NoteEvent> empty_b;
  EXPECT_NEAR(calculateRhythmIndependence(empty_a, empty_b), 0.0f, kEpsilon);
}

TEST(RhythmIndependenceTest, OneVoiceEmpty) {
  auto voice_a = makeAscendingLine(0, 60);
  std::vector<NoteEvent> empty_b;
  // One empty voice -> fully independent.
  EXPECT_NEAR(calculateRhythmIndependence(voice_a, empty_b), 1.0f, kEpsilon);
}

TEST(RhythmIndependenceTest, WithinTolerance) {
  // Two onsets 100 ticks apart (<= 120 tolerance) should count as simultaneous.
  std::vector<NoteEvent> voice_a = {{0, 480, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{100, 480, 72, 80, 1}};
  float score = calculateRhythmIndependence(voice_a, voice_b);
  // 2 unique onsets, 1 simultaneous. Score = 1 - 1/2 = 0.5.
  EXPECT_NEAR(score, 0.5f, kEpsilon);
}

TEST(RhythmIndependenceTest, JustOutsideTolerance) {
  // Two onsets 121 ticks apart (> 120 tolerance) -> not simultaneous.
  std::vector<NoteEvent> voice_a = {{0, 480, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{121, 480, 72, 80, 1}};
  float score = calculateRhythmIndependence(voice_a, voice_b);
  // 2 unique onsets, 0 simultaneous. Score = 1.0.
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// Contour independence
// ---------------------------------------------------------------------------

TEST(ContourIndependenceTest, OppositeContours) {
  // Voice A ascends, Voice B descends -> maximum contour independence.
  auto voice_a = makeAscendingLine(0, 60);
  auto voice_b = makeDescendingLine(1, 84);
  float score = calculateContourIndependence(voice_a, voice_b);
  // Every beat comparison should show opposite directions.
  EXPECT_GT(score, 0.8f);
}

TEST(ContourIndependenceTest, ParallelContours) {
  // Both voices ascend with same pattern -> low contour independence.
  auto voice_a = makeAscendingLine(0, 60);
  auto voice_b = makeAscendingLine(1, 72);
  float score = calculateContourIndependence(voice_a, voice_b);
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

TEST(ContourIndependenceTest, EmptyVoice) {
  auto voice_a = makeAscendingLine(0, 60);
  std::vector<NoteEvent> empty_b;
  EXPECT_NEAR(calculateContourIndependence(voice_a, empty_b), 0.0f, kEpsilon);
}

TEST(ContourIndependenceTest, BothEmpty) {
  std::vector<NoteEvent> empty_a;
  std::vector<NoteEvent> empty_b;
  EXPECT_NEAR(calculateContourIndependence(empty_a, empty_b), 0.0f, kEpsilon);
}

TEST(ContourIndependenceTest, SingleNoteEach) {
  // Single note per voice -> no direction change possible.
  std::vector<NoteEvent> voice_a = {{0, 480, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{0, 480, 72, 80, 1}};
  EXPECT_NEAR(calculateContourIndependence(voice_a, voice_b), 0.0f, kEpsilon);
}

TEST(ContourIndependenceTest, StaticVoiceIgnored) {
  // Voice A ascends. Voice B holds same pitch -> no direction in B.
  // "Same" directions are skipped, so no comparisons -> 0.0.
  auto voice_a = makeAscendingLine(0, 60);
  std::vector<NoteEvent> voice_b = {
      {0, kTicksPerBeat, 72, 80, 1},
      {kTicksPerBeat, kTicksPerBeat, 72, 80, 1},
      {kTicksPerBeat * 2, kTicksPerBeat, 72, 80, 1},
      {kTicksPerBeat * 3, kTicksPerBeat, 72, 80, 1},
  };
  float score = calculateContourIndependence(voice_a, voice_b);
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// Register separation
// ---------------------------------------------------------------------------

TEST(RegisterSeparationTest, NoOverlap) {
  // Voice A: C2-G2 (36-43), Voice B: C5-G5 (84-91).
  std::vector<NoteEvent> voice_a = {
      {0, 480, 36, 80, 0},
      {480, 480, 43, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {0, 480, 84, 80, 1},
      {480, 480, 91, 80, 1},
  };
  float score = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

TEST(RegisterSeparationTest, FullOverlap) {
  // Both voices span C4-G4 (60-67).
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 67, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {0, 480, 60, 80, 1},
      {480, 480, 67, 80, 1},
  };
  float score = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(score, 0.0f, kEpsilon);
}

TEST(RegisterSeparationTest, PartialOverlap) {
  // Voice A: 60-72 (range=12). Voice B: 66-78 (range=12).
  // Overlap: 66-72 = 6 semitones. max_range = 12. Score = 1 - 6/12 = 0.5.
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 72, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {0, 480, 66, 80, 1},
      {480, 480, 78, 80, 1},
  };
  float score = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(score, 0.5f, kEpsilon);
}

TEST(RegisterSeparationTest, EmptyVoice) {
  auto voice_a = makeAscendingLine(0, 60);
  std::vector<NoteEvent> empty_b;
  EXPECT_NEAR(calculateRegisterSeparation(voice_a, empty_b), 0.0f, kEpsilon);
}

TEST(RegisterSeparationTest, SinglePitchEach) {
  // Voice A: pitch 60 only. Voice B: pitch 72 only.
  // Range A = 0, Range B = 0. Overlap = max(0, 60-72) = 0.
  // max_range = max(0, 0, 1) = 1. Score = 1 - 0/1 = 1.0.
  std::vector<NoteEvent> voice_a = {{0, 480, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{0, 480, 72, 80, 1}};
  float score = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

TEST(RegisterSeparationTest, SameSinglePitch) {
  // Both voices on pitch 60. Range = 0 each. Overlap = max(0, 60-60) = 0.
  // max_range = 1 (minimum). Score = 1 - 0/1 = 1.0.
  // Even though same pitch, with zero range the formula yields 1.0.
  std::vector<NoteEvent> voice_a = {{0, 480, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{0, 480, 60, 80, 1}};
  float score = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(score, 1.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// analyzeVoicePair
// ---------------------------------------------------------------------------

TEST(AnalyzeVoicePairTest, IdenticalVoicesLowScore) {
  // Two voices with identical notes (same rhythm, same pitches, same range).
  auto soprano = makeAscendingLine(0, 60);
  auto alto = makeAscendingLine(1, 60);
  auto all_notes = merge(soprano, alto);

  auto score = analyzeVoicePair(all_notes, 0, 1);
  EXPECT_LT(score.composite(), 0.3f);
}

TEST(AnalyzeVoicePairTest, IndependentVoicesHighScore) {
  // Voice 0: ascending C4 range, on beats.
  // Voice 1: descending C5 range, on off-beats.
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 62, 80, 0},
      {960, 480, 64, 80, 0},
      {1440, 480, 66, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {240, 240, 84, 80, 1},
      {720, 240, 82, 80, 1},
      {1200, 240, 80, 80, 1},
      {1680, 240, 78, 80, 1},
  };
  auto all_notes = merge(voice_a, voice_b);

  auto score = analyzeVoicePair(all_notes, 0, 1);
  // Should have high rhythm independence (off-beat vs on-beat),
  // high register separation (C4 vs C5+), and contour independence (up vs down).
  EXPECT_GT(score.composite(), 0.5f);
  EXPECT_GT(score.rhythm_independence, 0.5f);
  EXPECT_GT(score.register_separation, 0.5f);
}

TEST(AnalyzeVoicePairTest, FiltersVoicesCorrectly) {
  // Mix of voice 0, 1, and 2 notes. Analyze pair (0, 2).
  std::vector<NoteEvent> all_notes = {
      {0, 480, 60, 80, 0},    // voice 0
      {0, 480, 72, 80, 1},    // voice 1 (should be ignored)
      {0, 480, 84, 80, 2},    // voice 2
      {480, 480, 62, 80, 0},  // voice 0
      {480, 480, 74, 80, 1},  // voice 1 (should be ignored)
      {480, 480, 82, 80, 2},  // voice 2
  };
  auto score = analyzeVoicePair(all_notes, 0, 2);
  // Voice 0 range: 60-62. Voice 2 range: 82-84. No overlap -> high separation.
  EXPECT_GT(score.register_separation, 0.8f);
}

TEST(AnalyzeVoicePairTest, EmptyVoiceReturnsZero) {
  // Voice 0 has notes, voice 1 has none.
  auto voice_a = makeAscendingLine(0, 60);
  auto score = analyzeVoicePair(voice_a, 0, 1);
  // rhythm_independence = 1.0 (one empty), contour = 0.0, register = 0.0
  // composite = 0.4*1.0 + 0.3*0.0 + 0.3*0.0 = 0.4
  EXPECT_NEAR(score.rhythm_independence, 1.0f, kEpsilon);
  EXPECT_NEAR(score.contour_independence, 0.0f, kEpsilon);
  EXPECT_NEAR(score.register_separation, 0.0f, kEpsilon);
}

TEST(AnalyzeVoicePairTest, BothVoicesEmpty) {
  std::vector<NoteEvent> empty;
  auto score = analyzeVoicePair(empty, 0, 1);
  EXPECT_NEAR(score.composite(), 0.0f, kEpsilon);
}

// ---------------------------------------------------------------------------
// analyzeOverall
// ---------------------------------------------------------------------------

TEST(AnalyzeOverallTest, ThreeVoicesReturnsMinimum) {
  // Voice 0: C2 range (very low). Voice 1: C4 range (middle). Voice 2: C4 range (same!).
  // Pair (1,2) should have worse register separation than (0,1) or (0,2).
  std::vector<NoteEvent> all_notes = {
      // Voice 0: low, ascending
      {0, 480, 36, 80, 0},
      {480, 480, 38, 80, 0},
      {960, 480, 40, 80, 0},
      {1440, 480, 42, 80, 0},
      // Voice 1: middle, ascending
      {0, 480, 60, 80, 1},
      {480, 480, 62, 80, 1},
      {960, 480, 64, 80, 1},
      {1440, 480, 66, 80, 1},
      // Voice 2: middle, ascending (same range as voice 1)
      {0, 480, 60, 80, 2},
      {480, 480, 62, 80, 2},
      {960, 480, 64, 80, 2},
      {1440, 480, 66, 80, 2},
  };
  auto overall = analyzeOverall(all_notes, 3);
  auto pair_12 = analyzeVoicePair(all_notes, 1, 2);

  // Overall should be the minimum across all pairs.
  // Pair (1,2) is the worst since they have identical rhythm, contour, and range.
  EXPECT_NEAR(overall.register_separation, pair_12.register_separation, kEpsilon);
  EXPECT_NEAR(overall.rhythm_independence, pair_12.rhythm_independence, kEpsilon);
}

TEST(AnalyzeOverallTest, SingleVoiceReturnsZero) {
  auto voice = makeAscendingLine(0, 60);
  auto score = analyzeOverall(voice, 1);
  EXPECT_NEAR(score.composite(), 0.0f, kEpsilon);
}

TEST(AnalyzeOverallTest, ZeroVoicesReturnsZero) {
  std::vector<NoteEvent> empty;
  auto score = analyzeOverall(empty, 0);
  EXPECT_NEAR(score.composite(), 0.0f, kEpsilon);
}

TEST(AnalyzeOverallTest, TwoIndependentVoices) {
  // Voice 0: low pedal range, Voice 1: high soprano range, off-beat rhythm.
  std::vector<NoteEvent> all_notes = {
      // Voice 0: pedal (ascending)
      {0, 480, 36, 80, 0},
      {480, 480, 38, 80, 0},
      {960, 480, 40, 80, 0},
      {1440, 480, 42, 80, 0},
      // Voice 1: soprano (descending, off-beat)
      {240, 240, 84, 80, 1},
      {720, 240, 82, 80, 1},
      {1200, 240, 80, 80, 1},
      {1680, 240, 78, 80, 1},
  };
  auto score = analyzeOverall(all_notes, 2);
  EXPECT_GT(score.composite(), 0.5f);
  EXPECT_TRUE(score.register_separation > 0.7f);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(VoiceIndependenceEdgeTest, VeryShortPiece) {
  // Two voices each with one note at tick 0.
  std::vector<NoteEvent> voice_a = {{0, 240, 60, 80, 0}};
  std::vector<NoteEvent> voice_b = {{0, 240, 84, 80, 1}};
  auto all = merge(voice_a, voice_b);
  auto score = analyzeVoicePair(all, 0, 1);
  // Should not crash, scores should be in [0,1].
  EXPECT_GE(score.rhythm_independence, 0.0f);
  EXPECT_LE(score.rhythm_independence, 1.0f);
  EXPECT_GE(score.register_separation, 0.0f);
  EXPECT_LE(score.register_separation, 1.0f);
}

TEST(VoiceIndependenceEdgeTest, LargeTickValues) {
  // Voices starting at very high tick values.
  Tick base = kTicksPerBar * 500;
  std::vector<NoteEvent> voice_a = {
      {base, 480, 60, 80, 0},
      {base + 480, 480, 64, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {base + 240, 480, 84, 80, 1},
      {base + 720, 480, 80, 80, 1},
  };
  auto all = merge(voice_a, voice_b);
  auto score = analyzeVoicePair(all, 0, 1);
  EXPECT_GE(score.composite(), 0.0f);
  EXPECT_LE(score.composite(), 1.0f);
}

TEST(VoiceIndependenceEdgeTest, SameRhythmDifferentPitches) {
  // Same rhythm (simultaneous attacks) but different pitch ranges.
  auto voice_a = makeAscendingLine(0, 36);   // C2 range
  auto voice_b = makeAscendingLine(1, 84);   // C6 range
  float rhythm = calculateRhythmIndependence(voice_a, voice_b);
  float reg = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_NEAR(rhythm, 0.0f, kEpsilon);  // Identical onsets.
  EXPECT_GT(reg, 0.8f);                 // Very different ranges.
}

TEST(VoiceIndependenceEdgeTest, DifferentRhythmSamePitches) {
  // Different rhythms but same pitches.
  std::vector<NoteEvent> voice_a = {
      {0, 480, 60, 80, 0},
      {480, 480, 64, 80, 0},
  };
  std::vector<NoteEvent> voice_b = {
      {240, 240, 60, 80, 1},
      {720, 240, 64, 80, 1},
  };
  float rhythm = calculateRhythmIndependence(voice_a, voice_b);
  float reg = calculateRegisterSeparation(voice_a, voice_b);
  EXPECT_GT(rhythm, 0.5f);              // Off-beat onsets.
  EXPECT_NEAR(reg, 0.0f, kEpsilon);     // Same pitch range.
}

}  // namespace
}  // namespace bach
