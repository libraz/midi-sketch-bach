// Implementation of fugue configuration utilities.

#include "fugue/fugue_config.h"

namespace bach {

const char* answerTypeToString(AnswerType type) {
  switch (type) {
    case AnswerType::Auto: return "Auto";
    case AnswerType::Real: return "Real";
    case AnswerType::Tonal: return "Tonal";
  }
  return "Unknown";
}

bool isCharacterAvailable(SubjectCharacter character, int phase) {
  // Phase is 1-based: phase 1 = Establish, phase 2 = Develop, etc.
  switch (character) {
    case SubjectCharacter::Severe:
    case SubjectCharacter::Playful:
      // Available in phases 1-2.
      return phase >= 1 && phase <= 2;

    case SubjectCharacter::Noble:
      // Available from phase 3 onward.
      return phase >= 3;

    case SubjectCharacter::Restless:
      // Available from phase 4 onward.
      return phase >= 4;
  }
  return false;
}

}  // namespace bach
