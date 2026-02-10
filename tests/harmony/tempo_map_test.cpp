// Tests for harmony/tempo_map.h -- per-form MIDI tempo map generation.

#include "harmony/tempo_map.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// adjustBpm
// ---------------------------------------------------------------------------

TEST(TempoMapTest, AdjustBpmZeroPercent) {
  EXPECT_EQ(adjustBpm(100, 0.0f), 100);
  EXPECT_EQ(adjustBpm(72, 0.0f), 72);
}

TEST(TempoMapTest, AdjustBpmPositivePercent) {
  EXPECT_EQ(adjustBpm(100, 10.0f), 110);
  EXPECT_EQ(adjustBpm(100, 3.0f), 103);
}

TEST(TempoMapTest, AdjustBpmNegativePercent) {
  EXPECT_EQ(adjustBpm(100, -10.0f), 90);
  EXPECT_EQ(adjustBpm(100, -5.0f), 95);
}

TEST(TempoMapTest, AdjustBpmClampsToMinimum) {
  // 40 BPM - 50% = 20 -> should clamp to 40.
  EXPECT_EQ(adjustBpm(40, -50.0f), 40);
  EXPECT_EQ(adjustBpm(50, -80.0f), 40);
}

TEST(TempoMapTest, AdjustBpmClampsToMaximum) {
  // 200 BPM + 10% = 220 -> should clamp to 200.
  EXPECT_EQ(adjustBpm(200, 10.0f), 200);
  EXPECT_EQ(adjustBpm(180, 20.0f), 200);
}

// ---------------------------------------------------------------------------
// generateFugueTempoMap
// ---------------------------------------------------------------------------

TEST(TempoMapTest, FugueTempoMapEmptyStructure) {
  FugueStructure empty_structure;
  auto events = generateFugueTempoMap(empty_structure, 100);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].bpm, 100);
}

TEST(TempoMapTest, FugueTempoMapSorted) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, 4 * kTicksPerBar, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       4 * kTicksPerBar, 8 * kTicksPerBar, Key::G);
  structure.addSection(SectionType::MiddleEntry, FuguePhase::Develop,
                       8 * kTicksPerBar, 12 * kTicksPerBar, Key::G);
  structure.addSection(SectionType::Stretto, FuguePhase::Resolve,
                       12 * kTicksPerBar, 16 * kTicksPerBar, Key::C);
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       16 * kTicksPerBar, 18 * kTicksPerBar, Key::C);

  auto events = generateFugueTempoMap(structure, 100);

  ASSERT_GE(events.size(), 5u) << "Should have at least one event per section";

  // Verify sorted by tick.
  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LE(events[idx - 1].tick, events[idx].tick)
        << "Events must be sorted by tick";
  }
}

TEST(TempoMapTest, FugueTempoMapSectionCount) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, 4 * kTicksPerBar, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       4 * kTicksPerBar, 8 * kTicksPerBar, Key::G);
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       8 * kTicksPerBar, 10 * kTicksPerBar, Key::C);

  auto events = generateFugueTempoMap(structure, 100);

  // At least one event per section.
  EXPECT_GE(events.size(), 3u);
}

TEST(TempoMapTest, FugueTempoMapExpositionIsBaseTempo) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, 4 * kTicksPerBar, Key::C);

  auto events = generateFugueTempoMap(structure, 100);

  ASSERT_GE(events.size(), 1u);
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].bpm, 100) << "Exposition should be at base tempo (0%)";
}

TEST(TempoMapTest, FugueTempoMapEpisodeFaster) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, 4 * kTicksPerBar, Key::C);
  structure.addSection(SectionType::Episode, FuguePhase::Develop,
                       4 * kTicksPerBar, 8 * kTicksPerBar, Key::G);

  auto events = generateFugueTempoMap(structure, 100);

  // Find episode event.
  bool found_episode = false;
  for (const auto& evt : events) {
    if (evt.tick == 4 * kTicksPerBar) {
      found_episode = true;
      EXPECT_GT(evt.bpm, 100) << "Episode should be faster than base (+3%)";
    }
  }
  EXPECT_TRUE(found_episode);
}

TEST(TempoMapTest, FugueTempoMapCodaSlower) {
  FugueStructure structure;
  structure.addSection(SectionType::Exposition, FuguePhase::Establish,
                       0, 4 * kTicksPerBar, Key::C);
  structure.addSection(SectionType::Coda, FuguePhase::Resolve,
                       4 * kTicksPerBar, 6 * kTicksPerBar, Key::C);

  auto events = generateFugueTempoMap(structure, 100);

  bool found_coda = false;
  for (const auto& evt : events) {
    if (evt.tick == 4 * kTicksPerBar) {
      found_coda = true;
      EXPECT_LT(evt.bpm, 100) << "Coda should be slower than base (-10%)";
      EXPECT_EQ(evt.bpm, 90);
    }
  }
  EXPECT_TRUE(found_coda);
}

// ---------------------------------------------------------------------------
// generateToccataTempoMap
// ---------------------------------------------------------------------------

