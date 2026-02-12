// Tests for fugue/fugue_config.h -- answer type strings, character phase
// restrictions, and FugueConfig defaults.

#include "fugue/fugue_config.h"

#include <gtest/gtest.h>

#include <random>
#include <set>
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

// ---------------------------------------------------------------------------
// FugueEnergyCurve::selectDuration
// ---------------------------------------------------------------------------

TEST(SelectDurationTest, DeterministicWithSameSeed) {
  std::mt19937 rng1(123);
  std::mt19937 rng2(123);
  for (int i = 0; i < 50; ++i) {
    Tick d1 = FugueEnergyCurve::selectDuration(0.5f, 0, rng1, 0);
    Tick d2 = FugueEnergyCurve::selectDuration(0.5f, 0, rng2, 0);
    EXPECT_EQ(d1, d2) << "Same seed must produce same duration at iteration " << i;
  }
}

TEST(SelectDurationTest, EnergyFloorSuppressesShortDurations) {
  std::mt19937 rng(42);
  // energy=0.2 -> minDuration returns kTicksPerBeat (quarter note).
  for (int i = 0; i < 1000; ++i) {
    Tick dur = FugueEnergyCurve::selectDuration(0.2f, 0, rng, 0);
    EXPECT_GE(dur, kTicksPerBeat)
        << "Low energy (0.2) should never produce sub-quarter durations";
  }
}

TEST(SelectDurationTest, BarStartFavorsLongNotes) {
  std::mt19937 rng_bar(99);
  std::mt19937 rng_off(99);
  int bar_long = 0;
  int off_long = 0;
  constexpr int kTrials = 2000;
  for (int i = 0; i < kTrials; ++i) {
    // Reseed each iteration for independence.
    rng_bar.seed(static_cast<uint32_t>(i));
    rng_off.seed(static_cast<uint32_t>(i));
    Tick d_bar = FugueEnergyCurve::selectDuration(0.5f, 0, rng_bar, 0);
    Tick d_off = FugueEnergyCurve::selectDuration(0.5f, kTicksPerBeat, rng_off, 0);
    if (d_bar >= kTicksPerBeat * 2) bar_long++;
    if (d_off >= kTicksPerBeat * 2) off_long++;
  }
  EXPECT_GT(bar_long, off_long)
      << "Bar start (tick=0) should produce more half-note-or-longer than offbeat";
}

TEST(SelectDurationTest, ComplementarityPrefersContrast) {
  std::mt19937 rng(77);
  int long_count = 0;
  constexpr int kTrials = 1000;
  for (int i = 0; i < kTrials; ++i) {
    // other_dur = eighth note (short) should bias toward quarter+.
    Tick dur = FugueEnergyCurve::selectDuration(0.6f, 0, rng, kTicksPerBeat / 2);
    if (dur >= kTicksPerBeat) long_count++;
  }
  EXPECT_GT(long_count, kTrials / 2)
      << "When other voice has short notes, quarter+ should be majority";
}

TEST(SelectDurationTest, HighEnergyReachesAllDurations) {
  std::mt19937 rng(55);
  std::set<Tick> seen;
  for (int i = 0; i < 10000; ++i) {
    Tick dur = FugueEnergyCurve::selectDuration(0.8f, 0, rng, 0);
    seen.insert(dur);
  }
  // All 6 baroque durations should appear at energy=0.8.
  EXPECT_GE(seen.size(), 5u)
      << "High energy should produce at least 5 distinct durations";
}

TEST(SelectDurationTest, BarStartLongNoteRatioAboveHalf) {
  std::mt19937 rng(88);
  int long_count = 0;
  constexpr int kTrials = 2000;
  for (int i = 0; i < kTrials; ++i) {
    Tick dur = FugueEnergyCurve::selectDuration(0.5f, 0, rng, 0);
    if (dur >= kTicksPerBeat) long_count++;
  }
  // At bar start with moderate energy, quarter+ should dominate.
  EXPECT_GT(long_count, kTrials / 2)
      << "Bar start should produce >50% quarter-note-or-longer";
}

}  // namespace
}  // namespace bach
