// Trio sonata generator for BWV 525-530 style organ trio sonatas.

#ifndef BACH_FORMS_TRIO_SONATA_H
#define BACH_FORMS_TRIO_SONATA_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for trio sonata generation.
struct TrioSonataConfig {
  KeySignature key = {Key::C, false};
  uint16_t bpm_fast = 120;  ///< BPM for outer movements (1st, 3rd).
  uint16_t bpm_slow = 60;   ///< BPM for middle movement (2nd).
  uint32_t seed = 42;
};

/// @brief A single movement of the trio sonata.
struct TrioSonataMovement {
  std::vector<Track> tracks;        ///< 3 tracks: RH (Great), LH (Swell), Pedal.
  Tick total_duration_ticks = 0;
  uint16_t bpm = 120;
  KeySignature key = {Key::C, false};
};

/// @brief Result of trio sonata generation.
struct TrioSonataResult {
  std::vector<TrioSonataMovement> movements;  ///< 3 movements: fast-slow-fast.
  bool success = false;
};

/// @brief Generate a BWV 525-530 style trio sonata.
///
/// Produces a 3-movement work for organ:
///   - Movement 1: Allegro (fast), 3-voice counterpoint
///   - Movement 2: Adagio (slow), lyrical, counterpoint relaxed
///   - Movement 3: Vivace (fast), energetic 3-voice counterpoint
///
/// All 3 voices are equal and independent (VoiceRole::Assert for all).
/// Manual assignment: Great (RH) + Swell (LH) + Pedal.
///
/// @param config Trio sonata configuration.
/// @return TrioSonataResult with 3 movements.
TrioSonataResult generateTrioSonata(const TrioSonataConfig& config);

}  // namespace bach

#endif  // BACH_FORMS_TRIO_SONATA_H