TEST(TempoMapTest, ToccataTempoMapStructure) {
  Tick opening_start = 0;
  Tick opening_end = 6 * kTicksPerBar;
  Tick recit_start = opening_end;
  Tick recit_end = recit_start + 12 * kTicksPerBar;
  Tick drive_start = recit_end;
  Tick drive_end = drive_start + 6 * kTicksPerBar;

  auto events = generateToccataTempoMap(
      opening_start, opening_end, recit_start, recit_end,
      drive_start, drive_end, 80);

  EXPECT_GE(events.size(), 4u) << "Should have opening, recit, drive, and transition events";

  // Verify sorted.
  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LE(events[idx - 1].tick, events[idx].tick);
  }
}

TEST(TempoMapTest, ToccataTempoMapOpeningFaster) {
  auto events = generateToccataTempoMap(
      0, 6 * kTicksPerBar,
      6 * kTicksPerBar, 18 * kTicksPerBar,
      18 * kTicksPerBar, 24 * kTicksPerBar,
      80);

  // First event is opening, should be faster than base (+8%).
  ASSERT_GE(events.size(), 1u);
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_GT(events[0].bpm, 80) << "Opening should be faster than base (+8%)";
}

TEST(TempoMapTest, ToccataTempoMapHasFermatas) {
  auto events = generateToccataTempoMap(
      0, 6 * kTicksPerBar,
      6 * kTicksPerBar, 18 * kTicksPerBar,
      18 * kTicksPerBar, 24 * kTicksPerBar,
      80);

  // Should contain fermata events (very slow, -25%).
  uint16_t fermata_bpm = adjustBpm(80, -25.0f);
  int fermata_count = 0;
  for (const auto& evt : events) {
    if (evt.bpm == fermata_bpm) {
      ++fermata_count;
    }
  }
  EXPECT_GE(fermata_count, 2) << "Should have at least 2 fermata spots";
}

// ---------------------------------------------------------------------------
// generatePassacagliaTempoMap
// ---------------------------------------------------------------------------

TEST(TempoMapTest, PassacagliaTempoMapSteady) {
  auto events = generatePassacagliaTempoMap(12, 8, 60);

  // Most events should be at base tempo.
  ASSERT_GE(events.size(), 1u);
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].bpm, 60) << "Passacaglia base should be steady (0%)";
}

TEST(TempoMapTest, PassacagliaTempoMapFinalSlower) {
  auto events = generatePassacagliaTempoMap(12, 8, 60);

  ASSERT_GE(events.size(), 2u);
  // Last event should be the final variation, slightly slower.
  EXPECT_LT(events.back().bpm, 60) << "Final variation should be slightly slower (-3%)";
}

TEST(TempoMapTest, PassacagliaTempoMapSingleVariation) {
  auto events = generatePassacagliaTempoMap(1, 8, 60);

  // Single variation: just base tempo, no final broadening.
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].bpm, 60);
}

// ---------------------------------------------------------------------------
// generateFantasiaTempoMap
// ---------------------------------------------------------------------------

TEST(TempoMapTest, FantasiaTempoMapContemplative) {
  Tick total_duration = 32 * kTicksPerBar;
  auto events = generateFantasiaTempoMap(total_duration, 32, 66);

  ASSERT_GE(events.size(), 1u);
  // Base tempo should be lower than input (-5%).
  EXPECT_LT(events[0].bpm, 66) << "Fantasia base should be contemplative (slower)";
}

TEST(TempoMapTest, FantasiaTempoMapHasPhraseBoundaries) {
  Tick total_duration = 32 * kTicksPerBar;
  auto events = generateFantasiaTempoMap(total_duration, 32, 66);

  // Should have more than just the base event (phrase boundaries + final).
  EXPECT_GE(events.size(), 3u) << "Should have phrase boundaries and final broadening";
}

TEST(TempoMapTest, FantasiaTempoMapSorted) {
  Tick total_duration = 32 * kTicksPerBar;
  auto events = generateFantasiaTempoMap(total_duration, 32, 66);

  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LE(events[idx - 1].tick, events[idx].tick)
        << "Events must be sorted by tick";
  }
}

TEST(TempoMapTest, FantasiaTempoMapFinalBroadening) {
  Tick total_duration = 32 * kTicksPerBar;
  auto events = generateFantasiaTempoMap(total_duration, 32, 100);

  // Find the event at bar 30 (section_bars - 2).
  Tick final_tick = 30 * kTicksPerBar;
  bool found_final = false;
  for (const auto& evt : events) {
    if (evt.tick == final_tick) {
      found_final = true;
      EXPECT_LT(evt.bpm, adjustBpm(100, -5.0f))
          << "Final broadening should be slower than base";
    }
  }
  EXPECT_TRUE(found_final) << "Should have a final broadening event";
}

// ---------------------------------------------------------------------------
// generateCadenceRitardando
// ---------------------------------------------------------------------------

