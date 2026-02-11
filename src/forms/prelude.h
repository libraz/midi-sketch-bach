// Prelude generator for organ preludes paired with fugues.

#ifndef BACH_FORMS_PRELUDE_H
#define BACH_FORMS_PRELUDE_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Type of prelude style.
enum class PreludeType : uint8_t {
  FreeForm,   ///< Free-form with passage work (BWV 532 style).
  Perpetual   ///< Perpetual motion, continuous 16th notes (BWV 543 style).
};

/// @brief Configuration for prelude generation.
struct PreludeConfig {
  KeySignature key = {Key::C, false};
  PreludeType type = PreludeType::FreeForm;
  uint8_t num_voices = 3;
  uint16_t bpm = 100;
  uint32_t seed = 42;
  Tick fugue_length_ticks = 0;  ///< Used to scale prelude length (60-80% of fugue).
  bool enable_picardy = true;   ///< Apply Picardy third in minor keys.
};;

/// @brief Result of prelude generation.
struct PreludeResult {
  std::vector<Track> tracks;        ///< MIDI tracks for each voice/manual.
  HarmonicTimeline timeline;        ///< Harmonic analysis of the prelude.
  Tick total_duration_ticks = 0;    ///< Total length in ticks.
  bool success = false;
};

/// @brief Generate an organ prelude.
///
/// Generates a prelude using the HarmonicTimeline as harmonic backbone.
/// FreeForm preludes use varied passage work patterns (scales and arpeggios).
/// Perpetual preludes use continuous 16th note figurations.
///
/// Length is 60-80% of fugue_length_ticks if provided, otherwise
/// defaults to 12 bars.
///
/// @param config Prelude configuration.
/// @return PreludeResult with generated tracks.
PreludeResult generatePrelude(const PreludeConfig& config);

/// @brief Calculate prelude length based on fugue length.
/// @param fugue_length_ticks Duration of the fugue in ticks.
/// @return Target prelude duration (70% of fugue, or 12 bars if fugue is 0).
Tick calculatePreludeLength(Tick fugue_length_ticks);

}  // namespace bach

#endif  // BACH_FORMS_PRELUDE_H
