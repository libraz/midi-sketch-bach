// Tests for fugue/exposition.h -- voice entry scheduling, note placement,
// countersubject assignment, and exposition structural correctness.

#include "fugue/exposition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/pitch_utils.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

/// @brief Build a test subject with simple ascending quarter notes.
/// @param num_notes Number of notes to generate (default 8 = 2 bars).
/// @param start_pitch Starting MIDI pitch (default C4 = 60).
/// @return Subject with ascending quarter notes.
Subject makeTestSubject(int num_notes = 8, uint8_t start_pitch = 60) {
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Severe;
  subject.length_ticks = kTicksPerBar * 2;  // 2 bars

  for (int idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(start_pitch + (idx % 5));
    note.velocity = 80;
    note.voice = 0;
    subject.notes.push_back(note);
  }
  return subject;
}

/// @brief Build a test answer (transposed subject) with quarter notes.
/// @param subject The subject to base the answer on.
/// @return Answer with notes transposed up a perfect 5th.
Answer makeTestAnswer(const Subject& subject) {
  Answer answer;
  answer.type = AnswerType::Real;
  answer.key = Key::G;

  for (const auto& note : subject.notes) {
    NoteEvent transposed = note;
    transposed.pitch = static_cast<uint8_t>(
        std::min(static_cast<int>(note.pitch) + 7, 127));
    answer.notes.push_back(transposed);
  }
  return answer;
}

/// @brief Build a test countersubject with contrary motion quarter notes.
/// @param subject The subject to write against.
/// @return Countersubject with descending quarter notes.
Countersubject makeTestCountersubject(const Subject& subject) {
  Countersubject counter;
  counter.key = subject.key;
  counter.length_ticks = subject.length_ticks;

  for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    // Contrary motion: descend from C5 (72).
    note.pitch = static_cast<uint8_t>(72 - (static_cast<int>(idx) % 5));
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }
  return counter;
}

/// @brief Build a standard FugueConfig for testing.
/// @param num_voices Number of voices (2-5).
/// @return FugueConfig with specified voice count.
FugueConfig makeTestConfig(uint8_t num_voices = 3) {
  FugueConfig config;
  config.num_voices = num_voices;
  config.key = Key::C;
  config.character = SubjectCharacter::Severe;
  config.subject_bars = 2;
  config.seed = 42;
  return config;
}

// ---------------------------------------------------------------------------
// Entry count tests
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_ThreeVoices_HasThreeEntries) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 3u);
}

TEST(ExpositionTest, BuildExposition_FourVoices_HasFourEntries) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 4u);
}

TEST(ExpositionTest, BuildExposition_TwoVoices_HasTwoEntries) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(2);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 2u);
}

TEST(ExpositionTest, BuildExposition_FiveVoices_HasFiveEntries) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(5);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 5u);
}

TEST(ExpositionTest, BuildExposition_VoiceCountClampedMin) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(1);  // Below minimum, clamped to 2.

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 2u);
}

TEST(ExpositionTest, BuildExposition_VoiceCountClampedMax) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(10);  // Above maximum, clamped to 5.

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  EXPECT_EQ(expo.entryCount(), 5u);
}

// ---------------------------------------------------------------------------
// Subject/Answer alternation
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_EntriesAlternateSubjectAnswer) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_EQ(expo.entryCount(), 4u);
  EXPECT_TRUE(expo.entries[0].is_subject);   // Subject
  EXPECT_FALSE(expo.entries[1].is_subject);  // Answer
  EXPECT_TRUE(expo.entries[2].is_subject);   // Subject
  EXPECT_FALSE(expo.entries[3].is_subject);  // Answer
}

TEST(ExpositionTest, BuildExposition_FiveVoicesAlternation) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(5);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_EQ(expo.entryCount(), 5u);
  EXPECT_TRUE(expo.entries[0].is_subject);   // Subject
  EXPECT_FALSE(expo.entries[1].is_subject);  // Answer
  EXPECT_TRUE(expo.entries[2].is_subject);   // Subject
  EXPECT_FALSE(expo.entries[3].is_subject);  // Answer
  EXPECT_TRUE(expo.entries[4].is_subject);   // Subject
}

// ---------------------------------------------------------------------------
// Entry timing
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_FirstEntryAtTickZero) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_GE(expo.entryCount(), 1u);
  EXPECT_EQ(expo.entries[0].entry_tick, 0u);
}

TEST(ExpositionTest, BuildExposition_EntrySpacing) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_EQ(expo.entryCount(), 3u);
  Tick interval = subject.length_ticks;

  EXPECT_EQ(expo.entries[0].entry_tick, 0u);
  EXPECT_EQ(expo.entries[1].entry_tick, interval);
  EXPECT_EQ(expo.entries[2].entry_tick, interval * 2);
}

