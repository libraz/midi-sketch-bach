// Tests for fugue/subject.h -- subject generation and structural properties.

#include "fugue/subject.h"

#include <gtest/gtest.h>

#include "core/pitch_utils.h"
#include "fugue/fugue_config.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Subject struct member functions
// ---------------------------------------------------------------------------

TEST(SubjectTest, EmptySubjectDefaults) {
  Subject subject;
  EXPECT_EQ(subject.noteCount(), 0u);
  EXPECT_EQ(subject.lowestPitch(), 127);
  EXPECT_EQ(subject.highestPitch(), 0);
  EXPECT_EQ(subject.range(), 0);
}

TEST(SubjectTest, SingleNote) {
  Subject subject;
  subject.notes.push_back({0, 480, 60, 80, 0});

  EXPECT_EQ(subject.noteCount(), 1u);
  EXPECT_EQ(subject.lowestPitch(), 60);
  EXPECT_EQ(subject.highestPitch(), 60);
  EXPECT_EQ(subject.range(), 0);
}

TEST(SubjectTest, RangeCalculation) {
  Subject subject;
  subject.notes.push_back({0, 480, 55, 80, 0});
  subject.notes.push_back({480, 480, 60, 80, 0});
  subject.notes.push_back({960, 480, 67, 80, 0});

  EXPECT_EQ(subject.lowestPitch(), 55);
  EXPECT_EQ(subject.highestPitch(), 67);
  EXPECT_EQ(subject.range(), 12);
}

// ---------------------------------------------------------------------------
// SubjectGenerator -- basic generation properties
// ---------------------------------------------------------------------------

class SubjectGeneratorTest : public ::testing::Test {
 protected:
  SubjectGenerator generator;
  FugueConfig config;

  void SetUp() override {
    config.key = Key::C;
    config.subject_bars = 2;
    config.seed = 42;
  }
};

TEST_F(SubjectGeneratorTest, GeneratesNonEmptySubject) {
  Subject subject = generator.generate(config, 42);
  EXPECT_GT(subject.noteCount(), 0u);
}

TEST_F(SubjectGeneratorTest, SubjectLength) {
  config.subject_bars = 2;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 2u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectLengthThreeBars) {
  config.subject_bars = 3;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 3u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectLengthFourBars) {
  config.subject_bars = 4;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 4u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectBarsClampedToMinTwo) {
  config.subject_bars = 1;  // Below minimum.
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 2u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectBarsClampedToMaxFour) {
  config.subject_bars = 10;  // Above maximum.
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 4u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, NotesDoNotExceedTotalLength) {
  Subject subject = generator.generate(config, 42);
  Tick last_end = 0;
  for (const auto& note : subject.notes) {
    Tick note_end = note.start_tick + note.duration;
    EXPECT_LE(note_end, subject.length_ticks)
        << "Note at tick " << note.start_tick << " exceeds subject length";
    if (note_end > last_end) last_end = note_end;
  }
}

TEST_F(SubjectGeneratorTest, EndsOnTonic) {
  config.key = Key::C;
  Subject subject = generator.generate(config, 42);
  ASSERT_GT(subject.noteCount(), 0u);

  // Last note should be on the tonic pitch class (C = 0).
  int last_pc = getPitchClass(subject.notes.back().pitch);
  int tonic_pc = static_cast<int>(config.key) % 12;
  EXPECT_EQ(last_pc, tonic_pc) << "Subject should end on the tonic";
}

TEST_F(SubjectGeneratorTest, StartsOnTonicOrDominant) {
  config.key = Key::C;
  Subject subject = generator.generate(config, 42);
  ASSERT_GT(subject.noteCount(), 0u);

  int first_pc = getPitchClass(subject.notes.front().pitch);
  int tonic_pc = static_cast<int>(config.key) % 12;     // 0 = C
  int dominant_pc = (tonic_pc + 7) % 12;  // 7 = G

  bool starts_ok = (first_pc == tonic_pc || first_pc == dominant_pc);
  EXPECT_TRUE(starts_ok) << "Subject should start on tonic or dominant";
}

TEST_F(SubjectGeneratorTest, PreservesKey) {
  config.key = Key::G;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.key, Key::G);
}

TEST_F(SubjectGeneratorTest, PreservesCharacter) {
  config.character = SubjectCharacter::Playful;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.character, SubjectCharacter::Playful);
}

// ---------------------------------------------------------------------------
// Character-specific constraints
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, SevereCharacterNarrowRange) {
  config.character = SubjectCharacter::Severe;
  // Test across multiple seeds.
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Subject subject = generator.generate(config, seed);
    EXPECT_LE(subject.range(), 12)
        << "Severe subject range should not exceed 12 semitones (seed "
        << seed << ")";
  }
}

TEST_F(SubjectGeneratorTest, PlayfulCharacterRange) {
  config.character = SubjectCharacter::Playful;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Subject subject = generator.generate(config, seed);
    EXPECT_LE(subject.range(), 16)
        << "Playful subject range should not exceed 16 semitones (seed "
        << seed << ")";
  }
}

// ---------------------------------------------------------------------------
// Noble character
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, NobleCharacterRange) {
  config.character = SubjectCharacter::Noble;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Subject subject = generator.generate(config, seed);
    EXPECT_LE(subject.range(), 14)
        << "Noble subject range should not exceed 14 semitones (seed "
        << seed << ")";
  }
}

TEST_F(SubjectGeneratorTest, NobleCharacterPreservesCharacter) {
  config.character = SubjectCharacter::Noble;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.character, SubjectCharacter::Noble);
}