TEST(TempoMapTest, CadenceRitardandoGeneratesEvents) {
  // A single cadence should produce 4 events:
  // 2-beats-before, 1-beat-before, on-cadence, a-tempo.
  std::vector<Tick> cadences = {8 * kTicksPerBeat};  // beat 8
  auto events = generateCadenceRitardando(100, cadences);
  ASSERT_EQ(events.size(), 4u);
}

TEST(TempoMapTest, CadenceRitardandoCorrectBPM) {
  // Verify the 3-stage BPM values match the design factors.
  const uint16_t base_bpm = 120;
  std::vector<Tick> cadences = {10 * kTicksPerBeat};
  auto events = generateCadenceRitardando(base_bpm, cadences);
  ASSERT_EQ(events.size(), 4u);

  // 2 beats before: 120 * 0.95 = 114
  EXPECT_EQ(events[0].tick, 8 * kTicksPerBeat);
  EXPECT_EQ(events[0].bpm, static_cast<uint16_t>(std::round(120.0f * 0.95f)));

  // 1 beat before: 120 * 0.92 = 110 (rounded)
  EXPECT_EQ(events[1].tick, 9 * kTicksPerBeat);
  EXPECT_EQ(events[1].bpm, static_cast<uint16_t>(std::round(120.0f * 0.92f)));

  // On cadence: 120 * 0.88 = 106 (rounded)
  EXPECT_EQ(events[2].tick, 10 * kTicksPerBeat);
  EXPECT_EQ(events[2].bpm, static_cast<uint16_t>(std::round(120.0f * 0.88f)));
}

TEST(TempoMapTest, CadenceRitardandoATempo) {
  // Verify the a tempo restoration event restores original BPM.
  const uint16_t base_bpm = 100;
  std::vector<Tick> cadences = {4 * kTicksPerBeat};
  auto events = generateCadenceRitardando(base_bpm, cadences);
  ASSERT_EQ(events.size(), 4u);

  // Last event: a tempo restoration at cadence_tick + kTicksPerBeat.
  EXPECT_EQ(events[3].tick, 5 * kTicksPerBeat);
  EXPECT_EQ(events[3].bpm, base_bpm);
}

TEST(TempoMapTest, CadenceRitardandoSkipsEarlyCadences) {
  // Cadence at beat 1 (tick 480) is too early (< 2 * kTicksPerBeat = 960).
  std::vector<Tick> cadences = {1 * kTicksPerBeat};
  auto events = generateCadenceRitardando(100, cadences);
  EXPECT_TRUE(events.empty()) << "Cadences at tick < 2*kTicksPerBeat should be skipped";

  // Cadence at exactly 2 * kTicksPerBeat is valid (first event at tick 0).
  cadences = {2 * kTicksPerBeat};
  events = generateCadenceRitardando(100, cadences);
  EXPECT_EQ(events.size(), 4u)
      << "Cadence at exactly 2*kTicksPerBeat should produce events (first at tick 0)";
  EXPECT_EQ(events[0].tick, 0u);
}

TEST(TempoMapTest, CadenceRitardandoMultipleCadences) {
  // Two cadences far apart: each produces 4 events = 8 total.
  std::vector<Tick> cadences = {
      4 * kTicksPerBar,   // bar 4, beat 0
      12 * kTicksPerBar,  // bar 12, beat 0
  };
  auto events = generateCadenceRitardando(100, cadences);
  ASSERT_EQ(events.size(), 8u);

  // Verify sorted by tick.
  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_LE(events[idx - 1].tick, events[idx].tick)
        << "Events must be sorted by tick at index " << idx;
  }

  // First cadence a-tempo at bar4 + 1 beat should come before second cadence's
  // first deceleration at bar12 - 2 beats.
  EXPECT_LT(events[3].tick, events[4].tick)
      << "First cadence's a-tempo must precede second cadence's deceleration";
}

TEST(TempoMapTest, CadenceRitardandoMultipleCadencesOverlapDedup) {
  // Two cadences very close: the a-tempo of the first overlaps with the
  // deceleration of the second. Deduplication should keep one event per tick.
  // Cadence at beat 4, then cadence at beat 7 (a-tempo of first = beat 5,
  // 2-beats-before second = beat 5 -> same tick -> dedup).
  std::vector<Tick> cadences = {
      4 * kTicksPerBeat,
      7 * kTicksPerBeat,
  };
  auto events = generateCadenceRitardando(100, cadences);

  // Without dedup: 8 events. With dedup at beat 5: 7 events.
  EXPECT_EQ(events.size(), 7u)
      << "Overlapping tick should be deduplicated to a single event";

  // Verify no duplicate ticks.
  for (size_t idx = 1; idx < events.size(); ++idx) {
    EXPECT_NE(events[idx - 1].tick, events[idx].tick)
        << "No two events should share the same tick after dedup";
  }
}

TEST(TempoMapTest, CadenceRitardandoEmptyInput) {
  // No cadences: no events.
  std::vector<Tick> cadences;
  auto events = generateCadenceRitardando(100, cadences);
  EXPECT_TRUE(events.empty());
}

}  // namespace
}  // namespace bach
