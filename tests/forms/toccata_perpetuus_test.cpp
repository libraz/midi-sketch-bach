// Tests for Perpetuus archetype toccata generation.

#include "forms/toccata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "test_helpers.h"

namespace bach {
namespace {

ToccataConfig makePerpetuusConfig(uint32_t seed = 42) {
  ToccataConfig config;
  config.archetype = ToccataArchetype::Perpetuus;
  config.key = {Key::D, true};
  config.bpm = 80;
  config.seed = seed;
  config.num_voices = 3;
  config.total_bars = 24;
  return config;
}

size_t countNotesInRange(const Track& track, Tick start, Tick end) {
  size_t count = 0;
  for (const auto& note : track.notes) {
    if (note.start_tick >= start && note.start_tick < end) ++count;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Basic generation
// ---------------------------------------------------------------------------

TEST(ToccataPerpetuusTest, GenerateSucceeds) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
  EXPECT_GT(result.total_duration_ticks, 0u);
}

TEST(ToccataPerpetuusTest, ArchetypeIsPerpetuus) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.archetype, ToccataArchetype::Perpetuus);
}

TEST(ToccataPerpetuusTest, ThreeSections) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);
  EXPECT_EQ(result.sections[0].id, ToccataSectionId::Ascent);
  EXPECT_EQ(result.sections[1].id, ToccataSectionId::Plateau);
  EXPECT_EQ(result.sections[2].id, ToccataSectionId::Climax);
}

TEST(ToccataPerpetuusTest, DeterministicOutput) {
  auto config = makePerpetuusConfig(12345);
  auto r1 = generateToccata(config);
  auto r2 = generateToccata(config);
  ASSERT_TRUE(r1.success);
  ASSERT_TRUE(r2.success);
  ASSERT_EQ(r1.tracks.size(), r2.tracks.size());
  for (size_t t = 0; t < r1.tracks.size(); ++t) {
    ASSERT_EQ(r1.tracks[t].notes.size(), r2.tracks[t].notes.size());
    for (size_t n = 0; n < r1.tracks[t].notes.size(); ++n) {
      EXPECT_EQ(r1.tracks[t].notes[n].pitch, r2.tracks[t].notes[n].pitch);
      EXPECT_EQ(r1.tracks[t].notes[n].start_tick, r2.tracks[t].notes[n].start_tick);
    }
  }
}

TEST(ToccataPerpetuusTest, AllNotesVelocity80) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u);
    }
  }
}

TEST(ToccataPerpetuusTest, AllNotesInRange) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual1Low);
    EXPECT_LE(note.pitch, organ_range::kManual1High);
  }
  for (const auto& note : result.tracks[1].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual2Low);
    EXPECT_LE(note.pitch, organ_range::kManual2High);
  }
  if (result.tracks.size() >= 3) {
    for (const auto& note : result.tracks[2].notes) {
      EXPECT_GE(note.pitch, organ_range::kPedalLow);
      EXPECT_LE(note.pitch, organ_range::kPedalHigh);
    }
  }
}

TEST(ToccataPerpetuusTest, NotesSorted) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    for (size_t i = 1; i < track.notes.size(); ++i) {
      EXPECT_LE(track.notes[i - 1].start_tick, track.notes[i].start_tick);
    }
  }
}

TEST(ToccataPerpetuusTest, AllNotesPositiveDuration) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_GT(note.duration, 0u);
    }
  }
}

TEST(ToccataPerpetuusTest, MultipleSeeds_AllSucceed) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    auto config = makePerpetuusConfig(seed);
    auto result = generateToccata(config);
    EXPECT_TRUE(result.success) << "Failed with seed " << seed;
    EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "No notes with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Perpetuus-specific tests
// ---------------------------------------------------------------------------