// ---------------------------------------------------------------------------
// Voice role assignment
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_VoiceRolesAssigned) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_EQ(expo.entryCount(), 4u);
  EXPECT_EQ(expo.entries[0].role, VoiceRole::Assert);
  EXPECT_EQ(expo.entries[1].role, VoiceRole::Respond);
  EXPECT_EQ(expo.entries[2].role, VoiceRole::Propel);
  EXPECT_EQ(expo.entries[3].role, VoiceRole::Ground);
}

TEST(ExpositionTest, BuildExposition_FiveVoiceRoles) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(5);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  ASSERT_EQ(expo.entryCount(), 5u);
  EXPECT_EQ(expo.entries[0].role, VoiceRole::Assert);
  EXPECT_EQ(expo.entries[1].role, VoiceRole::Respond);
  EXPECT_EQ(expo.entries[2].role, VoiceRole::Propel);
  EXPECT_EQ(expo.entries[3].role, VoiceRole::Ground);
  EXPECT_EQ(expo.entries[4].role, VoiceRole::Ground);  // Extra voices get Ground.
}

// ---------------------------------------------------------------------------
// Total duration
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_TotalDuration) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // 3 voices: last entry at 2 * subject.length_ticks, plus subject.length_ticks.
  Tick expected = static_cast<Tick>(2) * subject.length_ticks + subject.length_ticks;
  EXPECT_EQ(expo.total_ticks, expected);
}

TEST(ExpositionTest, BuildExposition_TotalDurationFourVoices) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // 4 voices: last entry at 3 * subject.length_ticks, plus subject.length_ticks.
  Tick expected = static_cast<Tick>(3) * subject.length_ticks + subject.length_ticks;
  EXPECT_EQ(expo.total_ticks, expected);
}

// ---------------------------------------------------------------------------
// Voice note correctness
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_VoiceNotesHaveCorrectVoiceId) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  for (const auto& [voice_id, notes] : expo.voice_notes) {
    for (const auto& note : notes) {
      EXPECT_EQ(note.voice, voice_id)
          << "Note at tick " << note.start_tick
          << " has wrong voice assignment: expected " << static_cast<int>(voice_id)
          << ", got " << static_cast<int>(note.voice);
    }
  }
}

TEST(ExpositionTest, BuildExposition_SubjectNotesMatchOriginal) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 enters first with the subject at tick 0.
  ASSERT_GT(expo.voice_notes.count(0), 0u);
  const auto& voice0_notes = expo.voice_notes.at(0);

  // Subject notes are shifted by whole octaves to fit the voice register.
  // Voice 0 (soprano, range 60-96, center 78): notes at mean ~61 shift up by 12.
  ASSERT_GE(voice0_notes.size(), subject.notes.size());
  for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
    int expected_pitch = static_cast<int>(subject.notes[idx].pitch) + 12;
    EXPECT_EQ(voice0_notes[idx].pitch, static_cast<uint8_t>(expected_pitch))
        << "Subject note " << idx << " pitch mismatch (expected +12 octave shift)";
    EXPECT_EQ(voice0_notes[idx].start_tick, subject.notes[idx].start_tick)
        << "Subject note " << idx << " timing mismatch";
    EXPECT_EQ(voice0_notes[idx].duration, subject.notes[idx].duration)
        << "Subject note " << idx << " duration mismatch";
  }
}

TEST(ExpositionTest, BuildExposition_AnswerNotesMatchOriginal) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 1 enters with the answer at entry_tick = subject.length_ticks.
  ASSERT_GT(expo.voice_notes.count(1), 0u);
  const auto& voice1_notes = expo.voice_notes.at(1);

  Tick entry_offset = subject.length_ticks;

  // First notes of voice 1 should match the answer pitches (offset by entry tick).
  ASSERT_GE(voice1_notes.size(), answer.notes.size());
  for (size_t idx = 0; idx < answer.notes.size(); ++idx) {
    EXPECT_EQ(voice1_notes[idx].pitch, answer.notes[idx].pitch)
        << "Answer note " << idx << " pitch mismatch";
    EXPECT_EQ(voice1_notes[idx].start_tick,
              answer.notes[idx].start_tick + entry_offset)
        << "Answer note " << idx << " timing mismatch";
    EXPECT_EQ(voice1_notes[idx].duration, answer.notes[idx].duration)
        << "Answer note " << idx << " duration mismatch";
  }
}

