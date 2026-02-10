// Tests for counterpoint/voice_manager.h -- registration, naming, and
// pitch penalty calculation.

#include "counterpoint/voice_manager.h"

#include <gtest/gtest.h>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

TEST(VoiceManagerTest, RegisterAndQuery) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);
  mgr.registerVoice(1, "Alto", 48, 72);

  EXPECT_TRUE(mgr.isRegistered(0));
  EXPECT_TRUE(mgr.isRegistered(1));
  EXPECT_FALSE(mgr.isRegistered(2));
  EXPECT_EQ(mgr.voiceCount(), 2u);
}

TEST(VoiceManagerTest, GetVoiceName) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);

  EXPECT_EQ(mgr.getVoiceName(0), "Soprano");
}

TEST(VoiceManagerTest, UnregisteredVoiceNameIsEmpty) {
  VoiceManager mgr;
  EXPECT_TRUE(mgr.getVoiceName(99).empty());
}

TEST(VoiceManagerTest, GetVoiceIdsPreservesOrder) {
  VoiceManager mgr;
  mgr.registerVoice(3, "Bass", 36, 60);
  mgr.registerVoice(0, "Soprano", 60, 84);
  mgr.registerVoice(1, "Alto", 48, 72);

  auto ids = mgr.getVoiceIds();
  ASSERT_EQ(ids.size(), 3u);
  EXPECT_EQ(ids[0], 3);
  EXPECT_EQ(ids[1], 0);
  EXPECT_EQ(ids[2], 1);
}

TEST(VoiceManagerTest, DuplicateRegistrationDoesNotDuplicate) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);
  mgr.registerVoice(0, "Soprano High", 64, 96);

  EXPECT_EQ(mgr.voiceCount(), 1u);
  EXPECT_EQ(mgr.getVoiceName(0), "Soprano High");
}

// ---------------------------------------------------------------------------
// Pitch penalty
// ---------------------------------------------------------------------------

TEST(VoiceManagerTest, PitchInRangeNoPenalty) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);

  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 60), 0.0f);  // Low boundary
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 72), 0.0f);  // Middle
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 84), 0.0f);  // High boundary
}

TEST(VoiceManagerTest, PitchBelowRange) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);

  // 2 semitones below: 2 * 5.0 = 10.0
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 58), 10.0f);

  // 1 semitone below: 1 * 5.0 = 5.0
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 59), 5.0f);
}

TEST(VoiceManagerTest, PitchAboveRange) {
  VoiceManager mgr;
  mgr.registerVoice(0, "Soprano", 60, 84);

  // 3 semitones above: 3 * 5.0 = 15.0
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(0, 87), 15.0f);
}

TEST(VoiceManagerTest, PitchPenaltyUnregisteredVoice) {
  VoiceManager mgr;
  // Unregistered voice returns kPenaltyPerSemitone.
  EXPECT_FLOAT_EQ(mgr.pitchPenalty(99, 60), VoiceManager::kPenaltyPerSemitone);
}

TEST(VoiceManagerTest, PenaltyPerSemitoneMatchesPedalPattern) {
  // Verify our penalty constant matches the pedal_constraints pattern.
  EXPECT_FLOAT_EQ(VoiceManager::kPenaltyPerSemitone, 5.0f);
}

}  // namespace
}  // namespace bach
