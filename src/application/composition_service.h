#ifndef BACH_APPLICATION_COMPOSITION_SERVICE_H
#define BACH_APPLICATION_COMPOSITION_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/expression_events.h"
#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach::application {

/// Surface-neutral request for the shipped form-composition product path.
struct CompositionRequest {
  FormType form = FormType::Fugue;
  KeySignature key;
  SubjectCharacter character = SubjectCharacter::Severe;
  InstrumentType instrument = InstrumentType::Organ;
  bool instrument_specified = false;
  DurationScale scale = DurationScale::Short;
  std::uint16_t target_bars = 0;
  std::uint16_t bpm = 100;
  std::uint32_t seed = 0;
  bool enable_free_counterpoint = false;
};

enum class CompositionStatus : std::uint8_t {
  Ok = 0,
  InvalidArgument,
  IncompatibleCharacter,
  InvalidForm,
  GenerationFailed,
  FinalValidationFailed,
  MidiFailed,
};

struct PerformanceProfile {
  bool registration_terraces = false;
  bool continuous_expression = false;
  composer::RitardandoStyle final_ritardando = composer::RitardandoStyle::None;
};

PerformanceProfile resolvePerformanceProfile(FormType form, InstrumentType instrument);

/// Complete owning product result shared by CLI and C/WASM adapters.
struct CompositionProduct {
  CompositionStatus status = CompositionStatus::InvalidArgument;
  composer::ComposeResult composition;
  composer::ValidationReport final_validation;
  FormType form = FormType::Fugue;
  KeySignature key;
  SubjectCharacter character = SubjectCharacter::Severe;
  InstrumentType instrument = InstrumentType::Organ;
  DurationScale scale = DurationScale::Short;
  std::uint16_t bpm = 0;
  std::uint32_t seed = 0;
  std::uint16_t resolved_bars = 0;
  std::uint32_t total_ticks = 0;
  std::uint16_t total_bars = 0;
  std::string form_display;
  PerformanceProfile performance_profile;
  std::vector<TempoEvent> tempo_events;
  std::vector<TimeSignatureEvent> time_signature_events;
  std::vector<std::uint8_t> midi_bytes;
  std::string homepage_events_json;
  std::string generated_json;
  std::string provenance_json;
  std::string diagnostic_json;
};

/// Resolve and execute the complete default product pipeline.
///
/// This is the only implementation of form resolution, fixture construction,
/// generation validation, ornamentation, instrument/expression application,
/// MIDI rendering, and JSON export used by CLI and C/WASM. Callers retain only
/// surface-specific parsing and file/FFI I/O.
CompositionStatus compose(const CompositionRequest& request, CompositionProduct* out);

}  // namespace bach::application

#endif  // BACH_APPLICATION_COMPOSITION_SERVICE_H
