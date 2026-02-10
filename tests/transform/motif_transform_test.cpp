// Tests for motif transformations and sequence generation.

#include "transform/motif_transform.h"

#include <gtest/gtest.h>

#include "core/scale.h"
#include "transform/sequence.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a simple 3-note motif (C4-E4-G4, quarter notes, no gaps)
// ---------------------------------------------------------------------------
std::vector<NoteEvent> makeTestMotif(Tick start = 0) {
  // C4=60, E4=64, G4=67, quarter note = 480 ticks
  return {
      {start, 480, 60, 80, 0},
      {start + 480, 480, 64, 80, 0},
      {start + 960, 480, 67, 80, 0},
  };
}

// ---------------------------------------------------------------------------
// InvertMelody tests
// ---------------------------------------------------------------------------

TEST(InvertMelodyTest, AroundMiddle) {
  auto motif = makeTestMotif();
  // Invert C4(60)-E4(64)-G4(67) around E4(64)
  // 60 -> 2*64-60=68, 64 -> 64, 67 -> 2*64-67=61
  auto result = invertMelody(motif, 64);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 68);  // Ab4
  EXPECT_EQ(result[1].pitch, 64);  // E4 (pivot, unchanged)
  EXPECT_EQ(result[2].pitch, 61);  // Db4
}

TEST(InvertMelodyTest, KeepsRhythm) {
  auto motif = makeTestMotif(1920);
  auto result = invertMelody(motif, 64);
  ASSERT_EQ(result.size(), 3u);
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(result[idx].start_tick, motif[idx].start_tick);
    EXPECT_EQ(result[idx].duration, motif[idx].duration);
    EXPECT_EQ(result[idx].velocity, motif[idx].velocity);
    EXPECT_EQ(result[idx].voice, motif[idx].voice);
  }
}

TEST(InvertMelodyTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = invertMelody(empty, 60);
  EXPECT_TRUE(result.empty());
}

TEST(InvertMelodyTest, ClampsToValidRange) {
  // Pitch 10, pivot 0 -> 2*0-10 = -10, should clamp to 0
  std::vector<NoteEvent> low_motif = {{0, 480, 10, 80, 0}};
  auto result = invertMelody(low_motif, 0);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 0);

  // Pitch 120, pivot 127 -> 2*127-120 = 134, should clamp to 127
  std::vector<NoteEvent> high_motif = {{0, 480, 120, 80, 0}};
  result = invertMelody(high_motif, 127);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].pitch, 127);
}

// ---------------------------------------------------------------------------
// RetrogradeMelody tests
// ---------------------------------------------------------------------------

TEST(RetrogradeMelodyTest, ReversesOrder) {
  auto motif = makeTestMotif();
  auto result = retrogradeMelody(motif, 0);
  ASSERT_EQ(result.size(), 3u);
  // Pitch order reversed: G4, E4, C4
  EXPECT_EQ(result[0].pitch, 67);
  EXPECT_EQ(result[1].pitch, 64);
  EXPECT_EQ(result[2].pitch, 60);
}

TEST(RetrogradeMelodyTest, RecalculatesTicks) {
  auto motif = makeTestMotif(100);
  auto result = retrogradeMelody(motif, 500);
  ASSERT_EQ(result.size(), 3u);
  // Original: all quarter notes (480 ticks), no gaps, starting at 100
  // Retrograde starting at 500 should produce contiguous notes:
  // [500, 500+480), [980, 980+480), [1460, 1460+480)
  EXPECT_EQ(result[0].start_tick, 500u);
  EXPECT_EQ(result[1].start_tick, 980u);
  EXPECT_EQ(result[2].start_tick, 1460u);
  // All durations stay 480
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(result[idx].duration, 480u);
  }
}

