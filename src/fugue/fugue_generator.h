// Fugue generator: orchestrates full 3-voice fugue creation.
//
// The generator follows a strict pipeline: subject generation with retry,
// answer derivation, countersubject creation, exposition building, tonal
// plan generation, episode/middle entry development, and stretto resolution.
// FuguePhase ordering (Establish -> Develop -> Resolve) is enforced.

#ifndef BACH_FUGUE_FUGUE_GENERATOR_H
#define BACH_FUGUE_FUGUE_GENERATOR_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_config.h"
#include "fugue/fugue_structure.h"

namespace bach {

/// @brief Result of fugue generation.
struct FugueResult {
  bool success = false;               ///< True if generation succeeded.
  std::vector<Track> tracks;          ///< MIDI tracks for output.
  FugueStructure structure;           ///< Formal structure with sections.
  std::string error_message;          ///< Error description if failed.
  int attempts = 0;                   ///< Number of subject generation attempts.
};

/// @brief Generate a complete fugue from the given configuration.
///
/// Pipeline:
///   1. Generate subject (with retry on validation failure)
///   2. Generate answer (Real/Tonal auto-detection)
///   3. Generate countersubject
///   4. Build exposition (FuguePhase::Establish)
///   5. Generate tonal plan
///   6. Generate episodes (FuguePhase::Develop)
///   7. Generate middle entries (FuguePhase::Develop)
///   8. Generate stretto (FuguePhase::Resolve)
///   9. Assign notes to MIDI tracks (organ channel mapping)
///
/// Voice-to-channel mapping follows the organ system:
///   Voice 0 -> Ch 0 (Manual I / Great, Church Organ)
///   Voice 1 -> Ch 1 (Manual II / Swell, Reed Organ)
///   Voice 2 -> Ch 2 (Manual III / Positiv, Church Organ)
///   Voice 3 -> Ch 3 (Pedal, Church Organ)
///
/// @param config Fugue configuration (key, character, voices, seed, etc.).
/// @return FugueResult with tracks, structure, and success status.
FugueResult generateFugue(const FugueConfig& config);

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_GENERATOR_H
