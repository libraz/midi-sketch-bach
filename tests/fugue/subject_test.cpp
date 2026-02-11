// Tests for fugue/subject.h -- subject generation and structural properties.

#include "fugue/subject.h"

#include <cmath>
#include <set>

#include <gtest/gtest.h>

#include "core/pitch_utils.h"
#include "core/scale.h"
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

TEST_F(SubjectGeneratorTest, EndsOnTonicOrDominant) {
  config.key = Key::C;
  Subject subject = generator.generate(config, 42);
  ASSERT_GT(subject.noteCount(), 0u);

  // Last note should be on the tonic pitch class (C = 0) or dominant (G = 7).
  int last_pc = getPitchClass(subject.notes.back().pitch);
  int tonic_pc = static_cast<int>(config.key) % 12;
  int dominant_pc = (tonic_pc + 7) % 12;
  bool ends_ok = (last_pc == tonic_pc || last_pc == dominant_pc);
  EXPECT_TRUE(ends_ok)
      << "Subject should end on tonic or dominant, got pitch class " << last_pc;
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

TEST_F(SubjectGeneratorTest, NobleCharacterHasLateClimax) {
  config.character = SubjectCharacter::Noble;
  // Noble subjects should reach their highest pitch in the latter portion
  // (delayed climax = stately character). The GoalTone position_ratio is 0.70.
  // We check by tick position rather than note index, since Noble templates
  // can produce long notes that make index-based position unreliable.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    ASSERT_GT(subject.notes.size(), 2u);

    // Find the tick position of the highest note.
    Tick climax_tick = 0;
    uint8_t highest = 0;
    for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
      if (subject.notes[idx].pitch > highest) {
        highest = subject.notes[idx].pitch;
        climax_tick = subject.notes[idx].start_tick;
      }
    }
    // The climax should not be at the very beginning. With interval fluctuation
    // (+/-1 degree, 40% probability), the highest note may shift earlier than
    // the designed GoalTone position, so we use a lenient threshold of 15%.
    float climax_position =
        static_cast<float>(climax_tick) / static_cast<float>(subject.length_ticks);
    EXPECT_GE(climax_position, 0.15f)
        << "Noble subject climax should not be at the very start (seed "
        << seed << ", tick " << climax_tick << "/" << subject.length_ticks << ")";
  }
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

TEST_F(SubjectGeneratorTest, RestlessCharacterHasDenseIntervalVariety) {
  config.character = SubjectCharacter::Restless;
  int distinct_intervals = 0;

  // Across multiple seeds, Restless should have diverse interval content
  // (many different interval sizes) reflecting its nervous, jittery character.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    std::set<int> interval_set;
    for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
      int interval = static_cast<int>(subject.notes[idx].pitch) -
                     static_cast<int>(subject.notes[idx - 1].pitch);
      interval_set.insert(interval);
    }
    distinct_intervals += static_cast<int>(interval_set.size());
  }
  // On average across 10 seeds, Restless should use at least 3 distinct
  // interval types per subject (reflecting varied, restless motion).
  float avg_distinct = static_cast<float>(distinct_intervals) / 10.0f;
  EXPECT_GE(avg_distinct, 3.0f)
      << "Restless should use diverse interval types, got avg "
      << avg_distinct;
}

// ---------------------------------------------------------------------------
// Subject diversity (template selection + RNG variation)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, DifferentSeedsProduceDiverseSubjects) {
  // Across 20 seeds, subjects should vary in note count and pitch content.
  // The expanded templates + RNG decision points should prevent near-identical
  // subjects across all seeds.
  config.character = SubjectCharacter::Severe;
  config.subject_bars = 2;

  std::set<size_t> note_counts;
  std::set<int> first_pitches;
  std::set<int> last_pitches;

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Subject subject = generator.generate(config, seed);
    note_counts.insert(subject.noteCount());
    if (!subject.notes.empty()) {
      first_pitches.insert(subject.notes.front().pitch);
      last_pitches.insert(subject.notes.back().pitch);
    }
  }

  // With 4 template pairs + interval fluctuation + ending variation, we
  // expect at least 2 distinct first pitches and 2 distinct last pitches
  // across 20 seeds. Note counts may be uniform since pair substitution
  // preserves total duration by design.
  EXPECT_GE(first_pitches.size(), 2u)
      << "Expected diverse first pitches across 20 seeds";
  EXPECT_GE(last_pitches.size(), 2u)
      << "Expected diverse last pitches across 20 seeds (tonic vs dominant)";
}

