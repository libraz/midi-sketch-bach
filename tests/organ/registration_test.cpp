// Tests for organ registration plan -- CC event generation and structural changes.

#include "organ/registration.h"

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

#include "fugue/fugue_structure.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// createDefaultRegistrationPlan
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, DefaultPlanExpositionHasCorrectPrograms) {
  auto plan = createDefaultRegistrationPlan();

  EXPECT_EQ(plan.exposition.manual1_program, 19);  // Church Organ
  EXPECT_EQ(plan.exposition.manual2_program, 20);  // Reed Organ
  EXPECT_EQ(plan.exposition.manual3_program, 19);  // Church Organ
  EXPECT_EQ(plan.exposition.pedal_program, 19);     // Church Organ
}

TEST(RegistrationPlanTest, DefaultPlanExpositionVelocityIs75) {
  auto plan = createDefaultRegistrationPlan();
  EXPECT_EQ(plan.exposition.velocity_hint, 75);
}

TEST(RegistrationPlanTest, DefaultPlanStrettoAllChurchOrgan) {
  auto plan = createDefaultRegistrationPlan();

  EXPECT_EQ(plan.stretto.manual1_program, 19);
  EXPECT_EQ(plan.stretto.manual2_program, 19);
  EXPECT_EQ(plan.stretto.manual3_program, 19);
  EXPECT_EQ(plan.stretto.pedal_program, 19);
}

TEST(RegistrationPlanTest, DefaultPlanStrettoVelocityIs90) {
  auto plan = createDefaultRegistrationPlan();
  EXPECT_EQ(plan.stretto.velocity_hint, 90);
}

TEST(RegistrationPlanTest, DefaultPlanCodaVelocityIs100) {
  auto plan = createDefaultRegistrationPlan();
  EXPECT_EQ(plan.coda.velocity_hint, 100);
}

// ---------------------------------------------------------------------------
// getRegistrationForPhase
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, EstablishPhaseReturnsExposition) {
  auto plan = createDefaultRegistrationPlan();
  const auto& reg = getRegistrationForPhase(plan, FuguePhase::Establish);

  EXPECT_EQ(reg.velocity_hint, plan.exposition.velocity_hint);
  EXPECT_EQ(reg.manual2_program, 20);  // Reed Organ in exposition
}

TEST(RegistrationPlanTest, DevelopPhaseReturnsExposition) {
  auto plan = createDefaultRegistrationPlan();
  const auto& reg = getRegistrationForPhase(plan, FuguePhase::Develop);

  // Development uses same registration as exposition (no mid-development change)
  EXPECT_EQ(reg.velocity_hint, plan.exposition.velocity_hint);
  EXPECT_EQ(reg.manual2_program, plan.exposition.manual2_program);
}

TEST(RegistrationPlanTest, ResolvePhaseReturnsStretto) {
  auto plan = createDefaultRegistrationPlan();
  const auto& reg = getRegistrationForPhase(plan, FuguePhase::Resolve);

  EXPECT_EQ(reg.velocity_hint, plan.stretto.velocity_hint);
  EXPECT_EQ(reg.manual2_program, 19);  // All Church Organ in stretto
}

TEST(RegistrationPlanTest, CodaFlagOverridesPhase) {
  auto plan = createDefaultRegistrationPlan();

  // Even in Establish phase, coda flag returns coda registration
  const auto& reg = getRegistrationForPhase(plan, FuguePhase::Establish, true);
  EXPECT_EQ(reg.velocity_hint, plan.coda.velocity_hint);
}

TEST(RegistrationPlanTest, CodaOverridesResolvePhase) {
  auto plan = createDefaultRegistrationPlan();
  const auto& reg = getRegistrationForPhase(plan, FuguePhase::Resolve, true);

  EXPECT_EQ(reg.velocity_hint, 100);
}

// ---------------------------------------------------------------------------
// generateRegistrationEvents
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, GenerateEventsProducesEightEvents) {
  Registration reg;
  reg.velocity_hint = 80;

  auto events = generateRegistrationEvents(reg, 0);
  EXPECT_EQ(events.size(), 8u);  // 4 channels x 2 CCs
}

TEST(RegistrationPlanTest, GenerateEventsCorrectTickPosition) {
  Registration reg;
  Tick tick = 1920;

  auto events = generateRegistrationEvents(reg, tick);
  for (const auto& evt : events) {
    EXPECT_EQ(evt.tick, 1920u);
  }
}

