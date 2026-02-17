#include <gtest/gtest.h>
#include <random>

#include "core/bach_vocabulary.h"
#include "core/basic_types.h"
#include "forms/gesture_template.h"

namespace bach {
namespace {

TEST(GestureVocabularyTest, DramaticusFigures) {
  auto hint = getArchetypeFigures(ToccataArchetype::Dramaticus);
  EXPECT_EQ(hint.count, 3);
  EXPECT_NEAR(hint.activation_prob, 0.50f, 0.01f);
  EXPECT_EQ(hint.figures[0], &kDescRun5);
  EXPECT_EQ(hint.figures[1], &kBrechungDesc);
  EXPECT_EQ(hint.figures[2], &kTrill5);
}

TEST(GestureVocabularyTest, PerpetuusFigures) {
  auto hint = getArchetypeFigures(ToccataArchetype::Perpetuus);
  EXPECT_EQ(hint.count, 3);
  EXPECT_NEAR(hint.activation_prob, 0.30f, 0.01f);
  EXPECT_EQ(hint.figures[0], &kUpperNbr);
  EXPECT_EQ(hint.figures[1], &kLowerNbr);
  EXPECT_EQ(hint.figures[2], &kAscRun4);
}

TEST(GestureVocabularyTest, ConcertatoFigures) {
  auto hint = getArchetypeFigures(ToccataArchetype::Concertato);
  EXPECT_EQ(hint.count, 3);
  EXPECT_NEAR(hint.activation_prob, 0.35f, 0.01f);
  EXPECT_EQ(hint.figures[0], &kLeapUpStepDown);
  EXPECT_EQ(hint.figures[1], &kEscapeDown);
  EXPECT_EQ(hint.figures[2], &kCambiataDown);
}

TEST(GestureVocabularyTest, SectionalisFigures) {
  auto hint = getArchetypeFigures(ToccataArchetype::Sectionalis);
  EXPECT_EQ(hint.count, 3);
  EXPECT_NEAR(hint.activation_prob, 0.25f, 0.01f);
  EXPECT_EQ(hint.figures[0], &kTurnUp);
  EXPECT_EQ(hint.figures[1], &kStepDownLeapUp);
  EXPECT_EQ(hint.figures[2], &kLeapRecovery);
}

TEST(GestureVocabularyTest, AllArchetypesReturnValidHints) {
  for (auto arch : {ToccataArchetype::Dramaticus, ToccataArchetype::Perpetuus,
                    ToccataArchetype::Concertato, ToccataArchetype::Sectionalis}) {
    auto hint = getArchetypeFigures(arch);
    EXPECT_GE(hint.count, 1);
    EXPECT_LE(hint.count, 3);
    EXPECT_GT(hint.activation_prob, 0.0f);
    EXPECT_LE(hint.activation_prob, 1.0f);
    EXPECT_NE(hint.figures[0], nullptr);
  }
}

TEST(GestureVocabularyTest, DramaticusFiguresAreTransposable) {
  auto hint = getArchetypeFigures(ToccataArchetype::Dramaticus);
  for (int idx = 0; idx < hint.count; ++idx) {
    ASSERT_NE(hint.figures[idx], nullptr);
    // kTrill5 uses Semitone mode (ornamental), others use Degree mode.
    if (hint.figures[idx]->primary_mode == IntervalMode::Degree) {
      EXPECT_TRUE(hint.figures[idx]->allow_transposition)
          << "Figure " << idx << " (" << hint.figures[idx]->name
          << ") should be transposable";
    }
  }
}

TEST(GestureVocabularyTest, AllFiguresHaveDegreeIntervals) {
  for (auto arch : {ToccataArchetype::Dramaticus, ToccataArchetype::Perpetuus,
                    ToccataArchetype::Concertato, ToccataArchetype::Sectionalis}) {
    auto hint = getArchetypeFigures(arch);
    for (int idx = 0; idx < hint.count; ++idx) {
      ASSERT_NE(hint.figures[idx], nullptr);
      // Semitone-mode figures (e.g., kTrill5) don't have degree_intervals.
      if (hint.figures[idx]->primary_mode == IntervalMode::Degree) {
        EXPECT_NE(hint.figures[idx]->degree_intervals, nullptr)
            << "Figure " << hint.figures[idx]->name
            << " for archetype " << static_cast<int>(arch)
            << " must have degree_intervals for scale-relative transposition";
      }
    }
  }
}

TEST(GestureVocabularyTest, FigureNoteCountsAreConsistent) {
  for (auto arch : {ToccataArchetype::Dramaticus, ToccataArchetype::Perpetuus,
                    ToccataArchetype::Concertato, ToccataArchetype::Sectionalis}) {
    auto hint = getArchetypeFigures(arch);
    for (int idx = 0; idx < hint.count; ++idx) {
      ASSERT_NE(hint.figures[idx], nullptr);
      // Vocabulary figures are 4-note or 5-note.
      EXPECT_TRUE(hint.figures[idx]->note_count == 4 ||
                  hint.figures[idx]->note_count == 5)
          << "Figure " << hint.figures[idx]->name
          << " has unexpected note_count " << hint.figures[idx]->note_count;
    }
  }
}

}  // namespace
}  // namespace bach
