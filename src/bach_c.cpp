// Implementation of C API for WASM and FFI bindings.
//
// This entry point drives the composer subsystem (the WASM product path).
// bach_c.cpp sits OUTSIDE the composer isolation boundary, so it may include
// both composer headers and legacy helpers (key/form string utilities); the
// isolation contract only forbids composer -> legacy includes, not the reverse.

#include "bach_c.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "application/composition_service.h"
#include "core/basic_types.h"
#include "core/json_parser.h"
#include "core/version_info.h"
#include "harmony/key.h"

namespace {

constexpr uint8_t kFormCount = static_cast<uint8_t>(bach::FormType::GoldbergVariations) + 1;
constexpr uint8_t kInstrumentCount = 6;
constexpr uint8_t kCharacterCount = 4;
constexpr uint8_t kKeyCount = 12;
constexpr uint8_t kScaleCount = 4;

constexpr uint16_t kMinBpm = 40;
constexpr uint16_t kMaxBpm = 200;

/// @brief Fully-resolved request derived from the JSON config.
///
/// Holds the validated, defaults-applied parameters the composer pipeline
/// consumes. Built by parseRequest(); parsing reports any violation via the
/// out-parameter error code rather than throwing.
struct ParsedRequest {
  bach::FormType form = bach::FormType::Fugue;
  bach::Key key_tonic = bach::Key::C;
  bool is_minor = false;
  bach::SubjectCharacter character = bach::SubjectCharacter::Severe;
  bach::InstrumentType instrument = bach::InstrumentType::Organ;
  bach::DurationScale scale = bach::DurationScale::Short;
  uint16_t target_bars = 0;
  uint16_t bpm = 0;
  uint32_t seed = 0;  // Resolved (non-zero) generation seed.
};

/// @brief Internal state held per BachHandle.
struct BachInstance {
  std::vector<uint8_t> midi_bytes;
  std::string events_json;
  std::string generated_json;
  std::string provenance_json;
  std::string diagnostic_json;
  bach::FormType form = bach::FormType::Fugue;
  bach::KeySignature key;
  uint16_t bpm = 0;
  uint32_t seed_used = 0;
  uint32_t total_ticks = 0;
  uint16_t total_bars = 0;
  uint8_t track_count = 0;
  bool has_result = false;
  BachInfo info = {};
};

bool readInteger(const bach::JsonValue& value, std::int64_t minimum, std::int64_t maximum,
                 std::int64_t* out) {
  std::int64_t parsed = 0;
  if (!value.asInt64(&parsed) || parsed < minimum || parsed > maximum) {
    return false;
  }
  *out = parsed;
  return true;
}

bool isKnownRequestKey(const std::string& key) {
  return key == "form" || key == "key" || key == "is_minor" || key == "num_voices" ||
         key == "bpm" || key == "seed" || key == "character" || key == "instrument" ||
         key == "scale" || key == "target_bars";
}

BachEventData* copyJsonData(const std::string& json) {
  if (json.empty()) {
    return nullptr;
  }
  auto* result = static_cast<BachEventData*>(malloc(sizeof(BachEventData)));
  if (!result) {
    return nullptr;
  }
  result->length = json.size();
  result->json = static_cast<char*>(malloc(result->length + 1));
  if (!result->json) {
    free(result);
    return nullptr;
  }
  memcpy(result->json, json.c_str(), result->length + 1);
  return result;
}

BachError parseKey(const bach::JsonValue& value, bach::Key* out) {
  if (value.type == bach::JsonValue::Number) {
    std::int64_t key_id = 0;
    if (!readInteger(value, 0, 11, &key_id)) {
      return BACH_ERROR_INVALID_KEY;
    }
    *out = static_cast<bach::Key>(key_id);
    return BACH_OK;
  }
  if (value.type == bach::JsonValue::String) {
    for (uint8_t key_id = 0; key_id < 12; ++key_id) {
      const auto key = static_cast<bach::Key>(key_id);
      if (value.string_val == bach::keyToString(key)) {
        *out = key;
        return BACH_OK;
      }
    }
  }
  return BACH_ERROR_INVALID_KEY;
}

/// @brief Strict-parse the "form" field.
/// @param kv Parsed JSON object.
/// @param out Receives the parsed FormType on success.
/// @return BACH_OK or BACH_ERROR_INVALID_FORM.
BachError parseForm(const std::map<std::string, bach::JsonValue>& kv, bach::FormType* out) {
  auto iter = kv.find("form");
  if (iter == kv.end()) {
    return BACH_OK;  // Default Fugue.
  }
  if (iter->second.type == bach::JsonValue::String) {
    const std::string& name = iter->second.string_val;
    bach::FormType parsed = bach::formTypeFromString(name);
    // formTypeFromString falls back to Fugue on unknown input; detect that
    // by round-tripping the parsed value back to its canonical name.
    if (bach::formTypeToString(parsed) != name) {
      return BACH_ERROR_INVALID_FORM;
    }
    *out = parsed;
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::Number) {
    std::int64_t form_id = 0;
    if (!readInteger(iter->second, 0, static_cast<int>(bach::FormType::GoldbergVariations),
                     &form_id)) {
      return BACH_ERROR_INVALID_FORM;
    }
    *out = static_cast<bach::FormType>(form_id);
    return BACH_OK;
  }
  return BACH_ERROR_INVALID_FORM;
}

/// @brief Strict-parse the "character" field.
/// @param kv Parsed JSON object.
/// @param out Receives the parsed SubjectCharacter on success.
/// @return BACH_OK or BACH_ERROR_INVALID_CHARACTER.
BachError parseCharacter(const std::map<std::string, bach::JsonValue>& kv,
                         bach::SubjectCharacter* out) {
  auto iter = kv.find("character");
  if (iter == kv.end()) {
    return BACH_OK;  // Default Severe.
  }
  if (iter->second.type == bach::JsonValue::String) {
    std::string val = iter->second.string_val;
    std::transform(val.begin(), val.end(), val.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (val == "severe") {
      *out = bach::SubjectCharacter::Severe;
    } else if (val == "playful") {
      *out = bach::SubjectCharacter::Playful;
    } else if (val == "noble") {
      *out = bach::SubjectCharacter::Noble;
    } else if (val == "restless") {
      *out = bach::SubjectCharacter::Restless;
    } else {
      return BACH_ERROR_INVALID_CHARACTER;
    }
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::Number) {
    std::int64_t char_id = 0;
    if (!readInteger(iter->second, 0, 3, &char_id)) {
      return BACH_ERROR_INVALID_CHARACTER;
    }
    *out = static_cast<bach::SubjectCharacter>(char_id);
    return BACH_OK;
  }
  return BACH_ERROR_INVALID_CHARACTER;
}

/// @brief Strict-parse the "instrument" field.
/// @param kv Parsed JSON object.
/// @param form Form used to pick the default instrument when absent.
/// @param out Receives the parsed InstrumentType on success.
/// @return BACH_OK or BACH_ERROR_INVALID_INSTRUMENT.
BachError parseInstrument(const std::map<std::string, bach::JsonValue>& kv, bach::FormType form,
                          bach::InstrumentType* out) {
  auto iter = kv.find("instrument");
  if (iter == kv.end()) {
    *out = bach::defaultInstrumentForForm(form);
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::String) {
    const std::string& name = iter->second.string_val;
    bach::InstrumentType parsed = bach::instrumentTypeFromString(name);
    // instrumentTypeFromString falls back to Organ on unknown input; detect
    // via round-trip against the canonical name.
    if (bach::instrumentTypeToString(parsed) != name) {
      return BACH_ERROR_INVALID_INSTRUMENT;
    }
    *out = parsed;
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::Number) {
    std::int64_t inst_id = 0;
    if (!readInteger(iter->second, 0, static_cast<int>(bach::InstrumentType::Guitar), &inst_id)) {
      return BACH_ERROR_INVALID_INSTRUMENT;
    }
    *out = static_cast<bach::InstrumentType>(inst_id);
    return BACH_OK;
  }
  return BACH_ERROR_INVALID_INSTRUMENT;
}

/// @brief Strict-parse the "scale" field.
/// @param kv Parsed JSON object.
/// @param out Receives the parsed DurationScale on success.
/// @return BACH_OK or BACH_ERROR_INVALID_CONFIG.
BachError parseScale(const std::map<std::string, bach::JsonValue>& kv, bach::DurationScale* out) {
  auto iter = kv.find("scale");
  if (iter == kv.end()) {
    return BACH_OK;  // Default Short.
  }
  if (iter->second.type == bach::JsonValue::String) {
    const std::string& name = iter->second.string_val;
    bach::DurationScale parsed = bach::durationScaleFromString(name);
    // durationScaleFromString falls back to Short on unknown input; detect
    // via round-trip against the canonical name.
    if (bach::durationScaleToString(parsed) != name) {
      return BACH_ERROR_INVALID_SCALE;
    }
    *out = parsed;
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::Number) {
    std::int64_t scale_id = 0;
    if (!readInteger(iter->second, 0, 3, &scale_id)) {
      return BACH_ERROR_INVALID_SCALE;
    }
    *out = static_cast<bach::DurationScale>(scale_id);
    return BACH_OK;
  }
  return BACH_ERROR_INVALID_SCALE;
}

/// @brief Parse and validate the JSON config into a ParsedRequest.
/// @param kv Parsed JSON object.
/// @param out Receives the resolved request on success.
/// @return BACH_OK or the first violated field's error code.
BachError parseRequest(const std::map<std::string, bach::JsonValue>& kv, ParsedRequest* out) {
  for (const auto& [key, value] : kv) {
    (void)value;
    if (!isKnownRequestKey(key)) {
      return BACH_ERROR_UNKNOWN_CONFIG_FIELD;
    }
  }

  BachError err = parseForm(kv, &out->form);
  if (err != BACH_OK) {
    return err;
  }

  auto iter = kv.find("key");
  if (iter != kv.end()) {
    err = parseKey(iter->second, &out->key_tonic);
    if (err != BACH_OK) {
      return err;
    }
  }

  iter = kv.find("is_minor");
  if (iter != kv.end()) {
    if (iter->second.type != bach::JsonValue::Bool) {
      return BACH_ERROR_INVALID_IS_MINOR;
    }
    out->is_minor = iter->second.bool_val;
  }

  // num_voices is accepted and ignored for backward compatibility; the form
  // now decides the voice count. Its legacy numeric type remains strict.
  iter = kv.find("num_voices");
  if (iter != kv.end()) {
    std::int64_t ignored_voice_count = 0;
    if (!readInteger(iter->second, 0, UINT8_MAX, &ignored_voice_count)) {
      return BACH_ERROR_INVALID_NUM_VOICES;
    }
  }

  iter = kv.find("bpm");
  if (iter != kv.end()) {
    std::int64_t bpm_val = 0;
    if (!readInteger(iter->second, 0, UINT16_MAX, &bpm_val)) {
      return BACH_ERROR_INVALID_BPM;
    }
    if (bpm_val == 0) {
      out->bpm = 0;
    } else if (bpm_val < kMinBpm || bpm_val > kMaxBpm) {
      return BACH_ERROR_INVALID_BPM;
    } else {
      out->bpm = static_cast<uint16_t>(bpm_val);
    }
  }

  iter = kv.find("seed");
  if (iter != kv.end()) {
    std::uint32_t seed = 0;
    if (!iter->second.asUint32(&seed)) {
      return BACH_ERROR_INVALID_SEED;
    }
    out->seed = seed;
  }
  err = parseCharacter(kv, &out->character);
  if (err != BACH_OK) {
    return err;
  }

  err = parseInstrument(kv, out->form, &out->instrument);
  if (err != BACH_OK) {
    return err;
  }

  err = parseScale(kv, &out->scale);
  if (err != BACH_OK) {
    return err;
  }

  iter = kv.find("target_bars");
  if (iter != kv.end()) {
    std::int64_t bars_val = 0;
    if (!readInteger(iter->second, 0, UINT16_MAX, &bars_val)) {
      return BACH_ERROR_INVALID_TARGET_BARS;
    }
    if (bars_val > 0) {
      out->target_bars = static_cast<uint16_t>(bars_val);
    }
  }

  return BACH_OK;
}

}  // namespace

extern "C" {

// ============================================================================
// Lifecycle
// ============================================================================

BachHandle bach_create(void) {
  return new (std::nothrow) BachInstance();
}

void bach_destroy(BachHandle handle) {
  delete static_cast<BachInstance*>(handle);
}

// ============================================================================
// Generation
// ============================================================================

BachError bach_generate_from_json(BachHandle handle, const char* json, size_t length) {
  if (!handle || !json) {
    return BACH_ERROR_INVALID_PARAM;
  }

  auto* instance = static_cast<BachInstance*>(handle);
  instance->has_result = false;
  instance->info = {};
  instance->midi_bytes.clear();
  instance->events_json.clear();
  instance->generated_json.clear();
  instance->provenance_json.clear();
  instance->diagnostic_json.clear();

  try {
    // 1. Parse + validate the JSON config.
    std::map<std::string, bach::JsonValue> kv;
    if (bach::parseJsonObject(json, length, &kv) != bach::JsonParseStatus::Ok) {
      return BACH_ERROR_INVALID_JSON;
    }
    ParsedRequest req;
    BachError err = parseRequest(kv, &req);
    if (err != BACH_OK) {
      return err;
    }

    bach::application::CompositionRequest product_request;
    product_request.form = req.form;
    product_request.key = {req.key_tonic, req.is_minor};
    product_request.character = req.character;
    product_request.instrument = req.instrument;
    product_request.instrument_specified = kv.find("instrument") != kv.end();
    product_request.scale = req.scale;
    product_request.target_bars = req.target_bars;
    product_request.bpm = req.bpm;
    product_request.seed = req.seed;

    bach::application::CompositionProduct product;
    const auto product_status = bach::application::compose(product_request, &product);
    instance->diagnostic_json = product.diagnostic_json;
    if (product_status == bach::application::CompositionStatus::IncompatibleCharacter) {
      return BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM;
    }
    if (product_status == bach::application::CompositionStatus::IncompatibleInstrument) {
      return BACH_ERROR_INCOMPATIBLE_INSTRUMENT_FORM;
    }
    if (product_status == bach::application::CompositionStatus::InvalidForm) {
      return BACH_ERROR_INVALID_FORM;
    }
    if (product_status != bach::application::CompositionStatus::Ok) {
      return BACH_ERROR_GENERATION_FAILED;
    }

    instance->midi_bytes = std::move(product.midi_bytes);
    instance->events_json = std::move(product.homepage_events_json);
    instance->generated_json = std::move(product.generated_json);
    instance->provenance_json = std::move(product.provenance_json);
    instance->form = product.form;
    instance->key = product.key;
    instance->bpm = product.bpm;
    instance->seed_used = product.seed;
    instance->total_ticks = product.total_ticks;
    instance->total_bars = product.total_bars;
    instance->track_count = static_cast<uint8_t>(product.composition.tracks.size());
    instance->info.total_ticks = instance->total_ticks;
    instance->info.total_bars = instance->total_bars;
    instance->info.bpm = instance->bpm;
    instance->info.track_count = instance->track_count;
    instance->info.seed_used = instance->seed_used;
    instance->has_result = true;
    return BACH_OK;
  } catch (...) {
    // Never allow a C++ exception to cross the C/WASM ABI boundary.  Clear
    // the public snapshot as well, so a failed regeneration cannot expose a
    // stale successful result.
    instance->has_result = false;
    instance->info = {};
    instance->midi_bytes.clear();
    instance->events_json.clear();
    instance->generated_json.clear();
    instance->provenance_json.clear();
    instance->diagnostic_json.clear();
    return BACH_ERROR_GENERATION_FAILED;
  }
}

// ============================================================================
// Output Retrieval
// ============================================================================

BachMidiData* bach_get_midi(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result || instance->midi_bytes.empty())
    return nullptr;

  auto* result = static_cast<BachMidiData*>(malloc(sizeof(BachMidiData)));
  if (!result)
    return nullptr;

  result->size = instance->midi_bytes.size();
  result->data = static_cast<uint8_t*>(malloc(result->size));
  if (!result->data) {
    free(result);
    return nullptr;
  }

  memcpy(result->data, instance->midi_bytes.data(), result->size);
  return result;
}

void bach_free_midi(BachMidiData* data) {
  if (data) {
    free(data->data);
    free(data);
  }
}

BachEventData* bach_get_events(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result)
    return nullptr;

