// Tests for HarpsichordModel.

#include "instrument/keyboard/harpsichord_model.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// HarpsichordConfig defaults
// ---------------------------------------------------------------------------

TEST(HarpsichordConfigTest, StandardConfigHasExpectedDefaults) {
  auto config = HarpsichordConfig::standard();

  // Lower manual: F1-F6
  EXPECT_EQ(config.lower_low, 29);
  EXPECT_EQ(config.lower_high, 89);

  // Upper manual: F1-F6
  EXPECT_EQ(config.upper_low, 29);
  EXPECT_EQ(config.upper_high, 89);
}

// ---------------------------------------------------------------------------
// HarpsichordModel construction
// ---------------------------------------------------------------------------

TEST(HarpsichordModelTest, DefaultConstructionUsesStandardConfig) {
  HarpsichordModel harpsichord;
  const auto& config = harpsichord.getHarpsichordConfig();
  EXPECT_EQ(config.lower_low, 29);
  EXPECT_EQ(config.lower_high, 89);
  EXPECT_EQ(config.upper_low, 29);
  EXPECT_EQ(config.upper_high, 89);
}

TEST(HarpsichordModelTest, CustomConfigIsStored) {
  HarpsichordConfig custom;
  custom.lower_low = 24;
  custom.lower_high = 84;
  custom.upper_low = 36;
  custom.upper_high = 96;

  HarpsichordModel harpsichord(custom);
  const auto& config = harpsichord.getHarpsichordConfig();
  EXPECT_EQ(config.lower_low, 24);
  EXPECT_EQ(config.lower_high, 84);
  EXPECT_EQ(config.upper_low, 36);
  EXPECT_EQ(config.upper_high, 96);
}

// ---------------------------------------------------------------------------
// Velocity sensitivity
// ---------------------------------------------------------------------------

TEST(HarpsichordModelTest, IsNotVelocitySensitive) {
  HarpsichordModel harpsichord;
  EXPECT_FALSE(harpsichord.isVelocitySensitive());
}

TEST(HarpsichordModelTest, DefaultVelocityIs80) {
  HarpsichordModel harpsichord;
  EXPECT_EQ(harpsichord.defaultVelocity(), 80);
}

// ---------------------------------------------------------------------------
// Inherits PianoModel with advanced constraints
// ---------------------------------------------------------------------------

TEST(HarpsichordModelTest, UsesAdvancedSpanConstraints) {
  HarpsichordModel harpsichord;
  auto expected = KeyboardSpanConstraints::advanced();
  const auto& actual = harpsichord.getSpanConstraints();
  EXPECT_EQ(actual.normal_span, expected.normal_span);
  EXPECT_EQ(actual.max_span, expected.max_span);
  EXPECT_EQ(actual.max_notes, expected.max_notes);
}

TEST(HarpsichordModelTest, UsesAdvancedHandPhysics) {
  HarpsichordModel harpsichord;
  auto expected = KeyboardHandPhysics::advanced();
  const auto& actual = harpsichord.getHandPhysics();
  EXPECT_FLOAT_EQ(actual.jump_cost_per_semitone, expected.jump_cost_per_semitone);
  EXPECT_FLOAT_EQ(actual.stretch_cost_per_semitone, expected.stretch_cost_per_semitone);
  EXPECT_FLOAT_EQ(actual.cross_over_cost, expected.cross_over_cost);
  EXPECT_FLOAT_EQ(actual.repeated_note_cost, expected.repeated_note_cost);
  EXPECT_FLOAT_EQ(actual.fatigue_recovery_rate, expected.fatigue_recovery_rate);
}

// ---------------------------------------------------------------------------
// Inherits IKeyboardInstrument interface from PianoModel
// ---------------------------------------------------------------------------

TEST(HarpsichordModelTest, InheritsPianoModelInterface) {
  HarpsichordModel harpsichord;

  // Verify the interface methods from IKeyboardInstrument are accessible
  // The PianoModel base uses A0 (21) to C8 (108)
  EXPECT_EQ(harpsichord.getLowestPitch(), 21);
  EXPECT_EQ(harpsichord.getHighestPitch(), 108);
}

TEST(HarpsichordModelTest, PitchRangeInheritedFromPianoModel) {
  HarpsichordModel harpsichord;

  // PianoModel range: A0 (21) to C8 (108)
  EXPECT_TRUE(harpsichord.isPitchInRange(21));   // A0
  EXPECT_TRUE(harpsichord.isPitchInRange(60));   // C4
  EXPECT_TRUE(harpsichord.isPitchInRange(108));  // C8
  EXPECT_FALSE(harpsichord.isPitchInRange(20));  // Below A0
  EXPECT_FALSE(harpsichord.isPitchInRange(109)); // Above C8
}

}  // namespace
}  // namespace bach
