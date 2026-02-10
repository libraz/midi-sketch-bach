// Tests for counterpoint/suspension_chain.h -- suspension chain generation
// with preparation-dissonance-resolution patterns.

#include "counterpoint/suspension_chain.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Single suspension: 4-3
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, SingleSuspension4_3) {
  auto chain = generateSuspensionChain(0, 1, 65, 0, SuspensionType::Sus4_3);

  ASSERT_EQ(chain.events.size(), 1u);
  const auto& evt = chain.events[0];

  EXPECT_EQ(evt.type, SuspensionType::Sus4_3);
  EXPECT_EQ(evt.preparation_tick, 0u);
  EXPECT_EQ(evt.dissonance_tick, kTicksPerBeat);
  EXPECT_EQ(evt.resolution_tick, 2 * kTicksPerBeat);
  EXPECT_EQ(evt.suspended_pitch, 65);
  // 4-3 resolves down by 1 semitone.
  EXPECT_EQ(evt.resolution_pitch, 64);
  EXPECT_EQ(evt.voice, 0);
}

// ---------------------------------------------------------------------------
// Single suspension: 7-6
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, SingleSuspension7_6) {
  auto chain = generateSuspensionChain(0, 1, 72, 1, SuspensionType::Sus7_6);

  ASSERT_EQ(chain.events.size(), 1u);
  const auto& evt = chain.events[0];

  EXPECT_EQ(evt.type, SuspensionType::Sus7_6);
  EXPECT_EQ(evt.suspended_pitch, 72);
  // 7-6 resolves down by 1 semitone.
  EXPECT_EQ(evt.resolution_pitch, 71);
}

// ---------------------------------------------------------------------------
// Single suspension: 9-8
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, SingleSuspension9_8) {
  auto chain = generateSuspensionChain(0, 1, 74, 2, SuspensionType::Sus9_8);

  ASSERT_EQ(chain.events.size(), 1u);
  const auto& evt = chain.events[0];

  EXPECT_EQ(evt.type, SuspensionType::Sus9_8);
  EXPECT_EQ(evt.suspended_pitch, 74);
  // 9-8 resolves down by 2 semitones.
  EXPECT_EQ(evt.resolution_pitch, 72);
}

// ---------------------------------------------------------------------------
// Single suspension: 2-3 (bass ascending)
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, SingleSuspension2_3) {
  auto chain = generateSuspensionChain(0, 1, 48, 3, SuspensionType::Sus2_3);

  ASSERT_EQ(chain.events.size(), 1u);
  const auto& evt = chain.events[0];

  EXPECT_EQ(evt.type, SuspensionType::Sus2_3);
  EXPECT_EQ(evt.suspended_pitch, 48);
  // 2-3 resolves UP by 1 semitone.
  EXPECT_EQ(evt.resolution_pitch, 49);
}

// ---------------------------------------------------------------------------
// Chain of three connected suspensions
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, ChainOfThree) {
  auto chain = generateSuspensionChain(0, 3, 67, 0, SuspensionType::Sus4_3);

  ASSERT_EQ(chain.events.size(), 3u);

  // Each resolution pitch becomes the next suspended pitch.
  EXPECT_EQ(chain.events[0].suspended_pitch, 67);
  EXPECT_EQ(chain.events[0].resolution_pitch, 66);

  EXPECT_EQ(chain.events[1].suspended_pitch, 66);
  EXPECT_EQ(chain.events[1].resolution_pitch, 65);

  EXPECT_EQ(chain.events[2].suspended_pitch, 65);
  EXPECT_EQ(chain.events[2].resolution_pitch, 64);
}

// ---------------------------------------------------------------------------
// Chain timing: 3 beats per suspension
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, ChainTiming) {
  constexpr Tick kStart = 1920;  // Bar 1
  auto chain = generateSuspensionChain(kStart, 2, 60, 0, SuspensionType::Sus4_3);

  ASSERT_EQ(chain.events.size(), 2u);
  EXPECT_EQ(chain.start_tick, kStart);

  // First suspension: beats 0, 1, 2 from start.
  EXPECT_EQ(chain.events[0].preparation_tick, kStart);
  EXPECT_EQ(chain.events[0].dissonance_tick, kStart + kTicksPerBeat);
  EXPECT_EQ(chain.events[0].resolution_tick, kStart + 2 * kTicksPerBeat);

  // Second suspension: beats 3, 4, 5 from start.
  constexpr Tick kSecondOffset = 3 * kTicksPerBeat;
  EXPECT_EQ(chain.events[1].preparation_tick, kStart + kSecondOffset);
  EXPECT_EQ(chain.events[1].dissonance_tick, kStart + kSecondOffset + kTicksPerBeat);
  EXPECT_EQ(chain.events[1].resolution_tick, kStart + kSecondOffset + 2 * kTicksPerBeat);

  // End tick: last resolution + 1 beat.
  EXPECT_EQ(chain.end_tick, chain.events[1].resolution_tick + kTicksPerBeat);
}

// ---------------------------------------------------------------------------
// Chain pitch progression
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, ChainPitchProgression) {
  auto chain = generateSuspensionChain(0, 4, 70, 0, SuspensionType::Sus4_3);

  ASSERT_EQ(chain.events.size(), 4u);

  // 4-3 chain: each step descends by 1 semitone.
  // 70 -> 69 -> 68 -> 67 -> 66
  for (uint8_t idx = 0; idx < 4; ++idx) {
    EXPECT_EQ(chain.events[idx].suspended_pitch, 70 - idx)
        << "Suspension " << static_cast<int>(idx) << " suspended pitch";
    EXPECT_EQ(chain.events[idx].resolution_pitch, 69 - idx)
        << "Suspension " << static_cast<int>(idx) << " resolution pitch";
  }
}

