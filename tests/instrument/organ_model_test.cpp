// Tests for OrganModel and related organ types.

#include "instrument/keyboard/organ_model.h"

#include <gtest/gtest.h>

#include "organ/pedal_constraints.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// OrganManual string conversion
// ---------------------------------------------------------------------------

TEST(OrganManualTest, ToStringReturnsCorrectNames) {
  EXPECT_STREQ(organManualToString(OrganManual::Great), "Great");
  EXPECT_STREQ(organManualToString(OrganManual::Swell), "Swell");
  EXPECT_STREQ(organManualToString(OrganManual::Positiv), "Positiv");
  EXPECT_STREQ(organManualToString(OrganManual::Pedal), "Pedal");
}

// ---------------------------------------------------------------------------
// OrganConfig defaults
// ---------------------------------------------------------------------------

TEST(OrganConfigTest, StandardConfigHasExpectedDefaults) {
  auto config = OrganConfig::standard();

  // Manual I (Great): C2-C6
  EXPECT_EQ(config.great_low, 36);
  EXPECT_EQ(config.great_high, 96);

  // Manual II (Swell): C2-C6
  EXPECT_EQ(config.swell_low, 36);
  EXPECT_EQ(config.swell_high, 96);

  // Manual III (Positiv): C3-C6
  EXPECT_EQ(config.positiv_low, 48);
  EXPECT_EQ(config.positiv_high, 96);

  // Pedal: C1-D3
  EXPECT_EQ(config.pedal_low, 24);
  EXPECT_EQ(config.pedal_high, 50);
}

// ---------------------------------------------------------------------------
// OrganModel construction
// ---------------------------------------------------------------------------

TEST(OrganModelTest, DefaultConstructionUsesStandardConfig) {
  OrganModel organ;
  const auto& config = organ.getOrganConfig();
  EXPECT_EQ(config.great_low, 36);
  EXPECT_EQ(config.great_high, 96);
  EXPECT_EQ(config.pedal_low, 24);
  EXPECT_EQ(config.pedal_high, 50);
}

TEST(OrganModelTest, CustomConfigIsStored) {
  OrganConfig custom;
  custom.great_low = 40;
  custom.great_high = 90;
  custom.pedal_low = 28;
  custom.pedal_high = 48;

  OrganModel organ(custom);
  const auto& config = organ.getOrganConfig();
  EXPECT_EQ(config.great_low, 40);
  EXPECT_EQ(config.great_high, 90);
  EXPECT_EQ(config.pedal_low, 28);
  EXPECT_EQ(config.pedal_high, 48);
}

// ---------------------------------------------------------------------------
// Velocity sensitivity
// ---------------------------------------------------------------------------

TEST(OrganModelTest, IsNotVelocitySensitive) {
  OrganModel organ;
  EXPECT_FALSE(organ.isVelocitySensitive());
}

TEST(OrganModelTest, DefaultVelocityIs80) {
  OrganModel organ;
  EXPECT_EQ(organ.defaultVelocity(), 80);
}

// ---------------------------------------------------------------------------
// Manual range queries
// ---------------------------------------------------------------------------

TEST(OrganModelTest, GetManualLowReturnsCorrectValues) {
  OrganModel organ;
  EXPECT_EQ(organ.getManualLow(OrganManual::Great), 36);
  EXPECT_EQ(organ.getManualLow(OrganManual::Swell), 36);
  EXPECT_EQ(organ.getManualLow(OrganManual::Positiv), 48);
  EXPECT_EQ(organ.getManualLow(OrganManual::Pedal), 24);
}

TEST(OrganModelTest, GetManualHighReturnsCorrectValues) {
  OrganModel organ;
  EXPECT_EQ(organ.getManualHigh(OrganManual::Great), 96);
  EXPECT_EQ(organ.getManualHigh(OrganManual::Swell), 96);
  EXPECT_EQ(organ.getManualHigh(OrganManual::Positiv), 96);
  EXPECT_EQ(organ.getManualHigh(OrganManual::Pedal), 50);
}

// ---------------------------------------------------------------------------
// isInManualRange
// ---------------------------------------------------------------------------

TEST(OrganModelTest, GreatManualRangeC2ToC6) {
  OrganModel organ;

  // Within range
  EXPECT_TRUE(organ.isInManualRange(36, OrganManual::Great));   // C2 (low boundary)
  EXPECT_TRUE(organ.isInManualRange(60, OrganManual::Great));   // C4 (middle)
  EXPECT_TRUE(organ.isInManualRange(96, OrganManual::Great));   // C6 (high boundary)

  // Out of range
  EXPECT_FALSE(organ.isInManualRange(35, OrganManual::Great));  // B1 (below)
  EXPECT_FALSE(organ.isInManualRange(97, OrganManual::Great));  // C#6 (above)
  EXPECT_FALSE(organ.isInManualRange(0, OrganManual::Great));   // Far below
  EXPECT_FALSE(organ.isInManualRange(127, OrganManual::Great)); // Far above
}

