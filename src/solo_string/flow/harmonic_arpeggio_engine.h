// Harmonic arpeggio engine for BWV1007-style solo string flow generation.

#ifndef BACH_SOLO_STRING_FLOW_HARMONIC_ARPEGGIO_ENGINE_H
#define BACH_SOLO_STRING_FLOW_HARMONIC_ARPEGGIO_ENGINE_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "solo_string/flow/arpeggio_flow_config.h"

namespace bach {

/// @brief Result from the harmonic arpeggio engine.
///
/// Contains the generated MIDI track(s) for a solo string flow piece
/// (typically a single track for a monophonic instrument) along with
/// duration metadata and error information.
struct ArpeggioFlowResult {
  std::vector<Track> tracks;       ///< Usually 1 track (solo instrument).
  Tick total_duration_ticks = 0;   ///< Total piece duration in ticks.
  bool success = false;            ///< True if generation completed without error.
  std::string error_message;       ///< Describes the failure if success is false.
  uint32_t seed_used = 0;         ///< The actual seed used (after auto-resolution).
};

/// @brief Generate a BWV1007-style harmonic arpeggio flow piece.
///
/// Pipeline:
///   1. Build HarmonicTimeline (bar resolution, weighted)
///   2. Divide into sections (config.num_sections x config.bars_per_section)
///   3. Assign GlobalArc (config-fixed, seed-independent)
///   4. For each section: assign PatternRoles, select ArpeggioPatterns
///   5. Generate notes: pattern x harmony x weight -> pitch sequence
///   6. Apply cadence processing for final bars
///
/// The GlobalArc determines the registral and dynamic shape of the piece:
///   - Ascent: register gradually expands upward
///   - Peak: full instrument range, maximum harmonic weight (design values, no search)
///   - Descent: register contracts toward the low end
///
/// Open strings are actively utilized for idiomatic BWV1007-style resonance.
/// This engine is harmony-driven (no counterpoint rules apply).
///
/// @param config Full configuration for the flow piece.
/// @return ArpeggioFlowResult with MIDI track and metadata.
ArpeggioFlowResult generateArpeggioFlow(const ArpeggioFlowConfig& config);

}  // namespace bach

#endif  // BACH_SOLO_STRING_FLOW_HARMONIC_ARPEGGIO_ENGINE_H
