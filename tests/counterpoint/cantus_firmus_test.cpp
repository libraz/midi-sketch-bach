// Tests for counterpoint/cantus_firmus.h -- Fux-style cantus firmus
// generation: tonic start/end, stepwise motion, single climax, range.

#include "counterpoint/cantus_firmus.h"

#include <gtest/gtest.h>

#include "core/pitch_utils.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// CantusFirmus struct
// ---------------------------------------------------------------------------

TEST(CantusFirmusTest, EmptyDefaults) {
  CantusFirmus cantus;
  EXPECT_EQ(cantus.noteCount(), 0u);
  EXPECT_EQ(cantus.lowestPitch(), 127);
  EXPECT_EQ(cantus.highestPitch(), 0);
}

TEST(CantusFirmusTest, NoteCountMatchesNotes) {
  CantusFirmus cantus;
  cantus.notes.push_back({0, kTicksPerBar, 60, 80, 0});
  cantus.notes.push_back({kTicksPerBar, kTicksPerBar, 62, 80, 0});
  EXPECT_EQ(cantus.noteCount(), 2u);
}

TEST(CantusFirmusTest, LowestAndHighestPitch) {
  CantusFirmus cantus;
  cantus.notes.push_back({0, kTicksPerBar, 60, 80, 0});
  cantus.notes.push_back({kTicksPerBar, kTicksPerBar, 67, 80, 0});
  cantus.notes.push_back({kTicksPerBar * 2, kTicksPerBar, 55, 80, 0});

  EXPECT_EQ(cantus.lowestPitch(), 55);
  EXPECT_EQ(cantus.highestPitch(), 67);
}

// ---------------------------------------------------------------------------
// CantusFirmusGenerator
// ---------------------------------------------------------------------------

class CantusFirmusGeneratorTest : public ::testing::Test {
 protected:
  CantusFirmusGenerator generator;
};

TEST_F(CantusFirmusGeneratorTest, GeneratesNonEmpty) {
  CantusFirmus cantus = generator.generate(Key::C, 4, 42);
  EXPECT_GT(cantus.noteCount(), 0u);
}

TEST_F(CantusFirmusGeneratorTest, CorrectNoteCount) {
  CantusFirmus cantus4 = generator.generate(Key::C, 4, 42);
  EXPECT_EQ(cantus4.noteCount(), 4u);

  CantusFirmus cantus6 = generator.generate(Key::C, 6, 42);
  EXPECT_EQ(cantus6.noteCount(), 6u);

  CantusFirmus cantus8 = generator.generate(Key::C, 8, 42);
  EXPECT_EQ(cantus8.noteCount(), 8u);
}

TEST_F(CantusFirmusGeneratorTest, LengthClampedToMin4) {
  CantusFirmus cantus = generator.generate(Key::C, 2, 42);
  EXPECT_EQ(cantus.noteCount(), 4u);
}

TEST_F(CantusFirmusGeneratorTest, LengthClampedToMax8) {
  CantusFirmus cantus = generator.generate(Key::C, 12, 42);
  EXPECT_EQ(cantus.noteCount(), 8u);
}

TEST_F(CantusFirmusGeneratorTest, AllWholeNotes) {
  CantusFirmus cantus = generator.generate(Key::C, 6, 42);
  for (const auto& note : cantus.notes) {
    EXPECT_EQ(note.duration, kTicksPerBar)
        << "Cantus firmus notes should be whole notes";
  }
}

TEST_F(CantusFirmusGeneratorTest, StartsOnTonic) {
  CantusFirmus cantus = generator.generate(Key::C, 6, 42);
  ASSERT_GT(cantus.noteCount(), 0u);

  int first_pc = getPitchClass(cantus.notes.front().pitch);
  int tonic_pc = static_cast<int>(Key::C) % 12;
  EXPECT_EQ(first_pc, tonic_pc) << "CF should start on tonic";
}

TEST_F(CantusFirmusGeneratorTest, EndsOnTonic) {
  CantusFirmus cantus = generator.generate(Key::C, 6, 42);
  ASSERT_GT(cantus.noteCount(), 0u);

  int last_pc = getPitchClass(cantus.notes.back().pitch);
  int tonic_pc = static_cast<int>(Key::C) % 12;
  EXPECT_EQ(last_pc, tonic_pc) << "CF should end on tonic";
}