  return copyJsonData(instance->events_json);
}

BachEventData* bach_get_generated(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result)
    return nullptr;
  return copyJsonData(instance->generated_json);
}

BachEventData* bach_get_provenance(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result)
    return nullptr;
  return copyJsonData(instance->provenance_json);
}

void bach_free_events(BachEventData* data) {
  if (data) {
    free(data->json);
    free(data);
  }
}

BachEventData* bach_get_diagnostic(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (instance->diagnostic_json.empty())
    return nullptr;

  return copyJsonData(instance->diagnostic_json);
}

const BachInfo* bach_get_info(BachHandle handle) {
  if (!handle)
    return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result)
    return nullptr;
  return &instance->info;
}

BachEventData* bach_get_info_json(BachHandle handle) {
  const BachInfo* info = bach_get_info(handle);
  if (info == nullptr)
    return nullptr;
  const std::string json = "{\"totalBars\":" + std::to_string(info->total_bars) +
                           ",\"totalTicks\":" + std::to_string(info->total_ticks) +
                           ",\"bpm\":" + std::to_string(info->bpm) +
                           ",\"trackCount\":" + std::to_string(info->track_count) +
                           ",\"seedUsed\":" + std::to_string(info->seed_used) + "}";
  return copyJsonData(json);
}

// ============================================================================
// Form Enumeration
// ============================================================================

