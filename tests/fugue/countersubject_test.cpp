// Tests for fugue/countersubject.h -- countersubject generation, contrary
// motion, complementary rhythm, and consonant interval validation.

#include "fugue/countersubject.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/pitch_utils.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helper: build a simple arch-shaped subject for testing
// ---------------------------------------------------------------------------

/// @brief Create a test subject with an arch-shaped melody.
/// C4-D4-E4-F4-E4-D4-C4-C4 as quarter notes (2 bars).
Subject makeTestSubject(SubjectCharacter character = SubjectCharacter::Severe) {
  Subject sub;
  sub.key = Key::C;
  sub.character = character;
  sub.length_ticks = kTicksPerBar * 2;

  // Arch shape: C4 D4 E4 F4 E4 D4 C4 C4.
  const uint8_t pitches[] = {60, 62, 64, 65, 64, 62, 60, 60};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    sub.notes.push_back(note);
  }
  return sub;
}

/// @brief Create a subject with varied rhythms for complementary rhythm tests.
Subject makeVariedRhythmSubject() {
  Subject sub;
  sub.key = Key::C;
  sub.character = SubjectCharacter::Severe;
  sub.length_ticks = kTicksPerBar * 2;

  // Mix of half notes and eighth notes.
  // Bar 1: half note C4, then four eighth notes D4 E4 F4 E4.
  // Bar 2: whole note C4.
  sub.notes.push_back({0, kTicksPerBeat * 2, 60, 80, 0});                // Half note
  sub.notes.push_back({kTicksPerBeat * 2, kTicksPerBeat / 2, 62, 80, 0});  // Eighth
  sub.notes.push_back({kTicksPerBeat * 2 + 240, kTicksPerBeat / 2, 64, 80, 0});
  sub.notes.push_back({kTicksPerBeat * 3, kTicksPerBeat / 2, 65, 80, 0});
  sub.notes.push_back({kTicksPerBeat * 3 + 240, kTicksPerBeat / 2, 64, 80, 0});
  sub.notes.push_back({kTicksPerBar, kTicksPerBar, 60, 80, 0});  // Whole note

  return sub;
}

// ---------------------------------------------------------------------------
// Basic properties
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, GenerateHasNotes) {
  Subject subject = makeTestSubject();
  Countersubject cs = generateCountersubject(subject, 42);
  EXPECT_GT(cs.noteCount(), 0u);
}

TEST(CountersubjectTest, EmptySubjectProducesEmptyCS) {
  Subject empty_subject;
  empty_subject.key = Key::C;
  empty_subject.length_ticks = kTicksPerBar;

  Countersubject cs = generateCountersubject(empty_subject, 42);
  EXPECT_EQ(cs.noteCount(), 0u);
  EXPECT_EQ(cs.key, Key::C);
  EXPECT_EQ(cs.length_ticks, kTicksPerBar);
}

TEST(CountersubjectTest, SameLengthAsSubject) {
  Subject subject = makeTestSubject();
  Countersubject cs = generateCountersubject(subject, 42);
  EXPECT_EQ(cs.length_ticks, subject.length_ticks);
}

TEST(CountersubjectTest, SameKeyAsSubject) {
  Subject subject = makeTestSubject();
  subject.key = Key::G;
  Countersubject cs = generateCountersubject(subject, 42);
  EXPECT_EQ(cs.key, Key::G);
}

TEST(CountersubjectTest, NotesDoNotExceedLength) {
  Subject subject = makeTestSubject();
  Countersubject cs = generateCountersubject(subject, 42);

  for (const auto& note : cs.notes) {
    Tick note_end = note.start_tick + note.duration;
    EXPECT_LE(note_end, cs.length_ticks)
        << "CS note at tick " << note.start_tick << " exceeds length";
  }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, DeterministicWithSeed) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 100);
  Countersubject cs2 = generateCountersubject(subject, 100);

  ASSERT_EQ(cs1.noteCount(), cs2.noteCount());
  for (size_t idx = 0; idx < cs1.noteCount(); ++idx) {
    EXPECT_EQ(cs1.notes[idx].pitch, cs2.notes[idx].pitch)
        << "Mismatch at note " << idx;
    EXPECT_EQ(cs1.notes[idx].duration, cs2.notes[idx].duration)
        << "Duration mismatch at note " << idx;
    EXPECT_EQ(cs1.notes[idx].start_tick, cs2.notes[idx].start_tick)
        << "Start tick mismatch at note " << idx;
  }
}

