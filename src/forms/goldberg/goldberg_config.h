// Configuration and result types for Goldberg Variations generation.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_CONFIG_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_CONFIG_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for Goldberg Variations generation.
struct GoldbergConfig {
  KeySignature key = {Key::G, false};       ///< Key (default: G major).
  uint16_t bpm = 60;                        ///< Base tempo (Aria tempo).
  uint32_t seed = 0;                        ///< Random seed (0 = auto).
  InstrumentType instrument = InstrumentType::Harpsichord;
  DurationScale scale = DurationScale::Short;
  bool apply_repeats = true;                ///< Apply binary form repeats.
  bool ornament_variation_on_repeat = false; ///< Vary ornaments on repeats.
};

/// @brief Result from Goldberg Variations generation.
struct GoldbergResult {
  std::vector<Track> tracks;
  std::vector<TempoEvent> tempo_events;
  std::vector<TimeSignatureEvent> time_sig_events;
  HarmonicTimeline timeline;
  Tick total_duration_ticks = 0;
  bool success = false;
  std::string error_message;
  uint32_t seed_used = 0;
};

/// @brief Generate Goldberg Variations (stub: returns success=false).
/// @param config Generation configuration.
/// @return GoldbergResult with tracks and metadata.
GoldbergResult generateGoldbergVariations(const GoldbergConfig& config);

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_CONFIG_H
