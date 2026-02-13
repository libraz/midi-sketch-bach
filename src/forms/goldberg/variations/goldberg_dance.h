// Dance variation generator for Goldberg Variations (Var 4, 7, 19, 26).

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_DANCE_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_DANCE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of dance variation generation.
struct DanceResult {
  std::vector<NoteEvent> notes;
  bool success = false;
};

/// @brief Dance variation profile for a specific variation number.
///
/// Each dance variation has a unique rhythmic character defined by its
/// primary and secondary FiguraType, time signature, density, and
/// directional bias. These are design values derived from the historical
/// Goldberg Variations.
struct DanceProfile {
  FiguraType primary_figura;     ///< Dominant figura type for the dance.
  FiguraType secondary_figura;   ///< Contrast figura for phrase variety.
  TimeSignature time_sig;        ///< Time signature (3/8, 6/8, 3/4).
  uint8_t notes_per_beat;        ///< Rhythmic density per beat.
  DirectionBias direction;       ///< Dominant melodic direction.
  float chord_tone_ratio;        ///< Proportion of chord-tone alignment.
  float sequence_probability;    ///< Probability of sequential repetition.
  uint8_t voice_count;           ///< Number of voices (2 or 3).
};

/// @brief Returns the dance profile for the given variation number.
///
/// Supported variation numbers: 4, 7, 19, 26. Other numbers return a
/// default Passepied profile.
///
/// @param variation_number The Goldberg variation number.
/// @return DanceProfile with design-value parameters for that variation.
DanceProfile getDanceProfile(int variation_number);

/// @brief Generates dance variations using FigurenGenerator with dance-specific profiles.
///
/// Dance variations (Var 4, 7, 19, 26) are generated via the Elaboratio pathway:
/// the structural grid provides harmonic foundation, and FiguraType-specific patterns
/// create idiomatic rhythmic character (Passepied, Gigue, Sarabande).
///
/// Var 26 is special: the first 16 bars use Sarabande, the last 16 switch to
/// Tirata (rapid scale passages), reflecting the original's dramatic shift.
class DanceGenerator {
 public:
  /// @brief Generate a complete dance variation.
  /// @param variation_number The Goldberg variation number (4, 7, 19, 26).
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for scale-aware pitch generation.
  /// @param seed Random seed for deterministic generation.
  /// @return DanceResult with generated notes and success status.
  DanceResult generate(
      int variation_number,
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_DANCE_H
