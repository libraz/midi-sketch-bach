// Unified generator: routes to appropriate generation system based on FormType.

#ifndef BACH_GENERATOR_H
#define BACH_GENERATOR_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"

namespace bach {

/// @brief Unified configuration for all generation forms.
struct GeneratorConfig {
  FormType form = FormType::Fugue;
  KeySignature key = {Key::C, false};
  uint8_t num_voices = 3;
  uint16_t bpm = 100;
  uint32_t seed = 0;  ///< 0 = auto (random).
  SubjectCharacter character = SubjectCharacter::Severe;
  InstrumentType instrument = InstrumentType::Organ;
  bool json_output = false;
  bool analyze = false;
  bool strict = false;
  uint8_t max_retry = 3;
  DurationScale scale = DurationScale::Short;  ///< Duration control.
  uint16_t target_bars = 0;  ///< Override: target bar count (0 = use scale).
};

/// @brief Result from unified generation.
struct GeneratorResult {
  std::vector<Track> tracks;
  std::vector<TempoEvent> tempo_events;
  Tick total_duration_ticks = 0;
  bool success = false;
  uint32_t seed_used = 0;
  std::string form_description;
  std::string error_message;
  HarmonicTimeline timeline;  ///< Harmonic context for analysis.
};

/// @brief Generate a Bach composition based on configuration.
///
/// Routes to the appropriate generator based on FormType:
///   - Fugue: FugueGenerator only
///   - PreludeAndFugue: PreludeGenerator + FugueGenerator concatenated
///   - ToccataAndFugue / FantasiaAndFugue / Passacaglia: delegates to FugueGenerator
///   - TrioSonata: TrioSonataGenerator (stub until implemented)
///   - ChoralePrelude / CelloPrelude / Chaconne: stub (future phases)
///
/// @param config Generation configuration.
/// @return GeneratorResult with MIDI tracks and metadata.
GeneratorResult generate(const GeneratorConfig& config);

/// @brief Auto-detect the default instrument for a given form type.
/// @param form The form type.
/// @return Default instrument appropriate for the form.
InstrumentType defaultInstrumentForForm(FormType form);

/// @brief Parse an InstrumentType from a string.
/// @param str String such as "organ", "harpsichord", "piano", "violin", "cello", "guitar".
/// @return Parsed InstrumentType. Defaults to InstrumentType::Organ on unrecognized input.
InstrumentType instrumentTypeFromString(const std::string& str);

}  // namespace bach

#endif  // BACH_GENERATOR_H
