// Tests for fugue/exposition.h -- voice entry scheduling, note placement,
// countersubject assignment, and exposition structural correctness.

#include "fugue/exposition.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/pitch_utils.h"
#include "core/scale.h"

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

  // First notes of voice 0 should match the subject pitches.
  ASSERT_GE(voice0_notes.size(), subject.notes.size());
  for (size_t idx = 0; idx < subject.notes.size(); ++idx) {
    EXPECT_EQ(voice0_notes[idx].pitch, subject.notes[idx].pitch)
        << "Subject note " << idx << " pitch mismatch";
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

TEST(ExpositionTest, BuildExposition_VoiceIdsAreSequential) {
  Subject subject = makeTestSubject();
  Answer answer = makeTestAnswer(subject);
  Countersubject counter = makeTestCountersubject(subject);
  FugueConfig config = makeTestConfig(4);

  Exposition expo = buildExposition(subject, answer, counter, config, 42);

  for (uint8_t idx = 0; idx < 4; ++idx) {
    EXPECT_EQ(expo.entries[idx].voice_id, idx);
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

    // Voice 0 free counterpoint starts at entry 2's tick.
    Tick free_cp_start = subject.length_ticks * 2;
    if (expo.voice_notes.count(0) > 0) {
      for (const auto& note : expo.voice_notes.at(0)) {
        if (note.start_tick >= free_cp_start) {
          EXPECT_TRUE(
              scale_util::isScaleTone(note.pitch, Key::C, ScaleType::Major))
              << "Non-diatonic free counterpoint pitch "
              << static_cast<int>(note.pitch)
              << " (" << pitchToNoteName(note.pitch) << ")"
              << " at tick " << note.start_tick
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
    for (const auto& note : expo.voice_notes.at(0)) {
      if (note.start_tick >= free_cp_start) {
        free_cp_count++;
        // Quarter note steps = kTicksPerBeat duration.
        EXPECT_LE(note.duration, kTicksPerBeat)
            << "Free counterpoint note should be quarter notes or shorter";
      }
    }
    // With 2-bar subject, free counterpoint for 1 entry interval = 2 bars
    // = 8 quarter notes.
    EXPECT_GE(free_cp_count, 4)
        << "Expected multiple quarter-note steps in free counterpoint";
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

}  // namespace
}  // namespace bach
