// Phase 6: Shared constraint-driven generation helpers for all Organ forms.
// ConstraintState-based generation with finalizeFormNotes pipeline.

#ifndef BACH_FORMS_FORM_CONSTRAINT_SETUP_H
#define BACH_FORMS_FORM_CONSTRAINT_SETUP_H

#include <cstdint>
#include <functional>
#include <vector>

#include "constraint/constraint_state.h"
#include "core/basic_types.h"

namespace bach {

/// @brief Initialize a ConstraintState for a form's generation phase.
///
/// Wires GravityConfig with kFugueUpperMarkov/kFugueVerticalTable,
/// sets InvariantSet defaults appropriate for Organ forms, and
/// configures cadence/duration tracking.
///
/// @param num_voices Number of active voices.
/// @param voice_range Callback returning {lo, hi} MIDI pitch for each voice.
/// @param total_duration Total duration of the piece in ticks.
/// @param phase Current structural phase (default: Develop).
/// @param energy Energy level for gravity scoring (default: 0.5).
/// @param cadence_ticks Sorted tick positions of cadences.
/// @return Fully configured ConstraintState.
ConstraintState setupFormConstraintState(
    uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    Tick total_duration,
    FuguePhase phase = FuguePhase::Develop,
    float energy = 0.5f,
    const std::vector<Tick>& cadence_ticks = {});

/// @brief Lightweight finalize: within-voice overlap dedup.
///
/// Replaces the legacy coordinateVoices + postValidateNotes + resolveLeaps
/// pipeline.  Constraint-driven generation should
/// produce clean output; this handles only residual overlaps from
/// multi-phase note placement.
///
/// Algorithm:
///   1. Sort by voice, then tick, then duration descending (longer wins).
///   2. Remove same-tick duplicates within each voice.
///   3. Truncate overlapping notes (minimum duration = 1 tick).
///
/// @param notes Note events to finalize (modified in place).
/// @param num_voices Number of voices (unused, reserved for assertions).
void finalizeFormNotes(std::vector<NoteEvent>& notes, uint8_t num_voices);

/// @brief Extended finalize with voice range clamping, diatonic repeat
///        mitigation, and lightweight leap resolution.
///
/// Same as finalizeFormNotes plus:
///   4. Clamp each note's pitch to its voice range.
///   5. Break runs of 3+ consecutive same-pitch notes using diatonic neighbor
///      decoration patterns (upper/lower neighbor, graduated return), all
///      snapped to the nearest scale tone via nearestScaleTone().
///   6. Lightweight leap resolution: leaps > 5 semitones are followed by
///      stepwise contrary motion (unless it flattens the contour).
///
/// @param notes Note events to finalize (modified in place).
/// @param num_voices Number of voices.
/// @param voice_range Callback returning {lo, hi} MIDI pitch for each voice.
/// @param key Musical key (tonic pitch class).
/// @param scale Scale type for diatonic snapping.
/// @param max_consecutive Maximum consecutive same-pitch notes (default 2).
void finalizeFormNotes(
    std::vector<NoteEvent>& notes, uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    Key key, ScaleType scale,
    int max_consecutive = 2);

namespace form_utils {

/// @brief Gather notes from all tracks, finalize (dedup/clamp/repeat-break),
///        then redistribute back to tracks by voice.
///
/// Consolidates the common pattern of flattening tracks into a single vector,
/// running finalizeFormNotes, clearing the tracks, and scattering notes back
/// by voice index.
///
/// When @p voice_range is provided, the extended finalize pipeline is used
/// (range clamping, repeat mitigation, leap resolution).  Otherwise the
/// lightweight overlap-dedup-only variant is used.
///
/// @param tracks        Tracks to normalize (modified in place).
/// @param num_voices    Number of active voices.
/// @param voice_range   Optional callback returning {lo, hi} MIDI pitch per voice.
/// @param key           Musical key for diatonic snapping (default: C).
/// @param scale         Scale type (default: Major).
/// @param max_consecutive  Maximum consecutive same-pitch notes before decoration
///                         (default: 0 = skip repeat breaking).
void normalizeAndRedistribute(
    std::vector<Track>& tracks, uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range = {},
    Key key = Key::C, ScaleType scale = ScaleType::Major,
    int max_consecutive = 0);

}  // namespace form_utils

}  // namespace bach

#endif  // BACH_FORMS_FORM_CONSTRAINT_SETUP_H
