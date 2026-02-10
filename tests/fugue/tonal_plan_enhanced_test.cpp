// Tests for toDetailedTimeline() -- beat-resolution harmonic timeline.

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/tonal_plan.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"

namespace bach {
namespace {

TEST(TonalPlanEnhancedTest, DetailedTimelineProducesBeatResolutionEvents) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;

  Tick total_duration = kTicksPerBar * 12;
  TonalPlan plan = generateTonalPlan(config, false, total_duration);
  HarmonicTimeline timeline = plan.toDetailedTimeline(total_duration);

  // Should have beat-resolution events (one per beat = every 480 ticks).
  ASSERT_GT(timeline.size(), 0u);

  // Check that events occur at beat boundaries.
  for (const auto& ev : timeline.events()) {
    EXPECT_EQ(ev.tick % kTicksPerBeat, 0u)
        << "Event at tick " << ev.tick << " is not beat-aligned";
  }

  // Should have more events than bar-only (12 bars -> 12 events for bar,
  // but ~48 events for beat resolution).
  HarmonicTimeline bar_timeline = plan.toHarmonicTimeline(total_duration);
  EXPECT_GT(timeline.size(), bar_timeline.size());
}

TEST(TonalPlanEnhancedTest, DetailedTimelineContainsNonIDegrees) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;

  Tick total_duration = kTicksPerBar * 16;
  TonalPlan plan = generateTonalPlan(config, false, total_duration);
  HarmonicTimeline timeline = plan.toDetailedTimeline(total_duration);

  // Check that we have chords other than I.
  bool has_non_I = false;
  for (const auto& ev : timeline.events()) {
    if (ev.chord.degree != ChordDegree::I) {
      has_non_I = true;
      break;
    }
  }
  EXPECT_TRUE(has_non_I) << "Detailed timeline should contain non-I degrees";
}

TEST(TonalPlanEnhancedTest, DetailedTimelineKeyChangesMatchModulations) {
  FugueConfig config;
  config.key = Key::C;
  config.num_voices = 3;

  Tick total_duration = kTicksPerBar * 24;
  TonalPlan plan = generateTonalPlan(config, false, total_duration);
  HarmonicTimeline timeline = plan.toDetailedTimeline(total_duration);

  // Verify that each modulation point produces a key change in the timeline.
  for (size_t idx = 1; idx < plan.modulations.size(); ++idx) {
    Tick mod_tick = plan.modulations[idx].tick;
    Key expected_key = plan.modulations[idx].target_key;
    Key timeline_key = timeline.getKeyAt(mod_tick);
    EXPECT_EQ(timeline_key, expected_key)
        << "Key mismatch at modulation tick " << mod_tick;
  }
}

TEST(TonalPlanEnhancedTest, DetailedTimelineMinorKey) {
  FugueConfig config;
  config.key = Key::A;
  config.num_voices = 3;
  config.is_minor = true;

  Tick total_duration = kTicksPerBar * 16;
  TonalPlan plan = generateTonalPlan(config, true, total_duration);
  HarmonicTimeline timeline = plan.toDetailedTimeline(total_duration);

  ASSERT_GT(timeline.size(), 0u);

  // First event should be in key A.
  EXPECT_EQ(timeline.events()[0].key, Key::A);
}

TEST(TonalPlanEnhancedTest, DetailedTimelineEmptyDuration) {
  FugueConfig config;
  config.key = Key::C;
  TonalPlan plan = generateTonalPlan(config, false, 0);
  HarmonicTimeline timeline = plan.toDetailedTimeline(0);
  EXPECT_EQ(timeline.size(), 0u);
}

}  // namespace
}  // namespace bach