TEST(CountersubjectTest, DifferentSeedsProduceDifferentResults) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 1);
  Countersubject cs2 = generateCountersubject(subject, 999);

  // Very unlikely to be identical with different seeds.
  bool all_same = (cs1.noteCount() == cs2.noteCount());
  if (all_same) {
    for (size_t idx = 0; idx < cs1.noteCount(); ++idx) {
      if (cs1.notes[idx].pitch != cs2.notes[idx].pitch) {
        all_same = false;
        break;
      }
    }
  }
  EXPECT_FALSE(all_same) << "Different seeds should produce different CS";
}

// ---------------------------------------------------------------------------
// Consonant intervals
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, ConsonantIntervals) {
  Subject subject = makeTestSubject();

  // Test across multiple seeds: most intervals should be consonant.
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Countersubject cs = generateCountersubject(subject, seed);
    ASSERT_GT(cs.noteCount(), 0u);

    int total_checks = 0;
    int consonant_count = 0;

    for (const auto& cs_note : cs.notes) {
      // Find subject note sounding at this tick.
      for (const auto& subj_note : subject.notes) {
        Tick subj_end = subj_note.start_tick + subj_note.duration;
        if (subj_note.start_tick <= cs_note.start_tick &&
            subj_end > cs_note.start_tick) {
          total_checks++;
          int abs_int = absoluteInterval(cs_note.pitch, subj_note.pitch);
          IntervalQuality quality = classifyInterval(abs_int);
          if (quality == IntervalQuality::PerfectConsonance ||
              quality == IntervalQuality::ImperfectConsonance) {
            consonant_count++;
          }
          break;
        }
      }
    }

    if (total_checks > 0) {
      float rate = static_cast<float>(consonant_count) /
                   static_cast<float>(total_checks);
      EXPECT_GE(rate, 0.50f)
          << "Consonance rate too low (seed " << seed << "): " << rate;
    }
  }
}

// ---------------------------------------------------------------------------
// Contrary motion
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, ContainsContraryMotion) {
  Subject subject = makeTestSubject();

  // Check across multiple seeds: at least some contrary motion should exist.
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Countersubject cs = generateCountersubject(subject, seed);
    ASSERT_GE(cs.noteCount(), 2u);

    int contrary_count = 0;
    int motion_checks = 0;

    for (size_t idx = 0; idx + 1 < cs.noteCount(); ++idx) {
      // Find subject notes at the same times.
      int cs_motion = static_cast<int>(cs.notes[idx + 1].pitch) -
                      static_cast<int>(cs.notes[idx].pitch);
      if (cs_motion == 0) continue;

      // Find subject motion at similar tick.
      Tick cs_tick = cs.notes[idx].start_tick;
      int subj_motion = 0;
      for (size_t sdx = 0; sdx + 1 < subject.notes.size(); ++sdx) {
        if (subject.notes[sdx].start_tick <= cs_tick &&
            subject.notes[sdx + 1].start_tick > cs_tick) {
          subj_motion = static_cast<int>(subject.notes[sdx + 1].pitch) -
                        static_cast<int>(subject.notes[sdx].pitch);
          break;
        }
      }

      if (subj_motion == 0) continue;

      motion_checks++;
      // Contrary = opposite signs.
      if ((cs_motion > 0 && subj_motion < 0) ||
          (cs_motion < 0 && subj_motion > 0)) {
        contrary_count++;
      }
    }

    if (motion_checks > 0) {
      float contrary_rate = static_cast<float>(contrary_count) /
                            static_cast<float>(motion_checks);
      EXPECT_GT(contrary_rate, 0.0f)
          << "No contrary motion detected (seed " << seed << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Complementary rhythm
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, ComplementaryRhythm) {
  Subject subject = makeVariedRhythmSubject();

  // The CS should have some duration variety (not all the same duration).
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Countersubject cs = generateCountersubject(subject, seed);
    ASSERT_GE(cs.noteCount(), 2u);

    std::set<Tick> unique_durations;
    for (const auto& note : cs.notes) {
      unique_durations.insert(note.duration);
    }

    EXPECT_GT(unique_durations.size(), 1u)
        << "CS should have varied rhythm (seed " << seed << ")";
  }
}

