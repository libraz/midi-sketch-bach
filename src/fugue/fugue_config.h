// Fugue configuration: subject source, answer type, character phase
// restrictions, and generation parameters.

#ifndef BACH_FUGUE_FUGUE_CONFIG_H
#define BACH_FUGUE_FUGUE_CONFIG_H

#include <cstdint>
#include <random>

#include "core/basic_types.h"
#include "harmony/modulation_plan.h"

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

  /// @brief Select a duration using weighted probabilities based on energy and context.
  ///
  /// Weights musical duration choices by beat position, energy level, and
  /// rhythmic complementarity with an adjacent voice. Returns one of the
  /// standard Baroque durations:
  ///   - Whole note (1920), half note (960), dotted quarter (720),
  ///     quarter note (480), eighth note (240), sixteenth note (120).
  ///
  /// @param energy Energy level from getLevel().
  /// @param tick Current tick position.
  /// @param rng Random number generator.
  /// @param other_voice_duration Duration of the most recent note in an adjacent voice
  ///        (0 = unknown). Used for rhythmic complementarity.
  /// @return Selected duration in ticks.
  static Tick selectDuration(float energy, Tick tick, std::mt19937& rng,
                             Tick other_voice_duration = 0) {
    // Standard Baroque durations with base weights.
    struct DurWeight {
      Tick duration;
      float weight;
    };
    DurWeight candidates[] = {
        {kTicksPerBar,          0.5f},   // Whole note
        {kTicksPerBeat * 2,     1.5f},   // Half note
        {kTicksPerBeat * 3 / 2, 1.2f},   // Dotted quarter
        {kTicksPerBeat,         3.0f},   // Quarter note
        {kTicksPerBeat / 2,     2.0f},   // Eighth note
        {kTicksPerBeat / 4,     0.8f},   // Sixteenth note
    };
    constexpr int kNumCandidates = 6;

    // Energy floor: suppress durations shorter than the minimum.
    Tick min_dur = minDuration(energy);
    for (int idx = 0; idx < kNumCandidates; ++idx) {
      if (candidates[idx].duration < min_dur) {
        candidates[idx].weight = 0.0f;
      }
    }

    // Beat position bonuses.
    bool is_bar_start = (tick % kTicksPerBar == 0);
    bool is_beat_start = (tick % kTicksPerBeat == 0);
    for (int idx = 0; idx < kNumCandidates; ++idx) {
      if (is_bar_start && candidates[idx].duration >= kTicksPerBeat * 2) {
        candidates[idx].weight *= 2.0f;  // Long notes on bar starts.
      } else if (is_beat_start && candidates[idx].duration >= kTicksPerBeat) {
        candidates[idx].weight *= 1.5f;  // Quarter+ on beat starts.
      }
    }

    // Rhythmic complementarity: when adjacent voice has short notes, prefer long,
    // and vice versa.
    if (other_voice_duration > 0) {
      bool other_is_short = (other_voice_duration <= kTicksPerBeat / 2);
      bool other_is_long = (other_voice_duration >= kTicksPerBeat * 2);
      for (int idx = 0; idx < kNumCandidates; ++idx) {
        if (other_is_short && candidates[idx].duration >= kTicksPerBeat) {
          candidates[idx].weight *= 2.0f;
        } else if (other_is_long && candidates[idx].duration <= kTicksPerBeat / 2) {
          candidates[idx].weight *= 2.0f;
        }
      }
    }

    // Compute total weight and select.
    float total = 0.0f;
    for (int idx = 0; idx < kNumCandidates; ++idx) {
      total += candidates[idx].weight;
    }
    if (total <= 0.0f) return kTicksPerBeat;  // Fallback: quarter note.

    std::uniform_real_distribution<float> dist(0.0f, total);
    float roll = dist(rng);
    float cumulative = 0.0f;
    for (int idx = 0; idx < kNumCandidates; ++idx) {
      cumulative += candidates[idx].weight;
      if (roll <= cumulative) {
        return candidates[idx].duration;
      }
    }
    return kTicksPerBeat;  // Fallback.
  }
};

/// Configuration for fugue generation.
struct FugueConfig {
  SubjectSource subject_source = SubjectSource::Generate;
  SubjectCharacter character = SubjectCharacter::Severe;
  AnswerType answer_type = AnswerType::Auto;
  uint8_t num_voices = 3;
  Key key = Key::C;
  bool is_minor = false;          ///< Whether the home key is minor.
  uint16_t bpm = 72;
  uint32_t seed = 0;
  uint8_t subject_bars = 2;       // Length in bars (2-4)
  int max_subject_retries = 10;   // Maximum generation attempts
  int develop_pairs = 2;          ///< Number of Episode+MiddleEntry pairs in Develop phase.
  int episode_bars = 2;           ///< Duration of each episode in bars.
  ModulationPlan modulation_plan;       ///< Key plan for episode modulations.
  bool has_modulation_plan = false;     ///< Whether modulation_plan was explicitly set.
  bool enable_picardy = true;           ///< Apply Picardy third in minor keys.
};;

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_CONFIG_H
