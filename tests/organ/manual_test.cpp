// Tests for organ manual assignment -- voice-to-manual routing.

#include "organ/manual.h"

#include <gtest/gtest.h>

#include "instrument/keyboard/organ_model.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// 3-voice fugue assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, ThreeVoiceFugueAssignsGreatSwellPositiv) {
  auto assignments = assignManuals(3, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 3u);

  EXPECT_EQ(assignments[0].voice, 0);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);

  EXPECT_EQ(assignments[1].voice, 1);
  EXPECT_EQ(assignments[1].manual, OrganManual::Swell);

  EXPECT_EQ(assignments[2].voice, 2);
  EXPECT_EQ(assignments[2].manual, OrganManual::Positiv);
}

// ---------------------------------------------------------------------------
// 4-voice fugue assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, FourVoiceFugueAssignsGreatSwellPositivPedal) {
  auto assignments = assignManuals(4, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 4u);

  EXPECT_EQ(assignments[0].voice, 0);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);

  EXPECT_EQ(assignments[1].voice, 1);
  EXPECT_EQ(assignments[1].manual, OrganManual::Swell);

  EXPECT_EQ(assignments[2].voice, 2);
  EXPECT_EQ(assignments[2].manual, OrganManual::Positiv);

  EXPECT_EQ(assignments[3].voice, 3);
  EXPECT_EQ(assignments[3].manual, OrganManual::Pedal);
}

// ---------------------------------------------------------------------------
// 5-voice fugue assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, FiveVoiceFugueHasTwoOnGreat) {
  auto assignments = assignManuals(5, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 5u);

  // Voice 0 and 1 both on Great
  EXPECT_EQ(assignments[0].voice, 0);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);

  EXPECT_EQ(assignments[1].voice, 1);
  EXPECT_EQ(assignments[1].manual, OrganManual::Great);

  // Remaining voices distributed across Swell, Positiv, Pedal
  EXPECT_EQ(assignments[2].voice, 2);
  EXPECT_EQ(assignments[2].manual, OrganManual::Swell);

  EXPECT_EQ(assignments[3].voice, 3);
  EXPECT_EQ(assignments[3].manual, OrganManual::Positiv);

  EXPECT_EQ(assignments[4].voice, 4);
  EXPECT_EQ(assignments[4].manual, OrganManual::Pedal);
}

// ---------------------------------------------------------------------------
// 2-voice assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, TwoVoiceFugueAssignsGreatSwell) {
  auto assignments = assignManuals(2, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 2u);

  EXPECT_EQ(assignments[0].voice, 0);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);

  EXPECT_EQ(assignments[1].voice, 1);
  EXPECT_EQ(assignments[1].manual, OrganManual::Swell);
}

// ---------------------------------------------------------------------------
// Trio sonata assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, TrioSonataAssignsGreatSwellPedal) {
  auto assignments = assignManuals(3, FormType::TrioSonata);
  ASSERT_EQ(assignments.size(), 3u);

  EXPECT_EQ(assignments[0].voice, 0);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);

  EXPECT_EQ(assignments[1].voice, 1);
  EXPECT_EQ(assignments[1].manual, OrganManual::Swell);

  EXPECT_EQ(assignments[2].voice, 2);
  EXPECT_EQ(assignments[2].manual, OrganManual::Pedal);
}

TEST(ManualAssignmentTest, TrioSonataAlwaysThreeVoicesRegardlessOfInput) {
  // Even if num_voices is 4, trio sonata is always 3 voices
  auto assignments = assignManuals(4, FormType::TrioSonata);
  EXPECT_EQ(assignments.size(), 3u);
}

// ---------------------------------------------------------------------------
// VoiceRole assignment -- fugue forms
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, FugueVoiceRolesFollowOrder) {
  auto assignments = assignManuals(4, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 4u);

  EXPECT_EQ(assignments[0].role, VoiceRole::Assert);
  EXPECT_EQ(assignments[1].role, VoiceRole::Respond);
  EXPECT_EQ(assignments[2].role, VoiceRole::Propel);
  EXPECT_EQ(assignments[3].role, VoiceRole::Ground);
}