TEST(OrganModelTest, SwellManualRangeC2ToC6) {
  OrganModel organ;
  EXPECT_TRUE(organ.isInManualRange(36, OrganManual::Swell));
  EXPECT_TRUE(organ.isInManualRange(96, OrganManual::Swell));
  EXPECT_FALSE(organ.isInManualRange(35, OrganManual::Swell));
  EXPECT_FALSE(organ.isInManualRange(97, OrganManual::Swell));
}

TEST(OrganModelTest, PositivManualRangeC3ToC6) {
  OrganModel organ;

  // Within range
  EXPECT_TRUE(organ.isInManualRange(48, OrganManual::Positiv));  // C3 (low boundary)
  EXPECT_TRUE(organ.isInManualRange(72, OrganManual::Positiv));  // C5 (middle)
  EXPECT_TRUE(organ.isInManualRange(96, OrganManual::Positiv));  // C6 (high boundary)

  // Out of range -- Positiv starts higher than Great/Swell
  EXPECT_FALSE(organ.isInManualRange(47, OrganManual::Positiv)); // B2 (below)
  EXPECT_FALSE(organ.isInManualRange(36, OrganManual::Positiv)); // C2 (Great range, not Positiv)
  EXPECT_FALSE(organ.isInManualRange(97, OrganManual::Positiv)); // C#6 (above)
}

TEST(OrganModelTest, PedalRangeC1ToD3) {
  OrganModel organ;

  // Within range
  EXPECT_TRUE(organ.isInManualRange(24, OrganManual::Pedal));   // C1 (low boundary)
  EXPECT_TRUE(organ.isInManualRange(36, OrganManual::Pedal));   // C2 (middle)
  EXPECT_TRUE(organ.isInManualRange(50, OrganManual::Pedal));   // D3 (high boundary)

  // Out of range
  EXPECT_FALSE(organ.isInManualRange(23, OrganManual::Pedal));  // B0 (below)
  EXPECT_FALSE(organ.isInManualRange(51, OrganManual::Pedal));  // D#3 (above)
  EXPECT_FALSE(organ.isInManualRange(60, OrganManual::Pedal));  // C4 (way above)
}

// ---------------------------------------------------------------------------
// Pedal penalty
// ---------------------------------------------------------------------------

TEST(OrganModelTest, PedalPenaltyZeroWithinIdealRange) {
  OrganModel organ;

  // Every pitch in [24, 50] should have zero penalty
  EXPECT_FLOAT_EQ(organ.pedalPenalty(24), 0.0f);  // C1 (low boundary)
  EXPECT_FLOAT_EQ(organ.pedalPenalty(36), 0.0f);  // C2 (middle)
  EXPECT_FLOAT_EQ(organ.pedalPenalty(50), 0.0f);  // D3 (high boundary)
  EXPECT_FLOAT_EQ(organ.pedalPenalty(37), 0.0f);  // C#2 (arbitrary middle)
}

TEST(OrganModelTest, PedalPenaltyIncreasesBelow) {
  OrganModel organ;

  // 1 semitone below C1
  EXPECT_FLOAT_EQ(organ.pedalPenalty(23), 5.0f);
  // 2 semitones below
  EXPECT_FLOAT_EQ(organ.pedalPenalty(22), 10.0f);
  // 12 semitones below (one octave)
  EXPECT_FLOAT_EQ(organ.pedalPenalty(12), 60.0f);
}

TEST(OrganModelTest, PedalPenaltyIncreasesAbove) {
  OrganModel organ;

  // 1 semitone above D3
  EXPECT_FLOAT_EQ(organ.pedalPenalty(51), 5.0f);
  // 2 semitones above
  EXPECT_FLOAT_EQ(organ.pedalPenalty(52), 10.0f);
  // 10 semitones above D3 (MIDI 60 = C4)
  EXPECT_FLOAT_EQ(organ.pedalPenalty(60), 50.0f);
}

// ---------------------------------------------------------------------------
// MIDI channel mapping
// ---------------------------------------------------------------------------

TEST(OrganModelTest, ChannelForManualMapping) {
  EXPECT_EQ(OrganModel::channelForManual(OrganManual::Great), 0);
  EXPECT_EQ(OrganModel::channelForManual(OrganManual::Swell), 1);
  EXPECT_EQ(OrganModel::channelForManual(OrganManual::Positiv), 2);
  EXPECT_EQ(OrganModel::channelForManual(OrganManual::Pedal), 3);
}

