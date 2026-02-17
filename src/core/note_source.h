// Note provenance tracking for debugging and analysis.

#ifndef BACH_CORE_NOTE_SOURCE_H
#define BACH_CORE_NOTE_SOURCE_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace bach {

/// Source of a generated note (for debugging provenance).
/// Maps to the "provenance.source" field in output.json.
enum class BachNoteSource : uint8_t {
  Unknown = 0,
  FugueSubject,     // Subject entry
  SubjectCore,      // Subject core Kerngestalt notes (immutable identity)
  FugueAnswer,      // Answer (Real/Tonal)
  Countersubject,   // Countersubject
  EpisodeMaterial,  // Episode motif
  FreeCounterpoint, // Free counterpoint
  CantusFixed,      // Cantus firmus (immutable)
  Ornament,         // Trill, mordent, turn
  PedalPoint,       // Pedal point
  ArpeggioFlow,     // Flow arpeggio (BWV1007)
  TextureNote,      // Arch texture (BWV1004)
  GroundBass,       // Ground bass (immutable)
  CollisionAvoid,   // Modified by collision avoidance
  PostProcess,      // Modified by post-processing
  ChromaticPassing, // Chromatic passing tone between chord tones
  FalseEntry,       // Truncated subject opening that diverges to free counterpoint
  Coda,             // Coda design value (Principle 4: immutable)
  SequenceNote,     // Diatonic sequence note (structural protection)
  CanonDux,        ///< Canon leader voice.
  CanonComes,      ///< Canon follower, derived from dux.
  CanonFreeBass,   ///< Free bass in canon variation.
  GoldbergAria,    ///< Aria theme.
  GoldbergBass,    ///< Realized bass line.
  GoldbergFigura,  ///< Elaboratio figura pattern (Figurenlehre).
  GoldbergSoggetto, ///< Inventio soggetto (short subject for canon/fughetta/invention).
  GoldbergDance,   ///< Dance variation pattern (Passepied, Gigue, Sarabande).
  GoldbergFughetta, ///< Fughetta/alla breve fugal variation entry.
  GoldbergInvention, ///< Invention/Sinfonia variation free counterpoint.
  QuodlibetMelody,  ///< Folk melody in Quodlibet.
  GoldbergOverture,  ///< French Overture variation (Var 16 Grave + Fugato).
  GoldbergSuspension, ///< BlackPearl (Var 25) suspension-driven notes.
  ChaconneBass,       ///< Realized chaconne bass line (structural, role-dependent).
  PreludeFiguration,  ///< Harmony-first prelude figuration note (structurally consonant).
  ToccataGesture,     ///< Stylus Phantasticus gesture note (structural, octave shift only).
  ToccataFigure,      ///< Vocabulary figure note (semi-protected, octave shift only).
  GrandPause,          ///< Structural silence marker (immutable).
  CadenceApproach      ///< Notes shaped by cadence approach formulas.
};

/// @brief Convert BachNoteSource to human-readable string.
/// @param source The note source enum value.
/// @return Null-terminated string representation.
const char* bachNoteSourceToString(BachNoteSource source);

/// @brief Protection level for collision resolution.
/// Determines how aggressively the resolver may modify a note's pitch.
enum class ProtectionLevel : uint8_t {
  Immutable,      ///< No pitch change allowed (subject core, cantus, ground bass).
  Architectural,  ///< Pitch, octave, duration, tick all immutable (cadence final notes).
  SemiImmutable,  ///< Octave shift only, pitch class preserved (subject entries).
  Structural,     ///< Octave shift only (answer, countersubject, pedal point).
  Flexible        ///< Full 5-stage cascade (free counterpoint, episodes, ornaments).
};

/// @brief Get the protection level for a given note source.
/// @param source The note source enum value.
/// @return Protection level governing collision resolution behavior.
ProtectionLevel getProtectionLevel(BachNoteSource source);

/// @brief Check if source is a structural (identity-preserving) note type.
/// Structural notes are subject, answer, countersubject, pedal, false entry, coda,
/// and sequence notes. They pass through post-processing without pitch alteration.
inline bool isStructuralSource(BachNoteSource source) {
  return source == BachNoteSource::SubjectCore ||
         source == BachNoteSource::FugueSubject ||
         source == BachNoteSource::FugueAnswer ||
         source == BachNoteSource::PedalPoint ||
         source == BachNoteSource::Countersubject ||
         source == BachNoteSource::FalseEntry ||
         source == BachNoteSource::Coda ||
         source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::CanonDux ||
         source == BachNoteSource::CanonComes ||
         source == BachNoteSource::GoldbergAria ||
         source == BachNoteSource::ToccataFigure ||
         source == BachNoteSource::CadenceApproach;
}