TEST(ManualAssignmentTest, FiveVoiceFugueLastTwoVoicesAreGround) {
  auto assignments = assignManuals(5, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 5u);

  EXPECT_EQ(assignments[0].role, VoiceRole::Assert);
  EXPECT_EQ(assignments[1].role, VoiceRole::Respond);
  EXPECT_EQ(assignments[2].role, VoiceRole::Propel);
  EXPECT_EQ(assignments[3].role, VoiceRole::Ground);
  EXPECT_EQ(assignments[4].role, VoiceRole::Ground);
}

// ---------------------------------------------------------------------------
// VoiceRole assignment -- trio sonata
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, TrioSonataAllVoicesAreAssert) {
  auto assignments = assignManuals(3, FormType::TrioSonata);
  ASSERT_EQ(assignments.size(), 3u);

  // All trio sonata voices are equal (Assert)
  EXPECT_EQ(assignments[0].role, VoiceRole::Assert);
  EXPECT_EQ(assignments[1].role, VoiceRole::Assert);
  EXPECT_EQ(assignments[2].role, VoiceRole::Assert);
}

// ---------------------------------------------------------------------------
// Channel mapping correctness
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, ChannelForAssignmentMatchesOrganModel) {
  auto assignments = assignManuals(4, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 4u);

  // Great=0, Swell=1, Positiv=2, Pedal=3
  EXPECT_EQ(channelForAssignment(assignments[0]), 0);  // Great
  EXPECT_EQ(channelForAssignment(assignments[1]), 1);  // Swell
  EXPECT_EQ(channelForAssignment(assignments[2]), 2);  // Positiv
  EXPECT_EQ(channelForAssignment(assignments[3]), 3);  // Pedal
}

TEST(ManualAssignmentTest, FiveVoiceFugueTwoVoicesShareGreatChannel) {
  auto assignments = assignManuals(5, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 5u);

  // Both voice 0 and voice 1 are on Great (channel 0)
  EXPECT_EQ(channelForAssignment(assignments[0]), 0);
  EXPECT_EQ(channelForAssignment(assignments[1]), 0);
}

// ---------------------------------------------------------------------------
// Program mapping correctness
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, ProgramForAssignmentMatchesOrganModel) {
  auto assignments = assignManuals(4, FormType::Fugue);
  ASSERT_EQ(assignments.size(), 4u);

  // Church Organ (19) for Great, Positiv, Pedal; Reed Organ (20) for Swell
  EXPECT_EQ(programForAssignment(assignments[0]), 19);  // Great
  EXPECT_EQ(programForAssignment(assignments[1]), 20);  // Swell
  EXPECT_EQ(programForAssignment(assignments[2]), 19);  // Positiv
  EXPECT_EQ(programForAssignment(assignments[3]), 19);  // Pedal
}

// ---------------------------------------------------------------------------
// Pitch playability check
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, PitchPlayableOnGreatManual) {
  OrganModel model;

  // Great range: C2-C6 (36-96)
  EXPECT_TRUE(isPitchPlayableOnManual(36, OrganManual::Great, model));
  EXPECT_TRUE(isPitchPlayableOnManual(60, OrganManual::Great, model));
  EXPECT_TRUE(isPitchPlayableOnManual(96, OrganManual::Great, model));

  EXPECT_FALSE(isPitchPlayableOnManual(35, OrganManual::Great, model));
  EXPECT_FALSE(isPitchPlayableOnManual(97, OrganManual::Great, model));
}

