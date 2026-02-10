// Tests for episode harmonic validation (Phase 3).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/episode.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "fugue/subject.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

TEST(ChordToneUtilsTest, NearestChordToneSnapsToRoot) {
  HarmonicEvent ev;
  ev.chord.root_pitch = 60;  // C4
  ev.chord.quality = ChordQuality::Major;

  // C# (61) should snap to C (60) or E (64) -- C is closer.
  uint8_t result = nearestChordTone(61, ev);
  EXPECT_TRUE(result == 60 || result == 64) << "Got " << static_cast<int>(result);
}

TEST(ChordToneUtilsTest, NearestChordToneSnapsToThird) {
  HarmonicEvent ev;
  ev.chord.root_pitch = 60;  // C4
  ev.chord.quality = ChordQuality::Major;

  // D (62) is equidistant from C (60) and E (64).
  uint8_t result = nearestChordTone(62, ev);
  EXPECT_TRUE(result == 60 || result == 64) << "Got " << static_cast<int>(result);
}

TEST(ChordToneUtilsTest, NearestChordToneSnapsToFifth) {
  HarmonicEvent ev;
  ev.chord.root_pitch = 60;  // C4
  ev.chord.quality = ChordQuality::Major;

  // F# (66) should snap to G (67).
  uint8_t result = nearestChordTone(66, ev);
  EXPECT_EQ(result, 67u);
}

TEST(ChordToneUtilsTest, NearestChordToneMinorChord) {
  HarmonicEvent ev;
  ev.chord.root_pitch = 60;  // C minor
  ev.chord.quality = ChordQuality::Minor;

  // D (62) should snap to Eb (63) -- minor 3rd.
  uint8_t result = nearestChordTone(62, ev);
  EXPECT_EQ(result, 63u);
}

TEST(ChordToneUtilsTest, ChordToneStaysUnchanged) {
  HarmonicEvent ev;
  ev.chord.root_pitch = 60;
  ev.chord.quality = ChordQuality::Major;

  // E (64) is already a chord tone (major 3rd).
  EXPECT_EQ(nearestChordTone(64, ev), 64u);
}

TEST(EpisodeHarmonicTest, FugueGenerationDoesNotCrash) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 3995423244u;

  FugueResult result = generateFugue(config);
  EXPECT_TRUE(result.success);
}

TEST(EpisodeHarmonicTest, MultipleSeedsSucceed) {
  for (uint32_t seed = 100; seed < 110; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Failed for seed " << seed;
  }
}

}  // namespace
}  // namespace bach