uint8_t bach_form_count(void) {
  return kFormCount;
}

const char* bach_form_name(uint8_t id) {
  if (id >= kFormCount)
    return "";
  return bach::formTypeToString(static_cast<bach::FormType>(id));
}

const char* bach_form_display(uint8_t id) {
  if (id >= kFormCount)
    return "";
  return bach::formTypeToDisplayString(static_cast<bach::FormType>(id));
}

// ============================================================================
// Instrument Enumeration
// ============================================================================

uint8_t bach_instrument_count(void) {
  return kInstrumentCount;
}

const char* bach_instrument_name(uint8_t id) {
  if (id >= kInstrumentCount)
    return "";
  return bach::instrumentTypeToString(static_cast<bach::InstrumentType>(id));
}

// ============================================================================
// Character Enumeration
// ============================================================================

uint8_t bach_character_count(void) {
  return kCharacterCount;
}

const char* bach_character_name(uint8_t id) {
  if (id >= kCharacterCount)
    return "";
  return bach::subjectCharacterToString(static_cast<bach::SubjectCharacter>(id));
}

// ============================================================================
// Key Enumeration
// ============================================================================

uint8_t bach_key_count(void) {
  return kKeyCount;
}

const char* bach_key_name(uint8_t id) {
  if (id >= kKeyCount)
    return "";
  return bach::keyToString(static_cast<bach::Key>(id));
}

