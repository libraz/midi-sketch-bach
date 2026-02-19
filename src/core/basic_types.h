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

// ---------------------------------------------------------------------------
// Time Signature (Goldberg Variations support)
// ---------------------------------------------------------------------------

/// @brief Time signature representation for variable meter support.
/// Existing code uses kTicksPerBar=1920 (4/4). TimeSignature::ticksPerBar()
/// provides form-specific calculation. Existing constants are unaffected.
struct TimeSignature {
  uint8_t numerator = 4;
  uint8_t denominator = 4;

  /// @brief Calculate ticks per bar for this time signature.
  constexpr Tick ticksPerBar() const {
    return static_cast<Tick>(numerator) * (kTicksPerBeat * 4 / denominator);
  }

  /// @brief Get notated beats per bar (numerator).
  constexpr uint8_t beatsPerBar() const { return numerator; }

  /// @brief Check if this is a compound time signature (6/8, 9/8, 12/8).
  constexpr bool isCompound() const {
    return numerator % 3 == 0 && numerator > 3;
  }

  /// @brief Get musical pulse count (6/8->2, 9/8->3, 3/4->3).
  constexpr uint8_t pulsesPerBar() const {
    return isCompound() ? numerator / 3 : numerator;
  }
};

/// @brief Time signature change event at a specific tick position.
struct TimeSignatureEvent {
  Tick tick = 0;
  TimeSignature time_sig = {4, 4};
};

/// @brief Metrical strength level for beat hierarchy.
/// Used by canon and variation generators for note placement scoring.
enum class MetricalStrength : uint8_t {
  Strong,  ///< Beat 1: unprepared dissonance penalized.
  Medium,  ///< Beat 2: Sarabande expressive strong beat.
  Weak     ///< Beat 3: relatively free.
};

/// @brief Meter profile for beat emphasis patterns.
/// Sarabande's beat 2 emphasis requires distinct handling.
enum class MeterProfile : uint8_t {
  StandardTriple,   ///< Normal 3/4: Strong-Medium-Weak (most variations).
  SarabandeTriple   ///< Sarabande 3/4: Strong-Strong-Weak (Aria, Var 13 etc).
};

/// @brief Determine metrical strength for a beat position.
/// @param beat_in_bar Zero-based beat index within the bar.
/// @param profile Meter profile (Standard or Sarabande).
/// @return MetricalStrength for the given beat position.
MetricalStrength getMetricalStrength(int beat_in_bar, MeterProfile profile);

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

/// @brief Metric hierarchy level for a given tick position.
/// Used to enforce Baroque counterpoint rules: strong beats require chord tones,
/// beat positions allow NHTs only with resolution, offbeats are unrestricted.
enum class MetricLevel : uint8_t {
  Bar = 2,     ///< Bar start: chord tone required (NHT forbidden).
  Beat = 1,    ///< Beat start: chord tone preferred, NHT needs resolution (next_pitch).
  Offbeat = 0  ///< Off-beat: passing/neighbor tones permitted.
};

/// @brief Determine the metric level of a tick position.
inline constexpr MetricLevel metricLevel(Tick tick) {
  if (tick % kTicksPerBar == 0) return MetricLevel::Bar;
  if (tick % kTicksPerBeat == 0) return MetricLevel::Beat;
  return MetricLevel::Offbeat;
}

/// Returns true if the absolute tick falls on beat 1 or 3 of its bar.
inline bool isStrongBeatInBar(Tick tick) {
  Tick pos = tick % kTicksPerBar;
  return pos == 0 || pos == kTicksPerBeat * 2;
}

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
  Resolve,    // Final entries, coda
  Conclude    // Coda / closing gesture
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
// Enums: Toccata archetypes
// ---------------------------------------------------------------------------

/// Toccata structural archetype. Each archetype defines a distinct section
/// layout, energy curve, and harmonic design. Dispatched via generateToccata().
enum class ToccataArchetype : uint8_t {
  Dramaticus,   ///< BWV 565. U-energy. Gesture->Recitative->Drive.
  Perpetuus,    ///< BWV 538. Ascending energy. Continuous 16th-note moto perpetuo.
  Concertato,   ///< BWV 564. Arch energy. Allegro->Adagio->Vivace.
  Sectionalis   ///< BWV 566. Wave energy. Free->QuasiFugal->Free->Cadenza->Coda.
};

