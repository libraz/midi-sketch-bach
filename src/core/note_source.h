// Note provenance tracking for debugging and analysis.

#ifndef BACH_CORE_NOTE_SOURCE_H
#define BACH_CORE_NOTE_SOURCE_H

#include <cstdint>
#include <cstddef>

namespace bach {

/// Source of a generated note (for debugging provenance).
/// Maps to the "provenance.source" field in output.json.
enum class BachNoteSource : uint8_t {
  Unknown = 0,
  FugueSubject,     // Subject entry
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
  PostProcess       // Modified by post-processing
};

/// @brief Convert BachNoteSource to human-readable string.
/// @param source The note source enum value.
/// @return Null-terminated string representation.
const char* bachNoteSourceToString(BachNoteSource source);

/// @brief Protection level for collision resolution.
/// Determines how aggressively the resolver may modify a note's pitch.
enum class ProtectionLevel : uint8_t {
  Immutable,   ///< No pitch change allowed (subject, cantus, ground bass).
  Structural,  ///< Octave shift only (answer, countersubject, pedal point).
  Flexible     ///< Full 5-stage cascade (free counterpoint, episodes, ornaments).
};

/// @brief Get the protection level for a given note source.
/// @param source The note source enum value.
/// @return Protection level governing collision resolution behavior.
ProtectionLevel getProtectionLevel(BachNoteSource source);

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

}  // namespace bach

#endif  // BACH_CORE_NOTE_SOURCE_H