// ============================================================================
// Scale Enumeration
// ============================================================================

uint8_t bach_scale_count(void) {
  return kScaleCount;
}

const char* bach_scale_name(uint8_t id) {
  if (id >= kScaleCount)
    return "";
  return bach::durationScaleToString(static_cast<bach::DurationScale>(id));
}

// ============================================================================
// Default Instrument
// ============================================================================

uint8_t bach_default_instrument_for_form(uint8_t form_id) {
  if (form_id >= kFormCount)
    return 0;
  return static_cast<uint8_t>(bach::defaultInstrumentForForm(static_cast<bach::FormType>(form_id)));
}

// ============================================================================
// Error Handling
// ============================================================================

const char* bach_error_string(BachError error) {
  switch (error) {
    case BACH_OK:
      return "No error";
    case BACH_ERROR_INVALID_PARAM:
      return "Invalid parameter";
    case BACH_ERROR_GENERATION_FAILED:
      return "Generation failed";
    case BACH_ERROR_INVALID_FORM:
      return "Invalid form type";
    case BACH_ERROR_INVALID_KEY:
      return "Invalid key (must be 0-11)";
    case BACH_ERROR_INVALID_CHARACTER:
      return "Invalid character";
    case BACH_ERROR_INVALID_INSTRUMENT:
      return "Invalid instrument";
    case BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM:
      return "Incompatible character for this form";
    case BACH_ERROR_INVALID_CONFIG:
      return "Invalid configuration value";
    case BACH_ERROR_INVALID_JSON:
      return "Invalid JSON configuration";
    case BACH_ERROR_UNKNOWN_CONFIG_FIELD:
      return "Unknown configuration field";
    case BACH_ERROR_INVALID_BPM:
      return "Invalid BPM (must be 0 or 40-200)";
    case BACH_ERROR_INVALID_SEED:
      return "Invalid seed (must be an unsigned 32-bit integer)";
    case BACH_ERROR_INVALID_TARGET_BARS:
      return "Invalid target_bars (must be an unsigned 16-bit integer)";
    case BACH_ERROR_INVALID_SCALE:
      return "Invalid duration scale";
    case BACH_ERROR_INVALID_IS_MINOR:
      return "Invalid is_minor (must be boolean)";
    case BACH_ERROR_INVALID_NUM_VOICES:
      return "Invalid num_voices (must be an unsigned 8-bit integer)";
    case BACH_ERROR_INCOMPATIBLE_INSTRUMENT_FORM:
      return "Incompatible instrument for this form";
  }
  return "Unknown error";
}

// ============================================================================
// Utilities
// ============================================================================

const char* bach_version(void) {
  return BACH_VERSION;
}

}  // extern "C"
