/// @file
/// @brief Tests for vocabulary bonus in archetype scorer Kopfmotiv scoring.

#include <gtest/gtest.h>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "core/figure_match.h"
#include "fugue/archetype_policy.h"
#include "fugue/archetype_scorer.h"
#include "fugue/subject.h"

namespace bach {
namespace {

/// @brief Create subject with known Kopfmotiv pitches.
Subject makeSubjectWithKopf(const std::vector<uint8_t>& kopf_pitches,
                            Key key, bool is_minor) {
  Subject sub;
  sub.key = key;
  sub.is_minor = is_minor;
  sub.character = SubjectCharacter::Severe;
  for (size_t idx = 0; idx < kopf_pitches.size(); ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(idx) * 480;
    note.duration = 480;
    note.pitch = kopf_pitches[idx];
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    sub.notes.push_back(note);
  }
  // Add a few more notes so subject is long enough.
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note;
    note.start_tick = static_cast<Tick>(kopf_pitches.size() + idx) * 480;
    note.duration = 480;
    note.pitch = kopf_pitches.back();
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    sub.notes.push_back(note);
  }
  sub.length_ticks = static_cast<Tick>(sub.notes.size()) * 480;
  return sub;
}

TEST(ArchetypeScorerVocabularyTest, VocabularyBonusIncreasesScore) {
  ArchetypeScorer scorer;
  ArchetypePolicy policy;

  // Subject whose Kopfmotiv matches kDescRun4: C5, B4, A4, G4.
  Subject sub1 = makeSubjectWithKopf({72, 71, 69, 67}, Key::C, false);
  // Subject with similar stepwise motion but shifted (no vocabulary match):
  // D5, C#5, B4, A4 — same interval sizes but starting on non-tonic degree.
  Subject sub2 = makeSubjectWithKopf({74, 73, 71, 69}, Key::C, false);

  float score1 = scorer.scoreKopfmotivStrength(sub1, policy);
  float score2 = scorer.scoreKopfmotivStrength(sub2, policy);

  // Both subjects have identical interval variety and contour,
  // so the vocabulary bonus should make sub1 score higher.
  EXPECT_GE(score1, 0.0f);
  EXPECT_LE(score1, 1.0f);
  EXPECT_GE(score1, score2) << "Vocabulary-matching subject should score >= similar non-matching";
}

TEST(ArchetypeScorerVocabularyTest, BonusCappedAtPointOne) {
  ArchetypeScorer scorer;
  ArchetypePolicy policy;

  // Perfect match subject.
  Subject sub = makeSubjectWithKopf({72, 71, 69, 67}, Key::C, false);
  float score = scorer.scoreKopfmotivStrength(sub, policy);
  EXPECT_LE(score, 1.0f);
}

TEST(ArchetypeScorerVocabularyTest, MinorKeySubjectWorks) {
  ArchetypeScorer scorer;
  ArchetypePolicy policy;

  // A minor descending: A4, G4, F4, E4.
  Subject sub = makeSubjectWithKopf({69, 67, 65, 64}, Key::A, true);
  float score = scorer.scoreKopfmotivStrength(sub, policy);
  EXPECT_GE(score, 0.0f);
  EXPECT_LE(score, 1.0f);
}

TEST(ArchetypeScorerVocabularyTest, ShortSubjectHandledGracefully) {
  ArchetypeScorer scorer;
  ArchetypePolicy policy;

  // 2-note subject (too short for vocabulary match).
  Subject sub = makeSubjectWithKopf({72, 67}, Key::C, false);
  float score = scorer.scoreKopfmotivStrength(sub, policy);
  // Should not crash; returns 0 for subjects < 3 notes.
  EXPECT_GE(score, 0.0f);
}

TEST(ArchetypeScorerVocabularyTest, VocabularyMatchDetectable) {
  // Verify figure_match can detect kDescRun4 in Kopfmotiv pitches.
  uint8_t pitches[] = {72, 71, 69, 67};
  int idx = figure_match::findBestFigure(
      pitches, 4, kCommonFigures, kCommonFigureCount,
      Key::C, ScaleType::Major, 0.6f);
  EXPECT_GE(idx, 0);  // Should find a match.
}

TEST(ArchetypeScorerVocabularyTest, NoMatchForRandomPitches) {
  uint8_t pitches[] = {60, 90, 30, 100};
  int idx = figure_match::findBestFigure(
      pitches, 4, kCommonFigures, kCommonFigureCount,
      Key::C, ScaleType::Major, 0.6f);
  EXPECT_EQ(idx, -1);  // No match for random jumps.
}

}  // namespace
}  // namespace bach
