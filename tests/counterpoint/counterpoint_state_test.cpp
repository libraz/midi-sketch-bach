// Tests for counterpoint/counterpoint_state.h -- voice registration,
// note storage, and tick-based lookup.

#include "counterpoint/counterpoint_state.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Voice registration
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, RegisterVoice) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(1, 48, 96);

  EXPECT_EQ(state.voiceCount(), 2u);
  EXPECT_EQ(state.getActiveVoices().size(), 2u);
  EXPECT_EQ(state.getActiveVoices()[0], 0);
  EXPECT_EQ(state.getActiveVoices()[1], 1);
}

TEST(CounterpointStateTest, VoiceRange) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  const auto* range = state.getVoiceRange(0);
  ASSERT_NE(range, nullptr);
  EXPECT_EQ(range->low, 36);
  EXPECT_EQ(range->high, 96);
}

TEST(CounterpointStateTest, UnregisteredVoiceRangeIsNull) {
  CounterpointState state;
  EXPECT_EQ(state.getVoiceRange(99), nullptr);
}

TEST(CounterpointStateTest, DuplicateRegistrationUpdatesRange) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.registerVoice(0, 48, 84);

  // Range should be updated.
  const auto* range = state.getVoiceRange(0);
  ASSERT_NE(range, nullptr);
  EXPECT_EQ(range->low, 48);
  EXPECT_EQ(range->high, 84);

  // But voice count should not increase.
  EXPECT_EQ(state.voiceCount(), 1u);
  EXPECT_EQ(state.getActiveVoices().size(), 1u);
}

// ---------------------------------------------------------------------------
// Note management
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, AddNoteChronological) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note1{0, 480, 60, 80, 0};
  NoteEvent note2{480, 480, 62, 80, 0};
  state.addNote(0, note1);
  state.addNote(0, note2);

  const auto& notes = state.getVoiceNotes(0);
  ASSERT_EQ(notes.size(), 2u);
  EXPECT_EQ(notes[0].start_tick, 0u);
  EXPECT_EQ(notes[1].start_tick, 480u);
}

TEST(CounterpointStateTest, AddNoteOutOfOrder) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note2{480, 480, 62, 80, 0};
  NoteEvent note1{0, 480, 60, 80, 0};
  state.addNote(0, note2);
  state.addNote(0, note1);

  const auto& notes = state.getVoiceNotes(0);
  ASSERT_EQ(notes.size(), 2u);
  // Should be sorted by start_tick.
  EXPECT_EQ(notes[0].start_tick, 0u);
  EXPECT_EQ(notes[1].start_tick, 480u);
}

TEST(CounterpointStateTest, AddNoteToUnregisteredVoiceIsIgnored) {
  CounterpointState state;
  NoteEvent note{0, 480, 60, 80, 5};
  state.addNote(5, note);  // Voice 5 not registered.

  EXPECT_TRUE(state.getVoiceNotes(5).empty());
}

// ---------------------------------------------------------------------------
// GetLastNote
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, GetLastNote) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note1{0, 480, 60, 80, 0};
  NoteEvent note2{480, 480, 64, 80, 0};
  state.addNote(0, note1);
  state.addNote(0, note2);

  const NoteEvent* last = state.getLastNote(0);
  ASSERT_NE(last, nullptr);
  EXPECT_EQ(last->pitch, 64);
  EXPECT_EQ(last->start_tick, 480u);
}

TEST(CounterpointStateTest, GetLastNoteEmptyIsNull) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  EXPECT_EQ(state.getLastNote(0), nullptr);
}

TEST(CounterpointStateTest, GetLastNoteUnregisteredIsNull) {
  CounterpointState state;
  EXPECT_EQ(state.getLastNote(99), nullptr);
}

// ---------------------------------------------------------------------------
// GetNoteAt
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, GetNoteAtExactStart) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note{480, 480, 60, 80, 0};
  state.addNote(0, note);

  const NoteEvent* found = state.getNoteAt(0, 480);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->pitch, 60);
}

TEST(CounterpointStateTest, GetNoteAtMiddleOfDuration) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note{480, 960, 60, 80, 0};  // Ticks 480-1439
  state.addNote(0, note);

  const NoteEvent* found = state.getNoteAt(0, 720);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->pitch, 60);
}

TEST(CounterpointStateTest, GetNoteAtAfterEnd) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note{480, 480, 60, 80, 0};  // Ticks 480-959
  state.addNote(0, note);

  // At tick 960, the note has ended.
  EXPECT_EQ(state.getNoteAt(0, 960), nullptr);
}

TEST(CounterpointStateTest, GetNoteAtBeforeStart) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note{480, 480, 60, 80, 0};
  state.addNote(0, note);

  EXPECT_EQ(state.getNoteAt(0, 0), nullptr);
}

TEST(CounterpointStateTest, GetNoteAtMultipleNotes) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);

  NoteEvent note1{0, 480, 60, 80, 0};
  NoteEvent note2{480, 480, 64, 80, 0};
  NoteEvent note3{960, 480, 67, 80, 0};
  state.addNote(0, note1);
  state.addNote(0, note2);
  state.addNote(0, note3);

  const NoteEvent* at_240 = state.getNoteAt(0, 240);
  ASSERT_NE(at_240, nullptr);
  EXPECT_EQ(at_240->pitch, 60);

  const NoteEvent* at_600 = state.getNoteAt(0, 600);
  ASSERT_NE(at_600, nullptr);
  EXPECT_EQ(at_600->pitch, 64);

  const NoteEvent* at_1000 = state.getNoteAt(0, 1000);
  ASSERT_NE(at_1000, nullptr);
  EXPECT_EQ(at_1000->pitch, 67);
}

// ---------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, DefaultKeyIsC) {
  CounterpointState state;
  EXPECT_EQ(state.getKey(), Key::C);
}

TEST(CounterpointStateTest, SetKey) {
  CounterpointState state;
  state.setKey(Key::G);
  EXPECT_EQ(state.getKey(), Key::G);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, DefaultTickIsZero) {
  CounterpointState state;
  EXPECT_EQ(state.getCurrentTick(), 0u);
}

TEST(CounterpointStateTest, SetCurrentTick) {
  CounterpointState state;
  state.setCurrentTick(1920);
  EXPECT_EQ(state.getCurrentTick(), 1920u);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST(CounterpointStateTest, ClearResetsEverything) {
  CounterpointState state;
  state.registerVoice(0, 36, 96);
  state.addNote(0, {0, 480, 60, 80, 0});
  state.setCurrentTick(1920);
  state.setKey(Key::G);

  state.clear();

  EXPECT_EQ(state.voiceCount(), 0u);
  EXPECT_TRUE(state.getActiveVoices().empty());
  EXPECT_EQ(state.getCurrentTick(), 0u);
  EXPECT_EQ(state.getKey(), Key::C);
}

}  // namespace
}  // namespace bach
