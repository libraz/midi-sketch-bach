#include "analysis/counterpoint_analyzer.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

TEST(CrossRelationTest, BNaturalVsBFlat_Detected) {
  std::vector<NoteEvent> notes;
  // Voice 0: B natural (71)
  NoteEvent v0;
  v0.pitch = 71;
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  // Voice 1: Bb (70) at same time
  NoteEvent v1;
  v1.pitch = 70;
  v1.start_tick = 0;
  v1.duration = kTicksPerBeat;
  v1.voice = 1;
  notes.push_back(v1);

  uint32_t count = countCrossRelations(notes, 2);
  EXPECT_GT(count, 0u);
}

TEST(CrossRelationTest, SamePitch_NotDetected) {
  std::vector<NoteEvent> notes;
  NoteEvent v0;
  v0.pitch = 60;
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  NoteEvent v1;
  v1.pitch = 60;
  v1.start_tick = 0;
  v1.duration = kTicksPerBeat;
  v1.voice = 1;
  notes.push_back(v1);

  uint32_t count = countCrossRelations(notes, 2);
  EXPECT_EQ(count, 0u);
}

TEST(CrossRelationTest, FarApart_NotDetected) {
  std::vector<NoteEvent> notes;
  NoteEvent v0;
  v0.pitch = 71;  // B natural
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  NoteEvent v1;
  v1.pitch = 70;  // Bb
  v1.start_tick = kTicksPerBeat * 10;  // Far away
  v1.duration = kTicksPerBeat;
  v1.voice = 1;
  notes.push_back(v1);

  uint32_t count = countCrossRelations(notes, 2);
  EXPECT_EQ(count, 0u);
}

TEST(CrossRelationTest, EF_NaturalHalfStep_NotDetected) {
  // E and F are natural half steps, not cross-relations.
  std::vector<NoteEvent> notes;
  NoteEvent v0;
  v0.pitch = 64;  // E
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  NoteEvent v1;
  v1.pitch = 65;  // F
  v1.start_tick = 0;
  v1.duration = kTicksPerBeat;
  v1.voice = 1;
  notes.push_back(v1);

  uint32_t count = countCrossRelations(notes, 2);
  EXPECT_EQ(count, 0u);
}

TEST(CrossRelationTest, FSharpVsFNatural_Detected) {
  std::vector<NoteEvent> notes;
  NoteEvent v0;
  v0.pitch = 66;  // F#
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  NoteEvent v1;
  v1.pitch = 65;  // F natural
  v1.start_tick = 0;
  v1.duration = kTicksPerBeat;
  v1.voice = 1;
  notes.push_back(v1);

  uint32_t count = countCrossRelations(notes, 2);
  EXPECT_GT(count, 0u);
}

TEST(CrossRelationTest, SingleVoice_NoCrossRelation) {
  std::vector<NoteEvent> notes;
  NoteEvent v0;
  v0.pitch = 71;
  v0.start_tick = 0;
  v0.duration = kTicksPerBeat;
  v0.voice = 0;
  notes.push_back(v0);

  uint32_t count = countCrossRelations(notes, 1);
  EXPECT_EQ(count, 0u);
}

}  // namespace
}  // namespace bach