TEST(RegistrationPlanTest, GenerateEventsCorrectStatusBytes) {
  Registration reg;
  auto events = generateRegistrationEvents(reg, 0);

  // Each pair of events should be for the same channel
  // Events are ordered: ch0-vol, ch0-expr, ch1-vol, ch1-expr, ...
  for (uint8_t idx = 0; idx < 4; ++idx) {
    uint8_t expected_status = 0xB0 | idx;
    EXPECT_EQ(events[idx * 2].status, expected_status);      // Volume
    EXPECT_EQ(events[idx * 2 + 1].status, expected_status);  // Expression
  }
}

TEST(RegistrationPlanTest, GenerateEventsVolumeAndExpressionCCs) {
  Registration reg;
  reg.velocity_hint = 85;

  auto events = generateRegistrationEvents(reg, 0);

  // Check first channel (Great, channel 0)
  EXPECT_EQ(events[0].data1, 7u);   // CC#7 Main Volume
  EXPECT_EQ(events[0].data2, 85u);
  EXPECT_EQ(events[1].data1, 11u);  // CC#11 Expression
  EXPECT_EQ(events[1].data2, 85u);
}

TEST(RegistrationPlanTest, GenerateEventsAllFourChannels) {
  Registration reg;
  reg.velocity_hint = 90;

  auto events = generateRegistrationEvents(reg, 0);

  // Verify each channel gets both CC events
  for (uint8_t channel = 0; channel < 4; ++channel) {
    auto vol_evt = events[channel * 2];
    auto expr_evt = events[channel * 2 + 1];

    EXPECT_EQ(vol_evt.status & 0x0F, channel);
    EXPECT_EQ(expr_evt.status & 0x0F, channel);
    EXPECT_EQ(vol_evt.data2, 90u);
    EXPECT_EQ(expr_evt.data2, 90u);
  }
}

TEST(RegistrationPlanTest, GenerateEventsVelocityClampedTo127) {
  Registration reg;
  reg.velocity_hint = 127;

  auto events = generateRegistrationEvents(reg, 0);
  for (const auto& evt : events) {
    EXPECT_LE(evt.data2, 127u);
  }
}

TEST(RegistrationPlanTest, GenerateEventsZeroVelocity) {
  Registration reg;
  reg.velocity_hint = 0;

  auto events = generateRegistrationEvents(reg, 0);
  for (const auto& evt : events) {
    EXPECT_EQ(evt.data2, 0u);
  }
}

// ---------------------------------------------------------------------------
// applyRegistrationPlan
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, ApplyPlanInsertsExpositionEvents) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  applyRegistrationPlan(tracks, plan, 0);

  // Each track should have 2 events (volume + expression)
  for (const auto& track : tracks) {
    EXPECT_EQ(track.events.size(), 2u);
  }
}

TEST(RegistrationPlanTest, ApplyPlanNoStrettoOrCodaWhenZero) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  applyRegistrationPlan(tracks, plan, 0, 0, 0);

  // Only exposition events (2 per track)
  for (const auto& track : tracks) {
    EXPECT_EQ(track.events.size(), 2u);
  }
}

TEST(RegistrationPlanTest, ApplyPlanWithStrettoAddsMoreEvents) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  Tick stretto_tick = kTicksPerBar * 16;  // Bar 16
  applyRegistrationPlan(tracks, plan, 0, stretto_tick);

  // 2 events for exposition + 2 for stretto = 4 per track
  for (const auto& track : tracks) {
    EXPECT_EQ(track.events.size(), 4u);
  }
}

TEST(RegistrationPlanTest, ApplyPlanWithAllThreePointsAddsSixEvents) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  Tick stretto_tick = kTicksPerBar * 16;
  Tick coda_tick = kTicksPerBar * 24;
  applyRegistrationPlan(tracks, plan, 0, stretto_tick, coda_tick);

  // 2 for expo + 2 for stretto + 2 for coda = 6 per track
  for (const auto& track : tracks) {
    EXPECT_EQ(track.events.size(), 6u);
  }
}

TEST(RegistrationPlanTest, ApplyPlanStrettoEventsAtCorrectTick) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  Tick stretto_tick = kTicksPerBar * 10;
  applyRegistrationPlan(tracks, plan, 0, stretto_tick);

  // Stretto events should be at the stretto tick
  // They are the 3rd and 4th events in each track
  for (const auto& track : tracks) {
    ASSERT_GE(track.events.size(), 4u);
    EXPECT_EQ(track.events[2].tick, stretto_tick);
    EXPECT_EQ(track.events[3].tick, stretto_tick);
  }
}