/// @brief Convert ToccataArchetype to human-readable string.
const char* toccataArchetypeToString(ToccataArchetype archetype);

/// @brief Parse ToccataArchetype from string.
/// @param str String such as "dramaticus", "perpetuus", "concertato", "sectionalis".
/// @return Parsed archetype. Defaults to ToccataArchetype::Dramaticus on unrecognized input.
ToccataArchetype toccataArchetypeFromString(const std::string& str);

// ---------------------------------------------------------------------------
// Enums: Fugue archetypes
// ---------------------------------------------------------------------------

/// @brief Archetype classification for fugue subjects.
///
/// Each archetype represents a structural strategy that shapes subject
/// generation, scoring, and development potential.
enum class FugueArchetype : uint8_t {
  Compact,     ///< BWV 578, 847. Short Kopfmotiv, fragmentable for sequences.
  Cantabile,   ///< BWV 542, 538. Through-composed melodic line, smooth motion.
  Invertible,  ///< BWV 548, 550. Mirror inversion and stretto design.
  Chromatic    ///< BWV 537, 686. Chromatic progression as structural element.
};

/// @brief Convert FugueArchetype to string.
const char* fugueArchetypeToString(FugueArchetype archetype);

/// Section identifiers for toccata structural boundaries.
enum class ToccataSectionId : uint8_t {
  // Dramaticus (legacy 3-section)
  Opening, Recitative, Drive,
  // Perpetuus
  Ascent, Plateau, Climax,
  // Concertato
  Allegro, Adagio, Vivace,
  // Sectionalis
  Free1, QuasiFugal, Free2, Cadenza, Coda,
  // Dramaticus 8-phase (new)
  Gesture, EchoCollapse, RecitExpansion, SequenceClimb1,
  HarmonicBreak, SequenceClimb2, DomObsession, FinalExplosion
};

/// @brief Toccata style mode for Dramaticus 8-phase sections.
/// Groups related ToccataSectionId phases into stylistic categories.
enum class ToccataStyleMode : uint8_t {
  Phantasticus,   ///< Gesture, EchoCollapse — rhetorical gestures and echo effects.
  Recitativo,     ///< RecitExpansion — free declamatory style.
  Sequence,       ///< SequenceClimb1/2 — sequential motivic development.
  Transitional    ///< HarmonicBreak, DomObsession, FinalExplosion — harmonic transitions.
};

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
  Chaconne,
  GoldbergVariations
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
// Enums: Gesture system
// ---------------------------------------------------------------------------

/// @brief Role of a note within a multi-note gesture group.
/// Notes sharing the same gesture_id form a single musical gesture.
enum class GestureRole : uint8_t {
  None = 0,      ///< Not part of any gesture.
  Leader,        ///< Primary melodic voice of the gesture.
  OctaveEcho,    ///< Octave doubling of the leader.
  LowerEcho,     ///< Lower register echo of the leader.
  PedalHit,      ///< Pedal strike accompanying the gesture.
  Accumulation   ///< Chord buildup within the gesture.
};

/// @brief Texture function for voice-specific parameter selection.
/// Used to select VoiceProfile parameters based on the musical role
/// of the voice at a given point in the piece.
enum class TextureFunction : uint8_t {
  Subject,           ///< Subject presentation (stricter leap control, identity).
  Countersubject,    ///< Countersubject (slightly relaxed from Subject).
  FreeCounterpoint,  ///< Free counterpoint (filling voice).
  PedalPoint,        ///< Pedal point (long held notes).
  CantusFirmus,      ///< Cantus firmus (chorale melody).
  BassLine,          ///< Normal bass motion.
  Default            ///< Inferred from voice ID.
};

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
  uint8_t modified_by = 0;    ///< NoteModifiedBy bit flags.
  uint16_t gesture_id = 0;    ///< Gesture group id (0 = not part of gesture).
  GestureRole gesture_role = GestureRole::None;  ///< Role within the gesture group.
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
