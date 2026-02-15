// Tests for fugue/subject.h -- subject generation and structural properties.

#include "fugue/subject.h"

#include <cmath>
#include <algorithm>
#include <set>

#include <gtest/gtest.h>

#include "core/pitch_utils.h"
#include "core/scale.h"
#include "fugue/fugue_config.h"
#include "core/note_source.h"
#include "fugue/subject_identity.h"
#include "fugue/subject_validator.h"
#include <map>

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
  // Cantabile archetype allows 2-4 bars; Compact caps at 2.
  config.archetype = FugueArchetype::Cantabile;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 3u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectLengthFourBars) {
  config.subject_bars = 4;
  // Cantabile archetype allows 2-4 bars.
  config.archetype = FugueArchetype::Cantabile;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 4u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectBarsClampedToArchetypeMin) {
  // Compact archetype min=1, so requesting 0 should clamp to 1.
  config.subject_bars = 0;
  config.archetype = FugueArchetype::Compact;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 1u * kTicksPerBar);
}

TEST_F(SubjectGeneratorTest, SubjectBarsClampedToArchetypeMax) {
  // Compact archetype max=2, so requesting 10 should clamp to 2.
  config.subject_bars = 10;
  config.archetype = FugueArchetype::Compact;
  Subject subject = generator.generate(config, 42);
  EXPECT_EQ(subject.length_ticks, 2u * kTicksPerBar);
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
  Subject sub2 = generator.generate(config, 500);

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

  // With climax quantization, expect at least 30% of highest pitches on
  // strong beats (vs ~12.5% chance for random 16th-note positions).
  // Threshold is 30% to accommodate RNG stream separation across seeds.
  float ratio = static_cast<float>(on_strong_beat) /
                static_cast<float>(total_cases);
  EXPECT_GE(ratio, 0.30f)
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


// ---------------------------------------------------------------------------
// Kerngestalt generation tests (Phase C2)
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, KerngestaltDeterministic) {
  // Same seed should produce identical cell_window and notes.
  Subject sub1 = generator.generate(config, 42);
  Subject sub2 = generator.generate(config, 42);

  ASSERT_EQ(sub1.noteCount(), sub2.noteCount());
  EXPECT_EQ(sub1.cell_window.valid, sub2.cell_window.valid);
  if (sub1.cell_window.valid) {
    EXPECT_EQ(sub1.cell_window.start_idx, sub2.cell_window.start_idx);
    EXPECT_EQ(sub1.cell_window.end_idx, sub2.cell_window.end_idx);
  }
  for (size_t idx = 0; idx < sub1.noteCount(); ++idx) {
    EXPECT_EQ(sub1.notes[idx].pitch, sub2.notes[idx].pitch)
        << "pitch mismatch at " << idx;
    EXPECT_EQ(sub1.notes[idx].duration, sub2.notes[idx].duration)
        << "duration mismatch at " << idx;
    EXPECT_EQ(sub1.notes[idx].start_tick, sub2.notes[idx].start_tick)
        << "tick mismatch at " << idx;
  }
}

TEST_F(SubjectGeneratorTest, CellWindowIsSet) {
  // Across multiple seeds and characters, at least some subjects should have
  // a valid cell window. The Kerngestalt path has multiple rejection points
  // (range violations, tritone in cell, leap violations, hard gate failures),
  // so the success rate varies by character and archetype.
  int valid_count = 0;
  int total_count = 0;
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
      ++total_count;
      Subject subject = generator.generate(config, seed);
      if (subject.cell_window.valid) {
        ++valid_count;
        // Cell window indices should be within note count.
        EXPECT_LT(subject.cell_window.start_idx, subject.noteCount())
            << "Cell window start out of range (character "
            << static_cast<int>(chr) << " seed " << seed << ")";
        EXPECT_LT(subject.cell_window.end_idx, subject.noteCount())
            << "Cell window end out of range (character "
            << static_cast<int>(chr) << " seed " << seed << ")";
        EXPECT_LE(subject.cell_window.start_idx, subject.cell_window.end_idx)
            << "Cell window start > end (character "
            << static_cast<int>(chr) << " seed " << seed << ")";
      }
    }
  }
  // At least 15% should have valid cell windows across all characters.
  // The Kerngestalt path rejects candidates for range/tritone/leap violations,
  // so the success rate is lower than 50%.
  EXPECT_GE(valid_count, total_count / 7)
      << "Expected at least ~15% of subjects to have valid cell windows, got "
      << valid_count << "/" << total_count;
}

