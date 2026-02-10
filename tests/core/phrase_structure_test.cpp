// Tests for core/phrase_structure.h -- phrase boundary detection, breathing
// points, and antecedent-consequent role assignment.

#include "core/phrase_structure.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"

namespace bach {
namespace {

TEST(PhraseStructureTest, EmptyDuration_EmptyResult) {
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({}, 0);
  EXPECT_EQ(ps.phraseCount(), 0u);
  EXPECT_TRUE(ps.breathingPoints().empty());
}

TEST(PhraseStructureTest, NoCadences_SinglePhrase) {
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({}, kTicksPerBar * 8);
  EXPECT_EQ(ps.phraseCount(), 1u);
  EXPECT_TRUE(ps.breathingPoints().empty());
}

TEST(PhraseStructureTest, OneCadence_TwoPhrases) {
  Tick cadence = kTicksPerBar * 4;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, kTicksPerBar * 8);
  EXPECT_EQ(ps.phraseCount(), 2u);
  EXPECT_EQ(ps.breathingPoints().size(), 1u);
}

TEST(PhraseStructureTest, TwoCadences_ThreePhrases) {
  std::vector<Tick> cadences = {kTicksPerBar * 4, kTicksPerBar * 8};
  PhraseStructure ps = PhraseStructure::fromCadenceTicks(cadences, kTicksPerBar * 12);
  EXPECT_EQ(ps.phraseCount(), 3u);
}

TEST(PhraseStructureTest, PhraseRolesAlternate) {
  std::vector<Tick> cadences = {kTicksPerBar * 4, kTicksPerBar * 8};
  PhraseStructure ps = PhraseStructure::fromCadenceTicks(cadences, kTicksPerBar * 12);

  const auto& phrases = ps.phrases();
  ASSERT_GE(phrases.size(), 2u);
  EXPECT_EQ(phrases[0].role, Phrase::Role::Antecedent);
  EXPECT_EQ(phrases[1].role, Phrase::Role::Consequent);
}

TEST(PhraseStructureTest, LastPhraseIsFree_WhenOddCount) {
  // Three phrases: Antecedent, Consequent, Free (last one unpaired).
  std::vector<Tick> cadences = {kTicksPerBar * 4, kTicksPerBar * 8};
  PhraseStructure ps = PhraseStructure::fromCadenceTicks(cadences, kTicksPerBar * 12);

  const auto& phrases = ps.phrases();
  ASSERT_EQ(phrases.size(), 3u);
  EXPECT_EQ(phrases[2].role, Phrase::Role::Free);
}

TEST(PhraseStructureTest, BreathingPointDuration_IsQuarterBeat) {
  Tick cadence = kTicksPerBar * 4;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, kTicksPerBar * 8);

  ASSERT_EQ(ps.breathingPoints().size(), 1u);
  EXPECT_EQ(ps.breathingPoints()[0].duration, kTicksPerBeat / 4);
}

TEST(PhraseStructureTest, BreathingPointPosition_AtCadenceTick) {
  Tick cadence = kTicksPerBar * 4;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, kTicksPerBar * 8);

  ASSERT_EQ(ps.breathingPoints().size(), 1u);
  EXPECT_EQ(ps.breathingPoints()[0].tick, cadence);
}

TEST(PhraseStructureTest, DistanceToBoundary_OnBoundary) {
  Tick cadence = kTicksPerBar * 4;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, kTicksPerBar * 8);

  // Start of first phrase (tick 0) is a boundary.
  EXPECT_EQ(ps.distanceToPhraseBoundary(0), 0u);
  // Cadence position is a boundary (end of phrase 0 = start of phrase 1).
  EXPECT_EQ(ps.distanceToPhraseBoundary(cadence), 0u);
}

TEST(PhraseStructureTest, DistanceToBoundary_MidPhrase) {
  Tick cadence = kTicksPerBar * 4;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, kTicksPerBar * 8);

  Tick mid = kTicksPerBar * 2;
  Tick dist = ps.distanceToPhraseBoundary(mid);
  EXPECT_GT(dist, 0u);
}

TEST(PhraseStructureTest, PhraseDuration_MatchesBoundarySpan) {
  Tick cadence = kTicksPerBar * 4;
  Tick total = kTicksPerBar * 8;
  PhraseStructure ps = PhraseStructure::fromCadenceTicks({cadence}, total);

  const auto& phrases = ps.phrases();
  ASSERT_EQ(phrases.size(), 2u);
  EXPECT_EQ(phrases[0].durationTicks(), cadence);
  EXPECT_EQ(phrases[1].durationTicks(), total - cadence);
}

TEST(PhraseStructureTest, DuplicateCadences_Deduplicated) {
  // Duplicate cadence ticks should not create extra tiny phrases.
  Tick cadence = kTicksPerBar * 4;
  std::vector<Tick> cadences = {cadence, cadence, cadence};
  PhraseStructure ps = PhraseStructure::fromCadenceTicks(cadences, kTicksPerBar * 8);

  EXPECT_EQ(ps.phraseCount(), 2u);
}

TEST(PhraseStructureTest, MultipleCadences_MultipleBreathingPoints) {
  // Four cadences -> 5 phrases, 4 breathing points (except last phrase has none).
  std::vector<Tick> cadences = {
      kTicksPerBar * 4, kTicksPerBar * 8,
      kTicksPerBar * 12, kTicksPerBar * 16};
  PhraseStructure ps = PhraseStructure::fromCadenceTicks(cadences, kTicksPerBar * 20);

  EXPECT_EQ(ps.phraseCount(), 5u);
  // Breathing points at all boundaries except the last phrase end.
  EXPECT_EQ(ps.breathingPoints().size(), 4u);
}

}  // namespace
}  // namespace bach