TEST(RegistrationPlanTest, ApplyPlanSkipsUnmatchedChannels) {
  // Only 2 tracks (channels 0 and 1), no channel 2 or 3
  std::vector<Track> tracks(2);
  tracks[0].channel = 0;
  tracks[1].channel = 1;

  auto plan = createDefaultRegistrationPlan();
  applyRegistrationPlan(tracks, plan, 0);

  // Only matched channels get events
  EXPECT_EQ(tracks[0].events.size(), 2u);
  EXPECT_EQ(tracks[1].events.size(), 2u);
}

TEST(RegistrationPlanTest, ApplyPlanEmptyTracksNoEffect) {
  std::vector<Track> tracks;
  auto plan = createDefaultRegistrationPlan();

  // Should not crash
  applyRegistrationPlan(tracks, plan, 0, kTicksPerBar * 8, kTicksPerBar * 16);
  EXPECT_TRUE(tracks.empty());
}

TEST(RegistrationPlanTest, ApplyPlanExpositionVelocityCorrect) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  auto plan = createDefaultRegistrationPlan();
  applyRegistrationPlan(tracks, plan, 0);

  // Exposition velocity_hint is 75
  for (const auto& track : tracks) {
    ASSERT_GE(track.events.size(), 2u);
    EXPECT_EQ(track.events[0].data2, 75u);  // CC#7 volume
    EXPECT_EQ(track.events[1].data2, 75u);  // CC#11 expression
  }
}

// ---------------------------------------------------------------------------
// Registration struct defaults
// ---------------------------------------------------------------------------

TEST(RegistrationTest, DefaultRegistrationValues) {
  Registration reg;
  EXPECT_EQ(reg.manual1_program, 19);
  EXPECT_EQ(reg.manual2_program, 20);
  EXPECT_EQ(reg.manual3_program, 19);
  EXPECT_EQ(reg.pedal_program, 19);
  EXPECT_EQ(reg.velocity_hint, 80);
}

// ---------------------------------------------------------------------------
// Custom registration plans
// ---------------------------------------------------------------------------

TEST(RegistrationPlanTest, CustomPlanPreservesValues) {
  RegistrationPlan plan;
  plan.exposition.velocity_hint = 60;
  plan.exposition.manual2_program = 21;  // Custom program

  plan.stretto.velocity_hint = 95;
  plan.coda.velocity_hint = 110;

  const auto& expo = getRegistrationForPhase(plan, FuguePhase::Establish);
  EXPECT_EQ(expo.velocity_hint, 60);
  EXPECT_EQ(expo.manual2_program, 21);

  const auto& stretto = getRegistrationForPhase(plan, FuguePhase::Resolve);
  EXPECT_EQ(stretto.velocity_hint, 95);

  const auto& coda = getRegistrationForPhase(plan, FuguePhase::Resolve, true);
  EXPECT_EQ(coda.velocity_hint, 110);
}

// ---------------------------------------------------------------------------
// generateEnergyRegistrationEvents
// ---------------------------------------------------------------------------

TEST(RegistrationTest, GenerateEnergyRegistrationEventsCorrectCount) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.2f},
      {kTicksPerBar * 4, 0.5f},
      {kTicksPerBar * 8, 0.9f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 4);
  // 3 time points x 4 channels = 12 events
  EXPECT_EQ(events.size(), 12u);
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsCorrectCC) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.5f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 4);
  ASSERT_EQ(events.size(), 4u);
  for (const auto& evt : events) {
    EXPECT_EQ(evt.data1, 7u);  // CC#7 Volume
  }
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsLowEnergyVolume) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.1f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].data2, 64u);  // Low energy -> volume 64
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsMidEnergyVolume) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.5f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].data2, 80u);  // Mid energy -> volume 80
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsHighEnergyVolume) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.9f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].data2, 120u);  // High energy -> volume 120
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsCorrectTicks) {
  Tick target_tick = kTicksPerBar * 5;
  std::vector<std::pair<Tick, float>> energy_levels = {
      {target_tick, 0.5f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 2);
  ASSERT_EQ(events.size(), 2u);
  for (const auto& evt : events) {
    EXPECT_EQ(evt.tick, target_tick);
  }
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsCorrectChannels) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.5f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 4);
  ASSERT_EQ(events.size(), 4u);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    uint8_t expected_channel = idx;
    EXPECT_EQ(events[idx].status & 0x0F, expected_channel);
    EXPECT_EQ(events[idx].status & 0xF0, 0xB0);  // Control Change
  }
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsEmptyInput) {
  std::vector<std::pair<Tick, float>> energy_levels;
  auto events = generateEnergyRegistrationEvents(energy_levels, 4);
  EXPECT_TRUE(events.empty());
}