TEST(RetrogradeMelodyTest, PreservesGaps) {
  // Create a motif with gaps between notes
  // Note 0: [0, 480), Note 1: [600, 1080), Note 2: [1200, 1680)
  // Gap01 = 600 - 480 = 120, Gap12 = 1200 - 1080 = 120
  std::vector<NoteEvent> gapped = {
      {0, 480, 60, 80, 0},
      {600, 480, 64, 80, 0},
      {1200, 480, 67, 80, 0},
  };
  auto result = retrogradeMelody(gapped, 0);
  ASSERT_EQ(result.size(), 3u);
  // Reversed: dur2=480, gap12=120, dur1=480, gap01=120, dur0=480
  EXPECT_EQ(result[0].start_tick, 0u);    // G4
  EXPECT_EQ(result[0].duration, 480u);
  EXPECT_EQ(result[1].start_tick, 600u);  // E4 (480 + 120 gap)
  EXPECT_EQ(result[1].duration, 480u);
  EXPECT_EQ(result[2].start_tick, 1200u); // C4 (600 + 480 + 120 gap)
  EXPECT_EQ(result[2].duration, 480u);
}

TEST(RetrogradeMelodyTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = retrogradeMelody(empty, 0);
  EXPECT_TRUE(result.empty());
}

TEST(RetrogradeMelodyTest, SingleNote) {
  std::vector<NoteEvent> single = {{100, 480, 60, 80, 0}};
  auto result = retrogradeMelody(single, 500);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].start_tick, 500u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[0].duration, 480u);
}

// ---------------------------------------------------------------------------
// AugmentMelody tests
// ---------------------------------------------------------------------------

TEST(AugmentMelodyTest, DoublesDuration) {
  auto motif = makeTestMotif(0);
  auto result = augmentMelody(motif, 0, 2);
  ASSERT_EQ(result.size(), 3u);
  // Durations doubled: 480 -> 960
  EXPECT_EQ(result[0].duration, 960u);
  EXPECT_EQ(result[1].duration, 960u);
  EXPECT_EQ(result[2].duration, 960u);
  // Tick positions scaled: 0->0, 480->960, 960->1920
  EXPECT_EQ(result[0].start_tick, 0u);
  EXPECT_EQ(result[1].start_tick, 960u);
  EXPECT_EQ(result[2].start_tick, 1920u);
}

TEST(AugmentMelodyTest, CustomFactor) {
  auto motif = makeTestMotif(0);
  auto result = augmentMelody(motif, 100, 3);
  ASSERT_EQ(result.size(), 3u);
  // Durations tripled: 480 -> 1440
  EXPECT_EQ(result[0].duration, 1440u);
  EXPECT_EQ(result[1].duration, 1440u);
  EXPECT_EQ(result[2].duration, 1440u);
  // Offsets tripled from start_tick=100: 0->100, 480->100+1440, 960->100+2880
  EXPECT_EQ(result[0].start_tick, 100u);
  EXPECT_EQ(result[1].start_tick, 1540u);
  EXPECT_EQ(result[2].start_tick, 2980u);
}

TEST(AugmentMelodyTest, PreservesPitch) {
  auto motif = makeTestMotif();
  auto result = augmentMelody(motif, 0, 2);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[1].pitch, 64);
  EXPECT_EQ(result[2].pitch, 67);
}

TEST(AugmentMelodyTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = augmentMelody(empty, 0, 2);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// DiminishMelody tests
// ---------------------------------------------------------------------------

TEST(DiminishMelodyTest, HalvesDuration) {
  auto motif = makeTestMotif(0);
  auto result = diminishMelody(motif, 0, 2);
  ASSERT_EQ(result.size(), 3u);
  // Durations halved: 480 -> 240
  EXPECT_EQ(result[0].duration, 240u);
  EXPECT_EQ(result[1].duration, 240u);
  EXPECT_EQ(result[2].duration, 240u);
  // Offsets halved: 0->0, 480->240, 960->480
  EXPECT_EQ(result[0].start_tick, 0u);
  EXPECT_EQ(result[1].start_tick, 240u);
  EXPECT_EQ(result[2].start_tick, 480u);
}

TEST(DiminishMelodyTest, MinimumOneTick) {
  // Duration of 1 diminished by 2 should still be 1 (not 0)
  std::vector<NoteEvent> tiny = {{0, 1, 60, 80, 0}};
  auto result = diminishMelody(tiny, 0, 2);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].duration, 1u);
}