TEST_F(SubjectGeneratorTest, EndsOnTonicOrDominantAllCharacters) {
  // Across all characters and many seeds, last note should always be tonic or dominant.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Subject subject = generator.generate(config, seed);
      ASSERT_GT(subject.noteCount(), 0u);

      int last_pc = getPitchClass(subject.notes.back().pitch);
      int tonic_pc = static_cast<int>(config.key) % 12;
      int dominant_pc = (tonic_pc + 7) % 12;
      bool ends_ok = (last_pc == tonic_pc || last_pc == dominant_pc);
      EXPECT_TRUE(ends_ok)
          << "Subject should end on tonic or dominant for character "
          << static_cast<int>(chr) << " seed " << seed
          << ", got pitch class " << last_pc;
    }
  }
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

// ---------------------------------------------------------------------------
// Anacrusis support [Task C]
// ---------------------------------------------------------------------------

TEST(SubjectAnacrusisTest, PlayfulHasHighAnacrusisProbability) {
  // Playful character has 70% anacrusis probability.
  // With many seeds, a significant portion should have anacrusis.
  SubjectGenerator gen;
  int anacrusis_count = 0;
  constexpr int kTrials = 50;

  for (int trial = 0; trial < kTrials; ++trial) {
    FugueConfig config;
    config.character = SubjectCharacter::Playful;
    config.key = Key::C;
    config.subject_bars = 2;

    auto subject = gen.generate(config, static_cast<uint32_t>(trial * 100));
    if (subject.anacrusis_ticks > 0) {
      anacrusis_count++;
    }
  }
  // With 70% probability, expect at least 20% to have anacrusis (conservative).
  EXPECT_GT(anacrusis_count, kTrials / 5)
      << "Expected many Playful subjects to have anacrusis";
}

TEST(SubjectAnacrusisTest, AnacrusisExtendsTotalLength) {
  SubjectGenerator gen;

  FugueConfig config;
  config.character = SubjectCharacter::Playful;
  config.key = Key::C;
  config.subject_bars = 2;

  // Try many seeds to find one with anacrusis.
  for (uint32_t seed = 0; seed < 100; ++seed) {
    auto subject = gen.generate(config, seed);
    if (subject.anacrusis_ticks > 0) {
      // Total length should be bars*kTicksPerBar + anacrusis_ticks.
      EXPECT_GT(subject.length_ticks,
                static_cast<Tick>(config.subject_bars) * kTicksPerBar);
      break;
    }
  }
}

TEST(SubjectAnacrusisTest, NoAnacrusis_ZeroTicks) {
  SubjectGenerator gen;

  FugueConfig config;
  config.character = SubjectCharacter::Severe;
  config.key = Key::C;
  config.subject_bars = 2;

  // Many Severe subjects should have no anacrusis (only 30% probability).
  // Find one without.
  for (uint32_t seed = 0; seed < 100; ++seed) {
    auto subject = gen.generate(config, seed);
    if (subject.anacrusis_ticks == 0) {
      EXPECT_EQ(subject.length_ticks,
                static_cast<Tick>(config.subject_bars) * kTicksPerBar);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Beat-grid alignment (Fix 1: no beat displacement)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, NotesAlignToBeatGrid) {
  // All note start_ticks should fall on standard subdivisions:
  // multiples of sixteenth note (kTicksPerBeat / 4 = 120 ticks).
  constexpr Tick kSixteenth = kTicksPerBeat / 4;
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Subject subject = generator.generate(config, seed);
      for (const auto& note : subject.notes) {
        EXPECT_EQ(note.start_tick % kSixteenth, 0u)
            << "Note at tick " << note.start_tick
            << " not on 16th-note grid (character "
            << static_cast<int>(chr) << " seed " << seed << ")";
      }
    }
  }
}

