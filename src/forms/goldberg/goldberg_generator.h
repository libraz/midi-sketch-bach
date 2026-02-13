// Main Goldberg Variations generator: orchestrates all variation generators.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_GENERATOR_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_GENERATOR_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_config.h"
#include "forms/goldberg/goldberg_plan.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Main Goldberg Variations generator.
///
/// Orchestrates all variation generators (AriaGenerator, CanonGenerator,
/// DanceGenerator, OrnamentalGenerator, FughettaGenerator, InventionGenerator,
/// CrossingGenerator, OvertureGenerator, VirtuosoGenerator, BlackPearlGenerator,
/// QuodlibetGenerator) into a complete BWV 988 output.
class GoldbergGenerator {
 public:
  /// @brief Generate the complete Goldberg Variations.
  /// @param config Generation configuration (key, tempo, scale, seed, etc.).
  /// @return GoldbergResult with tracks, tempo events, and metadata.
  GoldbergResult generate(const GoldbergConfig& config) const;

 private:
  /// @brief Generate a single variation based on its descriptor.
  /// @param desc Variation descriptor from the plan.
  /// @param grid_major Major key structural grid.
  /// @param grid_minor Minor key structural grid.
  /// @param key Base key signature for the piece.
  /// @param seed Per-variation seed for deterministic generation.
  /// @return Vector of NoteEvents for the variation.
  std::vector<NoteEvent> generateVariation(
      const GoldbergVariationDescriptor& desc,
      const GoldbergStructuralGrid& grid_major,
      const GoldbergStructuralGrid& grid_minor,
      const KeySignature& key,
      uint32_t seed) const;

  /// @brief Select variation indices based on DurationScale.
  /// @param plan Complete 32-entry variation plan.
  /// @param scale Duration scale controlling how many variations are included.
  /// @return Vector of 0-based indices into the plan.
  std::vector<size_t> selectVariations(
      const std::vector<GoldbergVariationDescriptor>& plan,
      DurationScale scale) const;

  /// @brief Apply ArticulationProfile to notes by adjusting durations.
  /// @param notes Notes to modify (in place).
  /// @param profile Articulation profile governing gate ratios.
  void applyArticulation(
      std::vector<NoteEvent>& notes,
      ArticulationProfile profile) const;

  /// @brief Calculate tempo BPM for a variation from its descriptor and base BPM.
  /// @param desc Variation descriptor with tempo_ratio and bpm_override.
  /// @param base_bpm Base Aria tempo in BPM.
  /// @return Effective BPM for this variation.
  uint16_t calculateVariationBpm(
      const GoldbergVariationDescriptor& desc,
      uint16_t base_bpm) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_GENERATOR_H
