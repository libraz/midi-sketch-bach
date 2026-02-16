// Tests for fugue texture density deviation detection.
// Verifies: density variation exists in 4-voice fugues (not all positions
// have all 4 voices sounding). Based on BWV578 reference: only 11% is
// full 4-voice tutti, 56% is 3 voices, 24% is 2 voices.

#include "fugue/fugue_generator.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/// @brief Create a FugueConfig for texture density testing.
/// @param seed Random seed.
/// @return FugueConfig with 4-voice settings.
FugueConfig makeDensityTestConfig(uint32_t seed = 42) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 4;
  config.bpm = 72;
  config.seed = seed;
  config.character = SubjectCharacter::Severe;
  config.max_subject_retries = 10;
  return config;
}

// ---------------------------------------------------------------------------
// Texture density variation tests
// ---------------------------------------------------------------------------

TEST(FugueTextureDensityTest, FourVoice_HasDensityVariation_Seed42) {
  FugueConfig config = makeDensityTestConfig(42);
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_EQ(result.tracks.size(), 4u);

  // Collect all notes from all tracks.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : result.tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }

  // Sample beat positions and count active voices at each.
  Tick total_ticks = 0;
  for (const auto& section : result.structure.sections) {
    if (section.end_tick > total_ticks) total_ticks = section.end_tick;
  }

  int beats_with_1_or_2_voices = 0;
  int total_beats_sampled = 0;

  for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
    // Count how many voices have notes sounding at this beat.
    bool voice_active[4] = {false, false, false, false};
    for (const auto& note : all_notes) {
      if (note.voice >= 4) continue;
      if (note.start_tick <= beat &&
          beat < note.start_tick + note.duration) {
        voice_active[note.voice] = true;
      }
    }

    int active_count = 0;
    for (int v = 0; v < 4; ++v) {
      if (voice_active[v]) ++active_count;
    }

    ++total_beats_sampled;
    if (active_count == 1 || active_count == 2) {
      ++beats_with_1_or_2_voices;
    }
  }

  // There must be at least some positions where only 1 or 2 voices are
  // sounding (proves density variation exists).
  EXPECT_GT(beats_with_1_or_2_voices, 0)
      << "Seed 42: no beat positions with only 1-2 voices active out of "
      << total_beats_sampled << " sampled beats. "
      << "A 4-voice fugue should have texture density variation.";
}

TEST(FugueTextureDensityTest, FourVoice_HasDensityVariation_MultiSeed) {
  uint32_t seeds[] = {1, 7, 42, 99, 200};
  for (uint32_t seed : seeds) {
    FugueConfig config = makeDensityTestConfig(seed);
    FugueResult result = generateFugue(config);
    ASSERT_TRUE(result.success) << "Seed " << seed << ": " << result.error_message;
    ASSERT_EQ(result.tracks.size(), 4u);

    std::vector<NoteEvent> all_notes;
    for (const auto& track : result.tracks) {
      all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
    }

    Tick total_ticks = 0;
    for (const auto& section : result.structure.sections) {
      if (section.end_tick > total_ticks) total_ticks = section.end_tick;
    }

    int beats_with_fewer_than_4 = 0;
    int total_beats = 0;

    for (Tick beat = 0; beat < total_ticks; beat += kTicksPerBeat) {
      bool voice_active[4] = {false, false, false, false};
      for (const auto& note : all_notes) {
        if (note.voice >= 4) continue;
        if (note.start_tick <= beat &&
            beat < note.start_tick + note.duration) {
          voice_active[note.voice] = true;
        }
      }

      int active = 0;
      for (int v = 0; v < 4; ++v) {
        if (voice_active[v]) ++active;
      }

      ++total_beats;
      if (active < 4) ++beats_with_fewer_than_4;
    }

    // BWV578: 89% of the time fewer than 4 voices. We use a lenient
    // threshold of at least 25% having reduced voices.
    if (total_beats > 0) {
      float ratio = static_cast<float>(beats_with_fewer_than_4) /
                    static_cast<float>(total_beats);
      EXPECT_GT(ratio, 0.25f)
          << "Seed " << seed << ": only " << (ratio * 100.0f)
          << "% of beats have fewer than 4 voices (need > 25%)";
    }
  }
}

// ---------------------------------------------------------------------------
// Density variation across sections (exposition vs episodes)
// ---------------------------------------------------------------------------

TEST(FugueTextureDensityTest, ExpositionHasGradualVoiceEntry) {
  FugueConfig config = makeDensityTestConfig(42);
  FugueResult result = generateFugue(config);
  ASSERT_TRUE(result.success);

  auto expositions = result.structure.getSectionsByType(SectionType::Exposition);
  ASSERT_EQ(expositions.size(), 1u);

  Tick expo_start = expositions[0].start_tick;
  Tick expo_end = expositions[0].end_tick;
  Tick expo_mid = expo_start + (expo_end - expo_start) / 2;

  std::vector<NoteEvent> all_notes;
  for (const auto& track : result.tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }

  // Count active voices in early exposition vs late exposition.
  auto countActiveVoicesAt = [&](Tick tick) {
    int count = 0;
    for (const auto& note : all_notes) {
      if (note.voice >= 4) continue;
      if (note.start_tick <= tick &&
          tick < note.start_tick + note.duration) {
        ++count;
      }
    }
    return count;
  };

  // Sample a few beats in each half.
  int early_total = 0, early_count = 0;
  int late_total = 0, late_count = 0;

  for (Tick beat = expo_start; beat < expo_mid; beat += kTicksPerBeat) {
    early_total += countActiveVoicesAt(beat);
    ++early_count;
  }
  for (Tick beat = expo_mid; beat < expo_end; beat += kTicksPerBeat) {
    late_total += countActiveVoicesAt(beat);
    ++late_count;
  }

  if (early_count > 0 && late_count > 0) {
    float early_avg = static_cast<float>(early_total) / early_count;
    float late_avg = static_cast<float>(late_total) / late_count;

    // In a fugue exposition, voices enter one by one. The late half
    // should have at least as many average active voices as the early half.
    EXPECT_GE(late_avg, early_avg)
        << "Exposition late half avg voices (" << late_avg
        << ") should be >= early half (" << early_avg << ")";
  }
}

}  // namespace
}  // namespace bach
