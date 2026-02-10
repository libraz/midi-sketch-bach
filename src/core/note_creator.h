// Bach-specific note creation API with counterpoint awareness.

#ifndef BACH_CORE_NOTE_CREATOR_H
#define BACH_CORE_NOTE_CREATOR_H

#include "core/basic_types.h"
#include "core/note_source.h"

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

}  // namespace bach

#endif  // BACH_CORE_NOTE_CREATOR_H