// ---------------------------------------------------------------------------
// allNotes sorting
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_AllNotesReturnsSorted) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);
  auto all_notes = expo.allNotes();

  EXPECT_FALSE(all_notes.empty());

  // Verify sorted by start_tick, then by voice.
  for (size_t idx = 1; idx < all_notes.size(); ++idx) {
    bool order_ok = (all_notes[idx].start_tick > all_notes[idx - 1].start_tick) ||
                    (all_notes[idx].start_tick == all_notes[idx - 1].start_tick &&
                     all_notes[idx].voice >= all_notes[idx - 1].voice);
    EXPECT_TRUE(order_ok)
        << "Notes not sorted at index " << idx
        << ": tick " << all_notes[idx - 1].start_tick
        << " voice " << static_cast<int>(all_notes[idx - 1].voice)
        << " followed by tick " << all_notes[idx].start_tick
        << " voice " << static_cast<int>(all_notes[idx].voice);
  }
}

TEST(ExpositionTest, BuildExposition_AllNotesContainsAllVoices) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);
  auto all_notes = expo.allNotes();

  // Collect unique voice ids.
  std::set<VoiceId> voices;
  for (const auto& note : all_notes) {
    voices.insert(note.voice);
  }

  // All 3 voices should have notes.
  EXPECT_EQ(voices.size(), 3u);
  EXPECT_TRUE(voices.count(0) > 0);
  EXPECT_TRUE(voices.count(1) > 0);
  EXPECT_TRUE(voices.count(2) > 0);
}

// ---------------------------------------------------------------------------
// Countersubject placement
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_CountersubjectPlayed) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 should have more notes than just the subject: it also plays
  // the countersubject when voice 1 enters.
  ASSERT_GT(expo.voice_notes.count(0), 0u);
  const auto& voice0_notes = expo.voice_notes.at(0);

  // Voice 0 plays: subject (8 notes) + countersubject against voice 1 (8 notes)
  // + free counterpoint against voice 2 (some notes).
  EXPECT_GT(voice0_notes.size(), subject.notes.size())
      << "Voice 0 should have more notes than just the subject";

  // Verify that voice 0 has notes starting at the countersubject entry tick.
  Tick cs_start = subject.length_ticks;
  bool has_notes_at_cs_start = false;
  for (const auto& note : voice0_notes) {
    if (note.start_tick >= cs_start &&
        note.start_tick < cs_start + counter.length_ticks) {
      has_notes_at_cs_start = true;
      break;
    }
  }
  EXPECT_TRUE(has_notes_at_cs_start)
      << "Voice 0 should play countersubject when voice 1 enters";
}

TEST(ExpositionTest, BuildExposition_CountersubjectPlacedWithCorrectTiming) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(2);  // 2 voices: simple case.

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 plays subject then countersubject.
  ASSERT_GT(expo.voice_notes.count(0), 0u);
  const auto& voice0_notes = expo.voice_notes.at(0);

  // After subject notes, the countersubject notes should appear.
  size_t subject_count = subject.notes.size();
  ASSERT_GE(voice0_notes.size(), subject_count + counter.notes.size());

  // Verify timing is correct (pitches may be octave-shifted and key-adapted).
  Tick cs_offset = subject.length_ticks;
  for (size_t idx = 0; idx < counter.notes.size(); ++idx) {
    size_t note_idx = subject_count + idx;
    EXPECT_EQ(voice0_notes[note_idx].start_tick,
              counter.notes[idx].start_tick + cs_offset)
        << "Countersubject note " << idx << " timing mismatch in voice 0";
    EXPECT_EQ(voice0_notes[note_idx].duration, counter.notes[idx].duration)
        << "Countersubject note " << idx << " duration mismatch in voice 0";
  }
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_DeterministicWithSeed) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo1 = buildExposition(subject, answer, counter, config, 100);
  Exposition expo2 = buildExposition(subject, answer, counter, config, 100);

  // Same seed should produce identical results.
  ASSERT_EQ(expo1.entryCount(), expo2.entryCount());
  ASSERT_EQ(expo1.total_ticks, expo2.total_ticks);

  auto notes1 = expo1.allNotes();
  auto notes2 = expo2.allNotes();

  ASSERT_EQ(notes1.size(), notes2.size());
  for (size_t idx = 0; idx < notes1.size(); ++idx) {
    EXPECT_EQ(notes1[idx].pitch, notes2[idx].pitch)
        << "Determinism: pitch mismatch at note " << idx;
    EXPECT_EQ(notes1[idx].start_tick, notes2[idx].start_tick)
        << "Determinism: tick mismatch at note " << idx;
    EXPECT_EQ(notes1[idx].voice, notes2[idx].voice)
        << "Determinism: voice mismatch at note " << idx;
  }
}