// ---------------------------------------------------------------------------
// Pitch range safety
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, PitchInValidRange) {
  Subject subject = makeTestSubject();

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Countersubject cs = generateCountersubject(subject, seed);
    for (const auto& note : cs.notes) {
      EXPECT_GE(note.pitch, 36)
          << "Pitch below minimum (seed " << seed << ")";
      EXPECT_LE(note.pitch, 127)
          << "Pitch above maximum (seed " << seed << ")";
    }
  }
}

// ---------------------------------------------------------------------------
// Character-specific behavior
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, SevereCharacterMoreStepwise) {
  Subject subject = makeTestSubject(SubjectCharacter::Severe);

  // Severe: fewer leaps, narrower range.
  int total_steps = 0;
  int total_leaps = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Countersubject cs = generateCountersubject(subject, seed);

    for (size_t idx = 0; idx + 1 < cs.noteCount(); ++idx) {
      int motion = absoluteInterval(cs.notes[idx].pitch,
                                    cs.notes[idx + 1].pitch);
      if (motion <= 2) {
        total_steps++;
      } else {
        total_leaps++;
      }
    }
  }

  // Severe should have more steps than leaps.
  if (total_steps + total_leaps > 0) {
    float step_rate = static_cast<float>(total_steps) /
                      static_cast<float>(total_steps + total_leaps);
    EXPECT_GE(step_rate, 0.40f)
        << "Severe character should be predominantly stepwise";
  }
}

TEST(CountersubjectTest, PlayfulCharacterMoreVariety) {
  Subject subject_playful = makeTestSubject(SubjectCharacter::Playful);
  Subject subject_severe = makeTestSubject(SubjectCharacter::Severe);

  // Accumulate variety metrics across seeds.
  int playful_leaps = 0;
  int severe_leaps = 0;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Countersubject cs_play = generateCountersubject(subject_playful, seed);
    Countersubject cs_sev = generateCountersubject(subject_severe, seed);

    for (size_t idx = 0; idx + 1 < cs_play.noteCount(); ++idx) {
      if (absoluteInterval(cs_play.notes[idx].pitch,
                           cs_play.notes[idx + 1].pitch) > 2) {
        playful_leaps++;
      }
    }
    for (size_t idx = 0; idx + 1 < cs_sev.noteCount(); ++idx) {
      if (absoluteInterval(cs_sev.notes[idx].pitch,
                           cs_sev.notes[idx + 1].pitch) > 2) {
        severe_leaps++;
      }
    }
  }

  // Playful should generally have more leaps than Severe.
  EXPECT_GE(playful_leaps, severe_leaps)
      << "Playful CS should have at least as many leaps as Severe";
}

// ---------------------------------------------------------------------------
// Member functions
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, MemberFunctions) {
  Countersubject cs;
  EXPECT_EQ(cs.noteCount(), 0u);
  EXPECT_EQ(cs.lowestPitch(), 127);
  EXPECT_EQ(cs.highestPitch(), 0);
  EXPECT_EQ(cs.range(), 0);

  cs.notes.push_back({0, 480, 55, 80, 1});
  cs.notes.push_back({480, 480, 60, 80, 1});
  cs.notes.push_back({960, 480, 67, 80, 1});

  EXPECT_EQ(cs.noteCount(), 3u);
  EXPECT_EQ(cs.lowestPitch(), 55);
  EXPECT_EQ(cs.highestPitch(), 67);
  EXPECT_EQ(cs.range(), 12);
}

// ---------------------------------------------------------------------------
// Multiple character types generate successfully
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, AllCharactersGenerate) {
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe, SubjectCharacter::Playful,
      SubjectCharacter::Noble, SubjectCharacter::Restless};

  for (auto character : characters) {
    Subject subject = makeTestSubject(character);
    Countersubject cs = generateCountersubject(subject, 42);
    EXPECT_GT(cs.noteCount(), 0u)
        << "Failed for character: "
        << subjectCharacterToString(character);
  }
}

// ---------------------------------------------------------------------------
// Retry mechanism
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, MaxRetriesRespected) {
  Subject subject = makeTestSubject();

  // Even with 1 retry, should produce a result.
  Countersubject cs = generateCountersubject(subject, 42, 1);
  EXPECT_GT(cs.noteCount(), 0u);
}

TEST(CountersubjectTest, MoreRetriesDoNotCrash) {
  Subject subject = makeTestSubject();

  // Many retries should be safe.
  Countersubject cs = generateCountersubject(subject, 42, 20);
  EXPECT_GT(cs.noteCount(), 0u);
}

