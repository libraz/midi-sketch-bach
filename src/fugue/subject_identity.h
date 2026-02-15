// Subject identity: 3-layer structure for Kerngestalt preservation.
//
// Layer 1 (Essential): Computed at generate(), immutable thereafter.
// Layer 2 (Derived): Pre-computed transformations, immutable.
// Layer 3 (Contextual): Lazily computed on demand by consuming components.

#ifndef BACH_FUGUE_SUBJECT_IDENTITY_H
#define BACH_FUGUE_SUBJECT_IDENTITY_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Kerngestalt type classification for fugue subjects.
///
/// Each type defines a different "nuclear shape" -- the 2-4 note cell
/// that makes the subject perceptually identifiable even under transformation.
enum class KerngestaltType : uint8_t {
  IntervalDriven,   ///< Type A: Signature leap (3rd+), BWV 542 style.
  ChromaticCell,    ///< Type B: Semitone neighbor, BWV 548 style.
  Arpeggio,         ///< Type C: Chord tone sequence, BWV 547 style.
  Linear            ///< Type D: Scale-based with internal repetition, BWV 578 style.
};

/// @brief Convert KerngestaltType to string.
const char* kerngestaltTypeToString(KerngestaltType type);

/// @brief Accent position classification for identity preservation.
enum class AccentPosition : uint8_t {
  Strong,  ///< Falls on bar or beat start.
  Weak     ///< Falls on offbeat position.
};

/// @brief Index range of the Kerngestalt cell within the subject note sequence.
struct CellWindow {
  size_t start_idx = 0;  ///< First note of the core cell (inclusive).
  size_t end_idx = 0;    ///< Last note of the core cell (inclusive).
  bool valid = false;
};

/// @brief Essential Identity (Layer 1) -- computed at generate(), immutable.
///
/// The three perceptual identity elements:
///   1. core_intervals (directed interval pattern)
///   2. core_rhythm (rhythmic pattern, inseparable from intervals)
///   3. accent_pattern (strong/weak beat placement)
struct EssentialIdentity {
  std::vector<int> core_intervals;             ///< Directed intervals between consecutive notes.
  std::vector<Tick> core_rhythm;               ///< Duration of each note.
  std::vector<AccentPosition> accent_pattern;  ///< Beat strength per note.

  int signature_interval = 0;   ///< Most characteristic interval (largest non-step).
  KerngestaltType kerngestalt_type = KerngestaltType::Linear;

  std::vector<int> head_fragment_intervals;    ///< First 2-3 note intervals.
  std::vector<Tick> head_fragment_rhythm;      ///< First 2-3 note durations.
  std::vector<int> tail_fragment_intervals;    ///< Last 2-3 note intervals.
  std::vector<Tick> tail_fragment_rhythm;      ///< Last 2-3 note durations.
  size_t natural_break_point = 0;              ///< Index splitting subject into head/tail.
  CellWindow cell_window;  ///< Kerngestalt cell window (set by generator, used for protection).

  /// @brief Check whether this identity has been populated.
  bool isValid() const { return !core_intervals.empty(); }
};

/// @brief Derived Transformations (Layer 2) -- pre-computed, immutable.
struct DerivedTransformations {
  std::vector<int> inverted_intervals;     ///< Melodic inversion of core_intervals.
  std::vector<Tick> inverted_rhythm;       ///< Rhythm preserved under inversion.
  std::vector<int> retrograde_intervals;   ///< Reversed interval sequence.
  std::vector<Tick> retrograde_rhythm;     ///< Reversed rhythm sequence.
  std::vector<Tick> augmented_rhythm;      ///< Doubled durations.
  std::vector<Tick> diminished_rhythm;     ///< Halved durations.

  /// @brief Check whether transformations have been computed.
  bool isValid() const { return !inverted_intervals.empty(); }
};

/// @brief Complete subject identity -- Layers 1 + 2.
///
/// Computed once in SubjectGenerator::generate() and attached to the Subject.
/// Referenced by all downstream components but never modified after construction.
struct SubjectIdentity {
  EssentialIdentity essential;
  DerivedTransformations derived;

  /// @brief Check whether the identity has been fully constructed.
  bool isValid() const { return essential.isValid() && derived.isValid(); }
};

/// @brief Validate that a KerngestaltType satisfies its type-specific conditions.
///
/// Each type has minimum requirements:
///   - All types: at least one non-step element in the core
///   - All types: head_fragment interval+rhythm pattern appears at least once in subject
///   - IntervalDriven: signature interval >= 3 semitones
///   - ChromaticCell: has +/-1 semitone motion
///   - Arpeggio: has consecutive chord-tone intervals (3, 4, 5, 7, 8, 9 semitones)
///   - Linear: has internal rhythmic repetition
///
/// @param identity The essential identity to validate.
/// @return True if the Kerngestalt conditions are met for the specified type.
bool isValidKerngestalt(const EssentialIdentity& identity);

/// @brief Build Layer 1 (Essential Identity) from subject notes.
/// @param notes Subject note events.
/// @param key Musical key of the subject.
/// @param is_minor Whether the subject is in minor mode.
/// @return Populated EssentialIdentity.
EssentialIdentity buildEssentialIdentity(const std::vector<NoteEvent>& notes,
                                         Key key, bool is_minor);

/// @brief Build Layer 2 (Derived Transformations) from Layer 1.
/// @param essential The essential identity to derive transformations from.
/// @return Populated DerivedTransformations.
DerivedTransformations buildDerivedTransformations(const EssentialIdentity& essential);

/// @brief Build complete SubjectIdentity (Layers 1 + 2) from subject notes.
/// @param notes Subject note events.
/// @param key Musical key of the subject.
/// @param is_minor Whether the subject is in minor mode.
/// @return Fully populated SubjectIdentity.
SubjectIdentity buildSubjectIdentity(const std::vector<NoteEvent>& notes,
                                      Key key, bool is_minor);

}  // namespace bach

#endif  // BACH_FUGUE_SUBJECT_IDENTITY_H