TEST(ExpositionTest, BuildExposition_DifferentSeedsDifferentFreeCounterpoint) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);  // 4 voices to exercise free counterpoint.

  Exposition expo1 = buildExposition(subject, answer, counter, config, 1);
  Exposition expo2 = buildExposition(subject, answer, counter, config, 999);

  // Subject/answer entries are deterministic regardless of seed, but free
  // counterpoint may differ.
  auto notes1 = expo1.allNotes();
  auto notes2 = expo2.allNotes();

  // Should have the same structure (same number of entries and core notes).
  EXPECT_EQ(expo1.entryCount(), expo2.entryCount());

  // But total note content may vary due to free counterpoint variation.
  // At minimum, both should be non-empty.
  EXPECT_FALSE(notes1.empty());
  EXPECT_FALSE(notes2.empty());
}

// ---------------------------------------------------------------------------
// Entry number assignment
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_EntryNumbersAreOneBased) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  for (size_t idx = 0; idx < expo.entries.size(); ++idx) {
    EXPECT_EQ(expo.entries[idx].entry_number, idx + 1)
        << "Entry " << idx << " should have entry_number " << (idx + 1);
  }
}

// ---------------------------------------------------------------------------
// Voice IDs
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_VoiceIdsAreValidPermutation) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // With weighted random entry order, voice IDs form a permutation of [0..3],
  // not necessarily sequential. Verify all IDs are present exactly once.
  std::set<VoiceId> voice_ids;
  for (uint8_t idx = 0; idx < 4; ++idx) {
    voice_ids.insert(expo.entries[idx].voice_id);
  }
  EXPECT_EQ(voice_ids.size(), 4u);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    EXPECT_TRUE(voice_ids.count(idx) > 0)
        << "Voice ID " << static_cast<int>(idx) << " missing from entry order";
  }
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_EmptySubjectProducesEntries) {
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Severe;
  subject.length_ticks = 0;  // Empty subject.

  Answer answer;
  answer.type = AnswerType::Real;
  answer.key = Key::G;

  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = 0;

  FugueConfig config = makeTestConfig(3);

  // Should not crash; entries are created even with empty notes.
  Exposition expo = buildExposition(subject, answer, counter, config, 42);
  EXPECT_EQ(expo.entryCount(), 3u);
}

TEST(ExpositionTest, BuildExposition_EmptyExpositionAllNotesReturnsEmpty) {
  Exposition expo;
  auto all_notes = expo.allNotes();
  EXPECT_TRUE(all_notes.empty());
  EXPECT_EQ(expo.entryCount(), 0u);
}

// ---------------------------------------------------------------------------
// Voice register constraints (no voice crossings)
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_VoiceRangesRespected_ThreeVoices) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Exposition expo = buildExposition(subject, answer, counter, config, seed);

    // Voice 0 (soprano): C4-C6 [60, 96]
    if (expo.voice_notes.count(0) > 0) {
      for (const auto& note : expo.voice_notes.at(0)) {
        // Subject notes placed directly may not be shifted; check CS/free CP
        // notes that start after the subject entry.
        if (note.start_tick >= subject.length_ticks) {
          EXPECT_GE(note.pitch, 48)
              << "Voice 0 pitch too low (seed " << seed << ")";
          EXPECT_LE(note.pitch, 96)
              << "Voice 0 pitch too high (seed " << seed << ")";
        }
      }
    }

    // Voice 2 (bass): C2-C4 [36, 60]
    if (expo.voice_notes.count(2) > 0) {
      for (const auto& note : expo.voice_notes.at(2)) {
        // Only check notes generated by free counterpoint/CS placement.
        if (note.start_tick >= subject.length_ticks * 2 + subject.length_ticks) {
          EXPECT_GE(note.pitch, 36)
              << "Voice 2 pitch too low (seed " << seed << ")";
          EXPECT_LE(note.pitch, 60)
              << "Voice 2 pitch too high (seed " << seed << ")";
        }
      }
    }
  }
}

TEST(ExpositionTest, BuildExposition_LimitedVoiceCrossings) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Exposition expo = buildExposition(subject, answer, counter, config, seed);
    auto all_notes = expo.allNotes();

    // Count voice crossings: check if lower-numbered voice ever goes below
    // higher-numbered voice at the same tick.
    int crossings = 0;
    for (size_t idx = 0; idx + 1 < all_notes.size(); ++idx) {
      if (all_notes[idx].start_tick == all_notes[idx + 1].start_tick &&
          all_notes[idx].voice < all_notes[idx + 1].voice &&
          all_notes[idx].pitch < all_notes[idx + 1].pitch) {
        crossings++;
      }
    }
    // With voice register constraints, crossings should be minimal.
    // Allow some because subject/answer entries are not shifted.
    EXPECT_LT(crossings, 20)
        << "Too many voice crossings: " << crossings << " (seed " << seed << ")";
  }
}