TEST(DiminishMelodyTest, CustomFactor) {
  auto motif = makeTestMotif(0);
  auto result = diminishMelody(motif, 100, 4);
  ASSERT_EQ(result.size(), 3u);
  // Durations quartered: 480 -> 120
  EXPECT_EQ(result[0].duration, 120u);
  EXPECT_EQ(result[1].duration, 120u);
  EXPECT_EQ(result[2].duration, 120u);
  // Offsets quartered from start_tick=100: 0->100, 480->100+120, 960->100+240
  EXPECT_EQ(result[0].start_tick, 100u);
  EXPECT_EQ(result[1].start_tick, 220u);
  EXPECT_EQ(result[2].start_tick, 340u);
}

TEST(DiminishMelodyTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = diminishMelody(empty, 0, 2);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// TransposeMelody tests
// ---------------------------------------------------------------------------

TEST(TransposeMelodyTest, Up) {
  auto motif = makeTestMotif();
  auto result = transposeMelody(motif, 7);  // Up a perfect 5th
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 67);  // C4+7 = G4
  EXPECT_EQ(result[1].pitch, 71);  // E4+7 = B4
  EXPECT_EQ(result[2].pitch, 74);  // G4+7 = D5
}

TEST(TransposeMelodyTest, Down) {
  auto motif = makeTestMotif();
  auto result = transposeMelody(motif, -3);  // Down a minor 3rd
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 57);  // C4-3 = A3
  EXPECT_EQ(result[1].pitch, 61);  // E4-3 = Db4
  EXPECT_EQ(result[2].pitch, 64);  // G4-3 = E4(Eb4 = 63... wait, 67-3=64)
}

TEST(TransposeMelodyTest, ClampHigh) {
  std::vector<NoteEvent> high = {{0, 480, 120, 80, 0}, {480, 480, 125, 80, 0}};
  auto result = transposeMelody(high, 10);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 127);  // 120+10=130 -> clamped to 127
  EXPECT_EQ(result[1].pitch, 127);  // 125+10=135 -> clamped to 127
}

TEST(TransposeMelodyTest, ClampLow) {
  std::vector<NoteEvent> low = {{0, 480, 5, 80, 0}, {480, 480, 2, 80, 0}};
  auto result = transposeMelody(low, -10);
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].pitch, 0);  // 5-10=-5 -> clamped to 0
  EXPECT_EQ(result[1].pitch, 0);  // 2-10=-8 -> clamped to 0
}

TEST(TransposeMelodyTest, PreservesRhythm) {
  auto motif = makeTestMotif(1000);
  auto result = transposeMelody(motif, 5);
  ASSERT_EQ(result.size(), 3u);
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(result[idx].start_tick, motif[idx].start_tick);
    EXPECT_EQ(result[idx].duration, motif[idx].duration);
  }
}

