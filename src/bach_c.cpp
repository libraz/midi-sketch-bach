// Implementation of C API for WASM and FFI bindings.

#include "bach_c.h"

#include <cstdlib>
#include <cstring>

#include "core/basic_types.h"
#include "core/json_helpers.h"
#include "core/json_parser.h"
#include "core/version_info.h"
#include "fugue/fugue_config.h"
#include "generator.h"
#include "harmony/key.h"
#include "midi/midi_writer.h"

namespace {

/// @brief Internal state held per BachHandle.
struct BachInstance {
  bach::GeneratorConfig config;
  bach::GeneratorResult result;
  std::vector<uint8_t> midi_bytes;
  std::string events_json;
  bool has_result = false;
};

/// @brief Parse a GeneratorConfig from a JSON key-value map.
bach::GeneratorConfig configFromJson(const std::map<std::string, bach::JsonValue>& kv) {
  bach::GeneratorConfig config;

  auto it = kv.find("form");
  if (it != kv.end() && it->second.type == bach::JsonValue::String) {
    config.form = bach::formTypeFromString(it->second.string_val);
  } else if (it != kv.end() && it->second.type == bach::JsonValue::Number) {
    auto form_id = static_cast<uint8_t>(it->second.number_val);
    if (form_id <= static_cast<uint8_t>(bach::FormType::Chaconne)) {
      config.form = static_cast<bach::FormType>(form_id);
    }
  }

  it = kv.find("key");
  if (it != kv.end() && it->second.type == bach::JsonValue::Number) {
    auto key_id = static_cast<uint8_t>(it->second.number_val);
    if (key_id <= 11) {
      config.key.tonic = static_cast<bach::Key>(key_id);
    }
  }

  it = kv.find("is_minor");
  if (it != kv.end()) {
    config.key.is_minor = it->second.asBool(false);
  }

  it = kv.find("num_voices");
  if (it != kv.end()) {
    config.num_voices = static_cast<uint8_t>(it->second.asInt(3));
  }

  it = kv.find("bpm");
  if (it != kv.end()) {
    int bpm_val = it->second.asInt(0);
    if (bpm_val > 0) {
      config.bpm = static_cast<uint16_t>(bpm_val);
    }
  }

  it = kv.find("seed");
  if (it != kv.end()) {
    config.seed = it->second.asUint(0);
  }

  it = kv.find("character");
  if (it != kv.end() && it->second.type == bach::JsonValue::String) {
    const auto& val = it->second.string_val;
    if (val == "severe") config.character = bach::SubjectCharacter::Severe;
    else if (val == "playful") config.character = bach::SubjectCharacter::Playful;
    else if (val == "noble") config.character = bach::SubjectCharacter::Noble;
    else if (val == "restless") config.character = bach::SubjectCharacter::Restless;
  } else if (it != kv.end() && it->second.type == bach::JsonValue::Number) {
    auto char_id = static_cast<uint8_t>(it->second.number_val);
    if (char_id <= 3) {
      config.character = static_cast<bach::SubjectCharacter>(char_id);
    }
  }

  it = kv.find("instrument");
  if (it != kv.end() && it->second.type == bach::JsonValue::String) {
    config.instrument = bach::instrumentTypeFromString(it->second.string_val);
  } else if (it != kv.end() && it->second.type == bach::JsonValue::Number) {
    auto inst_id = static_cast<uint8_t>(it->second.number_val);
    if (inst_id <= static_cast<uint8_t>(bach::InstrumentType::Guitar)) {
      config.instrument = static_cast<bach::InstrumentType>(inst_id);
    }
  } else {
    // Auto-detect from form if not specified
    config.instrument = bach::defaultInstrumentForForm(config.form);
  }

  it = kv.find("scale");
  if (it != kv.end() && it->second.type == bach::JsonValue::String) {
    config.scale = bach::durationScaleFromString(it->second.string_val);
  } else if (it != kv.end() && it->second.type == bach::JsonValue::Number) {
    auto scale_id = static_cast<uint8_t>(it->second.number_val);
    if (scale_id <= 3) {
      config.scale = static_cast<bach::DurationScale>(scale_id);
    }
  }

  it = kv.find("target_bars");
  if (it != kv.end()) {
    int bars_val = it->second.asInt(0);
    if (bars_val > 0) {
      config.target_bars = static_cast<uint16_t>(bars_val);
    }
  }

  return config;
}

/// @brief Validate a GeneratorConfig and return an error code.
BachError validateConfig(const bach::GeneratorConfig& config) {
  if (static_cast<uint8_t>(config.form) > static_cast<uint8_t>(bach::FormType::Chaconne)) {
    return BACH_ERROR_INVALID_FORM;
  }
  if (static_cast<uint8_t>(config.key.tonic) > 11) {
    return BACH_ERROR_INVALID_KEY;
  }
  if (static_cast<uint8_t>(config.character) > 3) {
    return BACH_ERROR_INVALID_CHARACTER;
  }
  if (static_cast<uint8_t>(config.instrument) >
      static_cast<uint8_t>(bach::InstrumentType::Guitar)) {
    return BACH_ERROR_INVALID_INSTRUMENT;
  }
  // Character-form compatibility check
  if (!bach::isCharacterFormCompatible(config.character, config.form)) {
    return BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM;
  }
  return BACH_OK;
}

/// @brief Build events JSON from GeneratorResult.
std::string buildEventsJson(const bach::GeneratorResult& result, const bach::GeneratorConfig& config) {
  bach::JsonWriter writer;
  writer.beginObject();

  writer.key("form");
  writer.value(std::string(bach::formTypeToString(config.form)));
  writer.key("key");
  writer.value(bach::keySignatureToString(config.key));
  writer.key("bpm");
  writer.value(static_cast<int>(config.bpm));
  writer.key("seed");
  writer.value(result.seed_used);
  writer.key("total_ticks");
  writer.value(result.total_duration_ticks);
  writer.key("total_bars");
  writer.value(static_cast<int>(result.total_duration_ticks / bach::kTicksPerBar));
  writer.key("description");
  writer.value(result.form_description);

  writer.key("tracks");
  writer.beginArray();
  for (const auto& track : result.tracks) {
    writer.beginObject();
    writer.key("name");
    writer.value(track.name);
    writer.key("channel");
    writer.value(static_cast<int>(track.channel));
    writer.key("program");
    writer.value(static_cast<int>(track.program));
    writer.key("note_count");
    writer.value(static_cast<int>(track.notes.size()));

    writer.key("notes");
    writer.beginArray();
    for (const auto& note : track.notes) {
      writer.beginObject();
      writer.key("pitch");
      writer.value(static_cast<int>(note.pitch));
      writer.key("velocity");
      writer.value(static_cast<int>(note.velocity));
      writer.key("start_tick");
      writer.value(note.start_tick);
      writer.key("duration");
      writer.value(note.duration);
      writer.key("voice");
      writer.value(static_cast<int>(note.voice));
      writer.endObject();
    }
    writer.endArray();

    writer.endObject();
  }
  writer.endArray();

  writer.endObject();
  return writer.toString();
}

// Display names for form types (human-readable)
const char* kFormDisplayNames[] = {
    "Fugue",
    "Prelude and Fugue",
    "Trio Sonata",
    "Chorale Prelude",
    "Toccata and Fugue",
    "Passacaglia",
    "Fantasia and Fugue",
    "Cello Prelude",
    "Chaconne",
};

constexpr uint8_t kFormCount = 9;
constexpr uint8_t kInstrumentCount = 6;
constexpr uint8_t kCharacterCount = 4;
constexpr uint8_t kKeyCount = 12;
constexpr uint8_t kScaleCount = 4;

}  // namespace