/// @brief Return the coordination priority tier for a note source.
/// Tier 0 = immutable (subject, answer, pedal, canon, aria, coda).
/// Tier 1 = semi-fixed (countersubject, episode, false entry, sequence).
/// Tier 2 = fully flexible (free counterpoint, ornament, etc.).
/// Lower value = higher priority in coordinateVoices and post-validation sort.
inline int sourcePriority(BachNoteSource source) {
  switch (source) {
    case BachNoteSource::FugueSubject:
    case BachNoteSource::FugueAnswer:
    case BachNoteSource::SubjectCore:
    case BachNoteSource::PedalPoint:
    case BachNoteSource::CanonDux:
    case BachNoteSource::CanonComes:
    case BachNoteSource::GoldbergAria:
    case BachNoteSource::Coda:
    case BachNoteSource::GrandPause:
    case BachNoteSource::CadenceApproach:
      return 0;  // Tier 1: immutable (design values, never modified)
    case BachNoteSource::ToccataGesture:
    case BachNoteSource::ToccataFigure:
      return 1;  // Tier 2: semi-fixed (gesture notes, octave shift only)
    case BachNoteSource::Countersubject:
    case BachNoteSource::EpisodeMaterial:
    case BachNoteSource::FalseEntry:
    case BachNoteSource::SequenceNote:
      return 1;  // Tier 2: semi-fixed
    default:
      return 2;  // Tier 3: fully flexible
  }
}

/// @brief Check if a voice index is the pedal voice for a given voice count.
/// Organ pedal is the last voice when num_voices >= 3.
inline bool isPedalVoice(uint8_t voice, uint8_t num_voices) {
  return num_voices >= 3 && voice == num_voices - 1;
}

/// Transformation steps applied to a note during generation.
/// Recorded in provenance for debugging the transform pipeline.
enum class BachTransformStep : uint8_t {
  None = 0,
  TonalAnswer,    // Subject -> tonal answer transformation
  RealAnswer,     // Subject -> real answer (transposition)
  Inversion,      // Melodic inversion
  Retrograde,     // Retrograde
  Augmentation,   // Rhythmic augmentation
  Diminution,     // Rhythmic diminution
  Sequence,       // Sequential transposition
  CollisionAvoid, // Collision avoidance adjustment
  RangeClamp,     // Range clamping
  OctaveAdjust,   // Octave transposition
  KeyTranspose    // Key transposition at output
};

/// @brief Bit flags recording which post-processing steps modified a note.
/// Multiple flags can be combined via bitwise OR.
enum class NoteModifiedBy : uint8_t {
  None             = 0,
  ParallelRepair   = 1 << 0,  // Parallel 5th/8ve repair
  ChordToneSnap    = 1 << 1,  // Chord tone / diatonic snap
  LeapResolution   = 1 << 2,  // Leap resolution (contrary step)
  OverlapTrim      = 1 << 3,  // Duration trim
  OctaveAdjust     = 1 << 4,  // Octave shift / register clamp / crossing fix
  Articulation     = 1 << 5,  // Articulation / breath
  RepeatedNoteRep  = 1 << 6,  // Repeated note repair
};

inline NoteModifiedBy operator|(NoteModifiedBy a, NoteModifiedBy b) {
  return static_cast<NoteModifiedBy>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline NoteModifiedBy& operator|=(NoteModifiedBy& a, NoteModifiedBy b) {
  a = a | b;
  return a;
}

/// @brief Convert NoteModifiedBy bit flags to comma-separated string.
/// @param flags Raw uint8_t flags value.
/// @return Comma-separated string (e.g. "parallel_repair,octave_adjust"), or "none".
std::string noteModifiedByToString(uint8_t flags);

/// @brief Convert BachTransformStep to human-readable string.
/// @param step The transform step enum value.
/// @return Null-terminated string representation.
const char* bachTransformStepToString(BachTransformStep step);

/// Provenance record for a single note.
/// Tracks the origin and every transformation applied, enabling
/// root-cause analysis when a note sounds wrong.
struct NoteProvenance {
  BachNoteSource source = BachNoteSource::Unknown;
  uint8_t original_pitch = 0;
  int8_t chord_degree = -1;    // -1 = unassigned
  uint32_t lookup_tick = 0;    // Harmonic timeline lookup tick
  uint8_t entry_number = 0;    // Fugue entry number (0 = not applicable)

  static constexpr size_t kMaxSteps = 8;
  BachTransformStep steps[kMaxSteps] = {};
  uint8_t step_count = 0;

  /// @brief Append a transform step to the provenance trail.
  /// @param step The transformation that was applied.
  /// @return true if added, false if step buffer is full.
  bool addStep(BachTransformStep step) {
    if (step_count >= kMaxSteps) return false;
    steps[step_count++] = step;
    return true;
  }

  /// @brief Check whether this provenance has a meaningful source.
  /// @return true if source is not Unknown.
  bool hasProvenance() const { return source != BachNoteSource::Unknown; }
};

struct NoteEvent;  // Forward declaration (defined in basic_types.h).

/// @brief Count notes with Unknown source in a note collection.
/// @param notes The notes to inspect.
/// @return Number of notes whose source is BachNoteSource::Unknown.
int countUnknownSource(const std::vector<NoteEvent>& notes);

}  // namespace bach

#endif  // BACH_CORE_NOTE_SOURCE_H
