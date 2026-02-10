// ChaconneEngine -- core integration engine for BWV1004-style chaconne generation.

#ifndef BACH_SOLO_STRING_ARCH_CHACONNE_ENGINE_H
#define BACH_SOLO_STRING_ARCH_CHACONNE_ENGINE_H

#include <string>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"
#include "solo_string/arch/chaconne_config.h"

namespace bach {

/// @brief Result from chaconne generation.
///
/// Contains a single MIDI track (solo instrument) with ground bass and
/// texture notes interleaved. The ground bass notes carry
/// BachNoteSource::GroundBass provenance and are verified post-generation.
struct ChaconneResult {
  std::vector<Track> tracks;       ///< Usually 1 track (solo instrument).
  Tick total_duration_ticks = 0;   ///< Total piece duration in ticks.
  bool success = false;            ///< True if generation completed without error.
  std::string error_message;       ///< Describes the failure if success is false.
  uint32_t seed_used = 0;          ///< The actual seed used (after auto-resolution).
  HarmonicTimeline timeline;       ///< Concatenated harmonic context across variations.
};

/// @brief Generate a BWV1004-style chaconne with ground bass variations.
///
/// Pipeline:
///   1. Initialize ground bass (immutable for the lifetime of the piece)
///   2. Create or validate variation plan (fixed structural order)
///   3. For each variation:
///      a. Place ground bass (copy, never modify original)
///      b. Build harmonic timeline segment for the variation's key
///      c. Generate texture notes based on VariationRole + TextureType
///      d. Apply major section constraints where applicable
///      e. Output climax design values directly for Accumulate variations
///   4. Verify ground bass integrity against all placed bass notes
///   5. Assemble final track sorted by start_tick
///
/// Recovery: on texture generation failure, retries with a different seed
/// up to config.max_variation_retries times. The ground bass is NEVER
/// modified during recovery.
///
/// @param config Full configuration for chaconne generation.
/// @return ChaconneResult with MIDI track and metadata.
ChaconneResult generateChaconne(const ChaconneConfig& config);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_CHACONNE_ENGINE_H
