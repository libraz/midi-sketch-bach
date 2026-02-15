// Motif template system for organic subject generation.
//
// Each SubjectCharacter maps to a fixed pair of MotifTemplates (A and B).
// Motif A drives toward the GoalTone (climax); Motif B descends from it.
// This replaces random-walk note generation with structured motivic patterns.

#ifndef BACH_FUGUE_MOTIF_TEMPLATE_H
#define BACH_FUGUE_MOTIF_TEMPLATE_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "fugue/subject_identity.h"

namespace bach {

/// @brief Types of motivic patterns for subject construction.
enum class MotifType : uint8_t {
  Scale,      ///< Stepwise motion (2-3 notes in scale degrees).
  Leap,       ///< Leap + contrary step (3rd+ jump then stepwise return).
  Rhythmic,   ///< Rhythmic pattern (repeated pitch with characteristic rhythm).
  Chromatic,  ///< Chromatic motion (half-step crawl, 2-3 notes).
  Sustain     ///< Sustained note (single long-value note).
};

/// @brief Structural function of a note within a motif template.
enum class NoteFunction : uint8_t {
  StructuralTone,  ///< Harmonic skeleton tone.
  PassingTone,     ///< Stepwise connection between structural tones.
  NeighborTone,    ///< Ornamental tone returning to origin.
  LeapTone,        ///< Leap arrival or departure.
  Resolution,      ///< Resolution of a dissonance or leap.
  ClimaxTone,      ///< Climax/peak tone.
  CadentialTone,   ///< Cadence-related tone.
  SequenceHead     ///< Sequence pattern start.
};

/// @brief A fixed motivic pattern template.
///
/// Each template defines a sequence of degree offsets from the starting degree
/// and a corresponding rhythm pattern. Templates are design values (Principle 4)
/// and are never generated randomly.
struct MotifTemplate {
  MotifType type;
  std::vector<int> degree_offsets;       ///< Relative scale degrees from start (0-based).
  std::vector<Tick> durations;           ///< Duration for each note.
  std::vector<NoteFunction> functions;   ///< Structural function per note.
};

/// @brief Goal tone position and pitch for subject climax.
///
/// Design values (Principle 4): fixed per character, not searched.
/// position_ratio: where in the subject the climax falls [0.0, 1.0].
/// pitch_ratio: how high in the available range [0.0, 1.0].
struct GoalTone {
  float position_ratio;  ///< Climax position as fraction of subject length.
  float pitch_ratio;     ///< Climax pitch as fraction of available range.
};

/// @brief Get the GoalTone design values for a given character.
///
/// Applies small RNG-driven variation to the base design values so that
/// different seeds produce subtly different goal tones for the same character.
///
/// @param character Subject character type.
/// @param rng Mersenne Twister for per-seed variation.
/// @return GoalTone with position and pitch ratios (design values + small perturbation).
GoalTone goalToneForCharacter(SubjectCharacter character, std::mt19937& rng);

/// @brief Get GoalTone clamped to archetype bounds.
///
/// Delegates to the base goalToneForCharacter then clamps position_ratio
/// and pitch_ratio to the archetype policy's climax bounds.
///
/// @param character Subject character type.
/// @param rng Mersenne Twister for per-seed variation.
/// @param policy Archetype policy providing climax bounds.
/// @return GoalTone with ratios clamped to archetype bounds.
struct ArchetypePolicy;
GoalTone goalToneForCharacter(SubjectCharacter character, std::mt19937& rng,
                               const ArchetypePolicy& policy);

// ---------------------------------------------------------------------------
// Kerngestalt cell system
// ---------------------------------------------------------------------------

/// @brief Rhythm token for Kerngestalt cell -- preserves "long-short" relationships.
struct RhythmToken {
  enum Kind : uint8_t {
    S,   ///< Short (8th note class).
    M,   ///< Medium (quarter note class).
    L,   ///< Long (half note class).
    DL,  ///< Dotted-long (first of a dotted pair).
    DS,  ///< Dotted-short (second of a dotted pair).
  };
  Kind kind;
  uint8_t base_class = 1;  ///< 0=8th, 1=quarter, 2=half.
};

/// @brief Kerngestalt cell: interval x rhythm pair defining the nuclear shape.
struct KerngestaltCell {
  KerngestaltType type;
  std::vector<int> intervals;       ///< Directed intervals (semitones). len = N.
  std::vector<RhythmToken> rhythm;  ///< Rhythm tokens. len = N+1 (per note).
  bool prefer_strong_beat;          ///< Prefer cell head on beat 0/2.
};

/// @brief Get a core Kerngestalt cell by type and index.
/// @param type Kerngestalt type (IntervalDriven, ChromaticCell, Arpeggio, Linear).
/// @param index Cell index within type (0-3).
/// @return KerngestaltCell definition.
const KerngestaltCell& getCoreCell(KerngestaltType type, int index);

/// @brief Select a KerngestaltType based on character and archetype.
///
/// Uses a weighted mapping table (primary 80% / secondary 20%):
///   - Each (character, archetype) pair maps to a primary and secondary type.
///   - RNG decides which one is selected.
///
/// @param character Subject character type.
/// @param archetype Fugue archetype.
/// @param rng Random number generator.
/// @return Selected KerngestaltType.
KerngestaltType selectKerngestaltType(SubjectCharacter character,
                                      FugueArchetype archetype,
                                      std::mt19937& rng);

/// @brief Get the pair of MotifTemplates (A and B) for a given character.
///
/// Each character has 4 template pairs (16 total). Use template_idx to select
/// among the variants; values are taken modulo 4. This expands subject diversity
/// while keeping fixed design values (Principle 3: reduce generation).
///
/// Motif A is used for the ascending portion (toward the goal tone).
/// Motif B is used for the descending portion (away from the goal tone).
///
/// @param character Subject character type.
/// @param template_idx Index to select among 4 template pairs (taken mod 4).
/// @return Pair of templates: first = Motif A (ascending), second = Motif B (descending).
std::pair<MotifTemplate, MotifTemplate> motifTemplatesForCharacter(
    SubjectCharacter character, uint32_t template_idx = 0);

}  // namespace bach

#endif  // BACH_FUGUE_MOTIF_TEMPLATE_H