TEST(ToccataPerpetuusTest, ContinuousTexture_Voice0HasNoLargeGaps) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GT(result.tracks[0].notes.size(), 1u);

  const auto& notes = result.tracks[0].notes;
  int large_gaps = 0;
  for (size_t i = 1; i < notes.size(); ++i) {
    Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;
    if (notes[i].start_tick > prev_end) {
      Tick gap = notes[i].start_tick - prev_end;
      // Allow small gaps from overlap cleanup, but no bar-sized gaps.
      if (gap > kTicksPerBar) ++large_gaps;
    }
  }
  EXPECT_LE(large_gaps, 1) << "Voice 0 should have continuous texture (moto perpetuo)";
}

TEST(ToccataPerpetuusTest, AscendingEnergy_NoteDensityIncreases) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);

  // Count notes in each section across all tracks.
  auto countSection = [&](size_t si) -> size_t {
    size_t count = 0;
    for (const auto& track : result.tracks) {
      count += countNotesInRange(track, result.sections[si].start,
                                 result.sections[si].end);
    }
    return count;
  };

  size_t ascent = countSection(0);
  size_t plateau = countSection(1);
  size_t climax = countSection(2);

  // Normalize by section duration for density comparison.
  float ascent_density = static_cast<float>(ascent) /
      static_cast<float>(result.sections[0].end - result.sections[0].start);
  float plateau_density = static_cast<float>(plateau) /
      static_cast<float>(result.sections[1].end - result.sections[1].start);
  float climax_density = static_cast<float>(climax) /
      static_cast<float>(result.sections[2].end - result.sections[2].start);

  EXPECT_LE(ascent_density, climax_density)
      << "Climax should have >= note density than Ascent";
  EXPECT_LE(ascent_density, plateau_density)
      << "Plateau should have >= note density than Ascent";
}

TEST(ToccataPerpetuusTest, PedalDelayedEntry) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_GE(result.tracks.size(), 3u);
  ASSERT_GE(result.sections.size(), 1u);

  // Pedal should not appear in the first 60% of Ascent.
  Tick ascent_60 = result.sections[0].start +
      (result.sections[0].end - result.sections[0].start) * 60 / 100;
  size_t early_pedal = countNotesInRange(result.tracks[2],
                                          result.sections[0].start, ascent_60);
  EXPECT_EQ(early_pedal, 0u)
      << "Pedal should not have notes in first 60% of Ascent";
}

TEST(ToccataPerpetuusTest, VoiceThickening) {
  auto config = makePerpetuusConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);

  // Count active voices per section (voices that have at least 1 note).
  auto activeVoices = [&](size_t si) -> int {
    int count = 0;
    for (const auto& track : result.tracks) {
      if (countNotesInRange(track, result.sections[si].start,
                            result.sections[si].end) > 0) {
        ++count;
      }
    }
    return count;
  };

  int ascent_voices = activeVoices(0);
  int climax_voices = activeVoices(2);

  EXPECT_LE(ascent_voices, climax_voices)
      << "Climax should have >= active voices than Ascent "
      << "(ascent=" << ascent_voices << ", climax=" << climax_voices << ")";
}

TEST(ToccataPerpetuusTest, ArchetypeDiversityVsDramaticus) {
  // Same seed, different archetypes should produce different section IDs.
  ToccataConfig config_d;
  config_d.archetype = ToccataArchetype::Dramaticus;
  config_d.key = {Key::D, true};
  config_d.seed = 42;
  config_d.total_bars = 24;

  ToccataConfig config_p;
  config_p.archetype = ToccataArchetype::Perpetuus;
  config_p.key = {Key::D, true};
  config_p.seed = 42;
  config_p.total_bars = 24;

  auto r_d = generateToccata(config_d);
  auto r_p = generateToccata(config_p);

  ASSERT_TRUE(r_d.success);
  ASSERT_TRUE(r_p.success);

  // Section IDs must differ.
  ASSERT_FALSE(r_d.sections.empty());
  ASSERT_FALSE(r_p.sections.empty());
  EXPECT_NE(static_cast<int>(r_d.sections[0].id),
            static_cast<int>(r_p.sections[0].id));
}

}  // namespace
}  // namespace bach
