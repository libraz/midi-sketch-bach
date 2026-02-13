// Harmonic time warp: Score → Performance tick transformation.

#ifndef BACH_HARMONY_HARMONIC_TIME_WARP_H
#define BACH_HARMONY_HARMONIC_TIME_WARP_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"

namespace bach {

/// @brief Apply harmonic time warp to note ticks.
///
/// Score → Performance transformation. Warps note start_tick values based on
/// harmonic gravity (bar tension gradient, cadences, bass resolution) combined
/// with metric emphasis (beat weight within bar).
///
/// Warps at beat granularity. Each 4-bar phrase satisfies Σδ=0 (phrase boundary
/// ticks remain invariant). Duration is scaled proportionally.
///
/// @param notes Notes to warp (modified in place). Ticks relative to variation start.
/// @param grid Structural grid providing per-bar harmonic weight.
/// @param desc Variation descriptor (time_sig, meter_profile, tempo_character).
/// @param seed Random seed for interpretation parameters (curvature, asymmetry).
void applyHarmonicTimeWarp(
    std::vector<NoteEvent>& notes,
    const GoldbergStructuralGrid& grid,
    const GoldbergVariationDescriptor& desc,
    uint32_t seed);

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_TIME_WARP_H
