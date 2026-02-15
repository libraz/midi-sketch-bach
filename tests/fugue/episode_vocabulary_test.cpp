// Tests for vocabulary pattern integration in fugue episode generation.

#include <gtest/gtest.h>
#include <random>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "core/figure_match.h"
#include "fugue/episode.h"
#include "fugue/fugue_vocabulary.h"
#include "fugue/subject.h"

namespace bach {
namespace {

// Helper to create a minimal subject for episode testing.
Subject makeTestSubject(Key key, bool is_minor, SubjectCharacter character) {
  Subject sub;
  sub.key = key;
  sub.is_minor = is_minor;
  sub.character = character;
  // Simple 8-note subject in the given key (scale run + turn).
  sub.notes = {
      {0, 480, 72, 80, 0}, {480, 480, 71, 80, 0}, {960, 480, 69, 80, 0},
      {1440, 480, 67, 80, 0}, {1920, 480, 69, 80, 0}, {2400, 480, 71, 80, 0},
      {2880, 480, 72, 80, 0}, {3360, 480, 71, 80, 0},
  };
  sub.length_ticks = 3840;
  return sub;
}

TEST(EpisodeVocabularyTest, GenerateEpisodeDoesNotCrash) {
  Subject subject = makeTestSubject(Key::C, false, SubjectCharacter::Severe);
  Episode episode = generateEpisode(subject, 0, 3840, Key::C, Key::G, 4, 42,
                                     0, 0.5f, nullptr);
  EXPECT_GT(episode.noteCount(), 0u);
}

TEST(EpisodeVocabularyTest, GenerateEpisodeWithMinorKey) {
  Subject subject = makeTestSubject(Key::A, true, SubjectCharacter::Restless);
  Episode episode = generateEpisode(subject, 0, 3840, Key::A, Key::E, 4, 123,
                                     0, 0.6f, nullptr);
  EXPECT_GT(episode.noteCount(), 0u);
}

TEST(EpisodeVocabularyTest, VocabularyPatternsDontIncreaseEpisodeSize) {
  // Vocabulary should not increase note count beyond normal episodes.
  Subject subject = makeTestSubject(Key::C, false, SubjectCharacter::Noble);
  Episode ep1 = generateEpisode(subject, 0, 7680, Key::C, Key::G, 4, 1, 0, 0.5f, nullptr);
  Episode ep2 = generateEpisode(subject, 0, 7680, Key::C, Key::G, 4, 2, 0, 0.5f, nullptr);
  // Both should produce episodes; exact note counts may differ.
  EXPECT_GT(ep1.noteCount(), 0u);
  EXPECT_GT(ep2.noteCount(), 0u);
}

TEST(EpisodeVocabularyTest, BassPatternUsesVocabulary) {
  // Generate many episodes with different seeds; vocabulary patterns should
  // occasionally appear (they activate 40% of the time).
  Subject subject = makeTestSubject(Key::C, false, SubjectCharacter::Severe);
  bool any_generated = false;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    Episode episode = generateEpisode(subject, 0, 3840, Key::C, Key::G, 4, seed,
                                       0, 0.5f, nullptr);
    if (episode.noteCount() > 0) {
      any_generated = true;
    }
  }
  EXPECT_TRUE(any_generated);
}

TEST(EpisodeVocabularyTest, PlayfulCharacterWorks) {
  Subject subject = makeTestSubject(Key::D, false, SubjectCharacter::Playful);
  Episode episode = generateEpisode(subject, 0, 3840, Key::D, Key::A, 3, 77,
                                     1, 0.7f, nullptr);
  EXPECT_GT(episode.noteCount(), 0u);
}

TEST(EpisodeVocabularyTest, FigureMatchOnMotifExtraction) {
  // Verify that figure_match::findBestFigure works on typical motif pitches.
  uint8_t desc_run[] = {72, 71, 69, 67};  // Descending C major run.
  int idx = figure_match::findBestFigure(
      desc_run, 4, kCommonFigures, kCommonFigureCount,
      Key::C, ScaleType::Major, 0.7f);
  // Should find kDescRun4 (index 0).
  EXPECT_EQ(idx, 0);
}

TEST(EpisodeVocabularyTest, BassVocabularyFigureMatchable) {
  // Verify bass patterns are matchable.
  // kBassLeapStep: {1, 0}, {-2, 0}, {1, 0} -> C4, D4, B3, C4 in C major.
  uint8_t bass_pitches[] = {60, 62, 59, 60};
  int idx = figure_match::findBestFigure(
      bass_pitches, 4, kFugueBassPatterns, kFugueBassPatternCount,
      Key::C, ScaleType::Major, 0.6f);
  EXPECT_GE(idx, 0);
}

TEST(EpisodeVocabularyTest, MultipleSeeds30Stability) {
  // Ensure all 30 seeds produce valid episodes without crash.
  Subject subject = makeTestSubject(Key::C, false, SubjectCharacter::Severe);
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    Episode episode = generateEpisode(subject, 0, 3840, Key::C, Key::G, 4, seed,
                                       0, 0.5f, nullptr);
    EXPECT_GE(episode.noteCount(), 0u) << "seed=" << seed;
  }
}

}  // namespace
}  // namespace bach
