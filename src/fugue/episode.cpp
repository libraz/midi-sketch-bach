// Fugue episode: modulatory development of subject material.
//
// Phase 3: Thin wrappers delegating to generateConstraintEpisode().
// Motif extraction functions preserved verbatim from pre-Phase-3 implementation.

#include "fugue/episode.h"

#include <algorithm>
#include <vector>

#include "constraint/constraint_state.h"
#include "constraint/episode_generator.h"
#include "constraint/motif_constraint.h"
#include "core/basic_types.h"
#include "core/markov_tables.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"
#include "fugue/subject.h"
#include "fugue/voice_registers.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

namespace {

/// @brief Build an EpisodeRequest from the common parameters shared by all
/// episode generation overloads.
///
/// Populates the ConstraintState with voice range invariants derived from
/// getFugueVoiceRange(). The grammar is selected by subject character via
/// getFortspinnungGrammar().
///
/// @param subject The fugue subject (source material and character).
/// @param start_tick Starting tick position.
/// @param duration_ticks Episode duration in ticks.
/// @param start_key Key at episode start.
/// @param target_key Key to modulate toward.
/// @param num_voices Number of active voices (1-5).
/// @param seed RNG seed for deterministic generation.
/// @param episode_index Episode ordinal (odd = invertible counterpoint).
/// @param energy_level Energy level in [0,1].
/// @param pool Motif pool pointer (read-only, may be null for empty-pool fallback).
/// @param rule_eval Rule evaluator for parallel-perfect checks (nullable).
/// @param crossing_eval Bach rule evaluator for crossing checks (nullable).
/// @param cp_state Counterpoint state context for crossing temporality (nullable).
/// @param pipeline_accum Section accumulator from pipeline (nullable).
/// @param prev_exit_state Previous episode's exit ConstraintState for chaining
///        gravity/accumulator data across consecutive episodes (nullable).
///        When provided, the entry_state is seeded from the previous exit state,
///        preserving accumulated distribution data and gravity scores, then
///        voice_range, energy, and phase are overridden from current parameters.
/// @return Populated EpisodeRequest ready for generateConstraintEpisode().
EpisodeRequest buildRequest(const Subject& subject, Tick start_tick,
                            Tick duration_ticks, Key start_key, Key target_key,
                            uint8_t num_voices, uint32_t seed,
                            int episode_index, float energy_level,
                            const MotifPool* pool,
                            const IRuleEvaluator* rule_eval = nullptr,
                            const BachRuleEvaluator* crossing_eval = nullptr,
                            const CounterpointState* cp_state = nullptr,
                            const SectionAccumulator* pipeline_accum = nullptr,
                            const HarmonicTimeline* timeline = nullptr,
                            const ConstraintState* prev_exit_state = nullptr,
                            const uint8_t* last_pitches = nullptr) {
  EpisodeRequest req;
  req.start_key = start_key;
  req.end_key = target_key;
  req.start_tick = start_tick;
  req.duration = duration_ticks;
  req.num_voices = num_voices;
  req.character = subject.character;
  req.grammar = getFortspinnungGrammar(subject.character);
  req.episode_index = episode_index;
  req.energy_level = energy_level;
  req.seed = seed;
  req.motif_pool = pool;

  // Seed entry_state from previous episode's exit state if available.
  // This carries forward accumulated distribution data (rhythm/harmony counts),
  // obligation tracking, and gravity scores from the preceding episode.
  if (prev_exit_state) {
    req.entry_state = *prev_exit_state;
  }

  // Set voice range invariants spanning all voices (override prev_exit values).
  if (num_voices >= 2) {
    auto [lo_soprano, hi_soprano] = getFugueVoiceRange(0, num_voices);
    auto [lo_lowest, hi_lowest] = getFugueVoiceRange(num_voices - 1, num_voices);
    req.entry_state.invariants.voice_range_lo = lo_lowest;
    req.entry_state.invariants.voice_range_hi = hi_soprano;
  } else if (num_voices == 1) {
    auto [lo_solo, hi_solo] = getFugueVoiceRange(0, num_voices);
    req.entry_state.invariants.voice_range_lo = lo_solo;
    req.entry_state.invariants.voice_range_hi = hi_solo;
  }

  req.entry_state.total_duration = duration_ticks;

  // Wire Gravity layer with Markov models and reference data.
  // Always override these from current parameters (not inherited from prev_exit).
  req.entry_state.gravity.melodic_model = &kFugueUpperMarkov;
  req.entry_state.gravity.vertical_table = &kFugueVerticalTable;
  req.entry_state.gravity.phase = FuguePhase::Develop;  // Episodes are development.
  req.entry_state.gravity.energy = energy_level;

  // Wire rule evaluators for InvariantSet parallel-perfect and crossing checks.
  req.rule_eval = rule_eval;
  req.crossing_eval = crossing_eval;
  req.cp_state_ctx = cp_state;

  // Import pipeline accumulator if provided (overrides prev_exit accumulator).
  if (pipeline_accum) {
    req.entry_state.accumulator = *pipeline_accum;
  }

  // Wire harmonic timeline for bass pitch selection.
  req.timeline = timeline;

  // Transfer per-voice last pitches for voice-leading continuity.
  if (last_pitches) {
    for (int i = 0; i < std::min(static_cast<int>(num_voices),
                                  EpisodeRequest::kMaxRequestVoices); ++i) {
      req.last_pitches[i] = last_pitches[i];
    }
  }

  return req;
}

/// @brief Convert an EpisodeResult from the constraint pipeline into the
/// public Episode struct.
///
/// @param res Result from generateConstraintEpisode().
/// @param start_tick Episode start tick (for timing fields).
/// @param duration_ticks Episode duration (for end_tick calculation).
/// @param start_key Key at episode start.
/// @param target_key Requested target key (used when constraint pipeline
///        does not report an achieved key).
/// @return Episode with notes and key/timing metadata.
Episode resultToEpisode(const EpisodeResult& res, Tick start_tick,
                        Tick duration_ticks, Key start_key, Key target_key) {
  Episode episode;
  episode.start_tick = start_tick;
  episode.end_tick = start_tick + duration_ticks;
  episode.start_key = start_key;
  episode.end_key = res.success ? res.achieved_key : target_key;
  episode.notes = res.notes;
  return episode;
}

/// @brief Build a temporary MotifPool from a Subject for non-pool episodes.
///
/// The raw generateEpisode() overloads do not receive a pre-built MotifPool,
/// so we construct one on the fly from the subject notes.
///
/// @param subject The fugue subject to extract motifs from.
/// @return MotifPool built from subject (no countersubject).
MotifPool buildPoolFromSubject(const Subject& subject) {
  MotifPool pool;
  pool.build(subject.notes, {}, subject.character);
  return pool;
}

/// @brief Sync generated episode notes into the CounterpointState.
///
/// For validated episode overloads, each placed note must be registered in
/// the counterpoint state so that subsequent generation phases see them.
///
/// @param cp_state Counterpoint state to update (modified in place).
/// @param notes Episode notes to add.
void syncToCounterpointState(CounterpointState& cp_state,
                             const std::vector<NoteEvent>& notes) {
  for (const auto& note : notes) {
    cp_state.addNote(note.voice, note);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Motif extraction functions (preserved verbatim from pre-Phase-3)
// ---------------------------------------------------------------------------

std::vector<NoteEvent> extractMotif(const Subject& subject, size_t max_notes) {
  std::vector<NoteEvent> motif;
  size_t count = std::min(max_notes, subject.noteCount());
  motif.reserve(count);
  for (size_t idx = 0; idx < count; ++idx) {
    motif.push_back(subject.notes[idx]);
  }
  return motif;
}

std::vector<NoteEvent> extractTailMotif(const std::vector<NoteEvent>& notes, size_t num_notes) {
  if (num_notes >= notes.size()) return notes;
  return std::vector<NoteEvent>(notes.end() - static_cast<int>(num_notes), notes.end());
}

std::vector<std::vector<NoteEvent>> fragmentMotif(const std::vector<NoteEvent>& notes,
                                                   size_t num_fragments) {
  std::vector<std::vector<NoteEvent>> fragments;
  if (num_fragments == 0 || notes.empty()) return fragments;
  size_t frag_size = notes.size() / num_fragments;
  if (frag_size == 0) frag_size = 1;
  for (size_t idx = 0; idx < notes.size(); idx += frag_size) {
    size_t end = std::min(idx + frag_size, notes.size());
    fragments.emplace_back(notes.begin() + static_cast<int>(idx),
                           notes.begin() + static_cast<int>(end));
    if (fragments.size() >= num_fragments) break;
  }
  return fragments;
}

std::vector<NoteEvent> extractCharacteristicMotif(const Subject& subject,
                                                   size_t motif_length) {
  if (subject.notes.size() <= motif_length) {
    return subject.notes;
  }

  float best_score = -1.0f;
  size_t best_start = 0;

  size_t window_count = subject.notes.size() - motif_length + 1;
  for (size_t start = 0; start < window_count; ++start) {
    float score = 0.0f;

    // Rhythmic diversity: count distinct durations in window.
    std::vector<Tick> durations;
    for (size_t idx = start; idx < start + motif_length; ++idx) {
      bool found = false;
      for (Tick dur : durations) {
        if (dur == subject.notes[idx].duration) {
          found = true;
          break;
        }
      }
      if (!found) durations.push_back(subject.notes[idx].duration);
    }
    score += 0.3f * static_cast<float>(durations.size()) /
             static_cast<float>(motif_length);

    // Intervallic interest: contains a leap (>= 3 semitones).
    bool has_leap = false;
    for (size_t idx = start + 1; idx < start + motif_length; ++idx) {
      int ivl = absoluteInterval(subject.notes[idx].pitch, subject.notes[idx - 1].pitch);
      if (ivl >= 3) {
        has_leap = true;
        break;
      }
    }
    if (has_leap) score += 0.3f;

    // Proximity to opening.
    float proximity = 1.0f - static_cast<float>(start) / static_cast<float>(window_count);
    score += 0.2f * proximity;

    // Tonal stability: contains root (pitch class 0 in subject context).
    int root_pc = getPitchClass(subject.notes[0].pitch);
    bool has_root = false;
    for (size_t idx = start; idx < start + motif_length; ++idx) {
      if (getPitchClass(subject.notes[idx].pitch) == root_pc) {
        has_root = true;
        break;
      }
    }
    if (has_root) score += 0.2f;

    if (score > best_score) {
      best_score = score;
      best_start = start;
    }
  }

  return std::vector<NoteEvent>(subject.notes.begin() + best_start,
                                 subject.notes.begin() + best_start + motif_length);
}

// ---------------------------------------------------------------------------
// generateEpisode (raw, no counterpoint validation)
// ---------------------------------------------------------------------------

Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index, float energy_level,
                        const uint8_t* last_pitches) {
  MotifPool pool = buildPoolFromSubject(subject);
  EpisodeRequest req = buildRequest(subject, start_tick, duration_ticks,
                                    start_key, target_key, num_voices, seed,
                                    episode_index, energy_level, &pool,
                                    nullptr, nullptr, nullptr, nullptr, nullptr,
                                    nullptr, last_pitches);
  EpisodeResult res = generateConstraintEpisode(req);
  return resultToEpisode(res, start_tick, duration_ticks, start_key, target_key);
}

// ---------------------------------------------------------------------------
// generateEpisode (validated, with counterpoint state)
// ---------------------------------------------------------------------------

Episode generateEpisode(const Subject& subject, Tick start_tick, Tick duration_ticks,
                        Key start_key, Key target_key, uint8_t num_voices, uint32_t seed,
                        int episode_index, float energy_level,
                        CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                        CollisionResolver& /*cp_resolver*/,
                        const HarmonicTimeline& timeline,
                        uint8_t pedal_pitch,
                        const ConstraintState* prev_exit_state,
                        ConstraintState* exit_state_out,
                        const uint8_t* last_pitches) {
  MotifPool pool = buildPoolFromSubject(subject);
  EpisodeRequest req = buildRequest(subject, start_tick, duration_ticks,
                                    start_key, target_key, num_voices, seed,
                                    episode_index, energy_level, &pool,
                                    &cp_rules,
                                    dynamic_cast<const BachRuleEvaluator*>(&cp_rules),
                                    &cp_state, nullptr, &timeline,
                                    prev_exit_state, last_pitches);
  req.pedal_pitch = pedal_pitch;
  EpisodeResult res = generateConstraintEpisode(req);
  Episode episode = resultToEpisode(res, start_tick, duration_ticks,
                                    start_key, target_key);

  if (exit_state_out) {
    *exit_state_out = std::move(res.exit_state);
  }

  syncToCounterpointState(cp_state, episode.notes);
  return episode;
}

// ---------------------------------------------------------------------------
// generateFortspinnungEpisode (raw, no counterpoint validation)
// ---------------------------------------------------------------------------

Episode generateFortspinnungEpisode(const Subject& subject, const MotifPool& pool,
                                    Tick start_tick, Tick duration_ticks,
                                    Key start_key, Key target_key,
                                    uint8_t num_voices, uint32_t seed,
                                    int episode_index, float energy_level,
                                    const uint8_t* last_pitches) {
  // Fall back to standard generation if the pool is empty.
  if (pool.empty()) {
    return generateEpisode(subject, start_tick, duration_ticks,
                           start_key, target_key, num_voices, seed,
                           episode_index, energy_level, last_pitches);
  }

  EpisodeRequest req = buildRequest(subject, start_tick, duration_ticks,
                                    start_key, target_key, num_voices, seed,
                                    episode_index, energy_level, &pool,
                                    nullptr, nullptr, nullptr, nullptr, nullptr,
                                    nullptr, last_pitches);
  EpisodeResult res = generateConstraintEpisode(req);
  return resultToEpisode(res, start_tick, duration_ticks, start_key, target_key);
}

// ---------------------------------------------------------------------------
// generateFortspinnungEpisode (validated, with counterpoint state)
// ---------------------------------------------------------------------------

Episode generateFortspinnungEpisode(const Subject& subject, const MotifPool& pool,
                                    Tick start_tick, Tick duration_ticks,
                                    Key start_key, Key target_key,
                                    uint8_t num_voices, uint32_t seed,
                                    int episode_index, float energy_level,
                                    CounterpointState& cp_state,
                                    IRuleEvaluator& cp_rules,
                                    CollisionResolver& cp_resolver,
                                    const HarmonicTimeline& timeline,
                                    uint8_t pedal_pitch,
                                    const SectionAccumulator* accum,
                                    const ConstraintState* prev_exit_state,
                                    ConstraintState* exit_state_out,
                                    const uint8_t* last_pitches) {
  // Fall back to validated non-pool overload if pool is empty.
  if (pool.empty()) {
    return generateEpisode(subject, start_tick, duration_ticks,
                           start_key, target_key, num_voices, seed,
                           episode_index, energy_level,
                           cp_state, cp_rules, cp_resolver,
                           timeline, pedal_pitch,
                           prev_exit_state, exit_state_out,
                           last_pitches);
  }

  EpisodeRequest req = buildRequest(subject, start_tick, duration_ticks,
                                    start_key, target_key, num_voices, seed,
                                    episode_index, energy_level, &pool,
                                    &cp_rules,
                                    dynamic_cast<const BachRuleEvaluator*>(&cp_rules),
                                    &cp_state, accum, &timeline,
                                    prev_exit_state, last_pitches);
  req.pedal_pitch = pedal_pitch;
  EpisodeResult res = generateConstraintEpisode(req);
  Episode episode = resultToEpisode(res, start_tick, duration_ticks,
                                    start_key, target_key);

  if (exit_state_out) {
    *exit_state_out = std::move(res.exit_state);
  }

  syncToCounterpointState(cp_state, episode.notes);
  return episode;
}

}  // namespace bach
