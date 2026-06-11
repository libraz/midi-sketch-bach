// Implementation of C API for WASM and FFI bindings.
//
// This entry point drives the composer subsystem (the WASM product path).
// bach_c.cpp sits OUTSIDE the composer isolation boundary, so it may include
// both composer headers and legacy helpers (key/form string utilities); the
// isolation contract only forbids composer -> legacy includes, not the reverse.

#include "bach_c.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "composer/composer.h"
#include "composer/expression_events.h"
#include "composer/form_director.h"
#include "composer/harmonic_plan.h"
#include "composer/harness_fixture.h"
#include "composer/json_export.h"
#include "composer/ornament_pass.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/instrument_program.h"
#include "core/json_parser.h"
#include "core/rng_util.h"
#include "core/version_info.h"
#include "harmony/key.h"
#include "midi/midi_writer.h"

namespace {

// Display names for form types (human-readable). Indexed by FormType ordinal.
const char* kFormDisplayNames[] = {
    "Fugue",       "Prelude and Fugue",  "Trio Sonata",   "Chorale Prelude", "Toccata and Fugue",
    "Passacaglia", "Fantasia and Fugue", "Cello Prelude", "Chaconne",        "Goldberg Variations",
};

constexpr uint8_t kFormCount = 10;
constexpr uint8_t kInstrumentCount = 6;
constexpr uint8_t kCharacterCount = 4;
constexpr uint8_t kKeyCount = 12;
constexpr uint8_t kScaleCount = 4;

constexpr uint16_t kDefaultBpm = 100;
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
  uint16_t bpm = kDefaultBpm;
  uint32_t seed = 0;  // Resolved (non-zero) generation seed.
};

/// @brief Internal state held per BachHandle.
struct BachInstance {
  std::vector<uint8_t> midi_bytes;
  std::string events_json;
  bach::FormType form = bach::FormType::Fugue;
  bach::KeySignature key;
  uint16_t bpm = kDefaultBpm;
  uint32_t seed_used = 0;
  uint32_t total_ticks = 0;
  uint16_t total_bars = 0;
  uint8_t track_count = 0;
  bool has_result = false;
};

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
    int form_id = iter->second.asInt(-1);
    if (form_id < 0 || form_id > static_cast<int>(bach::FormType::GoldbergVariations)) {
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
    const std::string& val = iter->second.string_val;
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
    int char_id = iter->second.asInt(-1);
    if (char_id < 0 || char_id > 3) {
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
    int inst_id = iter->second.asInt(-1);
    if (inst_id < 0 || inst_id > static_cast<int>(bach::InstrumentType::Guitar)) {
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
      return BACH_ERROR_INVALID_CONFIG;
    }
    *out = parsed;
    return BACH_OK;
  }
  if (iter->second.type == bach::JsonValue::Number) {
    int scale_id = iter->second.asInt(-1);
    if (scale_id < 0 || scale_id > 3) {
      return BACH_ERROR_INVALID_CONFIG;
    }
    *out = static_cast<bach::DurationScale>(scale_id);
    return BACH_OK;
  }
  return BACH_ERROR_INVALID_CONFIG;
}

/// @brief Parse and validate the JSON config into a ParsedRequest.
/// @param kv Parsed JSON object.
/// @param out Receives the resolved request on success.
/// @return BACH_OK or the first violated field's error code.
BachError parseRequest(const std::map<std::string, bach::JsonValue>& kv, ParsedRequest* out) {
  BachError err = parseForm(kv, &out->form);
  if (err != BACH_OK) {
    return err;
  }

  auto iter = kv.find("key");
  if (iter != kv.end()) {
    if (iter->second.type != bach::JsonValue::Number) {
      return BACH_ERROR_INVALID_KEY;
    }
    int key_id = iter->second.asInt(-1);
    if (key_id < 0 || key_id > 11) {
      return BACH_ERROR_INVALID_KEY;
    }
    out->key_tonic = static_cast<bach::Key>(key_id);
  }

  iter = kv.find("is_minor");
  if (iter != kv.end()) {
    out->is_minor = iter->second.asBool(false);
  }

  // num_voices is accepted and ignored for backward compatibility; the form
  // now decides the voice count. No validation, no error.

  iter = kv.find("bpm");
  if (iter != kv.end()) {
    int bpm_val = iter->second.asInt(0);
    if (bpm_val == 0) {
      out->bpm = kDefaultBpm;
    } else if (bpm_val < kMinBpm || bpm_val > kMaxBpm) {
      return BACH_ERROR_INVALID_CONFIG;
    } else {
      out->bpm = static_cast<uint16_t>(bpm_val);
    }
  }

  iter = kv.find("seed");
  if (iter != kv.end()) {
    out->seed = iter->second.asUint(0);
  }
  if (out->seed == 0) {
    out->seed = bach::rng::generateRandomSeed();
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
    int bars_val = iter->second.asInt(0);
    if (bars_val > 0) {
      out->target_bars = static_cast<uint16_t>(bars_val);
    }
  }

  return BACH_OK;
}

