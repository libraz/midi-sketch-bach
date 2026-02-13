// Soggetto (short subject) generator for Goldberg Variations Inventio mode.
// Reuses the fugue SubjectGenerator pipeline under structural grid constraints.

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_SOGGETTO_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_SOGGETTO_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "fugue/motif_template.h"
#include "fugue/subject.h"
#include "harmony/key.h"

namespace bach {

/// @brief Parameters for soggetto (short subject) generation.
///
/// Soggetti are short subjects used by Canon Dux, Fughetta, and Invention
/// variations operating in MelodyMode::Inventio. They are aligned to the
/// Goldberg 32-bar structural grid.
struct SoggettoParams {
  uint8_t length_bars = 2;                 ///< Subject length in bars (1-4).
  SubjectCharacter character =
      SubjectCharacter::Severe;            ///< Character (Severe/Playful/Noble/Restless).
  const GoldbergStructuralGrid* grid =
      nullptr;                             ///< Structural grid reference (required).
  uint8_t start_bar = 1;                   ///< Starting bar (1-based position on grid).
  int path_candidates = 8;                ///< N candidate count for scoring.
};

/// @brief Generates short subjects (soggetti) aligned to the Goldberg structural grid.
///
/// Reuses the SubjectGenerator pipeline: anchor -> rhythm skeleton -> N-path
/// candidate scoring. Key differences from SubjectGenerator:
///   - Length: 1-4 bars (vs 4-8 for fugue subjects)
///   - Constraints: Structural grid (harmonic pivots, CadenceType) vs ArchetypePolicy
///   - Climax: Guided to grid's Intensification position
///   - Ending: Aligned to grid's CadenceType
class SoggettoGenerator {
 public:
  /// @brief Generate a soggetto aligned to the structural grid.
  /// @param params Soggetto parameters (length, character, grid, start_bar).
  /// @param key Key signature for the variation.
  /// @param time_sig Time signature for bar duration calculation.
  /// @param seed Random seed for deterministic generation.
  /// @return Generated Subject with grid-aligned notes.
  Subject generate(const SoggettoParams& params,
                   const KeySignature& key,
                   const TimeSignature& time_sig,
                   uint32_t seed) const;

  /// @brief Score candidate path for structural grid alignment.
  /// @param candidate Candidate note sequence.
  /// @param params Soggetto parameters.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @return Weighted alignment score (higher = better).
  float scoreGridAlignment(
      const std::vector<NoteEvent>& candidate,
      const SoggettoParams& params,
      const KeySignature& key,
      const TimeSignature& time_sig) const;

 private:
  /// @brief Compute goal tone aligned to structural grid positions.
  /// @param params Soggetto parameters.
  /// @param rng RNG for small perturbation.
  /// @return GoalTone with position_ratio guided by Intensification bars.
  GoalTone computeGridAlignedGoalTone(
      const SoggettoParams& params,
      std::mt19937& rng) const;

  /// @brief Generate candidate pitch paths using MotifTemplate.
  /// @param params Soggetto parameters.
  /// @param goal Goal tone for climax placement.
  /// @param key Key signature.
  /// @param time_sig Time signature.
  /// @param rng RNG for path generation.
  /// @return Vector of candidate note sequences.
  std::vector<std::vector<NoteEvent>> generateCandidates(
      const SoggettoParams& params,
      const GoalTone& goal,
      const KeySignature& key,
      const TimeSignature& time_sig,
      std::mt19937& rng) const;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_SOGGETTO_H