TEST(TransposeMelodyTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = transposeMelody(empty, 7);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// GenerateSequence tests
// ---------------------------------------------------------------------------

TEST(GenerateSequenceTest, ThreeSteps) {
  auto motif = makeTestMotif(0);  // C4-E4-G4
  // 3 repetitions, -2 semitones each step
  auto result = generateSequence(motif, 3, -2, 1440);
  // 3 repetitions * 3 notes = 9 notes total
  ASSERT_EQ(result.size(), 9u);
  // Rep 1: transposed by -2 (Bb3, D4, F4)
  EXPECT_EQ(result[0].pitch, 58);
  EXPECT_EQ(result[1].pitch, 62);
  EXPECT_EQ(result[2].pitch, 65);
  // Rep 2: transposed by -4 (Ab3, C4, Eb4)
  EXPECT_EQ(result[3].pitch, 56);
  EXPECT_EQ(result[4].pitch, 60);
  EXPECT_EQ(result[5].pitch, 63);
  // Rep 3: transposed by -6 (Gb3, Bb3, Db4)
  EXPECT_EQ(result[6].pitch, 54);
  EXPECT_EQ(result[7].pitch, 58);
  EXPECT_EQ(result[8].pitch, 61);
}

TEST(GenerateSequenceTest, CorrectTiming) {
  auto motif = makeTestMotif(0);  // Total duration = 960 + 480 = 1440
  // Motif duration: max(0+480, 480+480, 960+480) - min(0) = 1440 - 0 = 1440
  auto result = generateSequence(motif, 2, -2, 1440);
  // Rep 1 starts at 1440 (offset = 1440 + 1440*0 - 0 = 1440)
  EXPECT_EQ(result[0].start_tick, 1440u);
  EXPECT_EQ(result[1].start_tick, 1920u);
  EXPECT_EQ(result[2].start_tick, 2400u);
  // Rep 2 starts at 1440 + 1440 = 2880
  EXPECT_EQ(result[3].start_tick, 2880u);
  EXPECT_EQ(result[4].start_tick, 3360u);
  EXPECT_EQ(result[5].start_tick, 3840u);
}

TEST(GenerateSequenceTest, EmptyMotif) {
  std::vector<NoteEvent> empty;
  auto result = generateSequence(empty, 3, -2, 0);
  EXPECT_TRUE(result.empty());
}

TEST(GenerateSequenceTest, ZeroRepetitions) {
  auto motif = makeTestMotif();
  auto result = generateSequence(motif, 0, -2, 0);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// MotifDuration tests
// ---------------------------------------------------------------------------

TEST(MotifDurationTest, Calculation) {
  auto motif = makeTestMotif(100);
  // Notes: [100, 580), [580, 1060), [1060, 1540)
  // Duration = 1540 - 100 = 1440
  EXPECT_EQ(motifDuration(motif), 1440u);
}

TEST(MotifDurationTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  EXPECT_EQ(motifDuration(empty), 0u);
}

TEST(MotifDurationTest, SingleNote) {
  std::vector<NoteEvent> single = {{0, 480, 60, 80, 0}};
  EXPECT_EQ(motifDuration(single), 480u);
}

TEST(MotifDurationTest, WithGaps) {
  // Notes with gaps: [0, 240), [480, 720)
  std::vector<NoteEvent> gapped = {
      {0, 240, 60, 80, 0},
      {480, 240, 64, 80, 0},
  };
  // Duration = 720 - 0 = 720
  EXPECT_EQ(motifDuration(gapped), 720u);
}

// ---------------------------------------------------------------------------
// InvertMelodyDiatonic tests
// ---------------------------------------------------------------------------

TEST(InvertMelodyDiatonicTest, DiatonicInversionCMajor) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  // Invert around C4(60) in C major.
  // C4 stays 60 (pivot).
  // E4 is +2 scale degrees from C -> inverted = -2 degrees from C = A3 = 57.
  // G4 is +4 scale degrees from C -> inverted = -4 degrees from C = F3 = 53.
  auto result = invertMelodyDiatonic(motif, 60, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 60);  // C4 (pivot)
  EXPECT_EQ(result[1].pitch, 57);  // A3
  EXPECT_EQ(result[2].pitch, 53);  // F3
}

TEST(InvertMelodyDiatonicTest, AllOutputsAreDiatonic) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  auto result = invertMelodyDiatonic(motif, 60, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  for (size_t idx = 0; idx < result.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(result[idx].pitch, Key::C, ScaleType::Major))
        << "Pitch " << static_cast<int>(result[idx].pitch) << " at index " << idx
        << " is not a C major scale tone";
  }
}

TEST(InvertMelodyDiatonicTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = invertMelodyDiatonic(empty, 60, Key::C, ScaleType::Major);
  EXPECT_TRUE(result.empty());
}

