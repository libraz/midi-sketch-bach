// Fugue pipeline: constraint-driven generation replacing post-repair passes.

#ifndef BACH_FUGUE_FUGUE_PIPELINE_H
#define BACH_FUGUE_FUGUE_PIPELINE_H

#include "fugue/fugue_generator.h"  // FugueResult, FugueConfig

namespace bach {

/// @brief Generate a complete fugue using the constraint-driven pipeline.
///
/// Replaces the monolithic generateFugue() with a 4-step pipeline:
///   1. buildMaterial()      - subject, answer, countersubject, constraint profile
///   2. planStructure()      - tonal plan, section layout, energy curve
///   3. generateSections()   - per-section generation with ConstraintState
///   4. finalize()           - minimal post-processing + track assembly
///
/// @param config Fugue configuration (key, character, voices, seed, etc.).
/// @return FugueResult with tracks, structure, and success status.
FugueResult generateFuguePipeline(const FugueConfig& config);

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_PIPELINE_H
