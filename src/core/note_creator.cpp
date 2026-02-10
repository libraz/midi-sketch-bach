// Creates notes with counterpoint rule awareness.
// When counterpoint state/rules/resolver are provided, validates and adjusts
// pitches to comply with counterpoint rules. Falls back to direct placement
// when these are nullptr (Phase 0 compatibility).

#include "core/note_creator.h"

#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {

BachCreateNoteResult createBachNote(
    CounterpointState* state,
    IRuleEvaluator* rules,
    CollisionResolver* resolver,
    const BachNoteOptions& opts) {
  BachCreateNoteResult result;

  // Record provenance (always).
  result.provenance.source = opts.source;
  result.provenance.original_pitch = opts.desired_pitch;
  result.provenance.lookup_tick = opts.tick;
  result.provenance.entry_number = opts.entry_number;

  // If counterpoint engine is available, use collision resolver.
  if (state && rules && resolver) {
    PlacementResult placement = resolver->findSafePitch(
        *state, *rules, opts.voice, opts.desired_pitch, opts.tick, opts.duration,
        opts.source, opts.next_pitch);

    if (placement.accepted) {
      result.accepted = true;
      result.final_pitch = placement.pitch;
      result.was_adjusted = (placement.pitch != opts.desired_pitch);

      // Build the NoteEvent with resolved pitch.
      result.note.start_tick = opts.tick;
      result.note.duration = opts.duration;
      result.note.pitch = placement.pitch;
      result.note.velocity = opts.velocity;
      result.note.voice = opts.voice;
      result.note.source = opts.source;

      // Record the adjustment in provenance.
      if (result.was_adjusted) {
        result.provenance.addStep(BachTransformStep::CollisionAvoid);
      }

      // Register the note in counterpoint state.
      state->addNote(opts.voice, result.note);
    } else {
      result.accepted = false;
      result.final_pitch = opts.desired_pitch;
      result.was_adjusted = false;
    }
    return result;
  }

  // Fallback: no counterpoint engine (Phase 0 behavior).
  result.accepted = true;
  result.was_adjusted = false;
  result.final_pitch = opts.desired_pitch;

  result.note.start_tick = opts.tick;
  result.note.duration = opts.duration;
  result.note.pitch = opts.desired_pitch;
  result.note.velocity = opts.velocity;
  result.note.voice = opts.voice;
  result.note.source = opts.source;

  return result;
}

}  // namespace bach
