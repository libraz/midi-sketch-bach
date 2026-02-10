// Fugue configuration: subject source, answer type, character phase
// restrictions, and generation parameters.

#ifndef BACH_FUGUE_FUGUE_CONFIG_H
#define BACH_FUGUE_FUGUE_CONFIG_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

/// How the fugue subject is obtained.
enum class SubjectSource : uint8_t {
  Generate,  // Algorithmically generated
  Import     // Loaded from external source
};

/// Answer type for the fugue comes entry.
enum class AnswerType : uint8_t {
  Auto,   // Automatically detect based on subject analysis
  Real,   // Exact transposition up a perfect 5th
  Tonal   // Tonal adjustment of tonic-dominant relationships
};

/// @brief Convert AnswerType to human-readable string.
/// @param type The answer type enum value.
/// @return Null-terminated string representation.
const char* answerTypeToString(AnswerType type);

/// @brief Check whether a SubjectCharacter is available at a given phase.
///
/// Phase restrictions (from design):
///   - Severe, Playful: phases 1-2 (index 0-1)
///   - Noble: phase 3+ (index 2+)
///   - Restless: phase 4+ (index 3+)
///
/// @param character The subject character to check.
/// @param phase Phase number (1-based: 1 = first phase).
/// @return True if the character is permitted at the given phase.
bool isCharacterAvailable(SubjectCharacter character, int phase);

/// @brief Check whether a SubjectCharacter is compatible with a FormType.
///
/// Forbidden combinations (return false):
///   - Playful x ChoralePrelude
///   - Restless x ChoralePrelude
///   - Noble x ToccataAndFugue
///
/// All other combinations are allowed. Solo String forms (CelloPrelude,
/// Chaconne) always return true because they do not use SubjectCharacter.
///
/// @param character The subject character to check.
/// @param form The form type to check against.
/// @return True if the combination is allowed, false if forbidden.
bool isCharacterFormCompatible(SubjectCharacter character, FormType form);

/// Configuration for fugue generation.
struct FugueConfig {
  SubjectSource subject_source = SubjectSource::Generate;
  SubjectCharacter character = SubjectCharacter::Severe;
  AnswerType answer_type = AnswerType::Auto;
  uint8_t num_voices = 3;
  Key key = Key::C;
  uint16_t bpm = 72;
  uint32_t seed = 0;
  uint8_t subject_bars = 2;       // Length in bars (2-4)
  int max_subject_retries = 10;   // Maximum generation attempts
};

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_CONFIG_H
