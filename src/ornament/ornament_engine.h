// Ornament engine: post-processing ornament application for Bach generation.

#ifndef BACH_ORNAMENT_ORNAMENT_ENGINE_H
#define BACH_ORNAMENT_ORNAMENT_ENGINE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "ornament/ornament_types.h"

namespace bach {

class HarmonicTimeline;

/// Context for ornament application, combining config with voice role and RNG seed.
struct OrnamentContext {
  OrnamentConfig config;
  VoiceRole role = VoiceRole::Respond;  // Affects ornament eligibility and density
  uint32_t seed = 0;                    // Deterministic RNG seed
  const HarmonicTimeline* timeline = nullptr;  // Harmonic context (nullptr = legacy behavior)
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

/// @brief Apply ornaments with optional counterpoint verification.
///
/// Same as the base applyOrnaments(), but when all_voice_notes is non-empty,
/// performs post-application counterpoint verification. Ornaments that
/// introduce parallel 5ths/8ths or voice crossings are reverted to the
/// original unornamented notes.
///
/// Violating ornaments are REMOVED, not replaced with alternatives
/// (Principle 3: Reduce Generation).
///
/// @param notes Input note sequence to ornament.
/// @param context Ornament context with config, role, and seed.
/// @param all_voice_notes All voices' notes for cross-voice checking.
///        Pass empty vector to skip verification (backward compatible).
/// @return New note sequence with ornaments applied and verified.
std::vector<NoteEvent> applyOrnaments(const std::vector<NoteEvent>& notes,
                                      const OrnamentContext& context,
                                      const std::vector<std::vector<NoteEvent>>& all_voice_notes);

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

/// @brief Context-aware ornament type selection using harmonic information.
///
/// When a HarmonicTimeline is available, the selection incorporates harmonic
/// context:
///   - Chord tones prefer Trill (stable, sustained ornament).
///   - Non-chord tones prefer Vorschlag (approach from upper neighbor).
///   - Notes with duration >= kTicksPerBeat are eligible for compound ornaments.
///
/// Falls back to metric-position-based selection when harmonic context does
/// not change the preference.
///
/// @param note The note to analyze.
/// @param config The ornament configuration (which types are enabled).
/// @param timeline Harmonic timeline for chord tone analysis (must not be null).
/// @param tick The tick position for harmonic lookup.
/// @return The preferred OrnamentType based on harmonic and metric context.
OrnamentType selectOrnamentType(const NoteEvent& note, const OrnamentConfig& config,
                                const HarmonicTimeline& timeline, Tick tick);

/// @brief Verify ornament-expanded notes against counterpoint rules.
///
/// Checks for parallel 5ths/8ths and voice crossings introduced by ornament
/// expansion. Violating ornaments are removed (notes reverted to original).
/// Does NOT search for alternative ornaments (Principle 3: Reduce Generation).
///
/// @param notes The notes with ornaments applied (modified in place).
/// @param original_notes The notes before ornament application (for reversion).
/// @param all_voices All voice notes for counterpoint checking.
/// @param num_voices Number of active voices.
void verifyOrnamentCounterpoint(std::vector<NoteEvent>& notes,
                                const std::vector<NoteEvent>& original_notes,
                                const std::vector<std::vector<NoteEvent>>& all_voices,
                                uint8_t num_voices);

}  // namespace bach

#endif  // BACH_ORNAMENT_ORNAMENT_ENGINE_H