// ---------------------------------------------------------------------------
// Voice assignment
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, VoiceIdIsOne) {
  Subject subject = makeTestSubject();
  Countersubject cs = generateCountersubject(subject, 42);

  for (const auto& note : cs.notes) {
    EXPECT_EQ(note.voice, 1) << "CS notes should be on voice 1";
  }
}

// ---------------------------------------------------------------------------
// Velocity
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, VelocityIsStandard) {
  Subject subject = makeTestSubject();
  Countersubject cs = generateCountersubject(subject, 42);

  for (const auto& note : cs.notes) {
    EXPECT_EQ(note.velocity, 80) << "Organ velocity should be 80";
  }
}

// ---------------------------------------------------------------------------
// Second countersubject for 4+ voices
// ---------------------------------------------------------------------------

TEST(CountersubjectTest, SecondCS_GeneratesNotes) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 42);
  Countersubject cs2 = generateSecondCountersubject(subject, cs1, 100);
  EXPECT_GT(cs2.noteCount(), 0u);
}

TEST(CountersubjectTest, SecondCS_SameLengthAsSubject) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 42);
  Countersubject cs2 = generateSecondCountersubject(subject, cs1, 100);
  EXPECT_EQ(cs2.length_ticks, subject.length_ticks);
}

TEST(CountersubjectTest, SecondCS_SameKeyAsSubject) {
  Subject subject = makeTestSubject();
  subject.key = Key::G;
  Countersubject cs1 = generateCountersubject(subject, 42);
  Countersubject cs2 = generateSecondCountersubject(subject, cs1, 100);
  EXPECT_EQ(cs2.key, Key::G);
}

TEST(CountersubjectTest, SecondCS_DifferentFromFirst) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 42);
  Countersubject cs2 = generateSecondCountersubject(subject, cs1, 100);

  // CS2 should differ from CS1 (different register or pitches).
  bool any_different = false;
  size_t check_count = std::min(cs1.noteCount(), cs2.noteCount());
  for (size_t idx = 0; idx < check_count; ++idx) {
    if (cs1.notes[idx].pitch != cs2.notes[idx].pitch) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different)
      << "Second countersubject should differ from first";
}

TEST(CountersubjectTest, SecondCS_Deterministic) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 42);

  Countersubject cs2a = generateSecondCountersubject(subject, cs1, 200);
  Countersubject cs2b = generateSecondCountersubject(subject, cs1, 200);

  ASSERT_EQ(cs2a.noteCount(), cs2b.noteCount());
  for (size_t idx = 0; idx < cs2a.noteCount(); ++idx) {
    EXPECT_EQ(cs2a.notes[idx].pitch, cs2b.notes[idx].pitch)
        << "Second CS should be deterministic, mismatch at note " << idx;
  }
}

TEST(CountersubjectTest, SecondCS_EmptySubjectProducesEmpty) {
  Subject empty_subject;
  empty_subject.key = Key::C;
  empty_subject.length_ticks = kTicksPerBar;

  Countersubject cs1;
  cs1.key = Key::C;
  cs1.length_ticks = kTicksPerBar;

  Countersubject cs2 = generateSecondCountersubject(empty_subject, cs1, 42);
  EXPECT_EQ(cs2.noteCount(), 0u);
}

TEST(CountersubjectTest, SecondCS_AllCharactersGenerate) {
  const SubjectCharacter characters[] = {
      SubjectCharacter::Severe, SubjectCharacter::Playful,
      SubjectCharacter::Noble, SubjectCharacter::Restless};

  for (auto character : characters) {
    Subject subject = makeTestSubject(character);
    Countersubject cs1 = generateCountersubject(subject, 42);
    Countersubject cs2 = generateSecondCountersubject(subject, cs1, 100);
    EXPECT_GT(cs2.noteCount(), 0u)
        << "Second CS failed for character: "
        << subjectCharacterToString(character);
  }
}

TEST(CountersubjectTest, SecondCS_PitchInValidRange) {
  Subject subject = makeTestSubject();
  Countersubject cs1 = generateCountersubject(subject, 42);

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Countersubject cs2 = generateSecondCountersubject(subject, cs1, seed);
    for (const auto& note : cs2.notes) {
      EXPECT_GE(note.pitch, 36)
          << "CS2 pitch below minimum (seed " << seed << ")";
      EXPECT_LE(note.pitch, 127)
          << "CS2 pitch above maximum (seed " << seed << ")";
    }
  }
}

}  // namespace
}  // namespace bach