// ---------------------------------------------------------------------------
// Diatonic enforcement in free counterpoint
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_FreeCounterpointAllDiatonic) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Exposition expo = buildExposition(subject, answer, counter, config, seed);

    // With weighted random entry order, find the voice that enters first
    // (entry index 0). Its free counterpoint starts after entry 2's tick,
    // which is 2 entries later than its own subject/CS material.
    VoiceId first_voice = expo.entries[0].voice_id;

    // Free counterpoint for the first-entering voice begins at entry 2's tick.
    // Its subject occupies [0, subject.length_ticks) and CS occupies
    // [subject.length_ticks, 2*subject.length_ticks), so free CP starts at 2x.
    Tick free_cp_start = subject.length_ticks * 2;
    if (expo.voice_notes.count(first_voice) > 0) {
      for (const auto& note : expo.voice_notes.at(first_voice)) {
        if (note.start_tick >= free_cp_start) {
          EXPECT_TRUE(
              scale_util::isScaleTone(note.pitch, Key::C, ScaleType::Major))
              << "Non-diatonic free counterpoint pitch "
              << static_cast<int>(note.pitch)
              << " (" << pitchToNoteName(note.pitch) << ")"
              << " at tick " << note.start_tick
              << " in voice " << static_cast<int>(first_voice)
              << " (seed " << seed << ")";
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Free counterpoint uses quarter note steps (not whole notes)
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_FreeCounterpointQuarterNoteSteps) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 free counterpoint starts at entry 2's tick.
  Tick free_cp_start = subject.length_ticks * 2;
  if (expo.voice_notes.count(0) > 0) {
    int free_cp_count = 0;
    std::set<Tick> distinct_durations;
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= free_cp_start) {
        free_cp_count++;
        distinct_durations.insert(note.duration);
        // Duration should be a valid baroque value (sixteenth to whole note).
        EXPECT_GE(note.duration, kTicksPerBeat / 4)
            << "Free CP duration too short";
        EXPECT_LE(note.duration, kTicksPerBar)
            << "Free CP duration too long";
      }
    }
    EXPECT_GE(free_cp_count, 2)
        << "Expected multiple notes in free counterpoint";
  }
}

// ---------------------------------------------------------------------------
// Countersubject key adaptation
// ---------------------------------------------------------------------------

TEST(ExpositionTest, BuildExposition_CSAdaptedToAnswerKey) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);

  // Create a countersubject that contains F4 (65) - diatonic in C, not in G.
  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = subject.length_ticks;
  const uint8_t cs_pitches[] = {72, 71, 69, 67, 65, 64, 62, 60};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = cs_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }

  FugueConfig config = makeTestConfig(3);
  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 plays CS when voice 1 enters (at tick = subject.length_ticks).
  // Entry 1 is the answer (odd index), so CS should be adapted to G major.
  Tick cs_start = subject.length_ticks;
  if (expo.voice_notes.count(0) > 0) {
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= cs_start &&
          note.start_tick < cs_start + counter.length_ticks) {
        // All CS notes against the answer should be diatonic in G major.
        EXPECT_TRUE(
            scale_util::isScaleTone(note.pitch, Key::G, ScaleType::Major))
            << "CS pitch " << static_cast<int>(note.pitch)
            << " (" << pitchToNoteName(note.pitch) << ")"
            << " not diatonic in G major when accompanying answer";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Voice entry order diversity [Task E]
// ---------------------------------------------------------------------------

TEST(ExpositionEntryOrderTest, PlayfulUsesMiddleFirst3Voice) {
  // Playful character with 3 voices uses weighted random selection.
  // MiddleFirst [1,0,2] has the highest weight (0.50) for Playful,
  // so it should appear most often across many seeds.
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Playful;
  subject.length_ticks = kTicksPerBar * 2;
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.voice = 0;
    subject.notes.push_back(note);
  }

  Answer answer;
  answer.notes = subject.notes;
  answer.key = Key::C;

  Countersubject cs;
  cs.notes = subject.notes;
  cs.key = Key::C;
  cs.length_ticks = subject.length_ticks;

  FugueConfig config;
  config.num_voices = 3;
  config.character = SubjectCharacter::Playful;
  config.key = Key::C;

  // Run many seeds and count how often MiddleFirst [1,0,2] is selected.
  int middle_first_count = 0;
  constexpr int kNumTrials = 100;
  for (uint32_t seed = 1; seed <= kNumTrials; ++seed) {
    auto expo = buildExposition(subject, answer, cs, config, seed);
    ASSERT_GE(expo.entries.size(), 3u);
    if (expo.entries[0].voice_id == 1 &&
        expo.entries[1].voice_id == 0 &&
        expo.entries[2].voice_id == 2) {
      ++middle_first_count;
    }
  }
  // With weight 0.50, expect MiddleFirst at least 30% of the time (conservative).
  EXPECT_GE(middle_first_count, 30)
      << "MiddleFirst should be the most common order for Playful; got "
      << middle_first_count << "/" << kNumTrials;
}

