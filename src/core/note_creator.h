// Bach-specific note creation API with counterpoint awareness.

#ifndef BACH_CORE_NOTE_CREATOR_H
#define BACH_CORE_NOTE_CREATOR_H

#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "counterpoint/melodic_context.h"
#include "harmony/key.h"
#include "instrument/keyboard/keyboard_instrument.h"

namespace bach {

// Forward declarations for Phase 1 counterpoint integration.
// In Phase 0, these are passed as nullptr.
class CounterpointState;
class IRuleEvaluator;
class CollisionResolver;

/// Options for creating a Bach note via createBachNote().
struct BachNoteOptions {
  VoiceId voice = 0;           // Target voice
  uint8_t desired_pitch = 60;  // Desired MIDI pitch (may be adjusted)
  Tick tick = 0;               // Start tick
  Tick duration = kTicksPerBeat;  // Duration in ticks
  uint8_t velocity = 80;       // MIDI velocity (organ default: 80)
  BachNoteSource source = BachNoteSource::Unknown;  // Provenance source
  uint8_t entry_number = 0;    // Fugue entry number (0 = not applicable)
  uint8_t prev_pitches[3] = {0, 0, 0};  // Melodic context: last 3 pitches (0=unknown)
  uint8_t prev_count = 0;               // Number of valid previous pitches
  int8_t prev_direction = 0;            // Previous motion direction (-1/0/1)
  std::optional<uint8_t> next_pitch;    // Next pitch for NHT validation (nullopt = unknown)
  const PhraseGoal* phrase_goal = nullptr;  // Optional phrase goal for melodic scoring
  const IKeyboardInstrument* instrument = nullptr;  // Optional instrument for range check
};

/// Result of note creation via createBachNote().
struct BachCreateNoteResult {
  bool accepted = false;       // true if note was placed successfully
  NoteEvent note;              // The created NoteEvent (valid only if accepted)
  NoteProvenance provenance;   // Full provenance record
  uint8_t final_pitch = 0;    // Final pitch after any adjustments
  bool was_adjusted = false;   // true if pitch was modified from desired_pitch
};

/// @brief Create a note with counterpoint rules applied.
///
/// In Phase 0 (stub): always returns accepted=true with no adjustments.
/// In Phase 1+: uses CounterpointState and IRuleEvaluator to validate
/// the note against active counterpoint rules, and CollisionResolver
/// to adjust pitch if needed.
///
/// @param state     Counterpoint state (nullptr in Phase 0).
/// @param rules     Rule evaluator (nullptr in Phase 0).
/// @param resolver  Collision resolver (nullptr in Phase 0).
/// @param opts      Note creation options.
/// @return Result containing the note, provenance, and acceptance status.
BachCreateNoteResult createBachNote(
    CounterpointState* state,
    IRuleEvaluator* rules,
    CollisionResolver* resolver,
    const BachNoteOptions& opts);

/// Statistics from postValidateNotes().
struct PostValidateStats {
  uint32_t total_input = 0;
  uint32_t accepted_original = 0;
  uint32_t repaired = 0;
  uint32_t dropped = 0;
  float drop_rate() const {
    return total_input > 0 ? static_cast<float>(dropped) / total_input : 0.0f;
  }
};

/// Per-voice protection level overrides.
using ProtectionOverrides = std::vector<std::pair<uint8_t, ProtectionLevel>>;

/// @brief Post-validate raw notes through the counterpoint engine.
///
/// Processes notes in priority order (Immutable -> Structural -> Flexible).
/// Each note is routed through createBachNote() which applies the 6-stage
/// repair cascade (original -> chord_tone -> suspension -> step_shift ->
/// octave_shift -> rest). Only truly irreconcilable notes are dropped.
///
/// @param raw_notes Input notes (consumed by move).
/// @param num_voices Number of active voices.
/// @param key_sig Key signature for counterpoint state and scale-tone validation.
/// @param voice_ranges Per-voice (low, high) ranges.
/// @param[out] stats Optional repair statistics.
/// @param protection_overrides Per-voice protection level overrides.
/// @return Validated notes with counterpoint rules enforced.
std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    const std::vector<std::pair<uint8_t, uint8_t>>& voice_ranges,
    PostValidateStats* stats = nullptr,
    const ProtectionOverrides& protection_overrides = {});

/// @brief Tick-aware overload: voice_range_fn(voice, tick) returns (low, high).
///
/// Uses note.start_tick for phase boundary lookup, enabling dynamic voice ranges
/// that change over time (e.g., per-section range adjustments).
///
/// @param raw_notes Input notes (consumed by move).
/// @param num_voices Number of active voices.
/// @param key_sig Key signature for counterpoint state and scale-tone validation.
/// @param voice_range_fn Function returning (low, high) for a given voice and tick.
/// @param[out] stats Optional repair statistics.
/// @param protection_overrides Per-voice protection level overrides.
/// @return Validated notes with counterpoint rules enforced.
std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t voice, Tick tick)> voice_range_fn,
    PostValidateStats* stats = nullptr,
    const ProtectionOverrides& protection_overrides = {});

/// @brief Build a MelodicContext from the counterpoint state for a given voice.
/// @param state The counterpoint state to query.
/// @param voice_id The voice to build context for.
/// @return MelodicContext populated with up to 2 previous pitches, direction,
///         leap_needs_resolution flag, and leading tone detection.
MelodicContext buildMelodicContextFromState(const CounterpointState& state, VoiceId voice_id);

}  // namespace bach

#endif  // BACH_CORE_NOTE_CREATOR_H
