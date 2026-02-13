// Hand-crossing variation generator for Goldberg Variations (Var 8, 17, 20).

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_CROSSING_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_CROSSING_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief 2-manual register assignment for hand-crossing variations.
///
/// Defines the pitch range for each manual (keyboard). The upper and lower
/// manual ranges intentionally overlap in the E3-E4 region, permitting
/// hand-crossing effects at structurally appropriate positions.
struct ManualAssignment {
  uint8_t upper_manual_low = 60;   ///< C4: lowest pitch on upper manual.
  uint8_t upper_manual_high = 84;  ///< C6: highest pitch on upper manual.
  uint8_t lower_manual_low = 36;   ///< C2: lowest pitch on lower manual.
  uint8_t lower_manual_high = 64;  ///< E4: highest pitch on lower manual.
  bool allow_crossing = true;      ///< Whether register crossing is permitted.
};

/// @brief Result of hand-crossing variation generation.
struct CrossingResult {
  std::vector<NoteEvent> notes;  ///< All notes (both manuals) for the variation.
  bool success = false;          ///< True if generation succeeded.
};

/// @brief Generates hand-crossing variations with register-protected dual-manual writing.
///
/// Hand-crossing variations (Var 8, 17, 20) feature rapid register alternation
/// between two manuals, characteristic of 2-manual harpsichord technique.
/// Uses FigurenGenerator with Batterie FiguraType for rapid leap alternation,
/// plus register protection to keep voices in their assigned manual ranges.
///
/// At structurally appropriate positions (Opening, Expansion), the voices may
/// cross registers for dramatic effect. At Cadence positions, crossing is
/// suppressed to ensure harmonic stability.
class CrossingGenerator {
 public:
  /// @brief Generate a complete hand-crossing variation.
  /// @param variation_number The Goldberg variation number (8, 17, or 20).
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for scale-aware pitch generation.
  /// @param time_sig Time signature for tick calculation.
  /// @param seed Random seed for deterministic generation.
  /// @return CrossingResult with generated notes and success status.
  ///         Returns success=false for unsupported variation numbers.
  CrossingResult generate(
      int variation_number,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

 private:
  /// @brief Build the FiguraProfile for a given hand-crossing variation.
  /// @param variation_number The variation number (8, 17, or 20).
  /// @return FiguraProfile with Batterie primary type and variation-specific parameters.
  static FiguraProfile buildProfile(int variation_number);

  /// @brief Get the ManualAssignment for a given variation number.
  /// @param variation_number The variation number (8, 17, or 20).
  /// @return ManualAssignment with variation-specific register bounds.
  static ManualAssignment getManualAssignment(int variation_number);

  /// @brief Check whether register crossing is allowed at a given phrase position.
  /// @param pos The current phrase position within the 4-bar group.
  /// @return True if crossing is permitted (Opening, Expansion); false at Cadence.
  static bool isCrossingAllowed(PhrasePosition pos);

  /// @brief Apply register protection, clamping notes to their assigned manual range.
  /// @param notes Notes to clamp (modified in place).
  /// @param manual The manual assignment defining valid register bounds.
  /// @param is_upper True for upper manual voice, false for lower manual voice.
  void applyRegisterProtection(
      std::vector<NoteEvent>& notes,
      const ManualAssignment& manual,
      bool is_upper) const;

  /// @brief Apply crossing logic at structurally appropriate positions.
  ///
  /// At Opening and Expansion positions, voices are allowed to temporarily
  /// enter the other manual's register for dramatic hand-crossing effect.
  /// The crossing is achieved by swapping pitch ranges: upper voice dips
  /// below the lower voice, or vice versa.
  ///
  /// @param upper_notes Upper manual notes (modified in place).
  /// @param lower_notes Lower manual notes (modified in place).
  /// @param grid The structural grid for phrase position queries.
  /// @param time_sig Time signature for bar duration calculation.
  void applyCrossingLogic(
      std::vector<NoteEvent>& upper_notes,
      std::vector<NoteEvent>& lower_notes,
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig) const;

  /// @brief Check if a variation number is a supported hand-crossing variation.
  /// @param variation_number The variation number to check.
  /// @return True if the variation number is 8, 17, or 20.
  static bool isSupportedVariation(int variation_number);
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_CROSSING_H
