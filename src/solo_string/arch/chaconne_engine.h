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
/// Contains two MIDI tracks: Track 0 is the bass voice
/// (BachNoteSource::ChaconneBass / GroundBass, channel 0) and Track 1 is the
/// texture voice (channel 1). Separating bass and texture avoids cross-voice
/// overlap truncation that destroys bass note durations.
struct ChaconneResult {
  std::vector<Track> tracks;       ///< 2 tracks: [0] bass, [1] texture.
  Tick total_duration_ticks = 0;   ///< Total piece duration in ticks.
  bool success = false;            ///< True if generation completed without error.
  std::string error_message;       ///< Describes the failure if success is false.
  uint32_t seed_used = 0;          ///< The actual seed used (after auto-resolution).
  HarmonicTimeline timeline;       ///< Concatenated harmonic context across variations.
};

/// @brief Generate a BWV1004-style chaconne with scheme-based bass variations.
///
/// Pipeline:
///   1. Initialize harmonic scheme (immutable for the lifetime of the piece)
///   2. Create or validate variation plan (fixed structural order)
///   3. For each variation:
///      a. Realize bass line from scheme (role-dependent via BassRealizer)
///      b. Build harmonic timeline segment from scheme for the variation's key
///      c. Generate texture notes based on VariationRole + TextureType
///      d. Apply major section constraints where applicable
///      e. Output climax design values directly for Accumulate variations
///   4. Verify harmonic scheme integrity against the concatenated timeline
///   5. Assemble final track sorted by start_tick
///
/// Recovery: on texture generation failure, retries with a different seed
/// up to config.max_variation_retries times. The harmonic scheme is NEVER
/// modified during recovery.
///
/// @param config Full configuration for chaconne generation.
/// @return ChaconneResult with MIDI track and metadata.
ChaconneResult generateChaconne(const ChaconneConfig& config);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_CHACONNE_ENGINE_H
