// Tests for binary form repeats.

#include "forms/goldberg/goldberg_binary.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

TEST(GoldbergBinaryTest, EmptyInputProducesEmptyOutput) {
  auto result = applyBinaryRepeats({}, 1440 * 16, false);
  EXPECT_TRUE(result.empty());
}

TEST(GoldbergBinaryTest, NoteCountDoubles) {
  // 4 notes in A, 4 notes in B = 8 unique. Repeats should produce 16.
  Tick section_ticks = 1440 * 16;  // 16 bars of 3/4

  std::vector<NoteEvent> notes;
  // A section notes (bars 1-16)
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent evt;
    evt.start_tick = static_cast<Tick>(idx) * 1440;
    evt.duration = 480;
    evt.pitch = 60;
    evt.velocity = 80;
    notes.push_back(evt);
  }
  // B section notes (bars 17-32)
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent evt;
    evt.start_tick = section_ticks + static_cast<Tick>(idx) * 1440;
    evt.duration = 480;
    evt.pitch = 67;
    evt.velocity = 80;
    notes.push_back(evt);
  }

  auto result = applyBinaryRepeats(notes, section_ticks, false);
  EXPECT_EQ(result.size(), 16u);
}

TEST(GoldbergBinaryTest, CorrectOrdering_AABB) {
  Tick section_ticks = 1920;  // 1 bar = 1 section (simplified)

  NoteEvent a_note;
  a_note.start_tick = 0;
  a_note.duration = 480;
  a_note.pitch = 60;
  a_note.velocity = 80;

  NoteEvent b_note;
  b_note.start_tick = 1920;
  b_note.duration = 480;
  b_note.pitch = 67;
  b_note.velocity = 80;

  auto result = applyBinaryRepeats({a_note, b_note}, section_ticks, false);
  ASSERT_EQ(result.size(), 4u);

  // A at tick 0, A repeat at tick 1920, B at tick 3840, B repeat at tick 5760
  EXPECT_EQ(result[0].pitch, 60);
  EXPECT_EQ(result[0].start_tick, 0u);
  EXPECT_EQ(result[1].pitch, 60);
  EXPECT_EQ(result[1].start_tick, section_ticks);
  EXPECT_EQ(result[2].pitch, 67);
  EXPECT_EQ(result[2].start_tick, section_ticks * 2);
  EXPECT_EQ(result[3].pitch, 67);
  EXPECT_EQ(result[3].start_tick, section_ticks * 3);
}

TEST(GoldbergBinaryTest, TotalDurationIs4xSection) {
  Tick section_ticks = 1440 * 16;  // 16 bars of 3/4

  NoteEvent last_note;
  last_note.start_tick = section_ticks * 2 - 480;  // Last beat of B section
  last_note.duration = 480;
  last_note.pitch = 55;
  last_note.velocity = 80;

  NoteEvent first_note;
  first_note.start_tick = 0;
  first_note.duration = 480;
  first_note.pitch = 60;
  first_note.velocity = 80;

  auto result = applyBinaryRepeats({first_note, last_note}, section_ticks, false);
  ASSERT_FALSE(result.empty());

  // Last note should be in the B repeat section
  Tick max_tick = 0;
  for (const auto& evt : result) {
    Tick end = evt.start_tick + evt.duration;
    if (end > max_tick) max_tick = end;
  }
  EXPECT_LE(max_tick, section_ticks * 4);
}

}  // namespace
}  // namespace bach