TEST(ManualAssignmentTest, PitchPlayableOnPedalManual) {
  OrganModel model;

  // Pedal range: C1-D3 (24-50)
  EXPECT_TRUE(isPitchPlayableOnManual(24, OrganManual::Pedal, model));
  EXPECT_TRUE(isPitchPlayableOnManual(50, OrganManual::Pedal, model));

  EXPECT_FALSE(isPitchPlayableOnManual(23, OrganManual::Pedal, model));
  EXPECT_FALSE(isPitchPlayableOnManual(51, OrganManual::Pedal, model));
  EXPECT_FALSE(isPitchPlayableOnManual(60, OrganManual::Pedal, model));
}

TEST(ManualAssignmentTest, PitchPlayableOnPositivManual) {
  OrganModel model;

  // Positiv range: C3-C6 (48-96) -- starts higher than Great/Swell
  EXPECT_TRUE(isPitchPlayableOnManual(48, OrganManual::Positiv, model));
  EXPECT_TRUE(isPitchPlayableOnManual(96, OrganManual::Positiv, model));

  EXPECT_FALSE(isPitchPlayableOnManual(47, OrganManual::Positiv, model));
  EXPECT_FALSE(isPitchPlayableOnManual(36, OrganManual::Positiv, model));
}

// ---------------------------------------------------------------------------
// Edge cases and invalid input
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, ZeroVoicesReturnsEmpty) {
  auto assignments = assignManuals(0, FormType::Fugue);
  EXPECT_TRUE(assignments.empty());
}

TEST(ManualAssignmentTest, OneVoiceReturnsEmpty) {
  auto assignments = assignManuals(1, FormType::Fugue);
  EXPECT_TRUE(assignments.empty());
}

TEST(ManualAssignmentTest, SixVoicesReturnsEmpty) {
  auto assignments = assignManuals(6, FormType::Fugue);
  EXPECT_TRUE(assignments.empty());
}

TEST(ManualAssignmentTest, SoloStringFormReturnsEmpty) {
  // Solo string forms should not use organ manual assignment
  auto cello = assignManuals(3, FormType::CelloPrelude);
  EXPECT_TRUE(cello.empty());

  auto chaconne = assignManuals(3, FormType::Chaconne);
  EXPECT_TRUE(chaconne.empty());
}

// ---------------------------------------------------------------------------
// Multiple organ form types use same fugue-style assignment
// ---------------------------------------------------------------------------

TEST(ManualAssignmentTest, PreludeAndFugueUseFugueAssignment) {
  auto assignments = assignManuals(4, FormType::PreludeAndFugue);
  ASSERT_EQ(assignments.size(), 4u);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);
  EXPECT_EQ(assignments[3].manual, OrganManual::Pedal);
}

TEST(ManualAssignmentTest, ToccataAndFugueUseFugueAssignment) {
  auto assignments = assignManuals(3, FormType::ToccataAndFugue);
  ASSERT_EQ(assignments.size(), 3u);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);
  EXPECT_EQ(assignments[2].manual, OrganManual::Positiv);
}

TEST(ManualAssignmentTest, PassacagliaUseFugueAssignment) {
  auto assignments = assignManuals(4, FormType::Passacaglia);
  ASSERT_EQ(assignments.size(), 4u);
  EXPECT_EQ(assignments[3].manual, OrganManual::Pedal);
  EXPECT_EQ(assignments[3].role, VoiceRole::Ground);
}

TEST(ManualAssignmentTest, ChoralePreludeUseFugueAssignment) {
  auto assignments = assignManuals(4, FormType::ChoralePrelude);
  ASSERT_EQ(assignments.size(), 4u);
  EXPECT_EQ(assignments[0].manual, OrganManual::Great);
  EXPECT_EQ(assignments[1].manual, OrganManual::Swell);
}

TEST(ManualAssignmentTest, FantasiaAndFugueUseFugueAssignment) {
  auto assignments = assignManuals(3, FormType::FantasiaAndFugue);
  ASSERT_EQ(assignments.size(), 3u);
  EXPECT_EQ(assignments[0].role, VoiceRole::Assert);
  EXPECT_EQ(assignments[2].role, VoiceRole::Propel);
}

}  // namespace
}  // namespace bach