TEST(RegistrationTest, GenerateEnergyRegistrationEventsZeroChannels) {
  std::vector<std::pair<Tick, float>> energy_levels = {
      {0, 0.5f}
  };
  auto events = generateEnergyRegistrationEvents(energy_levels, 0);
  EXPECT_TRUE(events.empty());
}

// ---------------------------------------------------------------------------
// ExtendedRegistrationPlan
// ---------------------------------------------------------------------------

TEST(ExtendedRegistrationPlanTest, AddPointStoresCorrectly) {
  ExtendedRegistrationPlan plan;
  Registration reg;
  reg.velocity_hint = 85;

  plan.addPoint(kTicksPerBar * 4, reg, "episode_1");

  ASSERT_EQ(plan.size(), 1u);
  EXPECT_EQ(plan.points[0].tick, kTicksPerBar * 4);
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 85);
  EXPECT_EQ(plan.points[0].label, "episode_1");
}

TEST(ExtendedRegistrationPlanTest, EmptyPlanIsEmpty) {
  ExtendedRegistrationPlan plan;
  EXPECT_TRUE(plan.empty());
  EXPECT_EQ(plan.size(), 0u);
}

TEST(ExtendedRegistrationPlanTest, MultiplePointsPreserveOrder) {
  ExtendedRegistrationPlan plan;
  Registration reg1;
  reg1.velocity_hint = 75;
  Registration reg2;
  reg2.velocity_hint = 90;

  plan.addPoint(0, reg1, "exposition");
  plan.addPoint(kTicksPerBar * 8, reg2, "stretto");

  ASSERT_EQ(plan.size(), 2u);
  EXPECT_EQ(plan.points[0].label, "exposition");
  EXPECT_EQ(plan.points[1].label, "stretto");
}

TEST(ExtendedRegistrationPlanTest, AddPointDefaultEmptyLabel) {
  ExtendedRegistrationPlan plan;
  Registration reg;
  plan.addPoint(0, reg);

  ASSERT_EQ(plan.size(), 1u);
  EXPECT_TRUE(plan.points[0].label.empty());
}

// ---------------------------------------------------------------------------
// createExtendedRegistrationPlan
// ---------------------------------------------------------------------------

TEST(ExtendedRegistrationPlanTest, FromSectionsHasCorrectPointCount) {
  // Create a typical fugue section layout: Exposition + 2 Episodes + 2 MiddleEntries
  // + Stretto + Coda = 7 sections
  std::vector<FugueSection> sections;

  FugueSection expo;
  expo.type = SectionType::Exposition;
  expo.start_tick = 0;
  expo.end_tick = kTicksPerBar * 4;
  sections.push_back(expo);

  FugueSection ep1;
  ep1.type = SectionType::Episode;
  ep1.start_tick = kTicksPerBar * 4;
  ep1.end_tick = kTicksPerBar * 6;
  sections.push_back(ep1);

  FugueSection me1;
  me1.type = SectionType::MiddleEntry;
  me1.start_tick = kTicksPerBar * 6;
  me1.end_tick = kTicksPerBar * 8;
  sections.push_back(me1);

  FugueSection ep2;
  ep2.type = SectionType::Episode;
  ep2.start_tick = kTicksPerBar * 8;
  ep2.end_tick = kTicksPerBar * 10;
  sections.push_back(ep2);

  FugueSection me2;
  me2.type = SectionType::MiddleEntry;
  me2.start_tick = kTicksPerBar * 10;
  me2.end_tick = kTicksPerBar * 12;
  sections.push_back(me2);

  FugueSection stretto;
  stretto.type = SectionType::Stretto;
  stretto.start_tick = kTicksPerBar * 12;
  stretto.end_tick = kTicksPerBar * 16;
  sections.push_back(stretto);

  FugueSection coda;
  coda.type = SectionType::Coda;
  coda.start_tick = kTicksPerBar * 16;
  coda.end_tick = kTicksPerBar * 18;
  sections.push_back(coda);

  Tick total_duration = kTicksPerBar * 18;
  auto plan = createExtendedRegistrationPlan(sections, total_duration);

  // One point per section
  EXPECT_EQ(plan.size(), 7u);
}

