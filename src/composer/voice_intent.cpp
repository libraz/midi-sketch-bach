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
  }
  return "Unknown";
}

}  // namespace bach::composer
