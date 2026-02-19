// French Overture variation generator for Goldberg Variations (Var 16).

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_OVERTURE_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_OVERTURE_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Result of French Overture variation generation.
struct OvertureResult {
  std::vector<NoteEvent> notes;
  bool success = false;
};

/// @brief Generates the French Overture variation (Var 16).
///
/// Var 16 is the midpoint of BWV 988, structured as a two-section French
/// Overture:
///   - Section A (Grave, bars 1-16): Stately dotted rhythms in alla breve
///     feel, using FigurenGenerator with DottedGrave FiguraType.
///   - Section B (Fugato, bars 17-32): Lively fugue-like section using
///     SoggettoGenerator for a short subject with staggered entries and
///     sequential development.
///
/// Both sections are concatenated and binary repeats are applied.
class OvertureGenerator {
 public:
  /// @brief Generate the complete French Overture variation.
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature for the variation (G major for standard Goldberg).
  /// @param time_sig Time signature (2/2 alla breve for Var 16).
  /// @param seed Random seed for deterministic generation.
  /// @return OvertureResult with notes spanning 32 bars (pre-repeat) and success status.
  OvertureResult generate(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      uint32_t seed) const;

 private:
  /// @brief Generate Grave section (bars 0-15): stately dotted rhythms.
  /// @param grid The 32-bar structural grid.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @param rng Random number generator.
  /// @return Vector of NoteEvents for the Grave section.
  std::vector<NoteEvent> generateGrave(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      std::mt19937& rng) const;

  /// @brief Generate Fugato section (bars 16-31): lively fugue-like entries.
  /// @param grid The 32-bar structural grid.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @param rng Random number generator.
  /// @return Vector of NoteEvents for the Fugato section.
  std::vector<NoteEvent> generateFugato(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      std::mt19937& rng) const;

  /// @brief Generate bass line from the structural grid for the Grave section.
  /// @param grid The 32-bar structural grid.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @param start_bar First bar (0-indexed).
  /// @param num_bars Number of bars to generate.
  /// @return Vector of bass NoteEvents.
  std::vector<NoteEvent> generateBassLine(
      const GoldbergStructuralGrid& grid,
      const KeySignature& key,
      const TimeSignature& time_sig,
      int start_bar,
      int num_bars) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_OVERTURE_H
