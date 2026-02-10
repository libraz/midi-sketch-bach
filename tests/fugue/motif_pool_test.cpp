// Tests for fugue/motif_pool.h -- motif pool construction, scoring,
// ranking, normalization, and immutability after build.

#include "fugue/motif_pool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper: create a subject note sequence with N quarter notes starting at C4.
// ---------------------------------------------------------------------------

std::vector<NoteEvent> makeSubjectNotes(size_t num_notes, Tick start_tick = 0) {
  std::vector<NoteEvent> notes;
  notes.reserve(num_notes);
  for (size_t idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = start_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(60 + (idx % 7));  // C4 through B4
    note.velocity = 80;
    note.voice = 0;
    notes.push_back(note);
  }
  return notes;
}

// ---------------------------------------------------------------------------
// Helper: create a countersubject note sequence.
// ---------------------------------------------------------------------------

std::vector<NoteEvent> makeCountersubjectNotes(size_t num_notes, Tick start_tick = 0) {
  std::vector<NoteEvent> notes;
  notes.reserve(num_notes);
  for (size_t idx = 0; idx < num_notes; ++idx) {
    NoteEvent note;
    note.start_tick = start_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    note.duration = kTicksPerBeat;
    note.pitch = static_cast<uint8_t>(72 - (idx % 5));  // C5 down through Ab4
    note.velocity = 80;
    note.voice = 1;
    notes.push_back(note);
  }
  return notes;
}

// ===========================================================================
// EmptyPoolFromEmptySubject
// ===========================================================================

TEST(MotifPoolTest, EmptyPoolFromEmptySubject) {
  MotifPool pool;
  std::vector<NoteEvent> empty_subject;
  std::vector<NoteEvent> empty_cs;

  pool.build(empty_subject, empty_cs, SubjectCharacter::Severe);

  EXPECT_TRUE(pool.empty());
  EXPECT_EQ(pool.size(), 0u);
  EXPECT_EQ(pool.best(), nullptr);
  EXPECT_EQ(pool.getByRank(0), nullptr);
}

// ===========================================================================
// BuildFromSubjectOnly
// ===========================================================================

TEST(MotifPoolTest, BuildFromSubjectOnly) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  // Should produce: subject_head, subject_characteristic, subject_tail, 2 fragments.
  // Total = 5 motifs (no countersubject).
  EXPECT_FALSE(pool.empty());
  EXPECT_EQ(pool.size(), 5u);

  // Verify no countersubject-origin motif is present.
  for (const auto& motif : pool.motifs()) {
    EXPECT_NE(motif.origin, "countersubject");
  }
}

// ===========================================================================
// BuildWithCountersubject
// ===========================================================================

TEST(MotifPoolTest, BuildWithCountersubject) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  auto counter = makeCountersubjectNotes(6);

  pool.build(subject, counter, SubjectCharacter::Playful);

  // Should produce: subject_head, subject_characteristic, subject_tail,
  //                 countersubject, 2 fragments = 6.
  EXPECT_EQ(pool.size(), 6u);

  // Verify countersubject-origin motif is present.
  bool found_cs = false;
  for (const auto& motif : pool.motifs()) {
    if (motif.origin == "countersubject") {
      found_cs = true;
      // CS head should have at most 4 notes.
      EXPECT_LE(motif.notes.size(), 4u);
      EXPECT_FLOAT_EQ(motif.characteristic_score, 0.7f);
      break;
    }
  }
  EXPECT_TRUE(found_cs);
}

// ===========================================================================
// MotifsSortedByScore
// ===========================================================================

TEST(MotifPoolTest, MotifsSortedByScore) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  auto counter = makeCountersubjectNotes(6);

  pool.build(subject, counter, SubjectCharacter::Noble);

  const auto& motifs = pool.motifs();
  ASSERT_GE(motifs.size(), 2u);

  for (size_t idx = 1; idx < motifs.size(); ++idx) {
    EXPECT_GE(motifs[idx - 1].characteristic_score, motifs[idx].characteristic_score)
        << "Motifs not sorted at index " << idx;
  }
}

// ===========================================================================
// BestReturnsHighestScore
// ===========================================================================

TEST(MotifPoolTest, BestReturnsHighestScore) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  const PooledMotif* top = pool.best();
  ASSERT_NE(top, nullptr);
  EXPECT_FLOAT_EQ(top->characteristic_score, 1.0f);
  EXPECT_EQ(top->origin, "subject_head");
}

// ===========================================================================
// GetByRankBounds
// ===========================================================================

TEST(MotifPoolTest, GetByRankBounds) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  // Rank 0 should be the best motif.
  const PooledMotif* rank0 = pool.getByRank(0);
  ASSERT_NE(rank0, nullptr);
  EXPECT_EQ(rank0, pool.best());

  // Rank within range should return a valid pointer.
  size_t last_rank = pool.size() - 1;
  const PooledMotif* last = pool.getByRank(last_rank);
  ASSERT_NE(last, nullptr);

  // Rank out of range should return nullptr.
  EXPECT_EQ(pool.getByRank(pool.size()), nullptr);
  EXPECT_EQ(pool.getByRank(pool.size() + 100), nullptr);
}

// ===========================================================================
// PoolIsReadOnly - build() then verify motifs don't change on second access
// ===========================================================================