/// @brief Collect the voices carrying immutable ground material (hard exempt).
///
/// Ground lines must never be ornamented because the ground bass is
/// immutable. The scan is intent-based, not per-form hardcoded: any span
/// whose VoiceIntent replays an immutable ground carrier marks its voice
/// exempt.
///
/// @param fixture The built form fixture.
/// @return Sorted, de-duplicated list of exempt VoiceIds.
std::vector<bach::VoiceId> exemptVoices(const bach::composer::HarnessFixture& fixture) {
  std::vector<bach::VoiceId> voices;
  for (const auto& span : fixture.voice_plan.spans) {
    const bool is_immutable_carrier = span.intent == bach::composer::VoiceIntent::GroundCarrier ||
                                      span.intent == bach::composer::VoiceIntent::PassacagliaGround;
    if (is_immutable_carrier) {
      voices.push_back(span.voice);
    }
  }
  std::sort(voices.begin(), voices.end());
  voices.erase(std::unique(voices.begin(), voices.end()), voices.end());
  return voices;
}

/// @brief Collect the cantus-firmus voices (skeleton exempt).
///
/// CF voices keep their bar-head onsets immutable but may carry within-bar
/// embellishment (character-gated inside the ornament pass), so they go to
/// the skeleton list instead of the hard-exempt list.
///
/// @param fixture The built form fixture.
/// @return Sorted, de-duplicated list of skeleton-exempt VoiceIds.
std::vector<bach::VoiceId> skeletonVoices(const bach::composer::HarnessFixture& fixture) {
  std::vector<bach::VoiceId> voices;
  for (const auto& span : fixture.voice_plan.spans) {
    if (span.intent == bach::composer::VoiceIntent::CantusFirmusCarrier) {
      voices.push_back(span.voice);
    }
  }
  std::sort(voices.begin(), voices.end());
  voices.erase(std::unique(voices.begin(), voices.end()), voices.end());
  return voices;
}