TEST_F(CantusFirmusGeneratorTest, StartsAndEndsOnTonicGKey) {
  CantusFirmus cantus = generator.generate(Key::G, 6, 42);
  ASSERT_GT(cantus.noteCount(), 0u);

  int tonic_pc = static_cast<int>(Key::G) % 12;  // 7
  EXPECT_EQ(getPitchClass(cantus.notes.front().pitch), tonic_pc);
  EXPECT_EQ(getPitchClass(cantus.notes.back().pitch), tonic_pc);
}

TEST_F(CantusFirmusGeneratorTest, SingleClimaxPoint) {
  // The highest pitch should appear exactly once.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    CantusFirmus cantus = generator.generate(Key::C, 6, seed);
    uint8_t highest = cantus.highestPitch();

    int peak_count = 0;
    for (const auto& note : cantus.notes) {
      if (note.pitch == highest) peak_count++;
    }

    EXPECT_EQ(peak_count, 1)
        << "CF should have exactly one climax (seed " << seed << ")";
  }
}

TEST_F(CantusFirmusGeneratorTest, RangeWithinOctave) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    CantusFirmus cantus = generator.generate(Key::C, 6, seed);
    int range = static_cast<int>(cantus.highestPitch()) -
                static_cast<int>(cantus.lowestPitch());
    EXPECT_LE(range, 12)
        << "CF range should be within an octave (seed " << seed << ")";
  }
}

TEST_F(CantusFirmusGeneratorTest, MostlyStepwiseMotion) {
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    CantusFirmus cantus = generator.generate(Key::C, 6, seed);
    int step_count = 0;
    int total_intervals = 0;

    for (size_t idx = 1; idx < cantus.notes.size(); ++idx) {
      int abs_interval = absoluteInterval(cantus.notes[idx].pitch,
                                          cantus.notes[idx - 1].pitch);
      total_intervals++;
      // Steps are 1-2 semitones; thirds are 3-4.
      if (abs_interval <= 4) step_count++;
    }

    if (total_intervals > 0) {
      float step_ratio = static_cast<float>(step_count) / total_intervals;
      EXPECT_GT(step_ratio, 0.5f)
          << "CF should be mostly stepwise (seed " << seed
          << ", ratio=" << step_ratio << ")";
    }
  }
}

TEST_F(CantusFirmusGeneratorTest, NotesInSequentialOrder) {
  CantusFirmus cantus = generator.generate(Key::C, 6, 42);
  for (size_t idx = 1; idx < cantus.notes.size(); ++idx) {
    EXPECT_GT(cantus.notes[idx].start_tick, cantus.notes[idx - 1].start_tick);
  }
}

TEST_F(CantusFirmusGeneratorTest, PreservesKey) {
  CantusFirmus cantus = generator.generate(Key::D, 5, 42);
  EXPECT_EQ(cantus.key, Key::D);
}

TEST_F(CantusFirmusGeneratorTest, DeterministicWithSameSeed) {
  CantusFirmus cf1 = generator.generate(Key::C, 6, 100);
  CantusFirmus cf2 = generator.generate(Key::C, 6, 100);

  ASSERT_EQ(cf1.noteCount(), cf2.noteCount());
  for (size_t idx = 0; idx < cf1.noteCount(); ++idx) {
    EXPECT_EQ(cf1.notes[idx].pitch, cf2.notes[idx].pitch);
    EXPECT_EQ(cf1.notes[idx].start_tick, cf2.notes[idx].start_tick);
  }
}

TEST_F(CantusFirmusGeneratorTest, DifferentSeedsDifferentResults) {
  CantusFirmus cf1 = generator.generate(Key::C, 6, 1);
  CantusFirmus cf2 = generator.generate(Key::C, 6, 999);

  bool all_same = (cf1.noteCount() == cf2.noteCount());
  if (all_same) {
    for (size_t idx = 0; idx < cf1.noteCount(); ++idx) {
      if (cf1.notes[idx].pitch != cf2.notes[idx].pitch) {
        all_same = false;
        break;
      }
    }
  }
  EXPECT_FALSE(all_same);
}

TEST_F(CantusFirmusGeneratorTest, PitchesInValidRange) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    CantusFirmus cantus = generator.generate(Key::C, 6, seed);
    for (const auto& note : cantus.notes) {
      EXPECT_GE(note.pitch, 36);
      EXPECT_LE(note.pitch, 96);
    }
  }
}

}  // namespace
}  // namespace bach