TEST(MotifPoolTest, PoolIsReadOnly) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  // Snapshot the state.
  size_t original_size = pool.size();
  float original_best_score = pool.best()->characteristic_score;
  std::string original_best_origin = pool.best()->origin;

  // Access multiple times -- should remain identical.
  EXPECT_EQ(pool.size(), original_size);
  EXPECT_FLOAT_EQ(pool.best()->characteristic_score, original_best_score);
  EXPECT_EQ(pool.best()->origin, original_best_origin);

  // Verify motifs() returns same data.
  const auto& motifs_first = pool.motifs();
  const auto& motifs_second = pool.motifs();
  EXPECT_EQ(motifs_first.size(), motifs_second.size());
  for (size_t idx = 0; idx < motifs_first.size(); ++idx) {
    EXPECT_FLOAT_EQ(motifs_first[idx].characteristic_score,
                    motifs_second[idx].characteristic_score);
    EXPECT_EQ(motifs_first[idx].origin, motifs_second[idx].origin);
    EXPECT_EQ(motifs_first[idx].notes.size(), motifs_second[idx].notes.size());
  }
}

// ===========================================================================
// NormalizedToTickZero -- all motifs start at tick 0
// ===========================================================================

TEST(MotifPoolTest, NormalizedToTickZero) {
  MotifPool pool;
  // Create subject starting at a non-zero tick.
  auto subject = makeSubjectNotes(8, /*start_tick=*/kTicksPerBar * 3);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Restless);

  for (const auto& motif : pool.motifs()) {
    ASSERT_FALSE(motif.notes.empty()) << "Motif '" << motif.origin << "' has no notes";
    EXPECT_EQ(motif.notes[0].start_tick, 0u)
        << "Motif '" << motif.origin << "' not normalized to tick 0 (start_tick="
        << motif.notes[0].start_tick << ")";
  }
}

// ===========================================================================
// NormalizedToTickZero with countersubject starting at non-zero tick
// ===========================================================================

TEST(MotifPoolTest, NormalizedToTickZeroWithCountersubject) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8, /*start_tick=*/kTicksPerBar * 2);
  auto counter = makeCountersubjectNotes(6, /*start_tick=*/kTicksPerBar * 4);

  pool.build(subject, counter, SubjectCharacter::Noble);

  for (const auto& motif : pool.motifs()) {
    ASSERT_FALSE(motif.notes.empty()) << "Motif '" << motif.origin << "' has no notes";
    EXPECT_EQ(motif.notes[0].start_tick, 0u)
        << "Motif '" << motif.origin << "' not normalized to tick 0";
  }
}

// ===========================================================================
// FragmentsPresent -- fragments are in the pool
// ===========================================================================

TEST(MotifPoolTest, FragmentsPresent) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  int fragment_count = 0;
  for (const auto& motif : pool.motifs()) {
    if (motif.origin == "fragment") {
      ++fragment_count;
      EXPECT_FLOAT_EQ(motif.characteristic_score, 0.6f);
      // Each fragment should be roughly half the subject length.
      EXPECT_GE(motif.notes.size(), 1u);
    }
  }
  EXPECT_EQ(fragment_count, 2) << "Expected 2 fragments from subject split";
}

// ===========================================================================
// SmallSubject -- subject with fewer than 4 notes
// ===========================================================================

TEST(MotifPoolTest, SmallSubject) {
  MotifPool pool;
  auto subject = makeSubjectNotes(3);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Playful);

  // With only 3 notes:
  //   - subject_head: 3 notes (all available)
  //   - subject_characteristic: NOT added (size <= kHeadLength)
  //   - subject_tail: 3 notes
  //   - 2 fragments
  // Total = 4
  EXPECT_FALSE(pool.empty());
  EXPECT_GE(pool.size(), 3u);

  // Best should still be subject_head with score 1.0.
  EXPECT_FLOAT_EQ(pool.best()->characteristic_score, 1.0f);
}

// ===========================================================================
// SingleNoteSubject -- edge case with 1 note
// ===========================================================================

TEST(MotifPoolTest, SingleNoteSubject) {
  MotifPool pool;
  auto subject = makeSubjectNotes(1);
  std::vector<NoteEvent> empty_cs;

  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  // Should have at least the subject head.
  EXPECT_FALSE(pool.empty());
  EXPECT_GE(pool.size(), 1u);
  EXPECT_FLOAT_EQ(pool.best()->characteristic_score, 1.0f);
}

// ===========================================================================
// AllOriginsPresent -- verify expected origin labels
// ===========================================================================

TEST(MotifPoolTest, AllOriginsPresent) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  auto counter = makeCountersubjectNotes(6);

  pool.build(subject, counter, SubjectCharacter::Noble);

  std::set<std::string> origins;
  for (const auto& motif : pool.motifs()) {
    origins.insert(motif.origin);
  }

  EXPECT_TRUE(origins.count("subject_head") > 0);
  EXPECT_TRUE(origins.count("subject_characteristic") > 0);
  EXPECT_TRUE(origins.count("subject_tail") > 0);
  EXPECT_TRUE(origins.count("countersubject") > 0);
  EXPECT_TRUE(origins.count("fragment") > 0);
}

// ===========================================================================
// RebuildClearsOldData -- calling build() again replaces pool contents
// ===========================================================================

TEST(MotifPoolTest, RebuildClearsOldData) {
  MotifPool pool;
  auto subject = makeSubjectNotes(8);
  auto counter = makeCountersubjectNotes(6);

  // First build with countersubject.
  pool.build(subject, counter, SubjectCharacter::Noble);
  size_t size_with_cs = pool.size();

  // Second build without countersubject.
  std::vector<NoteEvent> empty_cs;
  pool.build(subject, empty_cs, SubjectCharacter::Severe);

  // Should have fewer motifs (no countersubject).
  EXPECT_LT(pool.size(), size_with_cs);

  // Verify no countersubject-origin motif remains.
  for (const auto& motif : pool.motifs()) {
    EXPECT_NE(motif.origin, "countersubject");
  }
}

}  // namespace
}  // namespace bach
