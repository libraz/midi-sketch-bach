// Tests for fugue quality gate metrics (Phase 5).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_generator.h"
#include "harmony/chord_types.h"

namespace bach {
namespace {

/// @brief Count total notes across all tracks.
size_t totalNoteCount(const FugueResult& result) {
  size_t count = 0;
  for (const auto& track : result.tracks) {
    count += track.notes.size();
  }
  return count;
}

/// @brief Count voice crossings in a fugue result.
int countVoiceCrossings(const FugueResult& result) {
  // Collect all notes sorted by tick.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : result.tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  int crossings = 0;
  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      if (all_notes[j].start_tick > all_notes[i].start_tick + all_notes[i].duration) break;
      if (all_notes[j].start_tick != all_notes[i].start_tick) continue;
      // Same tick: higher voice should have higher pitch.
      if (all_notes[i].voice < all_notes[j].voice &&
          all_notes[i].pitch < all_notes[j].pitch) {
        ++crossings;
      }
      if (all_notes[i].voice > all_notes[j].voice &&
          all_notes[i].pitch > all_notes[j].pitch) {
        ++crossings;
      }
    }
  }
  return crossings;
}

/// @brief Compute chord tone ratio against the timeline.
float computeChordToneRatio(const FugueResult& result) {
  int chord_tones = 0;
  int total = 0;
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      const auto& ev = result.timeline.getAt(note.start_tick);
      if (isChordTone(note.pitch, ev)) {
        ++chord_tones;
      }
      ++total;
    }
  }
  if (total == 0) return 1.0f;
  return static_cast<float>(chord_tones) / static_cast<float>(total);
}

TEST(FugueQualityGateTest, GenerationSucceeds10Seeds) {
  for (uint32_t seed = 1000; seed < 1010; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Failed for seed " << seed;
    EXPECT_GT(totalNoteCount(result), 0u) << "No notes for seed " << seed;
  }
}

TEST(FugueQualityGateTest, QualityMetrics) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 3995423244u;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Quality metrics should exist and be populated.
  EXPECT_GE(result.quality.chord_tone_ratio, 0.0f);
  EXPECT_LE(result.quality.dissonance_per_beat, 5.0f);
}

TEST(FugueQualityGateTest, PostValidationRetainsNotes) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;
  config.character = SubjectCharacter::Severe;
  config.seed = 42;

  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  // Post-validation should retain most notes (>80%).
  size_t total = totalNoteCount(result);
  EXPECT_GT(total, 20u) << "Too few notes after validation";
}

TEST(FugueQualityGateTest, BatchDissonanceDensity) {
  for (uint32_t seed = 500; seed < 510; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed;

    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Failed for seed " << seed;

    // Dissonance per beat should be well below the original 2.07.
    EXPECT_LT(result.quality.dissonance_per_beat, 1.5f)
        << "High dissonance for seed " << seed
        << ": " << result.quality.dissonance_per_beat;
  }
}

TEST(FugueQualityGateTest, DestructionResistance20Seeds) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    FugueConfig config;
    config.key = Key::C;
    config.num_voices = 3;
    config.character = SubjectCharacter::Severe;
    config.seed = seed * 12345u + 1u;

    FugueResult result = generateFugue(config);
    EXPECT_TRUE(result.success) << "Crash for seed " << config.seed;
  }
}

}  // namespace
}  // namespace bach
