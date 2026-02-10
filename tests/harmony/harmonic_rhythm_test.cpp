// Tests for harmonic rhythm dynamic control.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_rhythm.h"

namespace bach {
namespace {

// Total duration of 3 bars (5760 ticks) for clean phase boundaries (1/3 each = 1920).
constexpr Tick kTestDuration = kTicksPerBar * 3;

// ---------------------------------------------------------------------------
// computeRhythmFactor tests
// ---------------------------------------------------------------------------

TEST(HarmonicRhythmTest, ComputeRhythmFactor_Establish_ReturnsDesignValue) {
  // Tick in the first third (Establish phase) should return 1.0.
  std::vector<Tick> no_cadences;

  // Beginning of the piece.
  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish,
                  computeRhythmFactor(0, kTestDuration, no_cadences));

  // Middle of Establish phase (quarter of first third).
  Tick mid_establish = kTestDuration / 6;  // 960 ticks
  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish,
                  computeRhythmFactor(mid_establish, kTestDuration, no_cadences));

  // Just before the Establish/Develop boundary.
  Tick just_before_develop = kTestDuration / 3 - 1;
  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish,
                  computeRhythmFactor(just_before_develop, kTestDuration, no_cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_Develop_ReturnsFasterRhythm) {
  // Tick in the middle third (Develop phase) should return 0.85.
  std::vector<Tick> no_cadences;

  // Start of Develop phase.
  Tick develop_start = kTestDuration / 3;  // 1920 ticks
  EXPECT_FLOAT_EQ(kHarmonicRhythmDevelop,
                  computeRhythmFactor(develop_start, kTestDuration, no_cadences));

  // Middle of Develop phase.
  Tick mid_develop = kTestDuration / 2;  // 2880 ticks
  EXPECT_FLOAT_EQ(kHarmonicRhythmDevelop,
                  computeRhythmFactor(mid_develop, kTestDuration, no_cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_Resolve_ReturnsSlowerRhythm) {
  // Tick in the last third (Resolve phase) should return 1.2.
  std::vector<Tick> no_cadences;

  // Start of Resolve phase.
  Tick resolve_start = kTestDuration * 2 / 3;  // 3840 ticks
  EXPECT_FLOAT_EQ(kHarmonicRhythmResolve,
                  computeRhythmFactor(resolve_start, kTestDuration, no_cadences));

  // End of piece.
  EXPECT_FLOAT_EQ(kHarmonicRhythmResolve,
                  computeRhythmFactor(kTestDuration, kTestDuration, no_cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_PreCadence_ReturnsAcceleration) {
  // Tick within 2 beats of a cadence should return the pre-cadence acceleration factor.
  Tick cadence_tick = kTestDuration / 2;  // 2880 ticks
  std::vector<Tick> cadences = {cadence_tick};

  // 1 beat before cadence (within the 2-beat window).
  Tick one_beat_before = cadence_tick - kTicksPerBeat;
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(one_beat_before, kTestDuration, cadences));

  // Exactly at window start (2 beats before cadence).
  Tick window_start = cadence_tick - kPreCadenceWindow;
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(window_start, kTestDuration, cadences));

  // Just before the window: should get the phase-based value instead.
  Tick before_window = window_start - 1;
  float factor = computeRhythmFactor(before_window, kTestDuration, cadences);
  EXPECT_NE(kPreCadenceAcceleration, factor);
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_PreCadenceTakesPriority) {
  // Even if the tick is in Resolve phase, pre-cadence acceleration wins.
  Tick cadence_tick = kTestDuration - kTicksPerBeat;  // Near the end (Resolve phase)
  std::vector<Tick> cadences = {cadence_tick};

  Tick in_window = cadence_tick - kTicksPerBeat;  // 1 beat before cadence
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(in_window, kTestDuration, cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_NoCadences_ReturnsPhaseOnly) {
  // Empty cadence list should still return phase-based values.
  std::vector<Tick> empty_cadences;

  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish,
                  computeRhythmFactor(0, kTestDuration, empty_cadences));
  EXPECT_FLOAT_EQ(kHarmonicRhythmDevelop,
                  computeRhythmFactor(kTestDuration / 2, kTestDuration, empty_cadences));
  EXPECT_FLOAT_EQ(kHarmonicRhythmResolve,
                  computeRhythmFactor(kTestDuration, kTestDuration, empty_cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_ZeroDuration_ReturnsDefault) {
  // Zero total duration is an edge case that should return 1.0 (safe default).
  std::vector<Tick> no_cadences;
  EXPECT_FLOAT_EQ(1.0f, computeRhythmFactor(0, 0, no_cadences));
  EXPECT_FLOAT_EQ(1.0f, computeRhythmFactor(100, 0, no_cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_MultipleCadences) {
  // Multiple cadence points: tick near any of them should accelerate.
  Tick cadence1 = kTicksPerBar;
  Tick cadence2 = kTicksPerBar * 2;
  std::vector<Tick> cadences = {cadence1, cadence2};

  // Near first cadence.
  Tick near_first = cadence1 - kTicksPerBeat;
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(near_first, kTestDuration, cadences));

  // Near second cadence.
  Tick near_second = cadence2 - kTicksPerBeat;
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(near_second, kTestDuration, cadences));
}

TEST(HarmonicRhythmTest, ComputeRhythmFactor_CadenceAtTickZero) {
  // Edge case: cadence at tick 0 with a small window.
  std::vector<Tick> cadences = {kTicksPerBeat};  // Cadence at beat 1

  // Tick 0 is within the pre-cadence window of a cadence at beat 1.
  EXPECT_FLOAT_EQ(kPreCadenceAcceleration,
                  computeRhythmFactor(0, kTestDuration, cadences));
}

// ---------------------------------------------------------------------------
// applyRhythmFactors tests
// ---------------------------------------------------------------------------

TEST(HarmonicRhythmTest, ApplyRhythmFactors_SetsFactorsOnEvents) {
  // Create events spanning all three phases.
  std::vector<HarmonicEvent> events;

  // Event in Establish phase (first third).
  HarmonicEvent evt_establish;
  evt_establish.tick = 0;
  evt_establish.end_tick = kTicksPerBar;
  events.push_back(evt_establish);

  // Event in Develop phase (middle third).
  HarmonicEvent evt_develop;
  evt_develop.tick = kTestDuration / 2;
  evt_develop.end_tick = kTestDuration / 2 + kTicksPerBar;
  events.push_back(evt_develop);

  // Event in Resolve phase (last third).
  HarmonicEvent evt_resolve;
  evt_resolve.tick = kTestDuration - kTicksPerBeat;
  evt_resolve.end_tick = kTestDuration;
  events.push_back(evt_resolve);

  std::vector<Tick> no_cadences;
  applyRhythmFactors(events, kTestDuration, no_cadences);

  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish, events[0].rhythm_factor);
  EXPECT_FLOAT_EQ(kHarmonicRhythmDevelop, events[1].rhythm_factor);
  EXPECT_FLOAT_EQ(kHarmonicRhythmResolve, events[2].rhythm_factor);
}

TEST(HarmonicRhythmTest, ApplyRhythmFactors_EmptyEvents) {
  // Applying to empty events should not crash.
  std::vector<HarmonicEvent> events;
  std::vector<Tick> no_cadences;
  applyRhythmFactors(events, kTestDuration, no_cadences);
  EXPECT_TRUE(events.empty());
}

TEST(HarmonicRhythmTest, ApplyRhythmFactors_WithCadences) {
  // Events near cadence points should get acceleration factor.
  Tick cadence_tick = kTestDuration / 2;
  std::vector<Tick> cadences = {cadence_tick};

  std::vector<HarmonicEvent> events;

  // Event right before the cadence (within 2-beat window).
  HarmonicEvent evt_pre_cadence;
  evt_pre_cadence.tick = cadence_tick - kTicksPerBeat;
  evt_pre_cadence.end_tick = cadence_tick;
  events.push_back(evt_pre_cadence);

  // Event far from cadence (in Establish phase).
  HarmonicEvent evt_far;
  evt_far.tick = 0;
  evt_far.end_tick = kTicksPerBar;
  events.push_back(evt_far);

  applyRhythmFactors(events, kTestDuration, cadences);

  EXPECT_FLOAT_EQ(kPreCadenceAcceleration, events[0].rhythm_factor);
  EXPECT_FLOAT_EQ(kHarmonicRhythmEstablish, events[1].rhythm_factor);
}

// ---------------------------------------------------------------------------
// Design value verification
// ---------------------------------------------------------------------------

TEST(HarmonicRhythmTest, DesignValues_AreCorrect) {
  // Verify the constant design values match the specification.
  EXPECT_FLOAT_EQ(1.0f, kHarmonicRhythmEstablish);
  EXPECT_FLOAT_EQ(0.85f, kHarmonicRhythmDevelop);
  EXPECT_FLOAT_EQ(1.2f, kHarmonicRhythmResolve);
  EXPECT_FLOAT_EQ(0.7f, kPreCadenceAcceleration);
  EXPECT_EQ(kTicksPerBeat * 2, kPreCadenceWindow);
}

TEST(HarmonicRhythmTest, RhythmFactor_DefaultInHarmonicEvent) {
  // Default HarmonicEvent should have rhythm_factor = 1.0.
  HarmonicEvent event;
  EXPECT_FLOAT_EQ(1.0f, event.rhythm_factor);
}

}  // namespace
}  // namespace bach
