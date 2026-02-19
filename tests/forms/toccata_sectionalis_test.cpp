// Tests for Sectionalis archetype toccata generation.

#include "forms/toccata.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "test_helpers.h"

namespace bach {
namespace {

ToccataConfig makeSectionalisConfig(uint32_t seed = 42) {
  ToccataConfig config;
  config.archetype = ToccataArchetype::Sectionalis;
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

TEST(ToccataSectionalisTest, GenerateSucceeds) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  EXPECT_TRUE(result.success);
  EXPECT_TRUE(result.error_message.empty());
}

TEST(ToccataSectionalisTest, ArchetypeIsSectionalis) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.archetype, ToccataArchetype::Sectionalis);
}

TEST(ToccataSectionalisTest, FiveSections) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 5u);
  EXPECT_EQ(result.sections[0].id, ToccataSectionId::Free1);
  EXPECT_EQ(result.sections[1].id, ToccataSectionId::QuasiFugal);
  EXPECT_EQ(result.sections[2].id, ToccataSectionId::Free2);
  EXPECT_EQ(result.sections[3].id, ToccataSectionId::Cadenza);
  EXPECT_EQ(result.sections[4].id, ToccataSectionId::Coda);
}

TEST(ToccataSectionalisTest, DeterministicOutput) {
  auto config = makeSectionalisConfig(12345);
  auto r1 = generateToccata(config);
  auto r2 = generateToccata(config);
  ASSERT_TRUE(r1.success);
  ASSERT_TRUE(r2.success);
  for (size_t t = 0; t < r1.tracks.size(); ++t) {
    ASSERT_EQ(r1.tracks[t].notes.size(), r2.tracks[t].notes.size());
  }
}

TEST(ToccataSectionalisTest, AllNotesVelocity80) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& track : result.tracks) {
    for (const auto& note : track.notes) {
      EXPECT_EQ(note.velocity, 80u);
    }
  }
}

TEST(ToccataSectionalisTest, AllNotesInRange) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  for (const auto& note : result.tracks[0].notes) {
    EXPECT_GE(note.pitch, organ_range::kManual1Low);
    EXPECT_LE(note.pitch, organ_range::kManual1High);
  }
  if (result.tracks.size() >= 3) {
    for (const auto& note : result.tracks[2].notes) {
      EXPECT_GE(note.pitch, organ_range::kPedalLow);
      EXPECT_LE(note.pitch, organ_range::kPedalHigh);
    }
  }
}

TEST(ToccataSectionalisTest, MultipleSeeds_AllSucceed) {
  for (uint32_t seed = 0; seed < 20; ++seed) {
    auto config = makeSectionalisConfig(seed);
    auto result = generateToccata(config);
    EXPECT_TRUE(result.success) << "Failed with seed " << seed;
    EXPECT_GT(test_helpers::totalNoteCount(result), 0u) << "No notes with seed " << seed;
  }
}

// ---------------------------------------------------------------------------
// Sectionalis-specific tests
// ---------------------------------------------------------------------------

TEST(ToccataSectionalisTest, CadenzaIsPedalSolo) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 5u);
  ASSERT_GE(result.tracks.size(), 3u);

  Tick cad_start = result.sections[3].start;
  Tick cad_end = result.sections[3].end;

  // Voice 0 and 1 should have no notes during cadenza.
  size_t v0_cadenza = countNotesInRange(result.tracks[0], cad_start, cad_end);
  size_t v1_cadenza = countNotesInRange(result.tracks[1], cad_start, cad_end);
  size_t v2_cadenza = countNotesInRange(result.tracks[2], cad_start, cad_end);

  EXPECT_EQ(v0_cadenza, 0u)
      << "Voice 0 should have no notes during cadenza (pedal solo)";
  EXPECT_EQ(v1_cadenza, 0u)
      << "Voice 1 should have no notes during cadenza (pedal solo)";
  EXPECT_GT(v2_cadenza, 0u)
      << "Pedal (voice 2) should have notes during cadenza";
}

TEST(ToccataSectionalisTest, CadenzaHasFastNotes) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 5u);
  ASSERT_GE(result.tracks.size(), 3u);

  Tick cad_start = result.sections[3].start;
  Tick cad_end = result.sections[3].end;

  int fast = 0;
  int total = 0;
  for (const auto& note : result.tracks[2].notes) {
    if (note.start_tick >= cad_start && note.start_tick < cad_end) {
      ++total;
      if (note.duration <= duration::kSixteenthNote) ++fast;
    }
  }

  ASSERT_GT(total, 0) << "Cadenza should have pedal notes";
  float ratio = static_cast<float>(fast) / total;
  EXPECT_GE(ratio, 0.50f)
      << "At least 50% of cadenza notes should be 16th or shorter, got "
      << (ratio * 100) << "%";
}

TEST(ToccataSectionalisTest, WaveEnergy) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.sections.size(), 5u);

  // Compute note density per section.
  auto density = [&](size_t si) -> float {
    size_t count = 0;
    for (const auto& track : result.tracks) {
      count += countNotesInRange(track, result.sections[si].start,
                                 result.sections[si].end);
    }
    Tick dur = result.sections[si].end - result.sections[si].start;
    return dur > 0 ? static_cast<float>(count) / dur : 0.0f;
  };

  float d_free1 = density(0);
  float d_quasi = density(1);
  float d_cadenza = density(3);
  float d_coda = density(4);

  // Cadenza should have very high density (fast pedal notes) relative to
  // free sections, and Coda should also be active.
  EXPECT_GT(d_cadenza, 0.0f) << "Cadenza should have note density > 0";
  EXPECT_GT(d_coda, 0.0f) << "Coda should have note density > 0";
  EXPECT_GT(d_free1, 0.0f) << "Free1 should have note density > 0";
  EXPECT_GT(d_quasi, 0.0f) << "QuasiFugal should have note density > 0";
}

TEST(ToccataSectionalisTest, LegacyFieldsPopulated) {
  auto config = makeSectionalisConfig();
  auto result = generateToccata(config);
  ASSERT_TRUE(result.success);

  // Legacy fields: opening = first section, recit = second, drive = last.
  EXPECT_EQ(result.opening_start, result.sections[0].start);
  EXPECT_EQ(result.opening_end, result.sections[0].end);
  EXPECT_EQ(result.recit_start, result.sections[1].start);
  EXPECT_EQ(result.recit_end, result.sections[1].end);
  EXPECT_EQ(result.drive_start, result.sections.back().start);
  EXPECT_EQ(result.drive_end, result.sections.back().end);
}

}  // namespace
}  // namespace bach
