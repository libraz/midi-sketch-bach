// Creates notes with counterpoint rule awareness.
// When counterpoint state/rules/resolver are provided, validates and adjusts
// pitches to comply with counterpoint rules. Falls back to direct placement
// when these are nullptr (Phase 0 compatibility).

#include "core/note_creator.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <map>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/melodic_context.h"
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
  uint8_t final_pitch = opts.desired_pitch;

  // Apply instrument range check if available.
  if (opts.instrument && !opts.instrument->isPitchInRange(final_pitch)) {
    uint8_t lo = opts.instrument->getLowestPitch();
    uint8_t hi = opts.instrument->getHighestPitch();
    int center = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
    int shift = nearestOctaveShift(center - static_cast<int>(final_pitch));
    final_pitch = clampPitch(static_cast<int>(final_pitch) + shift, lo, hi);
  }

  result.accepted = true;
  result.was_adjusted = (final_pitch != opts.desired_pitch);
  result.final_pitch = final_pitch;

  result.note.start_tick = opts.tick;
  result.note.duration = opts.duration;
  result.note.pitch = final_pitch;
  result.note.velocity = opts.velocity;
  result.note.voice = opts.voice;
  result.note.source = opts.source;

  if (result.was_adjusted) {
    result.provenance.addStep(BachTransformStep::RangeClamp);
  }

  return result;
}

MelodicContext buildMelodicContextFromState(const CounterpointState& state, VoiceId voice_id) {
  MelodicContext ctx;
  const auto& voice_notes = state.getVoiceNotes(voice_id);
  if (voice_notes.empty()) return ctx;
  auto iter = voice_notes.rbegin();
  ctx.prev_pitches[0] = iter->pitch;
  ctx.prev_count = 1;
  if (voice_notes.size() >= 2) {
    auto iter2 = iter;
    ++iter2;
    ctx.prev_pitches[1] = iter2->pitch;
    ctx.prev_count = 2;
    int dir = static_cast<int>(iter->pitch) - static_cast<int>(iter2->pitch);
    ctx.prev_direction = (dir > 0) ? 1 : (dir < 0) ? -1 : 0;
    ctx.leap_needs_resolution = (absoluteInterval(iter->pitch, iter2->pitch) >= 5);
  }
  if (voice_notes.size() >= 3) {
    auto iter3 = voice_notes.rbegin();
    ++iter3;
    ++iter3;
    ctx.prev_pitches[2] = iter3->pitch;
    ctx.prev_count = 3;
  }
  // Leading tone detection: pitch class == (key + 11) % 12
  uint8_t prev_pc = getPitchClass(ctx.prev_pitches[0]);
  ctx.is_leading_tone = (prev_pc == (static_cast<uint8_t>(state.getKey()) + 11) % 12);

  // Compute consecutive same-direction stepwise run length.
  // Walk back through the voice's recent history counting same-direction steps.
  ctx.consecutive_same_dir = 0;
  if (ctx.prev_count >= 2 && ctx.prev_direction != 0) {
    int8_t run_dir = ctx.prev_direction;
    int run = 0;
    for (int idx = static_cast<int>(voice_notes.size()) - 1; idx >= 1; --idx) {
      int diff = static_cast<int>(voice_notes[idx].pitch) -
                 static_cast<int>(voice_notes[idx - 1].pitch);
      int dir = (diff > 0) ? 1 : (diff < 0 ? -1 : 0);
      int ivl = std::abs(diff);
      if (dir == run_dir && ivl >= 1 && ivl <= 2) {
        ++run;
      } else {
        break;
      }
    }
    ctx.consecutive_same_dir = static_cast<int8_t>(run > 127 ? 127 : run);
  }

  return ctx;
}

