#include "composer/renderer.h"

#include <gtest/gtest.h>

namespace bach::composer {
namespace {

NoteEvent note(Tick start, Tick duration, std::uint8_t pitch, VoiceId voice) {
  NoteEvent result;
  result.start_tick = start;
  result.duration = duration;
  result.pitch = pitch;
  result.velocity = 80;
  result.voice = voice;
  return result;
}

TEST(RendererTest, GroupsByVoiceAndDropsZeroDurationAfterOverlapClamp) {
  const std::vector<NoteEvent> notes = {
      note(0, 480, 60, 2),
      note(0, 480, 64, 2),
      note(0, 480, 48, 0),
      note(480, 0, 50, 0),
  };
  const auto tracks = Renderer{}.render(notes);
  ASSERT_EQ(tracks.size(), 2u);
  EXPECT_EQ(tracks[0].channel, 0);
  EXPECT_EQ(tracks[1].channel, 2);
  ASSERT_EQ(tracks[1].notes.size(), 1u);
  EXPECT_EQ(tracks[1].notes.front().pitch, 64);
  EXPECT_GT(tracks[0].notes.front().duration, 0u);
}

}  // namespace
}  // namespace bach::composer
