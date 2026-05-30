#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "composer/provenance.h"
#include "composer/voice_intent.h"

namespace bach::composer {
namespace {

// Every VoiceIntent enumerator value 0..NctCarrier(19) must have a table
// entry whose name is not the "Unknown" fallback. Guards against a future
// intent silently missing the descriptor table.
TEST(IntentDescriptorTest, EveryEnumValueHasAnEntry) {
  for (std::uint8_t value = 0; value <= static_cast<std::uint8_t>(VoiceIntent::NctCarrier);
       ++value) {
    const auto intent = static_cast<VoiceIntent>(value);
    const IntentDescriptor& desc = describeIntent(intent);
    EXPECT_NE(desc.name, nullptr);
    EXPECT_STRNE(desc.name, "Unknown") << "value=" << static_cast<int>(value);
    EXPECT_GT(std::strlen(desc.name), 0u);
  }
}

// is_carrier must match the carrier set that was previously hardcoded in
// composer.cpp's isCarrierIntent `||` chain. Listed explicitly so a change
// to the table's classification is caught here.
TEST(IntentDescriptorTest, CarrierClassificationMatchesLegacySet) {
  const VoiceIntent carriers[] = {
      VoiceIntent::SubjectCarrier,
      VoiceIntent::AnswerCarrier,
      VoiceIntent::SuspensionCarrier,
      VoiceIntent::Episode,
      VoiceIntent::CountersubjectCarrier,
      VoiceIntent::FortspinnungSpan,
      VoiceIntent::MiddleEntryCarrier,
      VoiceIntent::StrettoCarrier,
      VoiceIntent::PedalCarrier,
      VoiceIntent::CodaCarrier,
      VoiceIntent::SubjectCarrierAugmented,
      VoiceIntent::SubjectCarrierDiminished,
      VoiceIntent::SubjectCarrierInverted,
      VoiceIntent::RhythmCarrier,
      VoiceIntent::NctCarrier,
  };
  const VoiceIntent non_carriers[] = {
      VoiceIntent::RepeatedReplyCell, VoiceIntent::SequentialCounterline,
      VoiceIntent::HarmonicSupport,   VoiceIntent::CadentialClosure,
      VoiceIntent::FillerGap,
  };
  for (auto intent : carriers) {
    EXPECT_TRUE(describeIntent(intent).is_carrier) << "name=" << describeIntent(intent).name;
    EXPECT_TRUE(isCarrierIntent(intent));
  }
  for (auto intent : non_carriers) {
    EXPECT_FALSE(describeIntent(intent).is_carrier) << "name=" << describeIntent(intent).name;
    EXPECT_FALSE(isCarrierIntent(intent));
  }
}

// voiceIntentToString must return the same strings as the old hand-written
// switch (spot checks across carrier / non-carrier / variant intents).
TEST(IntentDescriptorTest, NameSpotChecksMatchLegacyStrings) {
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::SubjectCarrier), "SubjectCarrier");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::SequentialCounterline), "SequentialCounterline");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::AnswerCarrier), "AnswerCarrier");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::Episode), "Episode");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::FortspinnungSpan), "FortspinnungSpan");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::SubjectCarrierInverted), "SubjectCarrierInverted");
  EXPECT_STREQ(voiceIntentToString(VoiceIntent::NctCarrier), "NctCarrier");
}

// Primary provenance bits for the carriers that stamp a single identity bit.
TEST(IntentDescriptorTest, PrimaryProvenanceBitsMatch) {
  EXPECT_TRUE(describeIntent(VoiceIntent::Episode).has_provenance_bit);
  EXPECT_EQ(describeIntent(VoiceIntent::Episode).provenance_bit, EpisodeMotifSourced);
  EXPECT_TRUE(describeIntent(VoiceIntent::CountersubjectCarrier).has_provenance_bit);
  EXPECT_EQ(describeIntent(VoiceIntent::CountersubjectCarrier).provenance_bit,
            CountersubjectActive);
  EXPECT_TRUE(describeIntent(VoiceIntent::FortspinnungSpan).has_provenance_bit);
  EXPECT_EQ(describeIntent(VoiceIntent::FortspinnungSpan).provenance_bit, FortspinnungSourced);
  EXPECT_TRUE(describeIntent(VoiceIntent::MiddleEntryCarrier).has_provenance_bit);
  EXPECT_EQ(describeIntent(VoiceIntent::MiddleEntryCarrier).provenance_bit, MiddleEntryCommitted);
  EXPECT_EQ(describeIntent(VoiceIntent::SubjectCarrierAugmented).provenance_bit,
            SubjectVariantApplied);

  // Intents that stamp no single primary bit.
  EXPECT_FALSE(describeIntent(VoiceIntent::SubjectCarrier).has_provenance_bit);
  EXPECT_FALSE(describeIntent(VoiceIntent::SuspensionCarrier).has_provenance_bit);
  EXPECT_FALSE(describeIntent(VoiceIntent::RhythmCarrier).has_provenance_bit);
  EXPECT_FALSE(describeIntent(VoiceIntent::FillerGap).has_provenance_bit);
}

// ReplayKind classification (consumed by the CandidateSearch dispatch).
TEST(IntentDescriptorTest, ReplayKindClassification) {
  EXPECT_EQ(describeIntent(VoiceIntent::SequentialCounterline).replay, ReplayKind::kCompose);
  EXPECT_EQ(describeIntent(VoiceIntent::SubjectCarrier).replay, ReplayKind::kVerbatimVector);
  EXPECT_EQ(describeIntent(VoiceIntent::Episode).replay, ReplayKind::kTransform);
  EXPECT_EQ(describeIntent(VoiceIntent::FortspinnungSpan).replay, ReplayKind::kSequence);
  EXPECT_EQ(describeIntent(VoiceIntent::SuspensionCarrier).replay, ReplayKind::kTriple);
}

}  // namespace
}  // namespace bach::composer
