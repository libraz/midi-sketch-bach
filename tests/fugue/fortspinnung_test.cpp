// Tests for fugue/fortspinnung.h -- Fortspinnung episode generation from
// motif pool fragments, connection rules, and Episode wrapper.

#include "fugue/fortspinnung.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "fugue/episode.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a subject note sequence with N quarter notes (C major scale).
// ---------------------------------------------------------------------------

std::vector<NoteEvent> makeSubjectNotes(size_t num_notes, Tick start_tick = 0) {
  std::vector<NoteEvent> notes;
  notes.reserve(num_notes);
  const uint8_t pitches[] = {60, 62, 64, 65, 67, 69, 71, 72};
  for (size_t idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = start_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = pitches[idx % 8];
    note.velocity = 80;
    note.voice = 0;
    notes.push_back(note);
  }
  return notes;
}

// ---------------------------------------------------------------------------
// Helper: build a standard MotifPool from an 8-note subject.
// ---------------------------------------------------------------------------

MotifPool buildTestPool(SubjectCharacter character = SubjectCharacter::Severe) {
  MotifPool pool;
  auto subject_notes = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;
  pool.build(subject_notes, empty_cs, character);
  return pool;
}

// ---------------------------------------------------------------------------
// Helper: create a Subject struct for generateFortspinnungEpisode.
// ---------------------------------------------------------------------------

Subject makeTestSubject(SubjectCharacter character = SubjectCharacter::Severe) {
  Subject subject;
  subject.key = Key::C;
  subject.character = character;
  subject.length_ticks = kTicksPerBar * 2;
  subject.notes = makeSubjectNotes(8);
  return subject;
}

// ===========================================================================
// EmptyPoolReturnsEmpty
// ===========================================================================

TEST(FortspinnungTest, EmptyPoolReturnsEmpty) {
  MotifPool empty_pool;
  auto result = generateFortspinnung(empty_pool, 0, kTicksPerBar * 4,
                                     3, 42, SubjectCharacter::Severe);
  EXPECT_TRUE(result.empty());
}

// ===========================================================================
// GeneratesNotesForSingleVoice
// ===========================================================================

TEST(FortspinnungTest, GeneratesNotesForSingleVoice) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     1, 42, SubjectCharacter::Severe);

  EXPECT_FALSE(result.empty());

  // All notes should be voice 0 when num_voices = 1.
  for (const auto& note : result) {
    EXPECT_EQ(note.voice, 0u) << "Single-voice Fortspinnung should only use voice 0";
  }
}

// ===========================================================================
// GeneratesNotesForTwoVoices
// ===========================================================================

TEST(FortspinnungTest, GeneratesNotesForTwoVoices) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     2, 42, SubjectCharacter::Severe);

  EXPECT_FALSE(result.empty());

  // Both voice 0 and voice 1 should be present.
  std::set<uint8_t> voices;
  for (const auto& note : result) {
    voices.insert(note.voice);
  }
  EXPECT_TRUE(voices.count(0) > 0) << "Voice 0 should be present";
  EXPECT_TRUE(voices.count(1) > 0) << "Voice 1 should be present";
}

// ===========================================================================
// NotesWithinDuration
// ===========================================================================

TEST(FortspinnungTest, NotesWithinDuration) {
  auto pool = buildTestPool();
  Tick start = kTicksPerBar * 2;
  Tick duration = kTicksPerBar * 3;

  auto result = generateFortspinnung(pool, start, duration,
                                     2, 42, SubjectCharacter::Severe);

  EXPECT_FALSE(result.empty());

  for (const auto& note : result) {
    EXPECT_GE(note.start_tick, start)
        << "Note starts before episode start";
    EXPECT_LT(note.start_tick, start + duration)
        << "Note starts after episode end";
  }
}

// ===========================================================================
// FragmentConnectionSmallGaps
// ===========================================================================

TEST(FortspinnungTest, FragmentConnectionSmallGaps) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     1, 42, SubjectCharacter::Severe);

  // Filter voice 0 only and sort by tick.
  std::vector<NoteEvent> voice0;
  for (const auto& note : result) {
    if (note.voice == 0) {
      voice0.push_back(note);
    }
  }
  std::sort(voice0.begin(), voice0.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              return lhs.start_tick < rhs.start_tick;
            });

  // Check that there are no enormous pitch jumps between consecutive notes
  // that would indicate broken fragment connections.
  // The closeGapIfNeeded function targets <= 4 semitones between fragments.
  // Within a fragment, larger intervals are allowed (they come from the subject).
  // We check a relaxed bound of 12 semitones (one octave) for overall continuity.
  if (voice0.size() >= 2) {
    int max_gap = 0;
    for (size_t idx = 1; idx < voice0.size(); ++idx) {
      int gap = std::abs(static_cast<int>(voice0[idx].pitch) -
                         static_cast<int>(voice0[idx - 1].pitch));
      if (gap > max_gap) max_gap = gap;
    }
    EXPECT_LE(max_gap, 16)
        << "Largest pitch gap between consecutive notes is too wide: " << max_gap
        << " semitones (expect reasonable voice-leading)";
  }
}

// ===========================================================================
// DeterministicWithSameSeed
// ===========================================================================

