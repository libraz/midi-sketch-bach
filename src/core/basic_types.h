// Basic types for Bach MIDI generation

#ifndef BACH_CORE_BASIC_TYPES_H
#define BACH_CORE_BASIC_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/note_source.h"

namespace bach {

/// Tick type for MIDI timing (absolute tick position).
using Tick = uint32_t;

/// Fundamental timing constants.
constexpr Tick kTicksPerBeat = 480;
constexpr uint8_t kBeatsPerBar = 4;
constexpr Tick kTicksPerBar = kTicksPerBeat * kBeatsPerBar;  // 1920

// ---------------------------------------------------------------------------
// Duration constants (based on kTicksPerBeat)
// ---------------------------------------------------------------------------

namespace duration {

constexpr Tick kWholeNote = kTicksPerBar;                           // 1920
constexpr Tick kHalfNote = kTicksPerBeat * 2;                      // 960
constexpr Tick kDottedQuarter = kTicksPerBeat + kTicksPerBeat / 2; // 720
constexpr Tick kQuarterNote = kTicksPerBeat;                       // 480
constexpr Tick kEighthNote = kTicksPerBeat / 2;                    // 240
constexpr Tick kSixteenthNote = kTicksPerBeat / 4;                 // 120
constexpr Tick kThirtySecondNote = kTicksPerBeat / 8;              // 60

}  // namespace duration
constexpr uint8_t kMidiC4 = 60;

// ---------------------------------------------------------------------------
// Tick / Bar / Beat conversions
// ---------------------------------------------------------------------------

/// @brief Convert tick to bar number (0-based).
inline constexpr Tick tickToBar(Tick tick) { return tick / kTicksPerBar; }

/// @brief Convert tick to beat number (0-based, global).
inline constexpr Tick tickToBeat(Tick tick) { return tick / kTicksPerBeat; }

/// @brief Get tick position within current bar.
inline constexpr Tick positionInBar(Tick tick) { return tick % kTicksPerBar; }

/// @brief Get beat index within current bar (0-3 for 4/4).
inline constexpr uint8_t beatInBar(Tick tick) {
  return static_cast<uint8_t>(positionInBar(tick) / kTicksPerBeat);
}

/// @brief Convert bar number to tick (start of bar).
inline constexpr Tick barToTick(Tick bar) { return bar * kTicksPerBar; }

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------

/// Voice identifier for counterpoint voices.
using VoiceId = uint8_t;

// ---------------------------------------------------------------------------
// Enums: Organ system
// ---------------------------------------------------------------------------

/// Voice roles for organ fugue (immutable once assigned).
/// See ImmutableContext -- roles never change at runtime.
enum class VoiceRole : uint8_t {
  Assert,   // Subject voice (dux)
  Respond,  // Answer voice (comes)
  Propel,   // Free counterpoint drive
  Ground    // Pedal / bass foundation
};

/// @brief Convert VoiceRole to human-readable string.
const char* voiceRoleToString(VoiceRole role);

/// Fugue phases define the global time axis. Order is strictly monotonic:
/// Establish -> Develop -> Resolve. Never reversed.
enum class FuguePhase : uint8_t {
  Establish,  // Exposition
  Develop,    // Middle entries, episodes
  Resolve     // Final entries, coda
};

/// @brief Convert FuguePhase to human-readable string.
const char* fuguePhaseToString(FuguePhase phase);

/// Subject character affects the personality of the entire fugue.
/// Phase restrictions: Severe/Playful (Ph1-2), Noble (Ph3+), Restless (Ph4+).
enum class SubjectCharacter : uint8_t {
  Severe,
  Playful,
  Noble,
  Restless
};

/// @brief Convert SubjectCharacter to human-readable string.
const char* subjectCharacterToString(SubjectCharacter character);

// ---------------------------------------------------------------------------
// Enums: Solo String Flow system
// ---------------------------------------------------------------------------

/// Arc phases for solo string flow pieces (e.g. BWV 1007).
/// Peak is exactly 1 section, config-fixed (seed-independent).
enum class ArcPhase : uint8_t {
  Ascent,
  Peak,
  Descent
};

/// @brief Convert ArcPhase to human-readable string.
const char* arcPhaseToString(ArcPhase phase);

// ---------------------------------------------------------------------------
// Enums: Solo String Arch system
// ---------------------------------------------------------------------------

/// Variation roles for arch-form pieces (e.g. Chaconne BWV 1004).
/// Fixed order. Accumulate = exactly 3. Resolve = Theme only.
enum class VariationRole : uint8_t {
  Establish,
  Develop,
  Destabilize,
  Illuminate,
  Accumulate,
  Resolve
};

/// @brief Convert VariationRole to human-readable string.
const char* variationRoleToString(VariationRole role);

// ---------------------------------------------------------------------------
// Enums: Shared
// ---------------------------------------------------------------------------

/// Failure classification for validation and fail-fast.
enum class FailKind : uint8_t {
  StructuralFail,  // Structure order violation
  MusicalFail,     // Counterpoint / harmony violation
  ConfigFail       // Configuration error
};

/// @brief Convert FailKind to human-readable string.
const char* failKindToString(FailKind kind);

/// Form types supported by the generator.
enum class FormType : uint8_t {
  Fugue,
  PreludeAndFugue,
  TrioSonata,
  ChoralePrelude,
  ToccataAndFugue,
  Passacaglia,
  FantasiaAndFugue,
  CelloPrelude,
  Chaconne
};

/// @brief Convert FormType to human-readable string.
const char* formTypeToString(FormType form);

/// @brief Parse FormType from string (e.g. "fugue", "prelude_and_fugue").
/// @param str String representation of form type.
/// @return Parsed FormType, defaults to FormType::Fugue on unrecognized input.
FormType formTypeFromString(const std::string& str);

/// Musical key (pitch class of tonic).
enum class Key : uint8_t {
  C = 0, Cs, D, Eb, E, F, Fs, G, Ab, A, Bb, B
};

/// @brief Convert Key to human-readable string.
const char* keyToString(Key key);

/// Instrument type for output routing and range constraints.
enum class InstrumentType : uint8_t {
  Organ,
  Harpsichord,
  Piano,
  Violin,
  Cello,
  Guitar
};

/// @brief Convert InstrumentType to human-readable string.
const char* instrumentTypeToString(InstrumentType inst);

/// Scale type for pitch generation.
enum class ScaleType : uint8_t {
  Major,
  NaturalMinor,
  HarmonicMinor,
  MelodicMinor,
  Dorian,
  Mixolydian
};

/// Duration scale for controlling piece length.
/// Short = current defaults (backward-compatible), Full = historical length.
enum class DurationScale : uint8_t {
  Short,   ///< Current default (~2.5 min chaconne, ~1.3 min fugue)
  Medium,  ///< Moderate length
  Long,    ///< Near historical length
  Full     ///< Full historical length
};

/// @brief Convert DurationScale to human-readable string.
const char* durationScaleToString(DurationScale scale);

/// @brief Parse DurationScale from string (e.g. "short", "medium", "long", "full").
/// @return Parsed DurationScale. Defaults to DurationScale::Short on unrecognized input.
DurationScale durationScaleFromString(const std::string& str);

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// Raw MIDI event for SMF output.
struct MidiEvent {
  Tick tick = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
};

/// Note event -- the core musical unit throughout the pipeline.
struct NoteEvent {
  Tick start_tick = 0;
  Tick duration = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 80;
  VoiceId voice = 0;
  BachNoteSource source = BachNoteSource::Unknown;  ///< Provenance source for debugging.
  uint8_t bow_direction = 0;  ///< BowDirection cast (0=Natural, 1=Down, 2=Up).
  uint8_t is_harmonic = 0;    ///< 0=normal, 1=natural harmonic.
};

/// Track: a collection of note events on a single MIDI channel.
struct Track {
  uint8_t channel = 0;
  uint8_t program = 0;  // GM program number
  std::string name;
  std::vector<NoteEvent> notes;
  std::vector<MidiEvent> events;  // Raw MIDI events (CC, pitch bend, etc.)
};

/// Tempo change event.
struct TempoEvent {
  Tick tick = 0;
  uint16_t bpm = 120;
};

/// Text or marker event embedded in MIDI.
struct TextEvent {
  Tick tick = 0;
  std::string text;
};

}  // namespace bach

#endif  // BACH_CORE_BASIC_TYPES_H
