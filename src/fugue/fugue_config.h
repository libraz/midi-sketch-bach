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

/// @brief Energy curve for fugue dynamics (Principle 4: design values).
///
/// Maps normalized position [0,1] within the fugue to an energy level [0,1].
/// These are fixed design values (not generated) per Principle 4.
struct FugueEnergyCurve {
  /// @brief Get energy level at a given position in the fugue.
  /// @param tick Current tick position.
  /// @param total_duration Total fugue duration in ticks.
  /// @return Energy level in [0.0, 1.0].
  static float getLevel(Tick tick, Tick total_duration) {
    if (total_duration == 0) return 0.5f;
    float pos = static_cast<float>(tick) / static_cast<float>(total_duration);
    if (pos < 0.0f) pos = 0.0f;
    if (pos > 1.0f) pos = 1.0f;

    // Establish (0-25%): steady 0.5
    if (pos < 0.25f) return 0.5f;
    // Develop (25-70%): 0.5 -> 0.7 with linear ramp
    if (pos < 0.70f) {
      float develop_pos = (pos - 0.25f) / 0.45f;  // 0..1 within Develop
      return 0.5f + develop_pos * 0.2f;
    }
    // Stretto (70-90%): 0.8 -> 1.0
    if (pos < 0.90f) {
      float stretto_pos = (pos - 0.70f) / 0.20f;
      return 0.8f + stretto_pos * 0.2f;
    }
    // Coda (90-100%): 0.9
    return 0.9f;
  }

  /// @brief Get minimum note duration based on energy (rhythm density control).
  /// @param energy Energy level from getLevel().
  /// @return Minimum duration in ticks.
  static Tick minDuration(float energy) {
    if (energy < 0.4f) return kTicksPerBeat;      // quarter note
    if (energy < 0.7f) return kTicksPerBeat / 2;  // eighth note
    return kTicksPerBeat / 4;                      // sixteenth note
  }
};

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
  int develop_pairs = 1;          ///< Number of Episode+MiddleEntry pairs in Develop phase.
  int episode_bars = 2;           ///< Duration of each episode in bars.
};

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_CONFIG_H
