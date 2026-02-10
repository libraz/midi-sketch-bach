// Tests for rhythm pattern module.

#include <gtest/gtest.h>

#include <numeric>

#include "core/rhythm_pattern.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// getPatternDurations: verify each pattern sums correctly
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, StraightDurationsSumToOneBeat) {
  auto durations = getPatternDurations(RhythmPattern::Straight);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat);
  EXPECT_EQ(durations.size(), 1u);
  EXPECT_EQ(durations[0], 480u);
}

TEST(RhythmPatternTest, DottedEighthDurationsSumToOneBeat) {
  auto durations = getPatternDurations(RhythmPattern::DottedEighth);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat);
  EXPECT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 360u);
  EXPECT_EQ(durations[1], 120u);
}

TEST(RhythmPatternTest, DottedQuarterDurationsSumToTwoBeats) {
  auto durations = getPatternDurations(RhythmPattern::DottedQuarter);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat * 2);  // 960
  EXPECT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 720u);
  EXPECT_EQ(durations[1], 240u);
}

TEST(RhythmPatternTest, SyncopatedDurationsSumToOneBeat) {
  auto durations = getPatternDurations(RhythmPattern::Syncopated);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat);
  EXPECT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 240u);
  EXPECT_EQ(durations[1], 240u);
}

TEST(RhythmPatternTest, LombardReverseDurationsSumToOneBeat) {
  auto durations = getPatternDurations(RhythmPattern::LombardReverse);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat);
  EXPECT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 120u);
  EXPECT_EQ(durations[1], 360u);
}

TEST(RhythmPatternTest, TripletDurationsSumToOneBeat) {
  auto durations = getPatternDurations(RhythmPattern::Triplet);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBeat);
  EXPECT_EQ(durations.size(), 3u);
  EXPECT_EQ(durations[0], 160u);
  EXPECT_EQ(durations[1], 160u);
  EXPECT_EQ(durations[2], 160u);
}

// ---------------------------------------------------------------------------
// getAllowedPatterns: verify character-pattern associations
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, SevereAllowsStraightAndDottedQuarter) {
  auto allowed = getAllowedPatterns(SubjectCharacter::Severe);
  ASSERT_EQ(allowed.size(), 2u);
  EXPECT_EQ(allowed[0], RhythmPattern::Straight);
  EXPECT_EQ(allowed[1], RhythmPattern::DottedQuarter);
}

TEST(RhythmPatternTest, PlayfulAllowsFourPatterns) {
  auto allowed = getAllowedPatterns(SubjectCharacter::Playful);
  ASSERT_EQ(allowed.size(), 4u);
  EXPECT_EQ(allowed[0], RhythmPattern::Straight);
  EXPECT_EQ(allowed[1], RhythmPattern::DottedEighth);
  EXPECT_EQ(allowed[2], RhythmPattern::Triplet);
  EXPECT_EQ(allowed[3], RhythmPattern::LombardReverse);
}

TEST(RhythmPatternTest, NobleAllowsStraightAndDottedQuarter) {
  auto allowed = getAllowedPatterns(SubjectCharacter::Noble);
  ASSERT_EQ(allowed.size(), 2u);
  EXPECT_EQ(allowed[0], RhythmPattern::Straight);
  EXPECT_EQ(allowed[1], RhythmPattern::DottedQuarter);
}

TEST(RhythmPatternTest, RestlessAllowsFourPatterns) {
  auto allowed = getAllowedPatterns(SubjectCharacter::Restless);
  ASSERT_EQ(allowed.size(), 4u);
  EXPECT_EQ(allowed[0], RhythmPattern::DottedEighth);
  EXPECT_EQ(allowed[1], RhythmPattern::Syncopated);
  EXPECT_EQ(allowed[2], RhythmPattern::LombardReverse);
  EXPECT_EQ(allowed[3], RhythmPattern::Triplet);
}

// ---------------------------------------------------------------------------
// selectPattern: determinism and coverage
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, SelectPatternDeterministic) {
  auto pat1 = selectPattern(SubjectCharacter::Playful, 42);
  auto pat2 = selectPattern(SubjectCharacter::Playful, 42);
  EXPECT_EQ(pat1, pat2);
}

TEST(RhythmPatternTest, SelectPatternCoversAllAllowed) {
  // For Playful (4 patterns), seeds 0-3 should each produce a different pattern.
  auto allowed = getAllowedPatterns(SubjectCharacter::Playful);
  for (uint32_t idx = 0; idx < allowed.size(); ++idx) {
    EXPECT_EQ(selectPattern(SubjectCharacter::Playful, idx), allowed[idx]);
  }
}

TEST(RhythmPatternTest, SelectPatternWrapsAround) {
  // Seed beyond allowed size should wrap.
  auto allowed = getAllowedPatterns(SubjectCharacter::Severe);
  EXPECT_EQ(selectPattern(SubjectCharacter::Severe, 0), allowed[0]);
  EXPECT_EQ(selectPattern(SubjectCharacter::Severe, 2), allowed[0]);  // 2 % 2 = 0
  EXPECT_EQ(selectPattern(SubjectCharacter::Severe, 3), allowed[1]);  // 3 % 2 = 1
}

// ---------------------------------------------------------------------------
// applyPatternToSpan: sum correctness and edge cases
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, ApplyPatternToSpanSumsToExactBar) {
  auto durations = applyPatternToSpan(RhythmPattern::DottedEighth, kTicksPerBar);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBar);
  // 1920 / 480 = 4 beats, each beat has 2 notes -> 8 notes
  EXPECT_EQ(durations.size(), 8u);
}

