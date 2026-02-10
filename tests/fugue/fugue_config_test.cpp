// Tests for fugue/fugue_config.h -- answer type strings, character phase
// restrictions, and FugueConfig defaults.

#include "fugue/fugue_config.h"

#include <gtest/gtest.h>

#include <string>

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// answerTypeToString
// ---------------------------------------------------------------------------

TEST(FugueConfigTest, AnswerTypeToStringAuto) {
  EXPECT_STREQ(answerTypeToString(AnswerType::Auto), "Auto");
}

TEST(FugueConfigTest, AnswerTypeToStringReal) {
  EXPECT_STREQ(answerTypeToString(AnswerType::Real), "Real");
}

TEST(FugueConfigTest, AnswerTypeToStringTonal) {
  EXPECT_STREQ(answerTypeToString(AnswerType::Tonal), "Tonal");
}

// ---------------------------------------------------------------------------
// isCharacterAvailable -- phase restrictions
// ---------------------------------------------------------------------------

TEST(FugueConfigTest, SevereAvailableInAllPhases) {
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Severe, 1));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Severe, 2));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Severe, 3));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Severe, 4));
}

TEST(FugueConfigTest, PlayfulAvailableInAllPhases) {
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Playful, 1));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Playful, 2));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Playful, 3));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Playful, 4));
}

TEST(FugueConfigTest, NobleAvailableInAllPhases) {
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Noble, 1));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Noble, 2));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Noble, 3));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Noble, 4));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Noble, 5));
}

TEST(FugueConfigTest, RestlessAvailableInAllPhases) {
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Restless, 1));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Restless, 2));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Restless, 3));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Restless, 4));
  EXPECT_TRUE(isCharacterAvailable(SubjectCharacter::Restless, 5));
}

TEST(FugueConfigTest, InvalidPhaseZero) {
  // Phase 0 is invalid; no character should be available.
  EXPECT_FALSE(isCharacterAvailable(SubjectCharacter::Severe, 0));
  EXPECT_FALSE(isCharacterAvailable(SubjectCharacter::Playful, 0));
  EXPECT_FALSE(isCharacterAvailable(SubjectCharacter::Noble, 0));
  EXPECT_FALSE(isCharacterAvailable(SubjectCharacter::Restless, 0));
}

TEST(FugueConfigTest, NegativePhase) {
  EXPECT_FALSE(isCharacterAvailable(SubjectCharacter::Severe, -1));
}

// ---------------------------------------------------------------------------
// FugueConfig defaults
// ---------------------------------------------------------------------------

TEST(FugueConfigTest, DefaultValues) {
  FugueConfig config;
  EXPECT_EQ(config.subject_source, SubjectSource::Generate);
  EXPECT_EQ(config.character, SubjectCharacter::Severe);
  EXPECT_EQ(config.answer_type, AnswerType::Auto);
  EXPECT_EQ(config.num_voices, 3);
  EXPECT_EQ(config.key, Key::C);
  EXPECT_EQ(config.bpm, 72);
  EXPECT_EQ(config.seed, 0u);
  EXPECT_EQ(config.subject_bars, 2);
  EXPECT_EQ(config.max_subject_retries, 10);
}

TEST(FugueConfigTest, CustomValues) {
  FugueConfig config;
  config.subject_source = SubjectSource::Import;
  config.character = SubjectCharacter::Playful;
  config.answer_type = AnswerType::Tonal;
  config.num_voices = 4;
  config.key = Key::G;
  config.bpm = 120;
  config.seed = 42;
  config.subject_bars = 3;
  config.max_subject_retries = 20;

  EXPECT_EQ(config.subject_source, SubjectSource::Import);
  EXPECT_EQ(config.character, SubjectCharacter::Playful);
  EXPECT_EQ(config.answer_type, AnswerType::Tonal);
  EXPECT_EQ(config.num_voices, 4);
  EXPECT_EQ(config.key, Key::G);
  EXPECT_EQ(config.bpm, 120);
  EXPECT_EQ(config.seed, 42u);
  EXPECT_EQ(config.subject_bars, 3);
  EXPECT_EQ(config.max_subject_retries, 20);
}

}  // namespace
}  // namespace bach