TEST(InvertMelodyDiatonicTest, PreservesRhythm) {
  auto motif = makeTestMotif(1920);
  auto result = invertMelodyDiatonic(motif, 60, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  for (size_t idx = 0; idx < 3; ++idx) {
    EXPECT_EQ(result[idx].start_tick, motif[idx].start_tick);
    EXPECT_EQ(result[idx].duration, motif[idx].duration);
    EXPECT_EQ(result[idx].velocity, motif[idx].velocity);
    EXPECT_EQ(result[idx].voice, motif[idx].voice);
  }
}

// ---------------------------------------------------------------------------
// TransposeMelodyDiatonic tests
// ---------------------------------------------------------------------------

TEST(TransposeMelodyDiatonicTest, StepDown) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  // Transpose by -1 degree step in C major.
  // C4(60) -> B3(59), E4(64) -> D4(62), G4(67) -> F4(65)
  auto result = transposeMelodyDiatonic(motif, -1, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 59);  // B3
  EXPECT_EQ(result[1].pitch, 62);  // D4
  EXPECT_EQ(result[2].pitch, 65);  // F4
}

TEST(TransposeMelodyDiatonicTest, StepUp) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  // Transpose by +1 degree step in C major.
  // C4(60) -> D4(62), E4(64) -> F4(65), G4(67) -> A4(69)
  auto result = transposeMelodyDiatonic(motif, 1, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  EXPECT_EQ(result[0].pitch, 62);  // D4
  EXPECT_EQ(result[1].pitch, 65);  // F4
  EXPECT_EQ(result[2].pitch, 69);  // A4
}

TEST(TransposeMelodyDiatonicTest, AllOutputsAreDiatonic) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  auto result = transposeMelodyDiatonic(motif, 2, Key::C, ScaleType::Major);
  ASSERT_EQ(result.size(), 3u);
  for (size_t idx = 0; idx < result.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(result[idx].pitch, Key::C, ScaleType::Major))
        << "Pitch " << static_cast<int>(result[idx].pitch) << " at index " << idx
        << " is not a C major scale tone";
  }
}

TEST(TransposeMelodyDiatonicTest, EmptyInput) {
  std::vector<NoteEvent> empty;
  auto result = transposeMelodyDiatonic(empty, 1, Key::C, ScaleType::Major);
  EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// GenerateDiatonicSequence tests
// ---------------------------------------------------------------------------

TEST(GenerateDiatonicSequenceTest, FourRepsAllDiatonic) {
  auto motif = makeTestMotif();  // C4(60)-E4(64)-G4(67)
  // 4 repetitions, degree_step=-1 in C major. All notes should be diatonic.
  auto result = generateDiatonicSequence(motif, 4, -1, 1440, Key::C, ScaleType::Major);
  // 4 repetitions * 3 notes = 12 notes
  ASSERT_EQ(result.size(), 12u);
  for (size_t idx = 0; idx < result.size(); ++idx) {
    EXPECT_TRUE(scale_util::isScaleTone(result[idx].pitch, Key::C, ScaleType::Major))
        << "Pitch " << static_cast<int>(result[idx].pitch) << " at index " << idx
        << " is not a C major scale tone";
  }
}

TEST(GenerateDiatonicSequenceTest, CorrectTiming) {
  auto motif = makeTestMotif(0);  // Total duration = 1440
  // Motif duration: max(0+480, 480+480, 960+480) - min(0) = 1440
  auto result = generateDiatonicSequence(motif, 2, -1, 1440, Key::C, ScaleType::Major);
  // Rep 1 starts at 1440
  EXPECT_EQ(result[0].start_tick, 1440u);
  EXPECT_EQ(result[1].start_tick, 1920u);
  EXPECT_EQ(result[2].start_tick, 2400u);
  // Rep 2 starts at 1440 + 1440 = 2880
  EXPECT_EQ(result[3].start_tick, 2880u);
  EXPECT_EQ(result[4].start_tick, 3360u);
  EXPECT_EQ(result[5].start_tick, 3840u);
}

TEST(GenerateDiatonicSequenceTest, EmptyMotif) {
  std::vector<NoteEvent> empty;
  auto result = generateDiatonicSequence(empty, 3, -1, 0, Key::C, ScaleType::Major);
  EXPECT_TRUE(result.empty());
}

TEST(GenerateDiatonicSequenceTest, ZeroRepetitions) {
  auto motif = makeTestMotif();
  auto result = generateDiatonicSequence(motif, 0, -1, 0, Key::C, ScaleType::Major);
  EXPECT_TRUE(result.empty());
}

}  // namespace
}  // namespace bach