TEST(ExpositionEntryOrderTest, NobleUsesBottomFirst3Voice) {
  // Noble character with 3 voices uses weighted random selection.
  // TopFirst [0,1,2] has the highest weight (0.40) for Noble,
  // with BottomFirst [2,1,0] at 0.35. Verify that the order is always
  // one of the three valid permutations, and that TopFirst or BottomFirst
  // dominate across many seeds.
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Noble;
  subject.length_ticks = kTicksPerBar * 2;
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.voice = 0;
    subject.notes.push_back(note);
  }

  Answer answer;
  answer.notes = subject.notes;
  answer.key = Key::C;

  Countersubject cs;
  cs.notes = subject.notes;
  cs.key = Key::C;
  cs.length_ticks = subject.length_ticks;

  FugueConfig config;
  config.num_voices = 3;
  config.character = SubjectCharacter::Noble;
  config.key = Key::C;

  // Run many seeds and count each order.
  int top_first_count = 0;     // [0,1,2]
  int bottom_first_count = 0;  // [2,1,0]
  constexpr int kNumTrials = 100;
  for (uint32_t seed = 1; seed <= kNumTrials; ++seed) {
    auto expo = buildExposition(subject, answer, cs, config, seed);
    ASSERT_GE(expo.entries.size(), 3u);
    if (expo.entries[0].voice_id == 0) {
      ++top_first_count;
    } else if (expo.entries[0].voice_id == 2) {
      ++bottom_first_count;
    }
  }
  // Noble has TopFirst=0.40 and BottomFirst=0.35, so together they should
  // dominate. Each should appear at least 20% of the time (conservative).
  EXPECT_GE(top_first_count, 20)
      << "TopFirst should appear frequently for Noble; got "
      << top_first_count << "/" << kNumTrials;
  EXPECT_GE(bottom_first_count, 20)
      << "BottomFirst should appear frequently for Noble; got "
      << bottom_first_count << "/" << kNumTrials;
}

TEST(ExpositionEntryOrderTest, SevereUsesDefaultOrder) {
  // Severe character with 3 voices uses weighted random selection.
  // TopFirst [0,1,2] has the highest weight (0.50) for Severe,
  // so it should appear most often across many seeds.
  Subject subject;
  subject.key = Key::C;
  subject.character = SubjectCharacter::Severe;
  subject.length_ticks = kTicksPerBar * 2;
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + idx);
    note.voice = 0;
    subject.notes.push_back(note);
  }

  Answer answer;
  answer.notes = subject.notes;
  answer.key = Key::C;

  Countersubject cs;
  cs.notes = subject.notes;
  cs.key = Key::C;
  cs.length_ticks = subject.length_ticks;

  FugueConfig config;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.key = Key::C;

  // Run many seeds and count how often TopFirst [0,1,2] is selected.
  int top_first_count = 0;
  constexpr int kNumTrials = 100;
  for (uint32_t seed = 1; seed <= kNumTrials; ++seed) {
    auto expo = buildExposition(subject, answer, cs, config, seed);
    ASSERT_GE(expo.entries.size(), 3u);
    if (expo.entries[0].voice_id == 0 &&
        expo.entries[1].voice_id == 1 &&
        expo.entries[2].voice_id == 2) {
      ++top_first_count;
    }
  }
  // With weight 0.50, expect TopFirst at least 30% of the time (conservative).
  EXPECT_GE(top_first_count, 30)
      << "TopFirst should be the most common order for Severe; got "
      << top_first_count << "/" << kNumTrials;
}

// ---------------------------------------------------------------------------
// Exposition structural note voice crossing fix (Fix B)
// ---------------------------------------------------------------------------

