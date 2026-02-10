// Ornament engine: post-processing ornament application for Bach generation.

#ifndef BACH_ORNAMENT_ORNAMENT_ENGINE_H
#define BACH_ORNAMENT_ORNAMENT_ENGINE_H

#include <vector>

#include "core/basic_types.h"
#include "ornament/ornament_types.h"

namespace bach {

/// Context for ornament application, combining config with voice role and RNG seed.
struct OrnamentContext {
  OrnamentConfig config;
  VoiceRole role = VoiceRole::Respond;  // Affects ornament eligibility and density
  uint32_t seed = 0;                    // Deterministic RNG seed
};

/// @brief Apply ornaments to a sequence of notes as post-processing.
///
/// Replaces eligible notes with ornamented note sequences based on the
/// context configuration. VoiceRole constraints:
///   - Assert:  minimal ornamentation (density * 0.5)
///   - Respond: normal density
///   - Propel:  full density
///   - Ground:  NO ornaments (forbidden, notes returned unchanged)
///
/// Strong beats (0, 2 in 4/4) prefer trills; weak beats (1, 3) prefer mordents.
/// Notes must have duration >= kTicksPerBeat/2 to be eligible.
///
/// @param notes Input note sequence to ornament.
/// @param context Ornament context with config, role, and seed.
/// @return New note sequence with ornaments applied. Non-ornamented notes
///         are passed through unchanged.
std::vector<NoteEvent> applyOrnaments(const std::vector<NoteEvent>& notes,
                                      const OrnamentContext& context);

/// @brief Check if a note is eligible for ornamentation.
///
/// A note is eligible if:
///   - Its voice role is not Ground
///   - Its duration is >= kTicksPerBeat / 2 (at least an eighth note)
///
/// @param note The note to check.
/// @param role The voice role of the note's voice.
/// @return true if the note can receive ornaments.
bool isEligibleForOrnament(const NoteEvent& note, VoiceRole role);

/// @brief Determine the preferred ornament type for a note based on metric position.
///
/// Strong beats (0, 2 in 4/4 time) prefer trills.
/// Weak beats (1, 3) prefer mordents.
/// Falls back to turn if enabled and neither trill nor mordent is available.
///
/// @param note The note to analyze.
/// @param config The ornament configuration (which types are enabled).
/// @return The preferred OrnamentType, or Trill as default fallback.
OrnamentType selectOrnamentType(const NoteEvent& note, const OrnamentConfig& config);

}  // namespace bach

#endif  // BACH_ORNAMENT_ORNAMENT_ENGINE_H
