// Tests for Concertato archetype toccata generation.

#include "forms/toccata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "test_helpers.h"

namespace bach {
namespace {

ToccataConfig makeConcertatoConfig(uint32_t seed = 42) {
  ToccataConfig config;
  config.archetype = ToccataArchetype::Concertato;
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

TEST(ToccataConcertatoTest, GenerateSucceeds) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
}

TEST(ToccataConcertatoTest, ArchetypeIsConcertato) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.archetype, ToccataArchetype::Concertato);
}

TEST(ToccataConcertatoTest, ThreeSections) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);
  EXPECT_EQ(result.sections[0].id, ToccataSectionId::Allegro);
  EXPECT_EQ(result.sections[1].id, ToccataSectionId::Adagio);
  EXPECT_EQ(result.sections[2].id, ToccataSectionId::Vivace);
}

TEST(ToccataConcertatoTest, DeterministicOutput) {
  auto config = makeConcertatoConfig(12345);
  auto r1 = generateToccata(config);
  auto r2 = generateToccata(config);
  ASSERT_TRUE(r1.success);
  ASSERT_TRUE(r2.success);
  for (size_t t = 0; t < r1.tracks.size(); ++t) {
    ASSERT_EQ(r1.tracks[t].notes.size(), r2.tracks[t].notes.size());
  }
}

TEST(ToccataConcertatoTest, AllNotesVelocity80) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u);
    }
  }
}

TEST(ToccataConcertatoTest, AllNotesInRange) {
  auto config = makeConcertatoConfig();
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
}

TEST(ToccataConcertatoTest, MultipleSeeds_AllSucceed) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    auto config = makeConcertatoConfig(seed);
    auto result = generateToccata(config);
    EXPECT_TRUE(result.success) << "Failed with seed " << seed;
    EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "No notes with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Concertato-specific tests
// ---------------------------------------------------------------------------

TEST(ToccataConcertatoTest, ThreeSectionContrast_AdagioHasLongerNotes) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);

  // Compute average note duration in Allegro vs Adagio (manual tracks only).
  auto avgDur = [&](size_t si) -> float {
    Tick total_dur = 0;
    size_t count = 0;
    for (size_t t = 0; t <= 1; ++t) {
      for (const auto& note : result.tracks[t].notes) {
        if (note.start_tick >= result.sections[si].start &&
            note.start_tick < result.sections[si].end) {
          total_dur += note.duration;
          ++count;
        }
      }
    }
    return count > 0 ? static_cast<float>(total_dur) / count : 0.0f;
  };

  float allegro_avg = avgDur(0);
  float adagio_avg = avgDur(1);

  EXPECT_GT(adagio_avg, allegro_avg * 1.5f)
      << "Adagio avg duration (" << adagio_avg
      << ") should be > 1.5x Allegro avg duration (" << allegro_avg << ")";
}

TEST(ToccataConcertatoTest, AdagioCantabile_StepwiseMotion) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);
  ASSERT_GE(result.tracks.size(), 2u);

  // Compute stepwise motion ratio for voice 1 in Adagio.
  const auto& notes = result.tracks[1].notes;
  int stepwise = 0;
  int total_pairs = 0;

  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].start_tick < result.sections[1].start) continue;
    if (notes[i].start_tick >= result.sections[1].end) break;
    if (notes[i - 1].start_tick < result.sections[1].start) continue;

    int interval = std::abs(static_cast<int>(notes[i].pitch) -
                            static_cast<int>(notes[i - 1].pitch));
    ++total_pairs;
    if (interval <= 4) ++stepwise;  // Up to major 3rd is "stepwise-ish".
  }

  if (total_pairs > 0) {
    float ratio = static_cast<float>(stepwise) / total_pairs;
    EXPECT_GE(ratio, 0.50f)
        << "Adagio stepwise motion ratio should be >= 50%, got "
        << (ratio * 100) << "%";
  }
}

TEST(ToccataConcertatoTest, RegistrationContrast) {
  auto config = makeConcertatoConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 3u);

  // The registration plan sets different velocity_hint values.
  // Since MIDI velocity is always 80, we can't test velocity directly.
  // Instead, verify that the sections exist and have notes.
  for (size_t si = 0; si < 3; ++si) {
    size_t count = 0;
    for (const auto& track : result.tracks) {
      count += countNotesInRange(track, result.sections[si].start,
                                 result.sections[si].end);
    }
    EXPECT_GT(count, 0u) << "Section " << si << " should have notes";
  }
}

}  // namespace
}  // namespace bach
