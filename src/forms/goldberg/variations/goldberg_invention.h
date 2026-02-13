// 3-voice invention (Sinfonia-style) variation generator for Goldberg Variations.

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_INVENTION_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_INVENTION_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of invention (Sinfonia-style) variation generation.
struct InventionResult {
  std::vector<NoteEvent> notes;  ///< All notes (3 voices) for the variation.
  bool success = false;
};

/// @brief Generates 3-voice invention (Sinfonia-style) variations.
///
/// Uses SoggettoGenerator for a 1-2 bar soggetto with Playful character,
/// then builds imitative counterpoint across 3 voices. Less strict than
/// Fughetta -- the Sinfonia style allows freer counterpoint and more
/// motivic play (inversions, sequences, episodes with Figurenlehre patterns).
///
/// Variation profile:
///   - Var 2: Invention, 3 voices, 3/4, SubjectCharacter::Playful
class InventionGenerator {
 public:
  /// @brief Generate a 3-voice invention variation.
  /// @param grid 32-bar structural grid for harmonic alignment.
  /// @param key Key signature for the variation.
  /// @param time_sig Time signature (3/4 for Var 2).
  /// @param seed Random seed for deterministic generation.
  /// @return InventionResult with notes across 3 voices and success status.
  InventionResult generate(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_INVENTION_H
