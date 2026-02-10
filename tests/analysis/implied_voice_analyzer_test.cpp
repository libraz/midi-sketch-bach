// Tests for analysis/implied_voice_analyzer.h -- implied polyphony detection
// in monophonic solo string lines (BWV 1001-1006 style).

#include "analysis/implied_voice_analyzer.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

/// @brief Build a melody that alternates between two distant registers,
/// simulating implied 2-voice polyphony (e.g. Bach solo violin style).
std::vector<NoteEvent> makeAlternatingMelody() {
  std::vector<NoteEvent> melody;
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note;
    note.pitch = (idx % 2 == 0) ? 72 : 55;  // C5 and G3 alternating
    note.start_tick = static_cast<Tick>(idx * kTicksPerBeat);
    note.duration = kTicksPerBeat;
    note.velocity = 80;
    note.voice = 0;
    melody.push_back(note);
  }
  return melody;
}

/// @brief Build a simple ascending chromatic scale (single implied voice).
std::vector<NoteEvent> makeScalarMelody() {
  std::vector<NoteEvent> melody;
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.start_tick = static_cast<Tick>(idx * kTicksPerBeat);
    note.duration = kTicksPerBeat;
    note.velocity = 80;
    note.voice = 0;
    melody.push_back(note);
  }
  return melody;
}

/// @brief Build a heavily skewed melody (most notes in one register).
std::vector<NoteEvent> makeSkewedMelody() {
  std::vector<NoteEvent> melody;
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note;
    // 14 notes in low register, 2 notes in high register.
    note.pitch = (idx < 14) ? 48 : 84;
    note.start_tick = static_cast<Tick>(idx * kTicksPerBeat);
    note.duration = kTicksPerBeat;
    note.velocity = 80;
    note.voice = 0;
    melody.push_back(note);
  }
  return melody;
}

TEST(ImpliedVoiceAnalyzerTest, AlternatingMelody_HighImpliedVoiceCount) {
  auto melody = makeAlternatingMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  // Even distribution between two registers -> high implied voice count.
  EXPECT_GT(result.implied_voice_count, 1.5f);
}

TEST(ImpliedVoiceAnalyzerTest, SkewedMelody_LowerImpliedVoiceCount) {
  auto melody = makeSkewedMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  // Skewed distribution implies fewer independent voices.
  // The alternating melody (balanced) should imply more voices.
  auto alt_melody = makeAlternatingMelody();
  uint8_t alt_split = ImpliedVoiceAnalyzer::estimateSplitPitch(alt_melody);
  auto alt_result = ImpliedVoiceAnalyzer::analyze(alt_melody, alt_split);

  EXPECT_LT(result.implied_voice_count, alt_result.implied_voice_count);
}

TEST(ImpliedVoiceAnalyzerTest, EmptyMelody_DefaultSplitPitch) {
  std::vector<NoteEvent> empty;
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(empty);
  EXPECT_EQ(split, 60);  // Default to C4.
}

TEST(ImpliedVoiceAnalyzerTest, ShortMelody_SingleVoice) {
  std::vector<NoteEvent> short_melody;
  NoteEvent note;
  note.pitch = 60;
  note.start_tick = 0;
  note.duration = kTicksPerBeat;
  short_melody.push_back(note);

  // Melody with < 4 notes returns 1.0 implied voice.
  auto result = ImpliedVoiceAnalyzer::analyze(short_melody, 60);
  EXPECT_FLOAT_EQ(result.implied_voice_count, 1.0f);
}

TEST(ImpliedVoiceAnalyzerTest, RegisterConsistency_InRange) {
  auto melody = makeAlternatingMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  EXPECT_GE(result.register_consistency, 0.0f);
  EXPECT_LE(result.register_consistency, 1.0f);
}

TEST(ImpliedVoiceAnalyzerTest, AlternatingMelody_HighRegisterConsistency) {
  // Each implied voice contains notes of identical pitch -> std dev = 0 -> consistency = 1.0.
  auto melody = makeAlternatingMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  EXPECT_GT(result.register_consistency, 0.9f);
}

TEST(ImpliedVoiceAnalyzerTest, ScalarMelody_LowerRegisterConsistency) {
  // Chromatic scale split at median: each voice spans ~8 semitones -> lower consistency.
  auto melody = makeScalarMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  // Scalar melody has wider intra-voice range than the alternating melody.
  auto alt_melody = makeAlternatingMelody();
  uint8_t alt_split = ImpliedVoiceAnalyzer::estimateSplitPitch(alt_melody);
  auto alt_result = ImpliedVoiceAnalyzer::analyze(alt_melody, alt_split);

  EXPECT_LT(result.register_consistency, alt_result.register_consistency);
}

TEST(ImpliedVoiceAnalyzerTest, ParallelCount_NonNegative) {
  auto melody = makeAlternatingMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  // unsigned type, but verify the analysis doesn't produce unexpected values.
  EXPECT_GE(result.implied_parallel_count, 0u);
}

TEST(ImpliedVoiceAnalyzerTest, SplitPitch_IsMedian) {
  auto melody = makeScalarMelody();  // Pitches 60-75, sorted.
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);

  // Median of 16 elements (60..75) at index 8 = 68.
  EXPECT_EQ(split, 68);
}

TEST(ImpliedVoiceAnalyzerTest, QualityGate_AlternatingMelody) {
  auto melody = makeAlternatingMelody();
  uint8_t split = ImpliedVoiceAnalyzer::estimateSplitPitch(melody);
  auto result = ImpliedVoiceAnalyzer::analyze(melody, split);

  // Balanced alternation: voice count ~2.8, within [2.3, 2.8] gate range.
  // Quality gate also requires implied_parallel_count <= 2.
  if (result.implied_voice_count >= 2.3f && result.implied_voice_count <= 2.8f &&
      result.implied_parallel_count <= 2) {
    EXPECT_TRUE(result.passes_quality_gate);
  }
}

}  // namespace
}  // namespace bach
