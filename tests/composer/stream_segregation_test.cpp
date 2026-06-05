#include "composer/stream_segregation.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "composer/material.h"
#include "core/basic_types.h"

namespace bach::composer {
namespace {

std::vector<MaterialNote> line(std::initializer_list<std::uint8_t> pitches, Tick step) {
  std::vector<MaterialNote> notes;
  Tick tick = 0;
  for (std::uint8_t pitch : pitches) {
    MaterialNote note;
    note.start_tick = tick;
    note.duration = step;
    note.pitch = pitch;
    notes.push_back(note);
    tick += step;
  }
  return notes;
}

}  // namespace

TEST(StreamSegregationTest, AscendingP4ArpeggioChainStaysOneStream) {
  const auto notes = line({60, 65, 70, 75, 80}, kTicksPerBeat / 4);
  const auto result = stream_segregation::analyzeSpan(notes, 7);
  EXPECT_EQ(result.span_id, 7u);
  EXPECT_EQ(result.detected_stream_count, 1);
  EXPECT_TRUE(result.transition_note_indices.empty());
}

TEST(StreamSegregationTest, TwoSemitoneAlternationStaysOneStream) {
  const auto notes = line({60, 62, 60, 62, 60, 62}, kTicksPerBeat / 4);
  const auto result = stream_segregation::analyzeSpan(notes, 7);
  EXPECT_EQ(result.detected_stream_count, 1);
  EXPECT_TRUE(result.transition_note_indices.empty());
}

TEST(StreamSegregationTest, LargeFastAlternationDetectsTwoStreams) {
  const auto notes = line({60, 62, 73, 62, 64}, kTicksPerBeat / 4);
  const auto result = stream_segregation::analyzeSpan(notes, 7);
  EXPECT_EQ(result.detected_stream_count, 2);
  ASSERT_FALSE(result.transition_note_indices.empty());
  EXPECT_GE(result.stream_separation_semitones, 9);
}

}  // namespace bach::composer