extern "C" {

// ============================================================================
// Lifecycle
// ============================================================================

BachHandle bach_create(void) {
  return new BachInstance();
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

  // Parse JSON config
  auto kv = bach::parseJsonObject(json, length);
  instance->config = configFromJson(kv);

  // Validate
  BachError err = validateConfig(instance->config);
  if (err != BACH_OK) {
    return err;
  }

  // Generate
  instance->result = bach::generate(instance->config);
  if (!instance->result.success) {
    instance->has_result = false;
    return BACH_ERROR_GENERATION_FAILED;
  }

  // Build MIDI bytes
  bach::MidiWriter writer;
  writer.build(instance->result.tracks, instance->config.bpm, instance->config.key.tonic);
  instance->midi_bytes = writer.toBytes();

  // Build events JSON
  instance->events_json = buildEventsJson(instance->result, instance->config);

  instance->has_result = true;
  return BACH_OK;
}

// ============================================================================
// Output Retrieval
// ============================================================================

BachMidiData* bach_get_midi(BachHandle handle) {
  if (!handle) return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result || instance->midi_bytes.empty()) return nullptr;

  auto* result = static_cast<BachMidiData*>(malloc(sizeof(BachMidiData)));
  if (!result) return nullptr;

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
  if (!handle) return nullptr;
  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result) return nullptr;

  auto* result = static_cast<BachEventData*>(malloc(sizeof(BachEventData)));
  if (!result) return nullptr;

  result->length = instance->events_json.size();
  result->json = static_cast<char*>(malloc(result->length + 1));
  if (!result->json) {
    free(result);
    return nullptr;
  }

  memcpy(result->json, instance->events_json.c_str(), result->length + 1);
  return result;
}

