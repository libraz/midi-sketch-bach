// Fantasia free section generator for organ fantasia (BWV 537/542 style).

#ifndef BACH_FORMS_FANTASIA_H
#define BACH_FORMS_FANTASIA_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for fantasia free section (BWV 537/542 style).
struct FantasiaConfig {
  KeySignature key = {Key::G, true};  ///< G minor default (BWV 542).
  uint16_t bpm = 66;
  uint32_t seed = 42;
  uint8_t num_voices = 4;
  int section_bars = 32;              ///< Length of free section in bars.
};

/// @brief Result of fantasia free section generation.
struct FantasiaResult {
  std::vector<Track> tracks;
  HarmonicTimeline timeline;
  Tick total_duration_ticks = 0;
  bool success = false;
  std::string error_message;
};

/// @brief Generate a fantasia free section (BWV 537/542 style).
///
/// Structure: Contemplative, sustained chords with ornamental melody.
/// More meditative than toccata. Focus on Swell manual (Manual II).
/// Features sustained harmonies, ornamental upper voice, slow bass.
///
/// Voice assignment:
///   - Voice 0 (Great/Manual I): Ornamental melody in quarter/eighth notes
///   - Voice 1 (Swell/Manual II): Sustained chords in half/whole notes
///   - Voice 2 (Positiv/Manual III): Light countermelody in eighth notes
///   - Pedal (last voice): Very long notes (whole notes), bass foundation
///
/// @param config Fantasia configuration.
/// @return FantasiaResult with generated tracks.
FantasiaResult generateFantasia(const FantasiaConfig& config);

}  // namespace bach

#endif  // BACH_FORMS_FANTASIA_H