TEST_F(SubjectGeneratorTest, NobleCharacterPrefersLongNotes) {
  config.character = SubjectCharacter::Noble;
  config.subject_bars = 3;
  // Noble subjects should have relatively fewer notes (longer durations).
  // Compared to Restless, Noble should have fewer notes per bar.
  int noble_total = 0;
  int restless_total = 0;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    config.character = SubjectCharacter::Noble;
    Subject noble_subj = generator.generate(config, seed);
    noble_total += static_cast<int>(noble_subj.noteCount());

    config.character = SubjectCharacter::Restless;
    Subject restless_subj = generator.generate(config, seed);
    restless_total += static_cast<int>(restless_subj.noteCount());
  }
  // On average across 5 seeds, Noble should have fewer notes than Restless.
  EXPECT_LT(noble_total, restless_total)
      << "Noble subjects should have fewer notes (longer values) than Restless";
}

TEST_F(SubjectGeneratorTest, NobleCharacterDownwardTendency) {
  config.character = SubjectCharacter::Noble;
  int descending_count = 0;
  int ascending_count = 0;

  // Across multiple seeds, Noble should show more descending intervals.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      int motion = static_cast<int>(subject.notes[idx].pitch) -
                   static_cast<int>(subject.notes[idx - 1].pitch);
      if (motion < 0) descending_count++;
      if (motion > 0) ascending_count++;
    }
  }
  // Noble should have more descending than ascending intervals on average.
  EXPECT_GE(descending_count, ascending_count)
      << "Noble character should favor downward melodic motion";
}

// ---------------------------------------------------------------------------
// Restless character
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, RestlessCharacterRange) {
  config.character = SubjectCharacter::Restless;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Subject subject = generator.generate(config, seed);
    EXPECT_LE(subject.range(), 16)
        << "Restless subject range should not exceed 16 semitones (seed "
        << seed << ")";
  }
}

TEST_F(SubjectGeneratorTest, RestlessCharacterPreservesCharacter) {
  config.character = SubjectCharacter::Restless;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.character, SubjectCharacter::Restless);
}

TEST_F(SubjectGeneratorTest, RestlessCharacterUsesShortNotes) {
  config.character = SubjectCharacter::Restless;
  config.subject_bars = 2;
  // Restless should have more notes per bar than Severe (shorter values).
  int restless_total = 0;
  int severe_total = 0;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    config.character = SubjectCharacter::Restless;
    Subject restless_subj = generator.generate(config, seed);
    restless_total += static_cast<int>(restless_subj.noteCount());

    config.character = SubjectCharacter::Severe;
    Subject severe_subj = generator.generate(config, seed);
    severe_total += static_cast<int>(severe_subj.noteCount());
  }
  EXPECT_GT(restless_total, severe_total)
      << "Restless subjects should have more notes (shorter values) than Severe";
}

TEST_F(SubjectGeneratorTest, RestlessCharacterHasChromaticMotion) {
  config.character = SubjectCharacter::Restless;
  int semitone_steps = 0;
  int total_intervals = 0;

  // Across multiple seeds, Restless should have more semitone intervals.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      int abs_interval = std::abs(static_cast<int>(subject.notes[idx].pitch) -
                                  static_cast<int>(subject.notes[idx - 1].pitch));
      total_intervals++;
      if (abs_interval == 1) semitone_steps++;
    }
  }
  // Restless should have a non-trivial number of semitone steps.
  EXPECT_GT(semitone_steps, 0)
      << "Restless character should include chromatic (semitone) motion";
  // At least 5% of intervals should be semitones across 10 seeds.
  float ratio = static_cast<float>(semitone_steps) /
                static_cast<float>(total_intervals);
  EXPECT_GT(ratio, 0.05f)
      << "Restless should have at least 5% semitone intervals, got "
      << (ratio * 100.0f) << "%";
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, DeterministicWithSameSeed) {
  Subject sub1 = generator.generate(config, 100);
  Subject sub2 = generator.generate(config, 100);

  ASSERT_EQ(sub1.noteCount(), sub2.noteCount());
  for (size_t idx = 0; idx < sub1.noteCount(); ++idx) {
    EXPECT_EQ(sub1.notes[idx].pitch, sub2.notes[idx].pitch);
    EXPECT_EQ(sub1.notes[idx].duration, sub2.notes[idx].duration);
    EXPECT_EQ(sub1.notes[idx].start_tick, sub2.notes[idx].start_tick);
  }
}

TEST_F(SubjectGeneratorTest, DifferentSeedsProduceDifferentResults) {
  Subject sub1 = generator.generate(config, 1);
  Subject sub2 = generator.generate(config, 999);

  // Very unlikely to be identical with different seeds.
  bool all_same = (sub1.noteCount() == sub2.noteCount());
  if (all_same) {
    for (size_t idx = 0; idx < sub1.noteCount(); ++idx) {
      if (sub1.notes[idx].pitch != sub2.notes[idx].pitch) {
        all_same = false;
        break;
      }
    }
  }
  EXPECT_FALSE(all_same) << "Different seeds should produce different subjects";
}

// ---------------------------------------------------------------------------
// Pitch range safety
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, PitchesInValidMidiRange) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    for (const auto& note : subject.notes) {
      EXPECT_GE(note.pitch, 36) << "Pitch below minimum (seed " << seed << ")";
      EXPECT_LE(note.pitch, 96) << "Pitch above maximum (seed " << seed << ")";
    }
  }
}

}  // namespace
}  // namespace bach