TEST_F(SubjectGeneratorTest, NotesDontOverlap) {
  // Adjacent notes should not overlap in time within the subject.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Subject subject = generator.generate(config, seed);
      for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
        Tick prev_end =
            subject.notes[idx - 1].start_tick + subject.notes[idx - 1].duration;
        EXPECT_LE(prev_end, subject.notes[idx].start_tick)
            << "Notes overlap at index " << idx << " (character "
            << static_cast<int>(chr) << " seed " << seed << ")";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// All-diatonic pitches (Fix 2: no chromatic errors)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, AllPitchesAreDiatonic) {
  // Every pitch in the subject should belong to the scale of the key.
  // Test C major and G minor.
  config.key = Key::C;
  config.is_minor = false;
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      Subject subject = generator.generate(config, seed);
      for (const auto& note : subject.notes) {
        EXPECT_TRUE(scale_util::isScaleTone(note.pitch, config.key,
                                            ScaleType::Major))
            << "Non-diatonic pitch " << static_cast<int>(note.pitch)
            << " (" << pitchToNoteName(note.pitch) << ") in C major subject"
            << " (character " << static_cast<int>(chr) << " seed " << seed
            << ")";
      }
    }
  }
}

TEST_F(SubjectGeneratorTest, AllPitchesAreDiatonicMinor) {
  config.key = Key::G;
  config.is_minor = true;
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Subject subject = generator.generate(config, seed);
      for (const auto& note : subject.notes) {
        EXPECT_TRUE(scale_util::isScaleTone(note.pitch, config.key,
                                            ScaleType::HarmonicMinor))
            << "Non-diatonic pitch " << static_cast<int>(note.pitch)
            << " (" << pitchToNoteName(note.pitch) << ") in G minor subject"
            << " (character " << static_cast<int>(chr) << " seed " << seed
            << ")";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Climax on strong beat (Fix 4)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, ClimaxOnStrongBeat) {
  // The designed climax note is quantized to beat 1 or beat 3. Due to interval
  // fluctuation, the absolute highest pitch may occasionally shift to a
  // non-strong beat. We verify statistically: across many seeds and all
  // characters, the majority of highest pitches fall on strong beats.
  constexpr Tick kBeat3Offset = kTicksPerBeat * 2;
  int total_cases = 0;
  int on_strong_beat = 0;

  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    config.subject_bars = 3;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      Subject subject = generator.generate(config, seed);
      ASSERT_GT(subject.notes.size(), 2u);

      // Find the tick of the highest note.
      Tick highest_tick = 0;
      uint8_t highest_pitch = 0;
      for (const auto& note : subject.notes) {
        if (note.pitch > highest_pitch) {
          highest_pitch = note.pitch;
          highest_tick = note.start_tick;
        }
      }

      Tick pos_in_bar = highest_tick % kTicksPerBar;
      bool on_beat1 = (pos_in_bar == 0);
      bool on_beat3 = (pos_in_bar == kBeat3Offset);
      if (on_beat1 || on_beat3) ++on_strong_beat;
      ++total_cases;
    }
  }

  // With climax quantization, expect at least 40% of highest pitches on
  // strong beats (vs ~12.5% chance for random 16th-note positions).
  float ratio = static_cast<float>(on_strong_beat) /
                static_cast<float>(total_cases);
  EXPECT_GE(ratio, 0.40f)
      << "Expected at least 40% of climax notes on strong beats, got "
      << on_strong_beat << "/" << total_cases << " (" << (ratio * 100.0f)
      << "%)";
}

// ---------------------------------------------------------------------------
// Ending note preference (Fix 7: 70% dominant)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, EndingPrefersDominant) {
  // Across many seeds, the dominant ending should be more frequent than tonic.
  config.key = Key::C;
  int dominant_count = 0;
  int tonic_count = 0;
  constexpr int kTrials = 100;
  int tonic_pc = static_cast<int>(config.key) % 12;
  int dominant_pc = (tonic_pc + 7) % 12;

  for (uint32_t seed = 1; seed <= kTrials; ++seed) {
    Subject subject = generator.generate(config, seed);
    ASSERT_GT(subject.noteCount(), 0u);
    int last_pc = getPitchClass(subject.notes.back().pitch);
    if (last_pc == dominant_pc) ++dominant_count;
    else if (last_pc == tonic_pc) ++tonic_count;
  }
  // With 70% dominant, expect dominant > tonic across 100 seeds.
  EXPECT_GT(dominant_count, tonic_count)
      << "Expected dominant endings (" << dominant_count
      << ") > tonic endings (" << tonic_count << ")";
}

}  // namespace
}  // namespace bach
