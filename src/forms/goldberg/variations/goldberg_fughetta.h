// Fughetta and alla breve fugal variation generator for Goldberg Variations.

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_FUGHETTA_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_FUGHETTA_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of fughetta or alla breve fugal variation generation.
struct FughettaResult {
  std::vector<NoteEvent> notes;
  bool success = false;
};

/// @brief Generates fughetta (Var 10) and alla breve fugal (Var 22) variations.
///
/// Uses SoggettoGenerator to create a 2-bar soggetto aligned to the structural
/// grid, then builds a 4-voice fugue exposition followed by episodic development
/// using inversions and diatonic sequences.
///
/// Variation profiles:
///   - Var 10: Fughetta, 4 voices, 3/4, Playful character
///   - Var 22: AllaBreveFugal, 4 voices, 2/2, Severe character (stile antico)
class FughettaGenerator {
 public:
  /// @brief Generate a fughetta or alla breve fugal variation.
  /// @param variation_number Variation number (10 or 22).
  /// @param grid 32-bar structural grid for harmonic alignment.
  /// @param key Key signature for the variation.
  /// @param time_sig Time signature (3/4 for Var 10, 2/2 for Var 22).
  /// @param seed Random seed for deterministic generation.
  /// @return FughettaResult with notes across 4 voices and success status.
  FughettaResult generate(int variation_number,
                          const GoldbergStructuralGrid& grid,
                          const KeySignature& key,
                          const TimeSignature& time_sig,
                          uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_FUGHETTA_H