namespace {

/// Classify ProtectionLevel into sort priority (lower = processed first).
int protectionPriority(ProtectionLevel level) {
  switch (level) {
    case ProtectionLevel::Immutable: return 0;
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
    PostValidateStats* stats,
    const ProtectionOverrides& protection_overrides,
    bool stylus_phantasticus) {
  auto range_fn = [&voice_ranges](uint8_t voice,
                                   Tick /*tick*/) -> std::pair<uint8_t, uint8_t> {
    if (voice < voice_ranges.size()) return voice_ranges[voice];
    return {0, 127};
  };
  return postValidateNotes(std::move(raw_notes), num_voices, key_sig, range_fn, stats,
                           protection_overrides, stylus_phantasticus);
}

std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t voice, Tick tick)> voice_range_fn,
    PostValidateStats* stats,
    const ProtectionOverrides& protection_overrides,
    bool stylus_phantasticus) {
  if (raw_notes.empty()) return {};

  // Initialize counterpoint engine.
  // Use voice ranges at tick 0 for initial registration.
  CounterpointState state;
  state.setKey(key_sig.tonic);
  for (uint8_t v = 0; v < num_voices; ++v) {
    auto [low, high] = voice_range_fn(v, 0);
    state.registerVoice(v, low, high);
  }

  BachRuleEvaluator rules(num_voices);
  rules.setFreeCounterpoint(true);

  CollisionResolver resolver;
  resolver.setRangeTolerance(0);  // Strict range enforcement for post-validation.

  // Build per-voice protection override lookup (num_voices-based, dynamic).
  std::vector<int> voice_prot_override(num_voices, -1);
  for (const auto& [v, level] : protection_overrides) {
    if (v < num_voices) {
      voice_prot_override[v] = static_cast<int>(level);
    }
  }

  // Sort by (tick ASC, protection_priority ASC, voice ASC).
  // Immutable notes processed first within each tick.
  std::stable_sort(raw_notes.begin(), raw_notes.end(),
      [&voice_prot_override, num_voices](const NoteEvent& a, const NoteEvent& b) {
        if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
        auto effectiveProt = [&](const NoteEvent& n) -> ProtectionLevel {
          if (n.voice < num_voices && voice_prot_override[n.voice] >= 0) {
            return static_cast<ProtectionLevel>(voice_prot_override[n.voice]);
          }
          return getProtectionLevel(n.source);
        };
        int pa = protectionPriority(effectiveProt(a));
        int pb = protectionPriority(effectiveProt(b));
        if (pa != pb) return pa < pb;
        return a.voice < b.voice;
      });

  // Build per-voice next-pitch index (after sort). O(n).
  // For each note, stores the pitch of the next note at a strictly later tick
  // in the same voice, enabling NHT validation in the repair cascade.
  std::vector<std::optional<uint8_t>> next_pitch_map(raw_notes.size());
  {
    std::array<std::vector<size_t>, 5> voice_indices;
    for (size_t idx = 0; idx < raw_notes.size(); ++idx) {
      uint8_t v = raw_notes[idx].voice;
      if (v < 5) voice_indices[v].push_back(idx);
    }
    for (uint8_t v = 0; v < 5; ++v) {
      const auto& vi = voice_indices[v];
      if (vi.size() < 2) continue;
      std::optional<uint8_t> last_candidate;
      Tick last_tick = 0;
      for (size_t k = vi.size(); k-- > 0;) {
        Tick this_tick = raw_notes[vi[k]].start_tick;
        if (last_tick > this_tick) {
          next_pitch_map[vi[k]] = last_candidate;
        }
        last_candidate = raw_notes[vi[k]].pitch;
        last_tick = this_tick;
      }
    }
  }

  PostValidateStats local_stats;
  std::vector<NoteEvent> result;
  result.reserve(raw_notes.size());
  std::vector<NoteEvent> dropped_flexible;  // Track for Phase 4 Structural rescue.

  size_t note_idx = 0;
  for (const auto& note : raw_notes) {
    local_stats.total_input++;

    ProtectionLevel prot = (note.voice < num_voices &&
                            voice_prot_override[note.voice] >= 0)
        ? static_cast<ProtectionLevel>(voice_prot_override[note.voice])
        : getProtectionLevel(note.source);

    if (prot == ProtectionLevel::Immutable) {
      // Immutable notes are registered directly without modification.
      state.setCurrentTick(note.start_tick);
      state.addNote(note.voice, note);
      result.push_back(note);
      local_stats.accepted_original++;
      ++note_idx;
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

    // Set next_pitch from pre-computed per-voice index for NHT validation.
    opts.next_pitch = next_pitch_map[note_idx];

    // Build melodic context from the last 2 notes in the same voice.
    const NoteEvent* prev = state.getLastNote(note.voice);
    if (prev) {
      opts.prev_pitches[0] = prev->pitch;
      opts.prev_count = 1;
      const auto& voice_notes = state.getVoiceNotes(note.voice);
      if (voice_notes.size() >= 2) {
        auto iter = voice_notes.rbegin();
        ++iter;
        opts.prev_pitches[1] = iter->pitch;
        opts.prev_count = 2;
        int dir = static_cast<int>(prev->pitch) - static_cast<int>(iter->pitch);
        opts.prev_direction = (dir > 0) ? 1 : (dir < 0) ? -1 : 0;
      } else {
        opts.prev_direction = (note.pitch > prev->pitch) ? 1 :
                              (note.pitch < prev->pitch) ? -1 : 0;
      }
    }

    state.setCurrentTick(note.start_tick);
    BachCreateNoteResult res = createBachNote(&state, &rules, &resolver, opts);

    if (res.accepted) {
      result.push_back(res.note);
      // Preserve gesture metadata through createBachNote pipeline.
      result.back().gesture_id = note.gesture_id;
      result.back().gesture_role = note.gesture_role;
      if (res.was_adjusted) {
        local_stats.repaired++;
      } else {
        local_stats.accepted_original++;
      }
    } else {
      local_stats.dropped++;
      // Track dropped Flexible notes for Structural micro-shift rescue.
      if (prot == ProtectionLevel::Flexible) {
        dropped_flexible.push_back(note);
      }
    }
    ++note_idx;
  }

  // --- Phase 3b: Voice-role-specific leap threshold enforcement ---
  // Scan consecutive same-voice notes and re-octave notes that exceed
  // voice-specific leap thresholds. This prevents unnaturally large leaps
  // while respecting that bass/pedal voices idiomatically use wider intervals.
  //
  // Thresholds by voice role (semitones):
  //   soprano/alto (voice 0, 1): 16 (10th) -- upper voices rarely exceed this
  //   tenor (voice num_voices-2):  16 (10th) -- inner voice, same as upper
  //   bass/pedal (last voice):     17 (11th) -- octave leaps permitted (Bach
  //     organ pedal idiom), but compound intervals beyond 11th are anomalous
  //   stylus_phantasticus forms:   19 (12th) -- toccata/fantasia allow wider
  //     leaps as part of the improvisatory rhetorical style
  {
    constexpr int kUpperVoiceMaxLeap = 16;    // 10th (compound minor 3rd)
    constexpr int kBassVoiceMaxLeap = 17;     // 11th (octave + minor 3rd)
    constexpr int kPhantasticusMaxLeap = 19;  // 12th (compound 5th)

    // Build per-voice index for sequential scanning.
    std::array<std::vector<size_t>, 5> voice_result_indices;
    for (size_t idx = 0; idx < result.size(); ++idx) {
      uint8_t vid = result[idx].voice;
      if (vid < 5) voice_result_indices[vid].push_back(idx);
    }

    for (uint8_t vid = 0; vid < std::min(num_voices, static_cast<uint8_t>(5)); ++vid) {
      const auto& indices = voice_result_indices[vid];
      bool is_bass_voice = (num_voices > 1 && vid == num_voices - 1);
      int base_max_leap = is_bass_voice ? kBassVoiceMaxLeap : kUpperVoiceMaxLeap;
      int max_leap = stylus_phantasticus
                         ? std::max(base_max_leap, kPhantasticusMaxLeap)
                         : base_max_leap;

      for (size_t pos = 1; pos < indices.size(); ++pos) {
        auto& curr = result[indices[pos]];
        const auto& prev = result[indices[pos - 1]];

        // Skip Immutable notes -- never modify identity notes.
        ProtectionLevel curr_prot = (curr.voice < num_voices &&
                                     voice_prot_override[curr.voice] >= 0)
            ? static_cast<ProtectionLevel>(voice_prot_override[curr.voice])
            : getProtectionLevel(curr.source);
        if (curr_prot == ProtectionLevel::Immutable) {
          continue;
        }

        int leap = std::abs(static_cast<int>(curr.pitch) -
                            static_cast<int>(prev.pitch));
        if (leap <= max_leap) continue;

        // Attempt re-octave: find the octave of curr.pitch closest to
        // prev.pitch that keeps the interval within the threshold and
        // stays within the voice range.
        auto [range_lo, range_hi] = voice_range_fn(vid, curr.start_tick);
        int pitch_class = static_cast<int>(curr.pitch) % 12;
        int prev_p = static_cast<int>(prev.pitch);

        int best_candidate = -1;
        int best_distance = 999;

        // Search all octave placements of the same pitch class within range.
        for (int octave = 0; octave <= 10; ++octave) {
          int candidate = pitch_class + octave * 12;
          if (candidate < static_cast<int>(range_lo) ||
              candidate > static_cast<int>(range_hi)) {
            continue;
          }
          int dist = std::abs(candidate - prev_p);
          if (dist <= max_leap && dist < best_distance) {
            best_candidate = candidate;
            best_distance = dist;
          }
        }

        if (best_candidate >= 0 &&
            static_cast<uint8_t>(best_candidate) != curr.pitch) {
          curr.pitch = static_cast<uint8_t>(best_candidate);
          curr.modified_by |= static_cast<uint8_t>(NoteModifiedBy::OctaveAdjust);
          local_stats.repaired++;
          // Corresponding decrement: this was already counted as
          // accepted_original or repaired in the main loop, but the
          // re-octave is an additional adjustment -- only count the
          // increment to track total modifications.
        }
      }
    }
  }

  // --- Phase 4: Structural micro-shift rescue for dropped Flexible notes ---
  // When a Flexible note was dropped due to dissonance with a Structural note
  // (specifically Countersubject), try +/-1/+/-2 step shifts on the Structural
  // note (direction-preserving) to resolve the conflict.
  // Limited to 20 rescues max and only targets Countersubject sources to
  // avoid O(n^2) blowup in large pieces (passacaglia, etc.).
  {
    constexpr int kMaxRescues = 20;
    int rescue_count = 0;
    // Build tick->structural note index for O(1) lookup.
    std::map<Tick, std::vector<size_t>> structural_at_tick;
    for (size_t idx = 0; idx < result.size(); ++idx) {
      if (result[idx].source == BachNoteSource::Countersubject) {
        structural_at_tick[result[idx].start_tick].push_back(idx);
      }
    }

    for (const auto& dropped : dropped_flexible) {
      if (rescue_count >= kMaxRescues) break;
      auto iter = structural_at_tick.find(dropped.start_tick);
      if (iter == structural_at_tick.end()) continue;

      for (size_t si : iter->second) {
        auto& structural = result[si];
        if (structural.voice == dropped.voice) continue;

        int interval = std::abs(static_cast<int>(structural.pitch) -
                                 static_cast<int>(dropped.pitch));
        int simple = interval_util::compoundToSimple(interval);
        if (interval_util::isConsonance(simple)) continue;

        // Determine direction from previous note.
        int prev_dir = 0;
        uint8_t prev_pitch = 0;
        for (auto rit = result.rbegin(); rit != result.rend(); ++rit) {
          if (rit->voice == structural.voice &&
              rit->start_tick < structural.start_tick) {
            prev_pitch = rit->pitch;
            if (structural.pitch > rit->pitch) prev_dir = 1;
            else if (structural.pitch < rit->pitch) prev_dir = -1;
            break;
          }
        }

        bool rescued = false;
        for (int delta : {-1, 1, -2, 2}) {
          int cand = static_cast<int>(structural.pitch) + delta;
          if (cand < 0 || cand > 127) continue;

          if (prev_dir != 0 && prev_pitch > 0) {
            int new_dir = (cand > static_cast<int>(prev_pitch)) ? 1 :
                          (cand < static_cast<int>(prev_pitch)) ? -1 : 0;
            if (new_dir != 0 && new_dir != prev_dir) continue;
          }

          auto [lo, hi] = voice_range_fn(structural.voice, structural.start_tick);
          if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi)) continue;

          int new_interval = std::abs(cand - static_cast<int>(dropped.pitch));
          int new_simple = interval_util::compoundToSimple(new_interval);
          if (!interval_util::isConsonance(new_simple)) continue;

          // Check consonance with all notes at the same tick.
          bool all_ok = true;
          for (size_t oi : iter->second) {
            if (oi == si) continue;
            int dist = std::abs(cand - static_cast<int>(result[oi].pitch));
            if (!interval_util::isConsonance(interval_util::compoundToSimple(dist))) {
              all_ok = false;
              break;
            }
          }
          if (!all_ok) continue;

          structural.pitch = static_cast<uint8_t>(cand);
          result.push_back(dropped);
          local_stats.dropped--;
          local_stats.repaired++;
          rescue_count++;
          rescued = true;
          break;
        }
        if (rescued) break;
      }
    }
  }

  // Clamp repaired notes to strict voice ranges (collision resolver allows
  // a soft tolerance, but final output must respect range boundaries).
  for (auto& note : result) {
    auto [lo, hi] = voice_range_fn(note.voice, note.start_tick);
    if (note.pitch < lo) note.pitch = lo;
    if (note.pitch > hi) note.pitch = hi;
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

// ---------------------------------------------------------------------------
// Policy-aware postValidateNotes overloads
// ---------------------------------------------------------------------------

std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    const std::vector<std::pair<uint8_t, uint8_t>>& voice_ranges,
    PostValidateStats* stats,
    const ProtectionOverrides& protection_overrides,
    const PostValidatePolicy& policy,
    Tick cadence_tick) {
  // Delegate to the function-based overload (same pattern as the original pair).
  auto range_fn = [&voice_ranges](uint8_t voice,
                                   Tick /*tick*/) -> std::pair<uint8_t, uint8_t> {
    if (voice < voice_ranges.size()) return voice_ranges[voice];
    return {0, 127};
  };
  return postValidateNotes(std::move(raw_notes), num_voices, key_sig, range_fn,
                           stats, protection_overrides, policy, cadence_tick);
}

