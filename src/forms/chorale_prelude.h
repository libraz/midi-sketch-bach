// Chorale prelude generator for BWV 599-650 style organ chorale preludes.

#ifndef BACH_FORMS_CHORALE_PRELUDE_H
#define BACH_FORMS_CHORALE_PRELUDE_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for chorale prelude generation.
struct ChoralePreludeConfig {
  KeySignature key = {Key::C, false};
  uint16_t bpm = 60;
  uint32_t seed = 42;
  bool enable_picardy = true;   ///< Apply Picardy third in minor keys.
};;

/// @brief Result of chorale prelude generation.
struct ChoralePreludeResult {
  std::vector<Track> tracks;  ///< 3 tracks: cantus (Swell), counterpoint (Great), pedal.
  Tick total_duration_ticks = 0;
  bool success = false;
  HarmonicTimeline timeline;  ///< Harmonic context used during generation.
};

/// @brief Generate a BWV 599-650 style chorale prelude.
///
/// Produces a 3-voice organ chorale prelude with:
///   - Cantus firmus on Manual II (Swell, ch 1) in long note values (whole/breve).
///   - Ornamental counterpoint on Manual I (Great, ch 0) in 8th/16th figurations.
///   - Pedal bass (Pedal, ch 3) in quarter/half notes on root and fifth.
///
/// The cantus firmus is treated as immutable (BachNoteSource::CantusFixed).
/// Three built-in chorale melodies are available, selected by seed.
/// Organ velocity is fixed at 80.
///
/// @param config Chorale prelude configuration.
/// @return ChoralePreludeResult with 3 tracks populated.
ChoralePreludeResult generateChoralePrelude(const ChoralePreludeConfig& config);

}  // namespace bach

#endif  // BACH_FORMS_CHORALE_PRELUDE_H
