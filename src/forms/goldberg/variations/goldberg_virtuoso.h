// Virtuoso variation generator for Goldberg Variations (Var 11, 23, 29).

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_VIRTUOSO_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_VIRTUOSO_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of virtuoso variation generation.
struct VirtuosoResult {
  std::vector<NoteEvent> notes;  ///< All notes for the variation.
  bool success = false;          ///< True if generation succeeded.
};

/// @brief Generates virtuoso variations: Toccata (Var 11), ScalePassage (Var 23),
///        BravuraChordal (Var 29 -- BWV 988 climax).
///
/// Each variation uses a distinct FiguraProfile and generation approach:
/// - Var 11 (Toccata): Wide arpeggio spread, 2 voices, alternating direction.
/// - Var 23 (ScalePassage): Rapid scale runs (Tirata), 2 voices, ascending bias.
/// - Var 29 (BravuraChordal): BWV 988 CLIMAX, 3-4 voice chordal texture with
///   increased density and wider dynamic range (Bariolage register alternation).
class VirtuosoGenerator {
 public:
  /// @brief Generate a virtuoso variation.
  /// @param variation_number The variation number (11, 23, or 29).
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for scale-aware pitch generation.
  /// @param time_sig Time signature for tick calculation.
  /// @param seed Random seed for deterministic generation.
  /// @return VirtuosoResult with notes and success status.
  ///         Returns success=false for unsupported variation numbers.
  VirtuosoResult generate(
      int variation_number,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

 private:
  /// @brief Build the FiguraProfile for a given variation number.
  /// @param variation_number The variation number (11, 23, or 29).
  /// @param[out] type Set to the GoldbergVariationType for this variation.
  /// @return FiguraProfile with variation-specific parameters.
  static FiguraProfile buildProfile(int variation_number,
                                    GoldbergVariationType& type);

  /// @brief Check if a variation number is a supported virtuoso variation.
  /// @param variation_number The variation number to check.
  /// @return True if the variation number is 11, 23, or 29.
  static bool isSupportedVariation(int variation_number);

  /// @brief Get the number of voices for a variation.
  /// @param variation_number The variation number.
  /// @return Number of voices (2 for Var 11/23, 3-4 for Var 29).
  static uint8_t getVoiceCount(int variation_number);

  /// @brief Generate bass line from the structural grid.
  /// @param grid The 32-bar structural grid.
  /// @param time_sig Time signature for bar duration.
  /// @param bass_voice Voice index for the bass line.
  /// @return Vector of bass NoteEvents spanning 32 bars.
  std::vector<NoteEvent> generateBassLine(
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig,
      uint8_t bass_voice) const;

  /// @brief Apply climax intensification for Var 29 (BWV 988 climax).
  ///
  /// Increases velocity, widens register, and boosts note density for the
  /// BravuraChordal climax variation.
  ///
  /// @param notes Notes to intensify (modified in place).
  /// @param grid Structural grid for tension-aware scaling.
  /// @param time_sig Time signature for bar-boundary calculation.
  void applyClimaxIntensification(
      std::vector<NoteEvent>& notes,
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_VIRTUOSO_H
