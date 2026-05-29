#include "composer/voice_intent.h"

#include <gtest/gtest.h>

#include <cstring>

namespace bach::composer {

TEST(VoiceIntentTest, AllValuesStringifyDistinctly) {
  const VoiceIntent all[] = {
      VoiceIntent::SubjectCarrier,        VoiceIntent::RepeatedReplyCell,
      VoiceIntent::SequentialCounterline, VoiceIntent::HarmonicSupport,
      VoiceIntent::CadentialClosure,      VoiceIntent::FillerGap,
  };
  for (auto a : all) {
    const char* sa = voiceIntentToString(a);
    EXPECT_NE(sa, nullptr);
    EXPECT_GT(std::strlen(sa), 0u);
    EXPECT_STRNE(sa, "Unknown");
    for (auto b : all) {
      if (a == b)
        continue;
      EXPECT_STRNE(sa, voiceIntentToString(b));
    }
  }
}

TEST(VoiceIntentTest, EnumValuesStableForLegacyMirroring) {
  // Mirror order with legacy bach::VoiceIntent. Reordering would silently
  // change provenance.json serialization. Keep this test failing loudly if
  // someone shuffles values.
  EXPECT_EQ(static_cast<int>(VoiceIntent::SubjectCarrier), 0);
  EXPECT_EQ(static_cast<int>(VoiceIntent::RepeatedReplyCell), 1);
  EXPECT_EQ(static_cast<int>(VoiceIntent::SequentialCounterline), 2);
  EXPECT_EQ(static_cast<int>(VoiceIntent::HarmonicSupport), 3);
  EXPECT_EQ(static_cast<int>(VoiceIntent::CadentialClosure), 4);
  EXPECT_EQ(static_cast<int>(VoiceIntent::FillerGap), 5);
}

}  // namespace bach::composer