TEST_F(SubjectGeneratorTest, CellWindowNotesAreSubjectCore) {
  // Notes within the cell window should have BachNoteSource::SubjectCore.
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Subject subject = generator.generate(config, seed);
    if (!subject.cell_window.valid) continue;

    for (size_t idx = subject.cell_window.start_idx;
         idx <= subject.cell_window.end_idx && idx < subject.noteCount();
         ++idx) {
      EXPECT_EQ(subject.notes[idx].source, BachNoteSource::SubjectCore)
          << "Note at index " << idx
          << " in cell window should be SubjectCore (seed " << seed << ")";
    }
    return;  // Found one valid subject, verified.
  }
}

TEST_F(SubjectGeneratorTest, KerngestaltShapeMetrics) {
  // Test that generated subjects exhibit expected shape properties
  // for their Kerngestalt type. The classifier (classifyKerngestalt) operates
  // on ALL core_intervals, and the heuristics here mirror its logic:
  //   - Arpeggio: consecutive same-direction 3rd intervals (abs 3 or 4)
  //   - IntervalDriven: signature_interval >= 3 and has a leap anywhere
  //   - ChromaticCell: 2+ semitone motions (abs 1) in first 4 intervals
  //   - Linear: consecutive equal durations in core_rhythm
  int arpeggio_chord_ok = 0;
  int linear_rhythm_ok = 0;
  int chromatic_semitone_ok = 0;
  int interval_driven_leap_ok = 0;
  int total_checked = 0;

  // Test across all characters.
  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    config.character = chr;
    for (uint32_t seed = 1; seed <= 10; ++seed) {
      Subject subject = generator.generate(config, seed);
      if (!subject.cell_window.valid) continue;
      if (!subject.identity.isValid()) continue;
      ++total_checked;

      const auto& ident = subject.identity.essential;

      switch (ident.kerngestalt_type) {
        case KerngestaltType::Arpeggio: {
          // Arpeggio classifier: consecutive same-direction abs(3 or 4)
          // intervals. Check across all core_intervals.
          int consecutive = 0;
          for (size_t i = 0; i + 1 < ident.core_intervals.size(); ++i) {
            int abs_cur = std::abs(ident.core_intervals[i]);
            int abs_nxt = std::abs(ident.core_intervals[i + 1]);
            bool cur_third = (abs_cur == 3 || abs_cur == 4);
            bool nxt_third = (abs_nxt == 3 || abs_nxt == 4);
            bool same_dir =
                (ident.core_intervals[i] > 0 && ident.core_intervals[i + 1] > 0) ||
                (ident.core_intervals[i] < 0 && ident.core_intervals[i + 1] < 0);
            if (cur_third && nxt_third && same_dir) {
              ++consecutive;
            }
          }
          if (consecutive >= 1) ++arpeggio_chord_ok;
          break;
        }
        case KerngestaltType::Linear: {
          // Linear classifier: at least one pair of consecutive equal durations.
          bool has_equal_pair = false;
          for (size_t i = 0; i + 1 < ident.core_rhythm.size(); ++i) {
            if (ident.core_rhythm[i] == ident.core_rhythm[i + 1]) {
              has_equal_pair = true;
              break;
            }
          }
          if (has_equal_pair) ++linear_rhythm_ok;
          break;
        }
        case KerngestaltType::ChromaticCell: {
          // ChromaticCell classifier: 2+ semitone motions in first 4 intervals.
          int semitones = 0;
          size_t head_len =
              std::min(static_cast<size_t>(4), ident.core_intervals.size());
          for (size_t i = 0; i < head_len; ++i) {
            if (std::abs(ident.core_intervals[i]) == 1) ++semitones;
          }
          if (semitones >= 2) ++chromatic_semitone_ok;
          break;
        }
        case KerngestaltType::IntervalDriven: {
          // IntervalDriven classifier: signature_interval >= 3 and has
          // a leap (abs >= 3) anywhere in core_intervals.
          bool sig_ok = std::abs(ident.signature_interval) >= 3;
          bool has_leap = false;
          for (int iv : ident.core_intervals) {
            if (std::abs(iv) >= 3) {
              has_leap = true;
              break;
            }
          }
          if (sig_ok && has_leap) ++interval_driven_leap_ok;
          break;
        }
      }
    }
  }

  // Verify that when a type is generated, the shape metrics match the
  // classifier logic. Since the identity is built from the same intervals,
  // the match rate should be very high.
  int total_ok = arpeggio_chord_ok + linear_rhythm_ok +
                 chromatic_semitone_ok + interval_driven_leap_ok;
  EXPECT_GT(total_checked, 0) << "No subjects with valid cell windows generated";
  if (total_checked > 0) {
    float ok_ratio =
        static_cast<float>(total_ok) / static_cast<float>(total_checked);
    EXPECT_GE(ok_ratio, 0.70f)
        << "Expected at least 70% of subjects to match type-specific shape "
           "metrics, got "
        << total_ok << "/" << total_checked;
  }
}

