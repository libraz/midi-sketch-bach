// Passacaglia generator for organ passacaglia (BWV 582 style).

#ifndef BACH_FORMS_PASSACAGLIA_H
#define BACH_FORMS_PASSACAGLIA_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Configuration for passacaglia generation (BWV 582 style).
struct PassacagliaConfig {
  KeySignature key = {Key::C, true};  ///< C minor default (BWV 582).
  uint16_t bpm = 60;
  uint32_t seed = 42;
  uint8_t num_voices = 4;        ///< 3-5 voices.
  int num_variations = 12;       ///< Number of ground bass variations.
  int ground_bass_bars = 8;      ///< Length of ground bass theme in bars.
  bool append_fugue = true;      ///< Whether to append a fugue section.
  bool enable_picardy = true;    ///< Apply Picardy third in minor keys.
};;

/// @brief Result of passacaglia generation.
struct PassacagliaResult {
  std::vector<Track> tracks;
  HarmonicTimeline timeline;
  Tick total_duration_ticks = 0;
  bool success = false;
  std::string error_message;
  uint32_t counterpoint_violations = 0;  ///< Warning count from inter-voice check.
};

/// @brief Generate a passacaglia (BWV 582 style).
///
/// Structure: Ground bass theme (8 bars) repeated with increasing variation
/// in upper voices. Optionally followed by a fugue using the ground bass
/// as subject.
///
/// The ground bass is immutable once generated (BachNoteSource::GroundBass).
/// Upper voices increase in complexity across four stages:
///   - Variations 0-2:  Quarter note chord tones (Establish)
///   - Variations 3-5:  Eighth note scale passages (Develop early)
///   - Variations 6-8:  Eighth note arpeggios (Develop late)
///   - Variations 9-11: Sixteenth note figurations (Accumulate/Resolve)
///
/// @param config Passacaglia configuration.
/// @return PassacagliaResult with generated tracks.
PassacagliaResult generatePassacaglia(const PassacagliaConfig& config);

/// @brief Generate a ground bass theme for passacaglia.
///
/// Creates an 8-bar descending bass line in the style of BWV 582:
/// Root descending stepwise to dominant, then cadential pattern.
/// Uses half notes (2 per bar).
///
/// @param key Key signature.
/// @param bars Number of bars for the ground bass.
/// @param seed RNG seed.
/// @return Vector of NoteEvent forming the ground bass.
std::vector<NoteEvent> generatePassacagliaGroundBass(const KeySignature& key,
                                                     int bars, uint32_t seed);

}  // namespace bach

#endif  // BACH_FORMS_PASSACAGLIA_H