TEST(ExtendedRegistrationPlanTest, ExpositionVelocityIs75) {
  std::vector<FugueSection> sections;
  FugueSection expo;
  expo.type = SectionType::Exposition;
  expo.start_tick = 0;
  expo.end_tick = kTicksPerBar * 4;
  sections.push_back(expo);

  auto plan = createExtendedRegistrationPlan(sections, kTicksPerBar * 20);

  ASSERT_EQ(plan.size(), 1u);
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 75);
  EXPECT_EQ(plan.points[0].label, "exposition");
}

TEST(ExtendedRegistrationPlanTest, CodaVelocityIs100) {
  std::vector<FugueSection> sections;
  FugueSection coda;
  coda.type = SectionType::Coda;
  coda.start_tick = kTicksPerBar * 16;
  coda.end_tick = kTicksPerBar * 18;
  sections.push_back(coda);

  auto plan = createExtendedRegistrationPlan(sections, kTicksPerBar * 18);

  ASSERT_EQ(plan.size(), 1u);
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 100);
  EXPECT_EQ(plan.points[0].label, "coda");
}

TEST(ExtendedRegistrationPlanTest, StrettoVelocityIs95) {
  std::vector<FugueSection> sections;
  FugueSection stretto;
  stretto.type = SectionType::Stretto;
  stretto.start_tick = kTicksPerBar * 12;
  stretto.end_tick = kTicksPerBar * 16;
  sections.push_back(stretto);

  auto plan = createExtendedRegistrationPlan(sections, kTicksPerBar * 18);

  ASSERT_EQ(plan.size(), 1u);
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 95);
  EXPECT_EQ(plan.points[0].label, "stretto");
}

TEST(ExtendedRegistrationPlanTest, EpisodesHaveGradualDynamic) {
  // Place episodes at 20%, 50%, 80% of total duration to verify increasing velocity.
  Tick total_duration = kTicksPerBar * 20;
  std::vector<FugueSection> sections;

  FugueSection ep1;
  ep1.type = SectionType::Episode;
  ep1.start_tick = kTicksPerBar * 4;   // 20%
  ep1.end_tick = kTicksPerBar * 6;
  sections.push_back(ep1);

  FugueSection ep2;
  ep2.type = SectionType::Episode;
  ep2.start_tick = kTicksPerBar * 10;  // 50%
  ep2.end_tick = kTicksPerBar * 12;
  sections.push_back(ep2);

  FugueSection ep3;
  ep3.type = SectionType::Episode;
  ep3.start_tick = kTicksPerBar * 16;  // 80%
  ep3.end_tick = kTicksPerBar * 18;
  sections.push_back(ep3);

  auto plan = createExtendedRegistrationPlan(sections, total_duration);

  ASSERT_EQ(plan.size(), 3u);

  // Later episodes should have higher velocity (gradual crescendo).
  uint8_t vel1 = plan.points[0].registration.velocity_hint;
  uint8_t vel2 = plan.points[1].registration.velocity_hint;
  uint8_t vel3 = plan.points[2].registration.velocity_hint;
  EXPECT_LT(vel1, vel2);
  EXPECT_LT(vel2, vel3);

  // Verify range: episode velocity is 70 + energy * 20, energy in [0.2, 0.8].
  EXPECT_GE(vel1, 70);
  EXPECT_LE(vel3, 90);
}

TEST(ExtendedRegistrationPlanTest, MiddleEntryHasBoostedVelocity) {
  Tick total_duration = kTicksPerBar * 20;
  std::vector<FugueSection> sections;

  FugueSection middle;
  middle.type = SectionType::MiddleEntry;
  middle.start_tick = kTicksPerBar * 10;  // 50%
  middle.end_tick = kTicksPerBar * 12;
  sections.push_back(middle);

  auto plan = createExtendedRegistrationPlan(sections, total_duration);

  ASSERT_EQ(plan.size(), 1u);
  // At 50% energy: 80 + 0.5 * 15 = 87
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 87);
  EXPECT_EQ(plan.points[0].label, "middle_entry");
}

TEST(ExtendedRegistrationPlanTest, EmptySectionsProducesEmptyPlan) {
  std::vector<FugueSection> sections;
  auto plan = createExtendedRegistrationPlan(sections, kTicksPerBar * 20);
  EXPECT_TRUE(plan.empty());
}

