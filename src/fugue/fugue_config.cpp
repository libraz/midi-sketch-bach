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
  // All four characters are now available at any valid phase (>= 1).
  // The original phase restrictions (Severe/Playful: 1-2, Noble: 3+,
  // Restless: 4+) were implementation-staging gates that have been lifted
  // now that all character types are fully implemented.
  if (phase < 1) return false;

  switch (character) {
    case SubjectCharacter::Severe:
    case SubjectCharacter::Playful:
    case SubjectCharacter::Noble:
    case SubjectCharacter::Restless:
      return true;
  }
  return false;
}

bool isCharacterFormCompatible(SubjectCharacter character, FormType form) {
  // Chorale preludes are sacred, contemplative works -- Playful and Restless
  // characters conflict with the genre's devotional nature.
  if (form == FormType::ChoralePrelude) {
    if (character == SubjectCharacter::Playful ||
        character == SubjectCharacter::Restless) {
      return false;
    }
  }

  // Toccata and fugue demands virtuosic energy -- Noble character's stately
  // pacing is incompatible with the genre's dramatic flair.
  if (form == FormType::ToccataAndFugue) {
    if (character == SubjectCharacter::Noble) {
      return false;
    }
  }

  return true;
}

}  // namespace bach