std::vector<NoteEvent> postValidateNotes(
    std::vector<NoteEvent> raw_notes,
    uint8_t num_voices,
    KeySignature key_sig,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t voice, Tick tick)> voice_range_fn,
    PostValidateStats* stats,
    const ProtectionOverrides& protection_overrides,
    const PostValidatePolicy& policy,
    Tick cadence_tick) {
  // protection_overrides, cadence_tick, num_voices, key_sig, and policy are
  // reserved for future refinement (cadence-zone resolution direction,
  // per-voice override in targeted pass).
  (void)protection_overrides;
  (void)cadence_tick;
  (void)num_voices;
  (void)key_sig;
  (void)policy;

  if (raw_notes.empty()) {
    if (stats) *stats = {};
    return {};
  }

  // This overload is a targeted safety net for notes that have ALREADY been
  // through a full validation pass. Unlike the existing postValidateNotes
  // (which builds a fresh CounterpointState and runs the full createBachNote
  // cascade), this version performs only:
  //   1. Range clamping (non-destructive, never drops notes)
  //   2. Shift-magnitude stat tracking
  //   3. Protected-note integrity verification
  //
  // This avoids the problem of re-validating already-valid notes against a
  // fresh counterpoint state, which can spuriously reject notes whose
  // counterpoint was correct in the original context.

  PostValidateStats local_stats;
  local_stats.total_input = static_cast<uint32_t>(raw_notes.size());

  // Snapshot original pitches for shift tracking.
  std::vector<uint8_t> original_pitches;
  original_pitches.reserve(raw_notes.size());
  for (const auto& note : raw_notes) {
    original_pitches.push_back(note.pitch);
  }

  // --- Step 1: Range clamping ---
  // Clamp notes to voice ranges. Non-structural notes that are out of range
  // get their pitch adjusted; no notes are dropped.
  for (auto& note : raw_notes) {
    auto [lo, hi] = voice_range_fn(note.voice, note.start_tick);
    if (note.pitch < lo || note.pitch > hi) {
      ProtectionLevel prot = getProtectionLevel(note.source);
      if (prot == ProtectionLevel::Immutable) {
        // Never touch immutable notes.
        continue;
      }
      if (note.pitch < lo) note.pitch = lo;
      if (note.pitch > hi) note.pitch = hi;
    }
  }

  // --- Step 2: Compute shift statistics and protected-note tracking ---
  uint32_t subject_touches = 0;
  uint32_t countersubject_touches = 0;
  int total_shift = 0;
  int max_shift = 0;
  int shift_count = 0;
  uint32_t repaired = 0;

  for (size_t idx = 0; idx < raw_notes.size(); ++idx) {
    int shift = std::abs(static_cast<int>(raw_notes[idx].pitch) -
                         static_cast<int>(original_pitches[idx]));
    if (shift > 0) {
      repaired++;
      total_shift += shift;
      shift_count++;
      if (shift > max_shift) max_shift = shift;

      // Track if a protected note was modified.
      BachNoteSource src = raw_notes[idx].source;
      if (src == BachNoteSource::SubjectCore ||
          src == BachNoteSource::FugueSubject ||
          src == BachNoteSource::FugueAnswer) {
        subject_touches++;
        // Restore protected note to original pitch.
        raw_notes[idx].pitch = original_pitches[idx];
      } else if (src == BachNoteSource::Countersubject) {
        countersubject_touches++;
        // Restore protected note to original pitch.
        raw_notes[idx].pitch = original_pitches[idx];
      }
    }
  }

  // --- Populate stats ---
  local_stats.accepted_original = local_stats.total_input - repaired;
  local_stats.repaired = repaired;
  local_stats.dropped = 0;  // Targeted pass never drops notes.
  local_stats.subject_touches = subject_touches;
  local_stats.countersubject_touches = countersubject_touches;
  local_stats.stretto_section_touches = 0;
  local_stats.avg_shift_semitones = shift_count > 0
      ? static_cast<float>(total_shift) / static_cast<float>(shift_count)
      : 0.0f;
  local_stats.max_shift_semitones = max_shift;
  local_stats.parallel_fixes = 0;
  local_stats.crossing_fixes = 0;
  local_stats.dissonance_fixes = 0;

  if (stats) *stats = local_stats;
  return raw_notes;
}

bool isVerticallyConsonant(uint8_t pitch, uint8_t voice, Tick tick,
                           const std::vector<NoteEvent>& placed,
                           uint8_t num_voices) {
  uint8_t lowest = pitch;
  for (const auto& note : placed) {
    if (note.voice == voice) continue;
    if (note.start_tick + note.duration <= tick || note.start_tick > tick) continue;
    if (note.pitch < lowest) lowest = note.pitch;
  }
  for (const auto& note : placed) {
    if (note.voice == voice) continue;
    if (note.start_tick + note.duration <= tick || note.start_tick > tick) continue;
    int reduced = interval_util::compoundToSimple(
        absoluteInterval(pitch, note.pitch));
    if (!interval_util::isConsonance(reduced)) {
      if (num_voices >= 3 && reduced == interval::kPerfect4th) {
        uint8_t lower = std::min(pitch, note.pitch);
        if (lower > lowest) continue;  // P4 between upper voices: OK.
      }
      return false;
    }
  }
  return true;
}

}  // namespace bach
