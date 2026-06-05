// C API for WASM and FFI bindings.

#ifndef BACH_C_H
#define BACH_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Handle and Error Definitions
// ============================================================================

/// @brief Opaque handle to a Bach generator instance.
typedef void* BachHandle;

/// @brief Error codes returned by API functions.
typedef enum {
  BACH_OK = 0,
  BACH_ERROR_INVALID_PARAM = 1,
  BACH_ERROR_GENERATION_FAILED = 2,
  BACH_ERROR_INVALID_FORM = 3,
  BACH_ERROR_INVALID_KEY = 4,
  BACH_ERROR_INVALID_CHARACTER = 5,
  BACH_ERROR_INVALID_INSTRUMENT = 6,
  BACH_ERROR_INCOMPATIBLE_CHARACTER_FORM = 7,
  BACH_ERROR_INVALID_CONFIG = 8,
} BachError;

// ============================================================================
// Output Data Structures
// ============================================================================

/// @brief MIDI binary output.
typedef struct {
  uint8_t* data;  ///< MIDI binary data
  size_t size;    ///< Size in bytes
} BachMidiData;

/// @brief Event JSON output.
typedef struct {
  char* json;     ///< JSON string
  size_t length;  ///< String length
} BachEventData;

/// @brief Generation info.
typedef struct {
  uint16_t total_bars;   ///< Total number of bars
  uint32_t total_ticks;  ///< Total duration in ticks
  uint16_t bpm;          ///< Actual BPM used
  uint8_t track_count;   ///< Number of tracks
  uint32_t seed_used;    ///< Seed used for generation
} BachInfo;

// ============================================================================
// Lifecycle
// ============================================================================

/// @brief Create a new Bach generator instance.
/// @return Handle (must be freed with bach_destroy)
BachHandle bach_create(void);

/// @brief Destroy a Bach generator instance.
/// @param handle Handle to destroy
void bach_destroy(BachHandle handle);

// ============================================================================
// Generation
// ============================================================================

/// @brief Generate a Bach composition from a JSON config string.
///
/// Drives the composer subsystem (the WASM product path). Internal generation
/// runs in C; transposition to the requested key happens only at MIDI output.
///
/// JSON fields:
///   form: string ("fugue", "prelude_and_fugue", ...) or number (0-9).
///         Strict: an unknown name or out-of-range number is rejected with
///         BACH_ERROR_INVALID_FORM.
///   key: number (0-11, pitch class). Out of range => BACH_ERROR_INVALID_KEY.
///   is_minor: boolean (default false).
///   num_voices: number. ACCEPTED AND IGNORED for backward compatibility; the
///         form now decides the voice count. Never an error.
///   bpm: number. 0 or absent => default 100. Otherwise must be in [40, 200],
///         else BACH_ERROR_INVALID_CONFIG.
///   seed: number. 0 or absent => a random non-zero seed is generated; the
///         resolved value is reported via BachInfo::seed_used.
///   character: string ("severe", "playful", "noble", "restless") or number
///         (0-3). Strict: unknown name / out-of-range number =>
///         BACH_ERROR_INVALID_CHARACTER.
///   instrument: string ("organ", "violin", "cello", ...) or number (0-5).
///         Absent => default for the form. Strict: unknown name /
///         out-of-range number => BACH_ERROR_INVALID_INSTRUMENT.
///   scale: string ("short", "medium", "long", "full") or number (0-3).
///         Strict: unknown => BACH_ERROR_INVALID_CONFIG. Default "short".
///   target_bars: number (0 = use scale, >0 = override scale).
///
/// @param handle Bach handle
/// @param json JSON config string
/// @param length Length of the JSON string
/// @return BACH_OK on success
BachError bach_generate_from_json(BachHandle handle, const char* json, size_t length);

// ============================================================================
// Output Retrieval
// ============================================================================

/// @brief Get generated MIDI data.
/// @param handle Bach handle
/// @return MidiData (must be freed with bach_free_midi)
BachMidiData* bach_get_midi(BachHandle handle);

/// @brief Free MIDI data.
/// @param data Pointer returned by bach_get_midi
void bach_free_midi(BachMidiData* data);

/// @brief Get event data as JSON.
/// @param handle Bach handle
/// @return EventData (must be freed with bach_free_events)
BachEventData* bach_get_events(BachHandle handle);

/// @brief Free event data.
/// @param data Pointer returned by bach_get_events
void bach_free_events(BachEventData* data);

/// @brief Get generation info.
/// @param handle Bach handle
/// @return Pointer to static BachInfo (valid until next call, do not free)
BachInfo* bach_get_info(BachHandle handle);

// ============================================================================
// Form Enumeration
// ============================================================================

/// @brief Get number of form types. @return Count
uint8_t bach_form_count(void);

/// @brief Get form internal name. @param id Form ID @return Name (e.g. "fugue")
const char* bach_form_name(uint8_t id);

/// @brief Get form display name. @param id Form ID @return Display name (e.g. "Prelude and Fugue")
const char* bach_form_display(uint8_t id);

// ============================================================================
// Instrument Enumeration
// ============================================================================

/// @brief Get number of instrument types. @return Count
uint8_t bach_instrument_count(void);

/// @brief Get instrument name. @param id Instrument ID @return Name (e.g. "organ")
const char* bach_instrument_name(uint8_t id);

// ============================================================================
// Character Enumeration
// ============================================================================

/// @brief Get number of subject characters. @return Count
uint8_t bach_character_count(void);

/// @brief Get character name. @param id Character ID @return Name (e.g. "severe")
const char* bach_character_name(uint8_t id);

// ============================================================================
// Key Enumeration
// ============================================================================

/// @brief Get number of keys. @return Count (12)
uint8_t bach_key_count(void);

/// @brief Get key name. @param id Key ID (0-11) @return Name (e.g. "C", "C#", "D")
const char* bach_key_name(uint8_t id);

// ============================================================================
// Scale Enumeration
// ============================================================================

/// @brief Get number of duration scales. @return Count (4)
uint8_t bach_scale_count(void);

/// @brief Get scale name. @param id Scale ID (0-3) @return Name (e.g. "short")
const char* bach_scale_name(uint8_t id);

// ============================================================================
// Default Instrument
// ============================================================================

/// @brief Get default instrument for a form type.
/// @param form_id Form ID
/// @return Instrument ID
uint8_t bach_default_instrument_for_form(uint8_t form_id);

// ============================================================================
// Error Handling
// ============================================================================

/// @brief Get error message for error code.
/// @param error Error code
/// @return Error message (static, do not free)
const char* bach_error_string(BachError error);

// ============================================================================
// Utilities
// ============================================================================

/// @brief Get library version string.
/// @return Version (e.g., "0.1.0")
const char* bach_version(void);

#ifdef __cplusplus
}
#endif

#endif  // BACH_C_H
