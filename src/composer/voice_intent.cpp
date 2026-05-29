#include "composer/voice_intent.h"

namespace bach::composer {

const char* voiceIntentToString(VoiceIntent intent) {
  switch (intent) {
    case VoiceIntent::SubjectCarrier:
      return "SubjectCarrier";
    case VoiceIntent::RepeatedReplyCell:
      return "RepeatedReplyCell";
    case VoiceIntent::SequentialCounterline:
      return "SequentialCounterline";
    case VoiceIntent::HarmonicSupport:
      return "HarmonicSupport";
    case VoiceIntent::CadentialClosure:
      return "CadentialClosure";
    case VoiceIntent::FillerGap:
      return "FillerGap";
    case VoiceIntent::AnswerCarrier:
      return "AnswerCarrier";
    case VoiceIntent::SuspensionCarrier:
      return "SuspensionCarrier";
    case VoiceIntent::Episode:
      return "Episode";
    case VoiceIntent::CountersubjectCarrier:
      return "CountersubjectCarrier";
    case VoiceIntent::FortspinnungSpan:
      return "FortspinnungSpan";
    case VoiceIntent::MiddleEntryCarrier:
      return "MiddleEntryCarrier";
    case VoiceIntent::StrettoCarrier:
      return "StrettoCarrier";
    case VoiceIntent::PedalCarrier:
      return "PedalCarrier";
    case VoiceIntent::CodaCarrier:
      return "CodaCarrier";
    case VoiceIntent::SubjectCarrierAugmented:
      return "SubjectCarrierAugmented";
    case VoiceIntent::SubjectCarrierDiminished:
      return "SubjectCarrierDiminished";
    case VoiceIntent::SubjectCarrierInverted:
      return "SubjectCarrierInverted";
    case VoiceIntent::RhythmCarrier:
      return "RhythmCarrier";
  }
  return "Unknown";
}

}  // namespace bach::composer