TEST(ExtendedRegistrationPlanTest, ZeroDurationUsesDefaultEnergy) {
  // When total_duration is 0, energy defaults to 0.5.
  std::vector<FugueSection> sections;
  FugueSection episode;
  episode.type = SectionType::Episode;
  episode.start_tick = 0;
  episode.end_tick = kTicksPerBar * 2;
  sections.push_back(episode);

  auto plan = createExtendedRegistrationPlan(sections, 0);

  ASSERT_EQ(plan.size(), 1u);
  // energy = 0.5: velocity = 70 + 0.5 * 20 = 80
  EXPECT_EQ(plan.points[0].registration.velocity_hint, 80);
}

// ---------------------------------------------------------------------------
// applyExtendedRegistrationPlan
// ---------------------------------------------------------------------------

TEST(ExtendedRegistrationPlanTest, ApplyInsertsEventsIntoTracks) {
  // Create 4 organ tracks (channels 0-3).
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  // Create a plan with 3 points: exposition, episode, stretto.
  ExtendedRegistrationPlan plan;
  Registration reg_expo;
  reg_expo.velocity_hint = 75;
  plan.addPoint(0, reg_expo, "exposition");

  Registration reg_ep;
  reg_ep.velocity_hint = 82;
  plan.addPoint(kTicksPerBar * 4, reg_ep, "episode");

  Registration reg_stretto;
  reg_stretto.velocity_hint = 95;
  plan.addPoint(kTicksPerBar * 12, reg_stretto, "stretto");

  applyExtendedRegistrationPlan(tracks, plan);

  // Each point generates 2 events per channel (CC#7 + CC#11).
  // 3 points x 2 events = 6 events per track.
  for (const auto& track : tracks) {
    EXPECT_EQ(track.events.size(), 6u);
  }

  // Verify first track's exposition events.
  EXPECT_EQ(tracks[0].events[0].tick, 0u);
  EXPECT_EQ(tracks[0].events[0].data1, 7u);   // CC#7
  EXPECT_EQ(tracks[0].events[0].data2, 75u);
  EXPECT_EQ(tracks[0].events[1].tick, 0u);
  EXPECT_EQ(tracks[0].events[1].data1, 11u);  // CC#11
  EXPECT_EQ(tracks[0].events[1].data2, 75u);

  // Verify episode events at correct tick.
  EXPECT_EQ(tracks[0].events[2].tick, kTicksPerBar * 4);
  EXPECT_EQ(tracks[0].events[2].data2, 82u);

  // Verify stretto events at correct tick.
  EXPECT_EQ(tracks[0].events[4].tick, kTicksPerBar * 12);
  EXPECT_EQ(tracks[0].events[4].data2, 95u);
}

TEST(ExtendedRegistrationPlanTest, ApplyEmptyPlanNoEvents) {
  std::vector<Track> tracks(4);
  for (uint8_t idx = 0; idx < 4; ++idx) {
    tracks[idx].channel = idx;
  }

  ExtendedRegistrationPlan plan;
  applyExtendedRegistrationPlan(tracks, plan);

  for (const auto& track : tracks) {
    EXPECT_TRUE(track.events.empty());
  }
}

TEST(ExtendedRegistrationPlanTest, ApplyToEmptyTracksNoEffect) {
  std::vector<Track> tracks;
  ExtendedRegistrationPlan plan;
  Registration reg;
  plan.addPoint(0, reg, "test");

  // Should not crash.
  applyExtendedRegistrationPlan(tracks, plan);
  EXPECT_TRUE(tracks.empty());
}

TEST(ExtendedRegistrationPlanTest, ApplySkipsUnmatchedChannels) {
  // Only 2 tracks (channels 0 and 1).
  std::vector<Track> tracks(2);
  tracks[0].channel = 0;
  tracks[1].channel = 1;

  ExtendedRegistrationPlan plan;
  Registration reg;
  reg.velocity_hint = 85;
  plan.addPoint(0, reg, "test");

  applyExtendedRegistrationPlan(tracks, plan);

  // Only channels 0 and 1 get events (not 2 and 3).
  EXPECT_EQ(tracks[0].events.size(), 2u);  // CC#7 + CC#11
  EXPECT_EQ(tracks[1].events.size(), 2u);
}

}  // namespace
}  // namespace bach