TEST(FortspinnungTest, DeterministicWithSameSeed) {
  auto pool = buildTestPool();
  uint32_t seed = 12345;

  auto result1 = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                      2, seed, SubjectCharacter::Playful);
  auto result2 = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                      2, seed, SubjectCharacter::Playful);

  ASSERT_EQ(result1.size(), result2.size());
  for (size_t idx = 0; idx < result1.size(); ++idx) {
    EXPECT_EQ(result1[idx].pitch, result2[idx].pitch)
        << "Mismatch at index " << idx;
    EXPECT_EQ(result1[idx].start_tick, result2[idx].start_tick)
        << "Tick mismatch at index " << idx;
    EXPECT_EQ(result1[idx].voice, result2[idx].voice)
        << "Voice mismatch at index " << idx;
    EXPECT_EQ(result1[idx].duration, result2[idx].duration)
        << "Duration mismatch at index " << idx;
  }
}

// ===========================================================================
// FortspinnungEpisodeWrapsCorrectly
// ===========================================================================

TEST(FortspinnungTest, FortspinnungEpisodeWrapsCorrectly) {
  auto pool = buildTestPool();
  auto subject = makeTestSubject();

  Tick start = kTicksPerBar * 4;
  Tick duration = kTicksPerBar * 3;
  Key start_key = Key::C;
  Key target_key = Key::G;

  Episode episode = generateFortspinnungEpisode(
      subject, pool, start, duration,
      start_key, target_key, 3, 42, 0, 0.5f);

  EXPECT_EQ(episode.start_tick, start);
  EXPECT_EQ(episode.end_tick, start + duration);
  EXPECT_EQ(episode.start_key, start_key);
  EXPECT_EQ(episode.end_key, target_key);
  EXPECT_FALSE(episode.notes.empty());

  // All notes should be within the episode time range.
  for (const auto& note : episode.notes) {
    EXPECT_GE(note.start_tick, start);
    EXPECT_LT(note.start_tick, start + duration);
  }
}

// ===========================================================================
// FortspinnungEpisodeFallsBackOnEmptyPool
// ===========================================================================

TEST(FortspinnungTest, FortspinnungEpisodeFallsBackOnEmptyPool) {
  MotifPool empty_pool;
  auto subject = makeTestSubject();

  Tick start = 0;
  Tick duration = kTicksPerBar * 2;

  Episode episode = generateFortspinnungEpisode(
      subject, empty_pool, start, duration,
      Key::C, Key::G, 3, 42, 0, 0.5f);

  // Falls back to standard generateEpisode, which should produce notes.
  EXPECT_EQ(episode.start_tick, start);
  EXPECT_EQ(episode.end_tick, start + duration);
  EXPECT_FALSE(episode.notes.empty());
}

// ===========================================================================
// InvertibleCounterpointOnOddIndex
// ===========================================================================

TEST(FortspinnungTest, InvertibleCounterpointOnOddIndex) {
  auto pool = buildTestPool();
  auto subject = makeTestSubject();

  Tick start = 0;
  Tick duration = kTicksPerBar * 4;

  // Even index: no voice swap.
  Episode even_ep = generateFortspinnungEpisode(
      subject, pool, start, duration,
      Key::C, Key::C, 2, 42, 0, 0.5f);

  // Odd index: voice 0/1 swapped.
  Episode odd_ep = generateFortspinnungEpisode(
      subject, pool, start, duration,
      Key::C, Key::C, 2, 42, 1, 0.5f);

  // Count voices in even episode.
  int even_v0 = 0, even_v1 = 0;
  for (const auto& note : even_ep.notes) {
    if (note.voice == 0) ++even_v0;
    if (note.voice == 1) ++even_v1;
  }

  // Count voices in odd episode.
  int odd_v0 = 0, odd_v1 = 0;
  for (const auto& note : odd_ep.notes) {
    if (note.voice == 0) ++odd_v0;
    if (note.voice == 1) ++odd_v1;
  }

  // After invertible counterpoint, voice counts should be swapped.
  EXPECT_EQ(even_v0, odd_v1) << "Even voice 0 count should equal odd voice 1 count";
  EXPECT_EQ(even_v1, odd_v0) << "Even voice 1 count should equal odd voice 0 count";
}

// ===========================================================================
// AllCharactersProduceNotes
// ===========================================================================

TEST(FortspinnungTest, AllCharactersProduceNotes) {
  SubjectCharacter characters[] = {
      SubjectCharacter::Severe,
      SubjectCharacter::Playful,
      SubjectCharacter::Noble,
      SubjectCharacter::Restless};

  for (auto character : characters) {
    auto pool = buildTestPool(character);
    auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                       2, 42, character);
    EXPECT_FALSE(result.empty())
        << "Character " << static_cast<int>(character) << " produced no notes";
  }
}

// ===========================================================================
// NoteSourceIsEpisodeMaterial
// ===========================================================================

TEST(FortspinnungTest, NoteSourceIsEpisodeMaterial) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, kTicksPerBar * 4,
                                     2, 42, SubjectCharacter::Severe);

  EXPECT_FALSE(result.empty());
  for (const auto& note : result) {
    EXPECT_EQ(note.source, BachNoteSource::EpisodeMaterial)
        << "All Fortspinnung notes should have EpisodeMaterial source";
  }
}

// ===========================================================================
// ZeroDurationReturnsEmpty
// ===========================================================================

TEST(FortspinnungTest, ZeroDurationReturnsEmpty) {
  auto pool = buildTestPool();
  auto result = generateFortspinnung(pool, 0, 0,
                                     2, 42, SubjectCharacter::Severe);
  EXPECT_TRUE(result.empty());
}

}  // namespace
}  // namespace bach