/// @brief Total length in ticks: max(start + duration) over all notes.
/// @param notes Composed note stream.
/// @return Total tick length, or 0 if empty.
uint32_t totalTicksOf(const std::vector<bach::NoteEvent>& notes) {
  uint32_t total = 0;
  for (const auto& note : notes) {
    const uint32_t end = note.start_tick + note.duration;
    if (end > total) {
      total = end;
    }
  }
  return total;
}

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
  instance->has_result = false;

  // 1. Parse + validate the JSON config.
  auto kv = bach::parseJsonObject(json, length);
  ParsedRequest req;
  BachError err = parseRequest(kv, &req);
  if (err != BACH_OK) {
    return err;
  }

  // 2. Character / form compatibility.
  if (!bach::composer::isFormCharacterCompatible(req.form, req.character)) {
    return BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM;
  }

  // 3. Resolve bars (target_bars overrides scale).
  const uint16_t resolved_bars = bach::composer::resolveBars(req.form, req.scale, req.target_bars);

  // 4. Build the form fixture.
  bach::composer::ComposeRequest compose_req;
  compose_req.form = req.form;
  compose_req.is_minor = req.is_minor;
  compose_req.character = req.character;
  compose_req.target_bars = resolved_bars;
  compose_req.seed = req.seed;

  bach::composer::HarnessFixture fixture;
  bach::composer::FormDirectorStatus dir_status =
      bach::composer::buildFormFixture(compose_req, &fixture);
  if (dir_status == bach::composer::FormDirectorStatus::IncompatibleCharacter) {
    return BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM;
  }
  if (dir_status == bach::composer::FormDirectorStatus::UnknownForm) {
    return BACH_ERROR_INVALID_FORM;
  }

  // 5. Run the composer pipeline.
  auto result =
      bach::composer::Composer{}.run(fixture.material, fixture.harmony, fixture.voice_plan);
  if (result.validation.status != bach::composer::ValidationStatus::Ok) {
    return BACH_ERROR_GENERATION_FAILED;
  }

  const bach::Tick ticks_per_bar = fixture.harmony.ticksPerBar();

  // The ornament pass subdivides notes in place and never extends the piece,
  // so the total computed here stays valid for the expression passes below.
  const uint32_t total_ticks = totalTicksOf(result.notes);

  // 6. Ornament post-pass. Ground voices are hard exempt; cantus-firmus
  // voices keep their bar-head skeleton but may carry within-bar
  // embellishment (character-gated inside the pass). The Goldberg opening
  // aria (block 0, the first four bars) is the designed ornament showcase.
  bach::composer::OrnamentParams orn;
  orn.character = req.character;
  orn.instrument = req.instrument;
  orn.mode =
      req.is_minor ? bach::composer::detail::Mode::Minor : bach::composer::detail::Mode::Major;
  orn.seed = req.seed;
  orn.ticks_per_bar = ticks_per_bar;
  orn.bpm = req.bpm;
  orn.exempt_voices = exemptVoices(fixture);
  orn.skeleton_exempt_voices = skeletonVoices(fixture);
  if (req.form == bach::FormType::GoldbergVariations) {
    orn.aria_end_tick = 4 * ticks_per_bar;
  }
  // Climax uplift: decoration intensifies in a two-bar window centred on the
  // macro arc's ~75% climax point (the registration plan's peak).
  const bach::Tick climax_tick =
      static_cast<bach::Tick>(static_cast<uint64_t>(total_ticks) * 3 / 4);
  orn.climax_start_tick = climax_tick > ticks_per_bar ? climax_tick - ticks_per_bar : 0;
  orn.climax_end_tick = climax_tick + ticks_per_bar;
  bach::composer::applyOrnamentPass(result, orn);

  // 7. Apply instrument program + default track names.
  bach::applyInstrument(result.tracks, req.instrument);

  // 8. Expression events.
  const bach::composer::FormSpec& spec = bach::composer::formSpec(req.form);
  const uint16_t snap = spec.snap_bars == 0 ? 1 : spec.snap_bars;
  std::size_t cycle_count = resolved_bars / snap;
  if (cycle_count == 0) {
    cycle_count = 1;
  }

  if (req.instrument == bach::InstrumentType::Organ) {
    auto cc = bach::composer::buildRegistrationPlan(resolved_bars, cycle_count, ticks_per_bar,
                                                    total_ticks);
    for (auto& track : result.tracks) {
      track.cc_events.insert(track.cc_events.end(), cc.begin(), cc.end());
    }
  }

  // Phrase-level expression arch (CC#11): every instrument that can shape
  // dynamics continuously breathes with the phrasing; a harpsichord has no
  // dynamic control, so it keeps the plain stream.
  if (req.instrument != bach::InstrumentType::Harpsichord) {
    auto phrase =
        bach::composer::buildPhraseDynamics(cycle_count, snap, ticks_per_bar, total_ticks);
    for (auto& track : result.tracks) {
      track.cc_events.insert(track.cc_events.end(), phrase.begin(), phrase.end());
    }
  }

  std::vector<bach::TempoEvent> tempo_events;
  tempo_events.push_back({0, req.bpm});
  auto rit = bach::composer::buildFinalRitardando(req.bpm, total_ticks, ticks_per_bar);
  tempo_events.insert(tempo_events.end(), rit.begin(), rit.end());

  // 9. Time signature (one event at tick 0 from the form's meter).
  std::vector<bach::TimeSignatureEvent> time_sig_events;
  bach::TimeSignatureEvent ts_event;
  ts_event.tick = 0;
  ts_event.time_sig.numerator = spec.ts_numerator;
  ts_event.time_sig.denominator = spec.ts_denominator;
  time_sig_events.push_back(ts_event);

  // 10. MIDI bytes (transposition to the requested key happens here only).
  bach::MidiWriter writer;
  writer.build(result.tracks, tempo_events, time_sig_events, req.key_tonic);
  instance->midi_bytes = writer.toBytes();

  // total_bars is meter-aware: round up by the actual bar length.
  const uint16_t total_bars =
      ticks_per_bar == 0 ? 0
                         : static_cast<uint16_t>((total_ticks + ticks_per_bar - 1) / ticks_per_bar);

  // 11. Homepage events JSON.
  bach::KeySignature key_sig{req.key_tonic, req.is_minor};
  bach::composer::HomepageMeta meta;
  meta.form_name = bach::formTypeToString(req.form);
  meta.key_name = bach::keySignatureToString(key_sig);
  meta.bpm = req.bpm;
  meta.seed = req.seed;
  meta.total_ticks = total_ticks;
  meta.total_bars = total_bars;
  const auto form_idx = static_cast<std::size_t>(req.form);
  const char* form_display = form_idx < kFormCount ? kFormDisplayNames[form_idx] : "Composition";
  meta.description = std::string(form_display) + " in " + meta.key_name;
  instance->events_json = bach::composer::buildHomepageEventsJson(result, meta);

  // 12. Cache info.
  instance->form = req.form;
  instance->key = key_sig;
  instance->bpm = req.bpm;
  instance->seed_used = req.seed;
  instance->total_ticks = total_ticks;
  instance->total_bars = total_bars;
  instance->track_count = static_cast<uint8_t>(result.tracks.size());
  instance->has_result = true;
  return BACH_OK;
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

  auto* result = static_cast<BachEventData*>(malloc(sizeof(BachEventData)));
  if (!result)
    return nullptr;

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
  if (!handle)
    return &s_info;

  auto* instance = static_cast<BachInstance*>(handle);
  if (!instance->has_result)
    return &s_info;

  s_info.total_ticks = instance->total_ticks;
  s_info.total_bars = instance->total_bars;
  s_info.bpm = instance->bpm;
  s_info.track_count = instance->track_count;
  s_info.seed_used = instance->seed_used;

  return &s_info;
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
  return kFormDisplayNames[id];
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