// ---------------------------------------------------------------------------
// GM program mapping
// ---------------------------------------------------------------------------

TEST(OrganModelTest, ProgramForManualMapping) {
  // Church Organ (19) for Great, Positiv, Pedal
  EXPECT_EQ(OrganModel::programForManual(OrganManual::Great), 19);
  EXPECT_EQ(OrganModel::programForManual(OrganManual::Positiv), 19);
  EXPECT_EQ(OrganModel::programForManual(OrganManual::Pedal), 19);

  // Reed Organ (20) for Swell
  EXPECT_EQ(OrganModel::programForManual(OrganManual::Swell), 20);
}

// ---------------------------------------------------------------------------
// Inherits PianoModel with virtuoso constraints
// ---------------------------------------------------------------------------

TEST(OrganModelTest, UsesVirtuosoSpanConstraints) {
  OrganModel organ;
  auto expected = KeyboardSpanConstraints::virtuoso();
  const auto& actual = organ.getSpanConstraints();
  EXPECT_EQ(actual.normal_span, expected.normal_span);
  EXPECT_EQ(actual.max_span, expected.max_span);
  EXPECT_EQ(actual.max_notes, expected.max_notes);
}

TEST(OrganModelTest, UsesVirtuosoHandPhysics) {
  OrganModel organ;
  auto expected = KeyboardHandPhysics::virtuoso();
  const auto& actual = organ.getHandPhysics();
  EXPECT_FLOAT_EQ(actual.jump_cost_per_semitone, expected.jump_cost_per_semitone);
  EXPECT_FLOAT_EQ(actual.stretch_cost_per_semitone, expected.stretch_cost_per_semitone);
  EXPECT_FLOAT_EQ(actual.cross_over_cost, expected.cross_over_cost);
  EXPECT_FLOAT_EQ(actual.repeated_note_cost, expected.repeated_note_cost);
  EXPECT_FLOAT_EQ(actual.fatigue_recovery_rate, expected.fatigue_recovery_rate);
}

// ---------------------------------------------------------------------------
// Pedal constraints (standalone utilities)
// ---------------------------------------------------------------------------

TEST(PedalConstraintsTest, ConstantsMatchOrganConfig) {
  EXPECT_EQ(kPedalLow, 24);   // C1
  EXPECT_EQ(kPedalHigh, 50);  // D3
  EXPECT_EQ(kPedalIdealLow, 24);
  EXPECT_EQ(kPedalIdealHigh, 50);
  EXPECT_FLOAT_EQ(kPedalPenaltyPerSemitone, 5.0f);
}

TEST(PedalConstraintsTest, CalculatePedalPenaltyWithinRange) {
  EXPECT_FLOAT_EQ(calculatePedalPenalty(24), 0.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(37), 0.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(50), 0.0f);
}

TEST(PedalConstraintsTest, CalculatePedalPenaltyOutsideRange) {
  // Below ideal range
  EXPECT_FLOAT_EQ(calculatePedalPenalty(23), 5.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(20), 20.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(0), 120.0f);

  // Above ideal range
  EXPECT_FLOAT_EQ(calculatePedalPenalty(51), 5.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(55), 25.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(60), 50.0f);
}

TEST(PedalConstraintsTest, CalculatePedalPenaltyBoundaryValues) {
  // Exact boundaries should have zero penalty
  EXPECT_FLOAT_EQ(calculatePedalPenalty(kPedalIdealLow), 0.0f);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(kPedalIdealHigh), 0.0f);

  // One step outside should be exactly kPedalPenaltyPerSemitone
  EXPECT_FLOAT_EQ(calculatePedalPenalty(kPedalIdealLow - 1), kPedalPenaltyPerSemitone);
  EXPECT_FLOAT_EQ(calculatePedalPenalty(kPedalIdealHigh + 1), kPedalPenaltyPerSemitone);
}

// ---------------------------------------------------------------------------
// OrganModel delegates pedalPenalty to calculatePedalPenalty
// ---------------------------------------------------------------------------

TEST(OrganModelTest, PedalPenaltyMatchesStandaloneFunction) {
  OrganModel organ;
  for (uint8_t pitch = 0; pitch < 128; ++pitch) {
    EXPECT_FLOAT_EQ(organ.pedalPenalty(pitch), calculatePedalPenalty(pitch))
        << "Mismatch at pitch " << static_cast<int>(pitch);
  }
}

}  // namespace
}  // namespace bach