void bach_free_events(BachEventData* data) {
  if (data) {
    free(data->json);
    free(data);
  }
}

// Static buffer for info queries
static BachInfo s_info;

BachInfo* bach_get_info(BachHandle handle) {
  s_info = {};
  if (!handle) return &s_info;

  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result) return &s_info;

  s_info.total_ticks = instance->result.total_duration_ticks;
  s_info.total_bars =
      static_cast<uint16_t>(instance->result.total_duration_ticks / bach::kTicksPerBar);
  s_info.bpm = instance->config.bpm;
  s_info.track_count = static_cast<uint8_t>(instance->result.tracks.size());
  s_info.seed_used = instance->result.seed_used;

  return &s_info;
}

// ============================================================================
// Form Enumeration
// ============================================================================

uint8_t bach_form_count(void) {
  return kFormCount;
}

const char* bach_form_name(uint8_t id) {
  if (id >= kFormCount) return "";
  return bach::formTypeToString(static_cast<bach::FormType>(id));
}

const char* bach_form_display(uint8_t id) {
  if (id >= kFormCount) return "";
  return kFormDisplayNames[id];
}

// ============================================================================
// Instrument Enumeration
// ============================================================================

uint8_t bach_instrument_count(void) {
  return kInstrumentCount;
}

const char* bach_instrument_name(uint8_t id) {
  if (id >= kInstrumentCount) return "";
  return bach::instrumentTypeToString(static_cast<bach::InstrumentType>(id));
}

// ============================================================================
// Character Enumeration
// ============================================================================

uint8_t bach_character_count(void) {
  return kCharacterCount;
}

const char* bach_character_name(uint8_t id) {
  if (id >= kCharacterCount) return "";
  return bach::subjectCharacterToString(static_cast<bach::SubjectCharacter>(id));
}

// ============================================================================
// Key Enumeration
// ============================================================================

uint8_t bach_key_count(void) {
  return kKeyCount;
}

const char* bach_key_name(uint8_t id) {
  if (id >= kKeyCount) return "";
  return bach::keyToString(static_cast<bach::Key>(id));
}

// ============================================================================
// Scale Enumeration
// ============================================================================

uint8_t bach_scale_count(void) {
  return kScaleCount;
}

const char* bach_scale_name(uint8_t id) {
  if (id >= kScaleCount) return "";
  return bach::durationScaleToString(static_cast<bach::DurationScale>(id));
}

// ============================================================================
// Default Instrument
// ============================================================================

uint8_t bach_default_instrument_for_form(uint8_t form_id) {
  if (form_id >= kFormCount) return 0;
  return static_cast<uint8_t>(
      bach::defaultInstrumentForForm(static_cast<bach::FormType>(form_id)));
}

// ============================================================================
// Error Handling
// ============================================================================

const char* bach_error_string(BachError error) {
  switch (error) {
    case BACH_OK: return "No error";
    case BACH_ERROR_INVALID_PARAM: return "Invalid parameter";
    case BACH_ERROR_GENERATION_FAILED: return "Generation failed";
    case BACH_ERROR_INVALID_FORM: return "Invalid form type";
    case BACH_ERROR_INVALID_KEY: return "Invalid key (must be 0-11)";
    case BACH_ERROR_INVALID_CHARACTER: return "Invalid character";
    case BACH_ERROR_INVALID_INSTRUMENT: return "Invalid instrument";
    case BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM: return "Incompatible character for this form";
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
