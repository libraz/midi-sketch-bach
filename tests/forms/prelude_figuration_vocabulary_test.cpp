// Tests for prelude figuration vocabulary slot pattern integration.

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "forms/prelude_figuration.h"
#include "solo_string/solo_vocabulary.h"

namespace bach {
namespace {

TEST(PreludeFigurationVocabularyTest, SlotPatternTypeExists) {
  // Verify the enum value exists.
  FigurationType typ = FigurationType::SlotPattern;
  EXPECT_EQ(static_cast<uint8_t>(typ), 3u);
}

TEST(PreludeFigurationVocabularyTest, CreateFromSlotPattern3Voice) {
  auto tmpl = createFigurationTemplateFromSlot(kFig3vRising, 3);
  EXPECT_EQ(tmpl.type, FigurationType::SlotPattern);
  EXPECT_EQ(tmpl.steps.size(), 3u);
  // Rising: bass(0) -> alto(1) -> soprano(2).
  EXPECT_EQ(tmpl.steps[0].voice_index, 0u);
  EXPECT_EQ(tmpl.steps[1].voice_index, 1u);
  EXPECT_EQ(tmpl.steps[2].voice_index, 2u);
}

TEST(PreludeFigurationVocabularyTest, CreateFromSlotPattern4Voice) {
  auto tmpl = createFigurationTemplateFromSlot(kFig4vRising, 4);
  EXPECT_EQ(tmpl.type, FigurationType::SlotPattern);
  EXPECT_EQ(tmpl.steps.size(), 4u);
}

TEST(PreludeFigurationVocabularyTest, VoiceIndexClamping) {
  // 4-voice pattern with only 2 voices available.
  auto tmpl = createFigurationTemplateFromSlot(kFig4vRising, 2);
  for (const auto& step : tmpl.steps) {
    EXPECT_LT(step.voice_index, 2u);
  }
}

TEST(PreludeFigurationVocabularyTest, StepTimingCorrect) {
  auto tmpl = createFigurationTemplateFromSlot(kFig3vFalling, 3);
  ASSERT_EQ(tmpl.steps.size(), 3u);
  constexpr Tick kStepDur = kTicksPerBeat / 4;  // 120
  EXPECT_EQ(tmpl.steps[0].relative_tick, 0u);
  EXPECT_EQ(tmpl.steps[1].relative_tick, kStepDur);
  EXPECT_EQ(tmpl.steps[2].relative_tick, kStepDur * 2);
  for (const auto& step : tmpl.steps) {
    EXPECT_EQ(step.duration, kStepDur);
    EXPECT_EQ(step.scale_offset, 0);  // Always chord tones.
  }
}

TEST(PreludeFigurationVocabularyTest, FallingPatternVoiceOrder) {
  // Falling: soprano(2) -> alto(1) -> bass(0).
  auto tmpl = createFigurationTemplateFromSlot(kFig3vFalling, 3);
  ASSERT_EQ(tmpl.steps.size(), 3u);
  EXPECT_EQ(tmpl.steps[0].voice_index, 2u);
  EXPECT_EQ(tmpl.steps[1].voice_index, 1u);
  EXPECT_EQ(tmpl.steps[2].voice_index, 0u);
}

TEST(PreludeFigurationVocabularyTest, CreateViaMainFactory) {
  // Creating via the main factory with SlotPattern should use default pattern.
  auto tmpl = createFigurationTemplate(FigurationType::SlotPattern, 3);
  EXPECT_EQ(tmpl.type, FigurationType::SlotPattern);
  EXPECT_FALSE(tmpl.steps.empty());
}

TEST(PreludeFigurationVocabularyTest, TwoVoiceOscillation) {
  // 2v oscillation up: soprano(1) -> bass(0) -> soprano(1).
  auto tmpl = createFigurationTemplateFromSlot(kFig2vOscUp, 2);
  ASSERT_EQ(tmpl.steps.size(), 3u);
  EXPECT_EQ(tmpl.steps[0].voice_index, 1u);
  EXPECT_EQ(tmpl.steps[1].voice_index, 0u);
  EXPECT_EQ(tmpl.steps[2].voice_index, 1u);
}

}  // namespace
}  // namespace bach
