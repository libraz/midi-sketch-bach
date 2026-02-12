// Creates notes with counterpoint rule awareness.
// When counterpoint state/rules/resolver are provided, validates and adjusts
// pitches to comply with counterpoint rules. Falls back to direct placement
// when these are nullptr (Phase 0 compatibility).

#include "core/note_creator.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "counterpoint/parallel_repair.h"

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

namespace {

/// Classify ProtectionLevel into sort priority (lower = processed first).
int protectionPriority(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::Immutable: return 0;
    case ProtectionLevel::Structural: return 1;
    case ProtectionLevel::Flexible: return 2;
  }
  return 2;
}

}  // anonymous namespace

std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    const std::vector<std::pair<uint8_t, uint8_t>>& voice_ranges,
    PostValidateStats* stats) {
  if (raw_notes.empty()) return {};

  // Determine scale type for diatonic validation of step shifts.
  ScaleType effective_scale = key_sig.is_minor ? ScaleType::NaturalMinor
                                               : ScaleType::Major;

  // Initialize counterpoint engine.
  CounterpointState state;
  state.setKey(key_sig.tonic);
  for (uint8_t v = 0; v < num_voices && v < voice_ranges.size(); ++v) {
    state.registerVoice(v, voice_ranges[v].first, voice_ranges[v].second);
  }

  BachRuleEvaluator rules(num_voices);
  rules.setFreeCounterpoint(true);

  CollisionResolver resolver;
  resolver.setRangeTolerance(0);  // Strict range enforcement for post-validation.

  // Sort by (tick ASC, protection_priority ASC, voice ASC).
  // Immutable notes processed first within each tick.
  std::stable_sort(raw_notes.begin(), raw_notes.end(),
      [](const NoteEvent& a, const NoteEvent& b) {
        if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
        int pa = protectionPriority(getProtectionLevel(a.source));
        int pb = protectionPriority(getProtectionLevel(b.source));
        if (pa != pb) return pa < pb;
        return a.voice < b.voice;
      });

  PostValidateStats local_stats;
  std::vector<NoteEvent> result;
  result.reserve(raw_notes.size());

  for (const auto& note : raw_notes) {
    local_stats.total_input++;

    ProtectionLevel prot = getProtectionLevel(note.source);

    if (prot == ProtectionLevel::Immutable) {
      // Immutable notes are registered directly without modification.
      state.setCurrentTick(note.start_tick);
      state.addNote(note.voice, note);
      result.push_back(note);
      local_stats.accepted_original++;
      continue;
    }

    // Route through createBachNote() for repair cascade.
    BachNoteOptions opts;
    opts.voice = note.voice;
    opts.desired_pitch = note.pitch;
    opts.tick = note.start_tick;
    opts.duration = note.duration;
    opts.velocity = note.velocity;
    opts.source = note.source;

    // Build melodic context from the last note in the same voice.
    const NoteEvent* prev = state.getLastNote(note.voice);
    if (prev) {
      opts.prev_pitches[0] = prev->pitch;
      opts.prev_count = 1;
      opts.prev_direction = (note.pitch > prev->pitch) ? 1 :
                            (note.pitch < prev->pitch) ? -1 : 0;
    }

    state.setCurrentTick(note.start_tick);
    BachCreateNoteResult res = createBachNote(&state, &rules, &resolver, opts);

    if (res.accepted) {
      result.push_back(res.note);
      if (res.was_adjusted) {
        local_stats.repaired++;
      } else {
        local_stats.accepted_original++;
      }
    } else {
      local_stats.dropped++;
    }
  }

  // Clamp repaired notes to strict voice ranges (collision resolver allows
  // a soft tolerance, but final output must respect range boundaries).
  for (auto& note : result) {
    if (note.voice < voice_ranges.size()) {
      auto [lo, hi] = voice_ranges[note.voice];
      if (note.pitch < lo) note.pitch = lo;
      if (note.pitch > hi) note.pitch = hi;
    }
  }

  // Final pass: fix parallel perfect consonances via shared repair utility.
  {
    ParallelRepairParams repair_params;
    repair_params.num_voices = num_voices;
    repair_params.scale = effective_scale;
    repair_params.key_at_tick = [&](Tick) { return key_sig.tonic; };
    repair_params.voice_range = [&](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      if (v < voice_ranges.size()) return voice_ranges[v];
      return {0, 127};
    };
    repairParallelPerfect(result, repair_params);
  }

  // Log warning if drop rate exceeds threshold.
  if (local_stats.drop_rate() > 0.10f) {
    std::fprintf(stderr,
        "[postValidateNotes] WARNING: drop_rate=%.1f%% (%u/%u dropped)\n",
        local_stats.drop_rate() * 100.0f,
        local_stats.dropped, local_stats.total_input);
  }

  if (stats) {
    *stats = local_stats;
  }

  return result;
}

}  // namespace bach