TEST(RhythmPatternTest, ApplyPatternToSpanStraightOneBar) {
  auto durations = applyPatternToSpan(RhythmPattern::Straight, kTicksPerBar);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBar);
  EXPECT_EQ(durations.size(), 4u);
  for (const auto& dur : durations) {
    EXPECT_EQ(dur, 480u);
  }
}

TEST(RhythmPatternTest, ApplyPatternToSpanTripletOneBar) {
  auto durations = applyPatternToSpan(RhythmPattern::Triplet, kTicksPerBar);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBar);
  // 1920 / 480 = 4 beats, each beat has 3 triplet notes -> 12 notes
  EXPECT_EQ(durations.size(), 12u);
}

TEST(RhythmPatternTest, ApplyPatternToSpanHandlesPartialCycle) {
  // 500 ticks: one full triplet cycle (480) + 20 remaining
  auto durations = applyPatternToSpan(RhythmPattern::Triplet, 500);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, Tick{500});
  // 3 triplet notes (160+160+160=480) + 1 truncated note (20) = 4 notes
  EXPECT_EQ(durations.size(), 4u);
  EXPECT_EQ(durations[3], 20u);
}

TEST(RhythmPatternTest, ApplyPatternToSpanDottedQuarterTwoBeats) {
  // DottedQuarter cycle is 960 ticks. 960 should produce exactly 1 cycle.
  auto durations = applyPatternToSpan(RhythmPattern::DottedQuarter, 960);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, Tick{960});
  EXPECT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 720u);
  EXPECT_EQ(durations[1], 240u);
}

TEST(RhythmPatternTest, ApplyPatternToSpanDottedQuarterOneBar) {
  // 1920 = 2 full DottedQuarter cycles (960 * 2)
  auto durations = applyPatternToSpan(RhythmPattern::DottedQuarter, kTicksPerBar);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, kTicksPerBar);
  EXPECT_EQ(durations.size(), 4u);
}

TEST(RhythmPatternTest, ApplyPatternToSpanZeroReturnsEmpty) {
  auto durations = applyPatternToSpan(RhythmPattern::Straight, 0);
  EXPECT_TRUE(durations.empty());
}

TEST(RhythmPatternTest, ApplyPatternToSpanSmallValueTruncates) {
  // Span smaller than first note in pattern
  auto durations = applyPatternToSpan(RhythmPattern::Straight, 100);
  Tick sum = std::accumulate(durations.begin(), durations.end(), Tick{0});
  EXPECT_EQ(sum, Tick{100});
  EXPECT_EQ(durations.size(), 1u);
  EXPECT_EQ(durations[0], 100u);
}

TEST(RhythmPatternTest, ApplyPatternAllDurationsPositive) {
  // Every returned duration must be > 0.
  auto patterns = {RhythmPattern::Straight, RhythmPattern::DottedEighth,
                   RhythmPattern::DottedQuarter, RhythmPattern::Syncopated,
                   RhythmPattern::LombardReverse, RhythmPattern::Triplet};
  for (auto pat : patterns) {
    auto durations = applyPatternToSpan(pat, kTicksPerBar * 3);
    for (const auto& dur : durations) {
      EXPECT_GT(dur, 0u) << "Pattern " << rhythmPatternToString(pat)
                          << " produced zero-duration note";
    }
  }
}

// ---------------------------------------------------------------------------
// notesPerBeat: verify per-pattern counts
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, NotesPerBeatCorrect) {
  EXPECT_EQ(notesPerBeat(RhythmPattern::Straight), 1);
  EXPECT_EQ(notesPerBeat(RhythmPattern::DottedEighth), 2);
  EXPECT_EQ(notesPerBeat(RhythmPattern::DottedQuarter), 1);
  EXPECT_EQ(notesPerBeat(RhythmPattern::Syncopated), 2);
  EXPECT_EQ(notesPerBeat(RhythmPattern::LombardReverse), 2);
  EXPECT_EQ(notesPerBeat(RhythmPattern::Triplet), 3);
}

// ---------------------------------------------------------------------------
// rhythmPatternToString: coverage
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, ToStringNotNull) {
  EXPECT_NE(rhythmPatternToString(RhythmPattern::Straight), nullptr);
  EXPECT_NE(rhythmPatternToString(RhythmPattern::DottedEighth), nullptr);
  EXPECT_NE(rhythmPatternToString(RhythmPattern::DottedQuarter), nullptr);
  EXPECT_NE(rhythmPatternToString(RhythmPattern::Syncopated), nullptr);
  EXPECT_NE(rhythmPatternToString(RhythmPattern::LombardReverse), nullptr);
  EXPECT_NE(rhythmPatternToString(RhythmPattern::Triplet), nullptr);
}

TEST(RhythmPatternTest, ToStringValues) {
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::Straight), "Straight");
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::DottedEighth), "DottedEighth");
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::DottedQuarter), "DottedQuarter");
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::Syncopated), "Syncopated");
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::LombardReverse), "LombardReverse");
  EXPECT_STREQ(rhythmPatternToString(RhythmPattern::Triplet), "Triplet");
}

// ---------------------------------------------------------------------------
// Syncopated pattern: specific duration values
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, SyncopatedDurationsAreEqualEighths) {
  auto durations = getPatternDurations(RhythmPattern::Syncopated);
  ASSERT_EQ(durations.size(), 2u);
  EXPECT_EQ(durations[0], 240u);
  EXPECT_EQ(durations[1], 240u);
}

// ---------------------------------------------------------------------------
// LombardReverse pattern: short-long ordering
// ---------------------------------------------------------------------------

TEST(RhythmPatternTest, LombardReverseIsShortLong) {
  auto durations = getPatternDurations(RhythmPattern::LombardReverse);
  ASSERT_EQ(durations.size(), 2u);
  EXPECT_LT(durations[0], durations[1]);  // short before long
}

}  // namespace
}  // namespace bach