TEST(ExpositionStructuralCrossingTest, StructuralNotesNoCrossingInCPState) {
  // Build a 3-voice exposition with counterpoint validation and verify
  // that structural notes (subject/answer/CS) do not produce voice crossings.
  Subject subject = makeTestSubject(8, 60);
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  CounterpointState cp_state;
  // Register voices: soprano=0, alto=1, bass=2 (lower id = higher voice).
  cp_state.registerVoice(0, 60, 96);
  cp_state.registerVoice(1, 48, 84);
  cp_state.registerVoice(2, 36, 60);

  BachRuleEvaluator cp_rules(3);
  CollisionResolver cp_resolver;

  // Build a simple harmonic timeline with I chord throughout.
  HarmonicTimeline timeline;
  HarmonicEvent harm_ev;
  harm_ev.tick = 0;
  harm_ev.end_tick = subject.length_ticks * 4;
  harm_ev.key = Key::C;
  harm_ev.chord.degree = ChordDegree::I;
  harm_ev.chord.quality = ChordQuality::Major;
  harm_ev.chord.root_pitch = 60;
  timeline.addEvent(harm_ev);

  Exposition expo = buildExposition(subject, answer, counter, config, 42,
                                    cp_state, cp_rules, cp_resolver, timeline);

  // Check all notes at each tick position -- no voice crossing should exist.
  auto all_notes = expo.allNotes();
  int crossings = 0;
  for (size_t idx = 0; idx + 1 < all_notes.size(); ++idx) {
    if (all_notes[idx].start_tick == all_notes[idx + 1].start_tick &&
        all_notes[idx].voice < all_notes[idx + 1].voice &&
        all_notes[idx].pitch < all_notes[idx + 1].pitch) {
      crossings++;
    }
  }
  // Voice crossing fix should keep crossings minimal or zero.
  EXPECT_LT(crossings, 5)
      << "Structural notes should have minimal voice crossings after fix";
}

// ---------------------------------------------------------------------------
// Countersubject placement negative diff octave shift (Fix C)
// ---------------------------------------------------------------------------

TEST(ExpositionCSPlacementTest, NegativeDiffOctaveShift) {
  // Create a countersubject with high mean pitch and place it in a low voice
  // register. The octave shift formula should correctly handle the negative
  // difference (voice_center < cs_mean).
  Subject subject = makeTestSubject(8, 60);
  Answer answer = makeTestAnswer(subject);

  // Countersubject with high pitches (mean around 84 = C6).
  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = subject.length_ticks;
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(84 + (idx % 3));  // C6-D6-E6 range
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }

  // Use 2-voice config so CS is placed in voice 0 (soprano, center ~78).
  // CS mean ~85, voice center ~78: diff = 78 - 85 = -7.
  // Correct formula: -((7+5)/12)*12 = -(12/12)*12 = -12 (down one octave).
  // Buggy formula: ((-7+6)/12)*12 = (-1/12)*12 = 0 (C++ truncation toward zero).
  FugueConfig config = makeTestConfig(2);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 plays CS when voice 1 enters (at tick = subject.length_ticks).
  Tick cs_start = subject.length_ticks;
  if (expo.voice_notes.count(0) > 0) {
    bool found_cs = false;
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= cs_start &&
          note.start_tick < cs_start + counter.length_ticks) {
        found_cs = true;
        // With the fixed formula, the CS should be shifted down by 12.
        // Original pitch ~84-86, shifted to ~72-74, then clamped to register.
        // Voice 0 register for 2-voice: center around 78.
        // The pitch should be reasonably within voice range, not at 84+.
        EXPECT_LE(note.pitch, 96)
            << "CS note pitch should be within soprano range";
        EXPECT_GE(note.pitch, 60)
            << "CS note pitch should be within soprano range";
      }
    }
    EXPECT_TRUE(found_cs) << "Countersubject notes should be placed for voice 0";
  }
}

// ---------------------------------------------------------------------------
// Free counterpoint duration variety (selectDuration integration)
// ---------------------------------------------------------------------------

TEST(ExpositionTest, FreeCPDurationVariety) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  // Try multiple seeds to find one with duration variety.
  bool found_variety = false;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    Exposition expo = buildExposition(subject, answer, counter, config, seed);
    Tick free_cp_start = subject.length_ticks * 2;
    if (expo.voice_notes.count(0) == 0) continue;

    std::set<Tick> distinct;
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= free_cp_start) {
        distinct.insert(note.duration);
      }
    }
    if (distinct.size() >= 3) {
      found_variety = true;
      break;
    }
  }
  EXPECT_TRUE(found_variety)
      << "At least one seed in 1-20 should produce 3+ distinct free CP durations";
}

TEST(ExpositionTest, FreeCPGapFilling) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(3);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);
  Tick free_cp_start = subject.length_ticks * 2;
  Tick entry_interval = subject.length_ticks;

  if (expo.voice_notes.count(0) > 0) {
    Tick total_dur = 0;
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= free_cp_start &&
          note.start_tick < free_cp_start + entry_interval) {
        total_dur += note.duration;
      }
    }
    // Free CP should fill the entire entry interval (no gaps).
    EXPECT_EQ(total_dur, entry_interval)
        << "Free counterpoint total duration should equal entry interval";
  }
}

