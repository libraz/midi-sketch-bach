#include "ornament/schleifer.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

TEST(SchleiferTest, BasicGeneration_ProducesNotes) {
  NoteEvent note;
  note.pitch = 67;  // G4
  note.start_tick = 0;
  note.duration = kTicksPerBeat * 2;  // Half note
  note.velocity = 80;
  note.voice = 0;

  auto result = generateSchleifer(note, Key::C, false);
  EXPECT_GE(result.size(), 3u);  // At least 2 grace notes + main
}

TEST(SchleiferTest, ShortNote_ReturnsEmpty) {
  NoteEvent note;
  note.pitch = 67;
  note.start_tick = 0;
  note.duration = kTicksPerBeat / 4;  // Too short
  note.velocity = 80;
  note.voice = 0;

  auto result = generateSchleifer(note, Key::C, false);
  EXPECT_TRUE(result.empty());
}

TEST(SchleiferTest, GraceNotesAscend) {
  NoteEvent note;
  note.pitch = 67;
  note.start_tick = 0;
  note.duration = kTicksPerBeat * 2;
  note.velocity = 80;
  note.voice = 0;

  auto result = generateSchleifer(note, Key::C, false);
  ASSERT_GE(result.size(), 3u);

  // Grace notes should ascend.
  for (size_t idx = 1; idx < result.size() - 1; ++idx) {
    EXPECT_GE(result[idx].pitch, result[idx - 1].pitch);
  }
}

TEST(SchleiferTest, MainNote_IsShortenedButPresent) {
  NoteEvent note;
  note.pitch = 67;
  note.start_tick = 0;
  note.duration = kTicksPerBeat * 2;
  note.velocity = 80;
  note.voice = 0;

  auto result = generateSchleifer(note, Key::C, false);
  ASSERT_FALSE(result.empty());

  // Last note should be the main note (same pitch).
  EXPECT_EQ(result.back().pitch, 67);
  EXPECT_LT(result.back().duration, note.duration);
}

TEST(DiatonicNeighborTest, UpperNeighbor_CMajor) {
  // Upper neighbor of C4 in C major should be D4.
  uint8_t neighbor = getDiatonicNeighbor(60, true, Key::C, false);
  EXPECT_EQ(neighbor, 62);
}

TEST(DiatonicNeighborTest, LowerNeighbor_CMajor) {
  // Lower neighbor of D4 in C major should be C4.
  uint8_t neighbor = getDiatonicNeighbor(62, false, Key::C, false);
  EXPECT_EQ(neighbor, 60);
}

TEST(DiatonicNeighborTest, MinorKey_HarmonicMinor) {
  // Upper neighbor of G#4 (Ab, pitch 68) in A minor (harmonic).
  // In A harmonic minor: A B C D E F G# -> neighbor of G# is A.
  uint8_t neighbor = getDiatonicNeighbor(68, true, Key::A, true);
  EXPECT_EQ(neighbor % 12, 9);  // A = pitch class 9
}

}  // namespace
}  // namespace bach