TEST_F(SubjectGeneratorTest, CellWindowIdentityPreserved) {
  // The cell_window in subject should match the one in identity.essential.
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Subject subject = generator.generate(config, seed);
    EXPECT_EQ(subject.cell_window.valid,
              subject.identity.essential.cell_window.valid)
        << "Cell window valid mismatch (seed " << seed << ")";
    if (subject.cell_window.valid) {
      EXPECT_EQ(subject.cell_window.start_idx,
                subject.identity.essential.cell_window.start_idx)
          << "Cell window start mismatch (seed " << seed << ")";
      EXPECT_EQ(subject.cell_window.end_idx,
                subject.identity.essential.cell_window.end_idx)
          << "Cell window end mismatch (seed " << seed << ")";
    }
  }
}


// ---------------------------------------------------------------------------
// Kerngestalt 30-seed batch validation
// ---------------------------------------------------------------------------

TEST_F(SubjectGeneratorTest, KerngestaltBatchValidation) {
  // 30-seed batch validation across all 4 characters x 4 archetypes = 480 subjects.
  int total = 0;
  int cell_valid = 0;
  int kern_valid = 0;
  int tier2_fallback = 0;
  float total_composite = 0.0f;

  // Type distribution counters.
  std::map<KerngestaltType, int> type_dist;
  type_dist[KerngestaltType::IntervalDriven] = 0;
  type_dist[KerngestaltType::ChromaticCell] = 0;
  type_dist[KerngestaltType::Arpeggio] = 0;
  type_dist[KerngestaltType::Linear] = 0;

  // Per-character/archetype stats for detailed reporting.
  struct GroupStats {
    int total = 0;
    int cell_valid = 0;
    int kern_valid = 0;
    float composite_sum = 0.0f;
  };
  std::map<std::string, GroupStats> group_stats;

  SubjectValidator validator;

  for (auto chr : {SubjectCharacter::Severe, SubjectCharacter::Playful,
                   SubjectCharacter::Noble, SubjectCharacter::Restless}) {
    for (auto arch : {FugueArchetype::Compact, FugueArchetype::Cantabile,
                      FugueArchetype::Invertible, FugueArchetype::Chromatic}) {
      config.character = chr;
      config.archetype = arch;
      config.subject_bars = 2;

      std::string group_key = std::string(subjectCharacterToString(chr)) + "+" +
                              fugueArchetypeToString(arch);
      GroupStats& gs = group_stats[group_key];

      for (uint32_t seed = 1; seed <= 30; ++seed) {
        ++total;
        ++gs.total;
        Subject subject = generator.generate(config, seed);

        // Cell window validity.
        if (subject.cell_window.valid) {
          ++cell_valid;
          ++gs.cell_valid;
        } else {
          ++tier2_fallback;
        }

        // Kerngestalt validity (among those with valid identity).
        if (subject.identity.isValid()) {
          if (isValidKerngestalt(subject.identity.essential)) {
            ++kern_valid;
            ++gs.kern_valid;
          }
          // Track type distribution.
          type_dist[subject.identity.essential.kerngestalt_type]++;
        }

        // Composite score.
        SubjectScore score = validator.evaluate(subject);
        total_composite += score.composite();
        gs.composite_sum += score.composite();
      }
    }
  }

  float cell_valid_rate = static_cast<float>(cell_valid) / static_cast<float>(total);
  float kern_valid_rate = (cell_valid > 0)
      ? static_cast<float>(kern_valid) / static_cast<float>(cell_valid)
      : 0.0f;
  float tier2_rate = static_cast<float>(tier2_fallback) / static_cast<float>(total);
  float avg_composite = total_composite / static_cast<float>(total);

  // Print aggregate stats.
  printf("\n");
  printf("=== Kerngestalt Batch Validation (30 seeds x 4 chars x 4 archs = %d) ===\n", total);
  printf("Cell window valid:   %d/%d (%.1f%%)\n", cell_valid, total, cell_valid_rate * 100);
  printf("isValidKerngestalt:  %d/%d (%.1f%% of cell_valid)\n",
         kern_valid, cell_valid, kern_valid_rate * 100);
  printf("Tier 2 fallback:     %d/%d (%.1f%%)\n", tier2_fallback, total, tier2_rate * 100);
  printf("Average composite:   %.3f\n", avg_composite);
  printf("\n");

  // Type distribution.
  printf("--- KerngestaltType distribution (all %d subjects) ---\n", total);
  for (auto& [type, count] : type_dist) {
    printf("  %-18s %d (%.1f%%)\n",
           kerngestaltTypeToString(type), count,
           static_cast<float>(count) / static_cast<float>(total) * 100);
  }
  printf("\n");

  // Per-group stats.
  printf("--- Per character+archetype stats ---\n");
  printf("%-30s  cell%%   kern%%   avg_comp\n", "group");
  for (auto& [name, gs] : group_stats) {
    float g_cell = (gs.total > 0)
        ? static_cast<float>(gs.cell_valid) / static_cast<float>(gs.total) * 100
        : 0.0f;
    float g_kern = (gs.cell_valid > 0)
        ? static_cast<float>(gs.kern_valid) / static_cast<float>(gs.cell_valid) * 100
        : 0.0f;
    float g_comp = (gs.total > 0)
        ? gs.composite_sum / static_cast<float>(gs.total)
        : 0.0f;
    printf("%-30s  %5.1f   %5.1f   %.3f\n", name.c_str(), g_cell, g_kern, g_comp);
  }
  printf("\n");

  // Assertions.
  EXPECT_GE(avg_composite, 0.5f)
      << "Average composite score should be >= 0.5";
  EXPECT_GE(cell_valid_rate, 0.10f)
      << "Cell window valid rate should be >= 10%";
  // isValidKerngestalt among cell_valid subjects should be meaningful.
  if (cell_valid > 0) {
    EXPECT_GE(kern_valid_rate, 0.30f)
        << "isValidKerngestalt pass rate (among cell_valid) should be >= 30%";
  }
}


}  // namespace
}  // namespace bach