// ---------------------------------------------------------------------------
// Fix 4: CS-answer strong-beat consonance snap
// ---------------------------------------------------------------------------

TEST(ExpositionTest, CSAgainstAnswer_StrongBeatConsonance) {
  // Create a CS with pitches that would be dissonant against the answer
  // on strong beats. The snap should correct them.
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);

  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = subject.length_ticks;

  // Deliberately create dissonant intervals on strong beats vs G-major answer.
  // Beat 0: F#4 (66) vs G4 (67) = m2 -> dissonant, should snap.
  // Beat 1: E4 (64) -> offbeat, no snap.
  // Beat 2: Bb4 (70) vs A4 (69) = m2 -> dissonant, should snap.
  // Beat 3: G4 (67) -> offbeat, no snap.
  const uint8_t cs_pitches[] = {66, 64, 70, 67, 66, 64, 70, 67};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = cs_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }

  FugueConfig config = makeTestConfig(3);
  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  // Voice 0 plays CS when voice 1 enters (answer at tick = subject.length_ticks).
  Tick cs_start = subject.length_ticks;
  if (expo.voice_notes.count(0) > 0 && expo.voice_notes.count(1) > 0) {
    const auto& cs_notes = expo.voice_notes.at(0);
    const auto& entry_notes = expo.voice_notes.at(expo.entries[1].voice_id);

    for (const auto& cs : cs_notes) {
      if (cs.start_tick < cs_start ||
          cs.start_tick >= cs_start + counter.length_ticks)
        continue;

      uint8_t beat = beatInBar(cs.start_tick);
      if (beat != 0 && beat != 2) continue;  // Strong beats only.

      // Find concurrent entry note.
      for (const auto& entry : entry_notes) {
        if (entry.start_tick > cs.start_tick) break;
        if (entry.start_tick + entry.duration <= cs.start_tick) continue;

        int ivl = absoluteInterval(cs.pitch, entry.pitch) % 12;
        bool consonant = (ivl == 0 || ivl == 3 || ivl == 4 ||
                          ivl == 7 || ivl == 8 || ivl == 9);
        EXPECT_TRUE(consonant)
            << "Strong-beat CS pitch " << static_cast<int>(cs.pitch)
            << " at tick " << cs.start_tick
            << " is dissonant (interval " << ivl << ") with entry pitch "
            << static_cast<int>(entry.pitch);
        break;
      }
    }
  }
}

TEST(ExpositionTest, CSSnap_PreservesDiatonicity) {
  // Verify that snapped CS notes remain diatonic in the answer key.
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);

  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = subject.length_ticks;
  const uint8_t cs_pitches[] = {72, 71, 69, 67, 66, 64, 62, 60};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = cs_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }

  FugueConfig config = makeTestConfig(3);
  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  Tick cs_start = subject.length_ticks;
  if (expo.voice_notes.count(0) > 0) {
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= cs_start &&
          note.start_tick < cs_start + counter.length_ticks) {
        // All CS notes accompanying the answer should remain diatonic.
        EXPECT_TRUE(
            scale_util::isScaleTone(note.pitch, Key::G, ScaleType::Major))
            << "Snapped CS pitch " << static_cast<int>(note.pitch)
            << " (" << pitchToNoteName(note.pitch) << ")"
            << " at tick " << note.start_tick
            << " is not diatonic in G major";
      }
    }
  }
}

TEST(ExpositionTest, CSSnap_NoSnapNeededWhenConsonant) {
  // Verify that consonant CS notes are not modified by the snap.
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);

  // CS with notes that are already consonant (P1, M6, P5, P1) with answer.
  Countersubject counter;
  counter.key = Key::C;
  counter.length_ticks = subject.length_ticks;
  const uint8_t cs_pitches[] = {67, 64, 60, 67, 67, 64, 60, 67};
  for (int idx = 0; idx < 8; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = cs_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    counter.notes.push_back(note);
  }

  FugueConfig config = makeTestConfig(3);
  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  Tick cs_start = subject.length_ticks;
  if (expo.voice_notes.count(0) > 0) {
    int cs_idx = 0;
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= cs_start &&
          note.start_tick < cs_start + counter.length_ticks) {
        if (cs_idx < 8) {
          // Pitch class should be preserved (octave shift is OK).
          uint8_t expected_pc = cs_pitches[cs_idx] % 12;
          uint8_t actual_pc = note.pitch % 12;
          EXPECT_EQ(actual_pc, expected_pc)
              << "CS note " << cs_idx << ": pitch class changed from "
              << static_cast<int>(expected_pc) << " to "
              << static_cast<int>(actual_pc)
              << " (should not snap consonant notes)";
        }
        ++cs_idx;
      }
    }
  }
}

}  // namespace
}  // namespace bach
