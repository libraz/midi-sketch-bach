// Ornamental variation and trill etude generator for Goldberg Variations.

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_ORNAMENTAL_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_ORNAMENTAL_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of ornamental/trill etude variation generation.
struct OrnamentalResult {
  std::vector<NoteEvent> notes;  ///< All notes (melody + bass) for the variation.
  bool success = false;
};

/// @brief Generates ornamental variations (Var 1, 5, 13) and trill etudes (Var 14, 28).
///
/// Uses FigurenGenerator for skeleton melody based on variation-specific FiguraProfile,
/// then applies OrnamentEngine for decoration. Ornamental variations use Circulatio
/// or Sarabande figurae; trill etudes use Trillo with additional trill ornaments on
/// structural tones.
class OrnamentalGenerator {
 public:
  /// @brief Generate an ornamental or trill etude variation.
  /// @param variation_number The variation number (1, 5, 13, 14, or 28).
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for scale-aware pitch generation.
  /// @param time_sig Time signature for tick calculation.
  /// @param seed Random seed for deterministic generation.
  /// @return OrnamentalResult with notes and success status.
  ///         Returns success=false for unsupported variation numbers.
  OrnamentalResult generate(
      int variation_number,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

 private:
  /// @brief Build the FiguraProfile for a given variation number.
  /// @param variation_number The variation number (1, 5, 13, 14, or 28).
  /// @param[out] type Set to the GoldbergVariationType (Ornamental or TrillEtude).
  /// @return FiguraProfile with variation-specific parameters.
  static FiguraProfile buildProfile(int variation_number,
                                    GoldbergVariationType& type);

  /// @brief Check if a variation number is a supported ornamental/trill etude.
  /// @param variation_number The variation number to check.
  /// @return true if the variation number is 1, 5, 13, 14, or 28.
  static bool isSupportedVariation(int variation_number);

  /// @brief Generate bass line from the structural grid.
  /// @param grid The 32-bar structural grid.
  /// @param time_sig Time signature for bar duration.
  /// @return Vector of bass NoteEvents spanning 32 bars.
  std::vector<NoteEvent> generateBassLine(
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig) const;

  /// @brief Apply ornaments to melody notes.
  /// @param notes Melody notes to ornament (modified in place via swap).
  /// @param is_trill_etude If true, apply higher trill density for trill etude variations.
  /// @param seed Random seed for ornament engine.
  void applyOrnaments(std::vector<NoteEvent>& notes,
                      bool is_trill_etude,
                      uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_ORNAMENTAL_H
