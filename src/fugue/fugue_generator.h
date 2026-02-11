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
#include "harmony/harmonic_timeline.h"

namespace bach {

/// @brief Quality metrics for generated fugue output.
struct FugueQualityMetrics {
  float dissonance_per_beat = 0.0f;     ///< Average dissonances per beat.
  float chord_tone_ratio = 0.0f;        ///< Fraction of notes that are chord tones.
  int voice_crossings = 0;              ///< Number of voice crossing incidents.
  int parallel_perfects = 0;            ///< Number of parallel 5ths/8ths.
  int structural_parallel_count = 0;    ///< Parallel perfects involving structural notes.
  float counterpoint_compliance = 0.0f; ///< Fraction of notes passing CP rules.
  int notes_rejected = 0;               ///< Notes removed by post-validation.
  int notes_adjusted = 0;               ///< Notes pitch-adjusted by validation.
};

/// @brief Result of fugue generation.
struct FugueResult {
  bool success = false;               ///< True if generation succeeded.
  std::vector<Track> tracks;          ///< MIDI tracks for output.
  FugueStructure structure;           ///< Formal structure with sections.
  std::string error_message;          ///< Error description if failed.
  int attempts = 0;                   ///< Number of subject generation attempts.
  HarmonicTimeline timeline;          ///< Harmonic context from tonal plan.
  HarmonicTimeline generation_timeline;  ///< Beat-resolution timeline used during generation.
  FugueQualityMetrics quality;        ///< Quality metrics from post-validation.
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