// ---------------------------------------------------------------------------
// Empty chain: zero count
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, EmptyChainZeroCount) {
  auto chain = generateSuspensionChain(960, 0, 60, 0);

  EXPECT_TRUE(chain.events.empty());
  EXPECT_EQ(chain.start_tick, 960u);
  EXPECT_EQ(chain.end_tick, 960u);
}

// ---------------------------------------------------------------------------
// Pitch clamping: extreme values
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, PitchClamping) {
  // Low pitch clamped to 24.
  auto chain_low = generateSuspensionChain(0, 1, 10, 0, SuspensionType::Sus4_3);
  ASSERT_EQ(chain_low.events.size(), 1u);
  EXPECT_EQ(chain_low.events[0].suspended_pitch, 24);
  // Resolution: 24 - 1 = 23, clamped to 24.
  EXPECT_EQ(chain_low.events[0].resolution_pitch, 24);

  // High pitch clamped to 96.
  auto chain_high = generateSuspensionChain(0, 1, 120, 0, SuspensionType::Sus2_3);
  ASSERT_EQ(chain_high.events.size(), 1u);
  EXPECT_EQ(chain_high.events[0].suspended_pitch, 96);
  // Resolution: 96 + 1 = 97, clamped to 96.
  EXPECT_EQ(chain_high.events[0].resolution_pitch, 96);
}

// ---------------------------------------------------------------------------
// String conversion
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, TypeToString) {
  EXPECT_STREQ(suspensionTypeToString(SuspensionType::Sus4_3), "4-3");
  EXPECT_STREQ(suspensionTypeToString(SuspensionType::Sus7_6), "7-6");
  EXPECT_STREQ(suspensionTypeToString(SuspensionType::Sus9_8), "9-8");
  EXPECT_STREQ(suspensionTypeToString(SuspensionType::Sus2_3), "2-3");
}

// ---------------------------------------------------------------------------
// Resolution intervals
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, ResolutionIntervals) {
  EXPECT_EQ(resolutionInterval(SuspensionType::Sus4_3), -1);
  EXPECT_EQ(resolutionInterval(SuspensionType::Sus7_6), -1);
  EXPECT_EQ(resolutionInterval(SuspensionType::Sus9_8), -2);
  EXPECT_EQ(resolutionInterval(SuspensionType::Sus2_3), 1);
}

// ---------------------------------------------------------------------------
// Diatonic suspension resolution [Task H]
// ---------------------------------------------------------------------------

TEST(SuspensionChainTest, DiatonicSus4_3_CMajor) {
  // In C major, E4(64) suspended: 4-3 resolution should step down to D4(62),
  // a whole step (2 semitones) -- not just 1 semitone as in the chromatic version.
  auto chain = generateSuspensionChain(0, 1, 64, 0, SuspensionType::Sus4_3,
                                       Key::C, ScaleType::Major);
  ASSERT_EQ(chain.events.size(), 1u);
  EXPECT_EQ(chain.events[0].suspended_pitch, 64);   // E4
  EXPECT_EQ(chain.events[0].resolution_pitch, 62);  // D4 (whole step down)
}

TEST(SuspensionChainTest, DiatonicSus4_3_CMajorFtoE) {
  // In C major, F4(65) suspended: 4-3 resolution should step down to E4(64),
  // a half step (1 semitone).
  auto chain = generateSuspensionChain(0, 1, 65, 0, SuspensionType::Sus4_3,
                                       Key::C, ScaleType::Major);
  ASSERT_EQ(chain.events.size(), 1u);
  EXPECT_EQ(chain.events[0].suspended_pitch, 65);   // F4
  EXPECT_EQ(chain.events[0].resolution_pitch, 64);  // E4 (half step down)
}

TEST(SuspensionChainTest, DiatonicSus2_3_AMinor) {
  // In A harmonic minor, G#4(68) suspended: 2-3 resolves UP to A4(69).
  auto chain = generateSuspensionChain(0, 1, 68, 0, SuspensionType::Sus2_3,
                                       Key::A, ScaleType::HarmonicMinor);
  ASSERT_EQ(chain.events.size(), 1u);
  EXPECT_EQ(chain.events[0].suspended_pitch, 68);   // G#4
  EXPECT_EQ(chain.events[0].resolution_pitch, 69);  // A4 (half step up)
}

TEST(SuspensionChainTest, DiatonicSus9_8_CMajor) {
  // In C major, D5(74) suspended: 9-8 resolves down 2 diatonic steps to B4(71).
  auto chain = generateSuspensionChain(0, 1, 74, 0, SuspensionType::Sus9_8,
                                       Key::C, ScaleType::Major);
  ASSERT_EQ(chain.events.size(), 1u);
  EXPECT_EQ(chain.events[0].suspended_pitch, 74);   // D5
  EXPECT_EQ(chain.events[0].resolution_pitch, 71);  // B4 (2 diatonic steps down)
}

TEST(SuspensionChainTest, DiatonicChainPreservesConnection) {
  // Chain of 2 suspensions: resolution of first becomes start of second.
  auto chain = generateSuspensionChain(0, 2, 67, 0, SuspensionType::Sus4_3,
                                       Key::C, ScaleType::Major);
  ASSERT_EQ(chain.events.size(), 2u);
  EXPECT_EQ(chain.events[0].suspended_pitch, 67);   // G4
  // G4 down 1 diatonic step in C major = F4(65)
  EXPECT_EQ(chain.events[0].resolution_pitch, 65);
  EXPECT_EQ(chain.events[1].suspended_pitch, 65);   // F4 (chained)
  // F4 down 1 diatonic step = E4(64)
  EXPECT_EQ(chain.events[1].resolution_pitch, 64);
}

}  // namespace
}  // namespace bach
