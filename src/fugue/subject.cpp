/// @file
/// @brief Fugue subject generation with character-driven note and rhythm selection.

#include "fugue/subject.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/archetype_policy.h"
#include "fugue/archetype_scorer.h"
#include "fugue/motif_template.h"
#include "fugue/subject_params.h"
#include "fugue/subject_validator.h"
#include "fugue/toccata_affinity.h"

namespace bach {

// ---------------------------------------------------------------------------
// Subject member functions
// ---------------------------------------------------------------------------

uint8_t Subject::lowestPitch() const {
  if (notes.empty()) return 127;
  uint8_t lowest = 127;
  for (const auto& note : notes) {
    if (note.pitch < lowest) lowest = note.pitch;
  }
  return lowest;
}

uint8_t Subject::highestPitch() const {
  if (notes.empty()) return 0;
  uint8_t highest = 0;
  for (const auto& note : notes) {
    if (note.pitch > highest) highest = note.pitch;
  }
  return highest;
}

int Subject::range() const {
  if (notes.empty()) return 0;
  return static_cast<int>(highestPitch()) - static_cast<int>(lowestPitch());
}

size_t Subject::noteCount() const {
  return notes.size();
}

std::vector<NoteEvent> Subject::extractKopfmotiv(size_t max_notes) const {
  std::vector<NoteEvent> result;
  size_t count = std::min(max_notes, notes.size());
  result.reserve(count);
  for (size_t idx = 0; idx < count; ++idx) {
    result.push_back(notes[idx]);
  }
  return result;
}

// Forward declarations for functions defined later in this file.
int maxLeapForCharacter(SubjectCharacter ch);

// ---------------------------------------------------------------------------
// Shared subject/answer pitch helpers
// ---------------------------------------------------------------------------

int maxLeapForCharacter(SubjectCharacter ch) {
  switch (ch) {
    case SubjectCharacter::Severe:  return 7;   // P5 max
    case SubjectCharacter::Noble:   return 7;   // P5 max
    case SubjectCharacter::Playful: return 9;   // M6 max (low frequency)
    case SubjectCharacter::Restless: return 9;  // M6 max (low frequency)
  }
  return 7;
}

int normalizeEndingPitch(int target_pitch_class, int prev_pitch,
                         int max_leap, Key key, ScaleType scale,
                         int floor, int ceil) {
  // Find nearest octave placement of target_pitch_class to prev_pitch.
  int shift = nearestOctaveShift(prev_pitch - target_pitch_class);
  int best_candidate = target_pitch_class + shift;

  // Check the nearest and its neighbors, respecting range and max_leap.
  int best = -1;
  int best_dist = 999;
  for (int offset : {0, -12, 12}) {
    int candidate = best_candidate + offset;
    if (candidate < floor || candidate > ceil) continue;
    int dist = std::abs(candidate - prev_pitch);
    if (dist <= max_leap && dist < best_dist) {
      best = candidate;
      best_dist = dist;
    }
  }
  if (best >= 0) return best;

  // No octave within max_leap: use stepwise approach (2-1 or 7-1).
  int step_approach;
  if (prev_pitch > ceil) {
    step_approach = prev_pitch - 2;
  } else if (prev_pitch < floor) {
    step_approach = prev_pitch + 2;
  } else {
    int nearest_target = target_pitch_class + (prev_pitch / 12) * 12;
    if (std::abs(nearest_target + 12 - prev_pitch) <
        std::abs(nearest_target - prev_pitch)) {
      nearest_target += 12;
    }
    step_approach = (prev_pitch > nearest_target) ? prev_pitch - 2 : prev_pitch + 2;
  }
  return snapToScale(step_approach, key, scale, floor, ceil);
}

// ---------------------------------------------------------------------------
// N-candidate pitch path selection: internal types and helpers
// ---------------------------------------------------------------------------

namespace {

/// Phase within the subject skeleton.
enum class SlotPhase : uint8_t {
  kMotifA,   ///< Ascending toward climax.
  kClimax,   ///< Climax note.
  kMotifB,   ///< Descending from climax.
  kCadence   ///< Cadential formula.
};

/// A single slot in the rhythm skeleton (fixed timing, pitch varies per candidate).
struct SkeletonSlot {
  Tick start_tick;
  Tick duration;
  SlotPhase phase;
  int degree_hint;    ///< Motif degree offset (MotifA/B) or absolute degree (Cadence).
  size_t phase_index; ///< Index within this phase (0, 1, 2, ...).
  NoteFunction function = NoteFunction::StructuralTone;  ///< Structural role hint.
};

/// Pre-computed anchor data for subject generation (fixed per seed).
struct SubjectAnchors {
  int start_degree;
  int start_pitch;
  int tonic_pitch;
  int climax_pitch;
  Tick climax_tick;
  Tick total_ticks;
  int pitch_floor;
  int pitch_ceil;
  int climax_abs_degree;
  Key key;
  int key_offset;
  ScaleType scale;
  SubjectCharacter character;
  float fluctuation_rate;
};

/// Build the rhythm skeleton from anchors and motif templates.
///
/// Computes all note slot timing (start_tick, duration) and phase assignments.
/// This is computed once per seed; pitch path candidates vary only pitch.
std::vector<SkeletonSlot> buildRhythmSkeleton(
    const SubjectAnchors& a,
    const MotifTemplate& motif_a,
    const MotifTemplate& motif_b,
    const CadentialFormula& cadence,
    const AccelProfile& accel_profile,
    std::mt19937& rhythm_gen) {
  std::vector<SkeletonSlot> slots;
  Tick current_tick = 0;

  // --- Motif A slots ---
  size_t motif_a_count = 0;
  for (size_t idx = 0;
       idx < motif_a.degree_offsets.size() && current_tick < a.climax_tick;
       ++idx) {
    Tick duration = (idx < motif_a.durations.size())
                        ? motif_a.durations[idx]
                        : kTicksPerBeat;

    // Apply acceleration profile before pair substitution.
    // Progress: 0.0 at start, 1.0 at end of motif phase.
    float accel_progress = (a.total_ticks > 0)
        ? static_cast<float>(current_tick) / static_cast<float>(a.total_ticks)
        : 0.0f;
    if (accel_profile.curve_type == AccelCurveType::EaseIn) {
      // Ease-in: slow start (factor ~1.0), accelerate at end (factor ~0.5).
      float factor = 1.0f - 0.5f * accel_progress * accel_progress;
      duration = std::max(accel_profile.min_dur,
                          static_cast<Tick>(static_cast<float>(duration) * factor));
    } else if (accel_profile.curve_type == AccelCurveType::Linear) {
      float factor = 1.0f - 0.4f * accel_progress;
      duration = std::max(accel_profile.min_dur,
                          static_cast<Tick>(static_cast<float>(duration) * factor));
    }

    // Pair substitution on even-indexed pairs only.
    // Guard first 3 notes from variation to preserve motif recognition.
    if (idx >= 3 && idx % 2 == 0 && idx + 1 < motif_a.durations.size() &&
        idx + 1 < motif_a.degree_offsets.size()) {
      Tick next_dur = motif_a.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, a.character, rhythm_gen, new_a, new_b);
      // Clamp varied duration to prevent undoing acceleration (+-1 duration class).
      if (accel_profile.curve_type != AccelCurveType::None) {
        Tick accel_floor = std::max(accel_profile.min_dur, duration / 2);
        Tick accel_ceil = duration * 2;
        new_a = std::clamp(new_a, accel_floor, accel_ceil);
      }
      duration = new_a;
    }

    if (current_tick + duration > a.climax_tick) {
      duration = a.climax_tick - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    NoteFunction func = (idx < motif_a.functions.size())
                             ? motif_a.functions[idx]
                             : NoteFunction::StructuralTone;
    slots.push_back({current_tick, duration, SlotPhase::kMotifA,
                      motif_a.degree_offsets[idx], motif_a_count, func});
    current_tick += duration;
    ++motif_a_count;
  }

  // Bridge gap: extend last Motif A slot to reach climax_tick.
  if (current_tick < a.climax_tick && !slots.empty()) {
    slots.back().duration += (a.climax_tick - current_tick);
    current_tick = a.climax_tick;
  }

  // --- Climax slot ---
  if (current_tick < a.total_ticks) {
    Tick climax_dur = kTicksPerBeat;
    if (current_tick + climax_dur > a.total_ticks) {
      climax_dur = a.total_ticks - current_tick;
    }
    slots.push_back({current_tick, climax_dur, SlotPhase::kClimax, 0, 0,
                      NoteFunction::ClimaxTone});
    current_tick += climax_dur;
  }

  // --- Motif B slots ---
  size_t motif_b_count = 0;
  for (size_t idx = 0;
       idx < motif_b.degree_offsets.size() && current_tick < a.total_ticks;
       ++idx) {
    Tick duration = (idx < motif_b.durations.size())
                        ? motif_b.durations[idx]
                        : kTicksPerBeat;

    // Apply acceleration profile to Motif B as well.
    float accel_progress_b = (a.total_ticks > 0)
        ? static_cast<float>(current_tick) / static_cast<float>(a.total_ticks)
        : 0.0f;
    if (accel_profile.curve_type == AccelCurveType::EaseIn) {
      float factor = 1.0f - 0.5f * accel_progress_b * accel_progress_b;
      duration = std::max(accel_profile.min_dur,
                          static_cast<Tick>(static_cast<float>(duration) * factor));
    } else if (accel_profile.curve_type == AccelCurveType::Linear) {
      float factor = 1.0f - 0.4f * accel_progress_b;
      duration = std::max(accel_profile.min_dur,
                          static_cast<Tick>(static_cast<float>(duration) * factor));
    }

    if (idx % 2 == 0 && idx + 1 < motif_b.durations.size()) {
      Tick next_dur = motif_b.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, a.character, rhythm_gen, new_a, new_b);
      // Clamp varied duration to prevent undoing acceleration.
      if (accel_profile.curve_type != AccelCurveType::None) {
        Tick accel_floor = std::max(accel_profile.min_dur, duration / 2);
        Tick accel_ceil = duration * 2;
        new_a = std::clamp(new_a, accel_floor, accel_ceil);
      }
      duration = new_a;
    }

    if (current_tick + duration > a.total_ticks) {
      duration = a.total_ticks - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    NoteFunction func_b = (idx < motif_b.functions.size())
                               ? motif_b.functions[idx]
                               : NoteFunction::StructuralTone;
    slots.push_back({current_tick, duration, SlotPhase::kMotifB,
                      motif_b.degree_offsets[idx], motif_b_count, func_b});
    current_tick += duration;
    ++motif_b_count;
  }

  // --- Cadence slots ---
  if (current_tick < a.total_ticks) {
    int tonic_abs_degree = scale_util::pitchToAbsoluteDegree(
        clampPitch(a.tonic_pitch, 0, 127),
        a.key, a.scale);

    Tick cadence_total = 0;
    for (int ci = 0; ci < cadence.count; ++ci) {
      cadence_total += cadence.durations[ci];
    }

    Tick remaining = a.total_ticks - current_tick;
    int start_idx = 0;
    if (remaining < cadence_total) {
      Tick accumulated = 0;
      for (int ci = 0; ci < cadence.count; ++ci) {
        if (cadence_total - accumulated <= remaining) {
          start_idx = ci;
          break;
        }
        accumulated += cadence.durations[ci];
      }
    }

    size_t cadence_count = 0;
    for (int ci = start_idx;
         ci < cadence.count && current_tick < a.total_ticks; ++ci) {
      Tick dur = cadence.durations[ci];
      if (current_tick + dur > a.total_ticks) {
        dur = a.total_ticks - current_tick;
        if (dur < kTicksPerBeat / 4) break;
      }

      int degree_abs = tonic_abs_degree + cadence.degrees[ci];
      slots.push_back({current_tick, dur, SlotPhase::kCadence,
                        degree_abs, cadence_count, NoteFunction::CadentialTone});
      current_tick += dur;
      ++cadence_count;
    }
  }

  return slots;
}

/// @brief Evaluate how well a candidate pitch fits its NoteFunction.
///
/// Returns a bonus in [0, 1] for use in composite scoring. Each NoteFunction
/// type rewards pitch choices that match its structural role.
///
/// @param candidate_pitch The candidate MIDI pitch.
/// @param prev_pitch Previous note pitch (-1 if none).
/// @param function The NoteFunction for this slot.
/// @param key Musical key.
/// @param scale Scale type.
/// @param num_slots Total skeleton slots (for normalization).
/// @return Bonus score in [0, 1].
float evaluateNoteFunctionFit(int candidate_pitch, int prev_pitch,
                               NoteFunction function, Key key,
                               ScaleType /* scale */, size_t num_slots) {
  if (num_slots == 0) return 0.0f;
  float per_note = 1.0f / static_cast<float>(num_slots);

  switch (function) {
    case NoteFunction::StructuralTone: {
      // Structural tones should be chord tones (scale degrees 1, 3, 5).
      int pc = getPitchClass(static_cast<uint8_t>(candidate_pitch));
      int root = static_cast<int>(key);
      int rel = getPitchClassSigned(pc - root);
      // Major/minor chord tones: root(0), third(3 or 4), fifth(7).
      if (rel == 0 || rel == 3 || rel == 4 || rel == 7) return per_note;
      return 0.0f;
    }
    case NoteFunction::PassingTone: {
      // Passing tones should be stepwise from previous note.
      if (prev_pitch < 0) return 0.0f;
      int dist = std::abs(candidate_pitch - prev_pitch);
      if (dist >= 1 && dist <= 2) return per_note;
      return 0.0f;
    }
    case NoteFunction::NeighborTone: {
      // Neighbor tones should be within 2 semitones of previous.
      if (prev_pitch < 0) return 0.0f;
      int dist = std::abs(candidate_pitch - prev_pitch);
      if (dist <= 2) return per_note;
      return 0.0f;
    }
    case NoteFunction::Resolution: {
      // Resolution: descending or ascending semitone from previous.
      if (prev_pitch < 0) return 0.0f;
      int interval = candidate_pitch - prev_pitch;
      if (interval == -1 || interval == -2 || interval == 1) return per_note;
      return 0.0f;
    }
    case NoteFunction::SequenceHead: {
      // SequenceHead: no tritone from previous note.
      if (prev_pitch < 0) return per_note;
      int dist = std::abs(candidate_pitch - prev_pitch);
      int simple = interval_util::compoundToSimple(dist);
      if (simple != 6) return per_note;  // Not a tritone.
      return 0.0f;
    }
    // LeapTone, ClimaxTone, CadentialTone: handled by existing phase logic.
    default:
      return 0.0f;
  }
}

/// Generate a pitch path for the given skeleton using a candidate-specific seed.
///
/// Legacy pitch path: anchors and rhythm skeleton are fixed; only pitch decisions
/// vary per candidate. Used as fallback when Kerngestalt path generation fails.
std::vector<NoteEvent> generateLegacyPitchPath(
    const SubjectAnchors& a,
    const std::vector<SkeletonSlot>& skeleton,
    const ArchetypePolicy& policy,
    uint32_t path_seed) {
  constexpr int kBaseNote = 60;
  std::mt19937 gen(path_seed);

  std::vector<NoteEvent> result;
  result.reserve(skeleton.size());

  bool needs_compensation = false;
  int compensation_direction = 0;
  int large_leap_count = 0;
  bool last_fluctuated = false;

  // Climax pitch may be adjusted per-candidate (depends on preceding pitch).
  int actual_climax_pitch = a.climax_pitch;
  int actual_climax_abs_degree = a.climax_abs_degree;

  for (size_t si = 0; si < skeleton.size(); ++si) {
    const auto& slot = skeleton[si];
    int pitch = 0;

    switch (slot.phase) {
      case SlotPhase::kMotifA: {
        int target_degree = a.start_degree + slot.degree_hint;
        int target_pitch =
            degreeToPitch(target_degree, kBaseNote, a.key_offset, a.scale);

        // Scale toward climax: interpolate pitch between start and climax.
        float progress =
            (a.climax_tick > 0)
                ? static_cast<float>(slot.start_tick) /
                      static_cast<float>(a.climax_tick)
                : 0.0f;
        int interp_pitch =
            a.start_pitch +
            static_cast<int>(
                static_cast<float>(a.climax_pitch - a.start_pitch) * progress);

        int degree_shift = 0;
        if (target_pitch < interp_pitch) {
          degree_shift = (interp_pitch - target_pitch + 1) / 2;
        }
        int adjusted_degree = target_degree + degree_shift;

        // Interval fluctuation: skip first note of phase.
        if (slot.phase_index > 0 &&
            rng::rollProbability(gen, a.fluctuation_rate)) {
          adjusted_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
        }

        // Compensation after large leap.
        if (needs_compensation) {
          adjusted_degree += compensation_direction;
          needs_compensation = false;
        }

        pitch = snapToScale(
            degreeToPitch(adjusted_degree, kBaseNote, a.key_offset, a.scale),
            a.key, a.scale, a.pitch_floor, a.climax_pitch);

        // Post-leap stepwise preference for Kopfmotiv (70%).
        if (slot.phase_index > 0 && slot.phase_index <= 2 && !result.empty()) {
          int prev_p = static_cast<int>(result.back().pitch);
          int head_interval = (result.size() >= 2)
              ? std::abs(static_cast<int>(result[1].pitch) -
                         static_cast<int>(result[0].pitch))
              : 0;
          if (head_interval >= 5 && rng::rollProbability(gen, 0.70f)) {
            int step_dir = (result.back().pitch > result[0].pitch) ? -1 : 1;
            int step_pitch = snapToScale(
                prev_p + step_dir * 2, a.key, a.scale,
                a.pitch_floor, a.pitch_ceil);
            pitch = step_pitch;
          }
        }

        int prev_pitch =
            result.empty() ? -1 : static_cast<int>(result.back().pitch);
        pitch = avoidUnison(pitch, prev_pitch, a.key, a.scale,
                            a.pitch_floor, a.climax_pitch);
        pitch = clampLeap(pitch, prev_pitch, a.character, a.key, a.scale,
                          a.pitch_floor, a.climax_pitch, gen, &large_leap_count);

        if (prev_pitch >= 0 && std::abs(pitch - prev_pitch) >= 5) {
          needs_compensation = true;
          compensation_direction = (pitch > prev_pitch) ? -1 : 1;
        }
        break;
      }

      case SlotPhase::kClimax: {
        int clamped_climax = a.climax_pitch;
        if (!result.empty()) {
          int prev = static_cast<int>(result.back().pitch);
          clamped_climax =
              clampLeap(a.climax_pitch, prev, a.character, a.key, a.scale,
                        a.pitch_floor, a.pitch_ceil, gen, &large_leap_count);
        }
        pitch = clamped_climax;
        actual_climax_pitch = clamped_climax;
        actual_climax_abs_degree = scale_util::pitchToAbsoluteDegree(
            clampPitch(clamped_climax, 0, 127),
            a.key, a.scale);

        // Reset state for descent phase.
        needs_compensation = false;
        compensation_direction = 0;
        last_fluctuated = false;
        break;
      }

      case SlotPhase::kMotifB: {
        int offset = slot.degree_hint;

        float remaining_ratio =
            (a.total_ticks > a.climax_tick)
                ? static_cast<float>(slot.start_tick - a.climax_tick) /
                      static_cast<float>(a.total_ticks - a.climax_tick)
                : 1.0f;
        int interp_pitch =
            actual_climax_pitch +
            static_cast<int>(
                static_cast<float>(a.tonic_pitch - actual_climax_pitch) *
                remaining_ratio);

        int target_degree_abs = actual_climax_abs_degree + offset;
        int target_pitch = static_cast<int>(
            scale_util::absoluteDegreeToPitch(target_degree_abs, a.key,
                                               a.scale));

        int degree_shift = 0;
        if (target_pitch > interp_pitch + 2) {
          degree_shift = -1;
        } else if (target_pitch < interp_pitch - 2) {
          degree_shift = 1;
        }
        int adjusted_abs_degree = target_degree_abs + degree_shift;

        // Fluctuation: skip first note of phase, never two consecutive.
        bool fluctuated = false;
        if (slot.phase_index > 0 && !last_fluctuated &&
            rng::rollProbability(gen, a.fluctuation_rate)) {
          adjusted_abs_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
          fluctuated = true;
        }
        last_fluctuated = fluctuated;

        if (needs_compensation) {
          adjusted_abs_degree += compensation_direction;
          needs_compensation = false;
        }

        pitch = snapToScale(
            static_cast<int>(scale_util::absoluteDegreeToPitch(
                adjusted_abs_degree, a.key, a.scale)),
            a.key, a.scale, a.pitch_floor, actual_climax_pitch);

        int prev_pitch =
            result.empty() ? -1 : static_cast<int>(result.back().pitch);
        pitch = avoidUnison(pitch, prev_pitch, a.key, a.scale,
                            a.pitch_floor, actual_climax_pitch);
        pitch = clampLeap(pitch, prev_pitch, a.character, a.key, a.scale,
                          a.pitch_floor, actual_climax_pitch, gen,
                          &large_leap_count);

        if (prev_pitch >= 0 && std::abs(pitch - prev_pitch) >= 5) {
          needs_compensation = true;
          compensation_direction = (pitch > prev_pitch) ? -1 : 1;
        }
        break;
      }

      case SlotPhase::kCadence: {
        pitch = snapToScale(
            static_cast<int>(scale_util::absoluteDegreeToPitch(
                slot.degree_hint, a.key, a.scale)),
            a.key, a.scale, a.pitch_floor, actual_climax_pitch);

        int prev_pitch =
            result.empty() ? -1 : static_cast<int>(result.back().pitch);
        pitch = avoidUnison(pitch, prev_pitch, a.key, a.scale,
                            a.pitch_floor, actual_climax_pitch);
        pitch = clampLeap(pitch, prev_pitch, a.character, a.key, a.scale,
                          a.pitch_floor, actual_climax_pitch, gen,
                          &large_leap_count);
        break;
      }
    }

    // Tritone avoidance: always correct tritone leaps.
    // Only exempt: both notes <= 240 ticks (matches validator short-note rule).
    if (!result.empty()) {
      int prev = static_cast<int>(result.back().pitch);
      int dist = std::abs(pitch - prev);
      int simple = interval_util::compoundToSimple(dist);
      if (simple == 6) {
        bool exempt = false;

        // Short note exemption: both notes <= 240 ticks.
        Tick prev_dur = result.back().duration;
        if (prev_dur <= 240 && slot.duration <= 240) exempt = true;

        if (!exempt) {
          // Evaluate 3 candidates and pick the best non-tritone replacement.
          // (1) opposite-dir +/-1 snapped to scale (chromatic passing, Bach-like)
          // (2) same-dir +/-1 snapped to scale
          // (3) same-dir +2 snapped to scale (last resort)
          int direction = (pitch > prev) ? 1 : -1;
          struct TritoneCandidate {
            int cand_pitch;
            int score;  // Lower is better.
          };
          TritoneCandidate candidates[3];
          // (1) Opposite direction +-1.
          candidates[0].cand_pitch = snapToScale(
              pitch + (-direction), a.key, a.scale, a.pitch_floor, a.pitch_ceil);
          // (2) Same direction +-1.
          candidates[1].cand_pitch = snapToScale(
              pitch + direction, a.key, a.scale, a.pitch_floor, a.pitch_ceil);
          // (3) Same direction +2.
          candidates[2].cand_pitch = snapToScale(
              pitch + direction * 2, a.key, a.scale, a.pitch_floor, a.pitch_ceil);

          int best_idx = -1;
          int best_score = 9999;
          for (int cidx = 0; cidx < 3; ++cidx) {
            int cand = candidates[cidx].cand_pitch;
            int cand_simple = interval_util::compoundToSimple(std::abs(cand - prev));
            if (cand_simple == 6) continue;  // Still a tritone, skip.
            int leap_size = std::abs(cand - prev);
            int unison_penalty = (cand == prev) ? 30 : 0;
            int score = leap_size + unison_penalty;
            if (score < best_score) {
              best_score = score;
              best_idx = cidx;
            }
          }
          if (best_idx >= 0) {
            pitch = candidates[best_idx].cand_pitch;
          }
        }
      }
    }

    NoteEvent note;
    note.start_tick = slot.start_tick;
    note.duration = slot.duration;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = 0;
    note.source = BachNoteSource::FugueSubject;
    result.push_back(note);
  }

  // Ending normalization: 70% dominant, 30% tonic (per archetype policy).
  if (!result.empty()) {
    int prev_pitch_for_ending =
        (result.size() >= 2)
            ? static_cast<int>(result[result.size() - 2].pitch)
            : static_cast<int>(result.back().pitch);
    int max_leap = maxLeapForCharacter(a.character);

    float dominant_rate = policy.dominant_ending_prob;
    if (rng::rollProbability(gen, dominant_rate)) {
      int dom_pc =
          getPitchClass(static_cast<uint8_t>(degreeToPitch(4, kBaseNote, a.key_offset, a.scale)));
      int ending = normalizeEndingPitch(dom_pc, prev_pitch_for_ending,
                                        max_leap, a.key, a.scale,
                                        a.pitch_floor, a.pitch_ceil);
      result.back().pitch = static_cast<uint8_t>(ending);
    } else {
      int tonic_pc = getPitchClass(static_cast<uint8_t>(a.tonic_pitch));
      int ending = normalizeEndingPitch(tonic_pc, prev_pitch_for_ending,
                                        max_leap, a.key, a.scale,
                                        a.pitch_floor, a.pitch_ceil);
      result.back().pitch = static_cast<uint8_t>(ending);
    }
  }

  // Snap all start_ticks to 16th-note grid for metric integrity.
  constexpr Tick kTickQuantum = kTicksPerBeat / 4;  // 120
  for (auto& note : result) {
    note.start_tick = (note.start_tick / kTickQuantum) * kTickQuantum;
  }
  // Fix any overlaps introduced by quantization.
  for (size_t i = 0; i + 1 < result.size(); ++i) {
    Tick next_start = result[i + 1].start_tick;
    if (result[i].start_tick + result[i].duration > next_start) {
      result[i].duration = next_start - result[i].start_tick;
      if (result[i].duration < kTickQuantum) {
        result[i].duration = kTickQuantum;
      }
    }
  }

  // Post-processing leap enforcement: catch intervals that escaped
  // per-note clampLeap (ending normalization, climax transitions, etc.).
  int post_max_leap = maxLeapForCharacter(a.character);
  for (size_t i = 1; i < result.size(); ++i) {
    int prev_p = static_cast<int>(result[i - 1].pitch);
    int cur_p = static_cast<int>(result[i].pitch);
    int interval = cur_p - prev_p;
    if (std::abs(interval) > post_max_leap) {
      int direction = (interval > 0) ? 1 : -1;
      for (int attempt = post_max_leap; attempt >= 0; --attempt) {
        int candidate = prev_p + direction * attempt;
        candidate =
            std::max(a.pitch_floor, std::min(a.pitch_ceil, candidate));
        int snapped =
            snapToScale(candidate, a.key, a.scale, a.pitch_floor, a.pitch_ceil);
        if (std::abs(snapped - prev_p) <= post_max_leap) {
          result[i].pitch = clampPitch(snapped, 0, 127);
          break;
        }
      }
    }
  }

  // Post-processing: fix ALL remaining tritone intervals.
  // Max 2 passes to resolve chains introduced by corrections.
  for (int tritone_pass = 0; tritone_pass < 2; ++tritone_pass) {
    bool any_fixed = false;
    for (size_t idx = 1; idx < result.size(); ++idx) {
      int prev_p = static_cast<int>(result[idx - 1].pitch);
      int cur_p = static_cast<int>(result[idx].pitch);
      int simple = interval_util::compoundToSimple(std::abs(cur_p - prev_p));
      if (simple != 6) continue;

      // Short note exemption: both notes <= 240 ticks.
      if (result[idx - 1].duration <= 240 && result[idx].duration <= 240) continue;

      // Try deltas {1, -1, 2, -2}, snap to scale, pick best non-tritone
      // candidate that doesn't exceed post_max_leap.
      int best_cand = cur_p;
      int best_cost = 9999;
      for (int delta : {1, -1, 2, -2}) {
        int shifted = cur_p + delta;
        int snapped = snapToScale(shifted, a.key, a.scale,
                                  a.pitch_floor, a.pitch_ceil);
        int new_simple = interval_util::compoundToSimple(
            std::abs(snapped - prev_p));
        if (new_simple == 6) continue;  // Still a tritone.
        if (std::abs(snapped - prev_p) > post_max_leap) continue;
        // Also check forward: if there is a next note, avoid creating a new
        // tritone with it.
        if (idx + 1 < result.size()) {
          int next_p = static_cast<int>(result[idx + 1].pitch);
          int fwd_simple = interval_util::compoundToSimple(
              std::abs(snapped - next_p));
          if (fwd_simple == 6) continue;
        }
        int cost = std::abs(snapped - prev_p) + ((snapped == prev_p) ? 30 : 0);
        if (cost < best_cost) {
          best_cost = cost;
          best_cand = snapped;
        }
      }
      if (best_cand != cur_p) {
        result[idx].pitch = clampPitch(best_cand, 0, 127);
        any_fixed = true;
      }
    }
    if (!any_fixed) break;
  }

  return result;
}

/// @brief Generate a pitch path using Kerngestalt cell placement.
///
/// Places a core cell at the subject head, then fills remaining slots
/// using type-aware pitch selection. Returns the note sequence with
/// cell_window marking the core cell boundaries.
///
/// @param anchors Pre-computed anchors.
/// @param skeleton Rhythm skeleton slots.
/// @param policy Archetype policy.
/// @param cell The Kerngestalt cell to place.
/// @param path_seed Per-candidate RNG seed.
/// @param[out] out_cell_window Cell window boundaries (set on success).
/// @return Generated note sequence, or empty if cell cannot be placed.
std::vector<NoteEvent> generateKerngestaltPath(
    const SubjectAnchors& anchors,
    const std::vector<SkeletonSlot>& skeleton,
    const ArchetypePolicy& policy,
    const KerngestaltCell& cell,
    uint32_t path_seed,
    CellWindow& out_cell_window) {
  constexpr int kBaseNote = 60;
  std::mt19937 gen(path_seed);

  std::vector<NoteEvent> result;
  result.reserve(skeleton.size());

  // --- Stage 1: Core Cell Placement ---
  size_t cell_note_count = cell.intervals.size() + 1;
  size_t cell_start_slot = 0;

  // Check if pickup needed for strong-beat preference.
  bool needs_pickup = false;
  if (cell.prefer_strong_beat && !skeleton.empty()) {
    Tick pos_in_bar = skeleton[0].start_tick % kTicksPerBar;
    bool on_strong = (pos_in_bar == 0 || pos_in_bar == kTicksPerBeat * 2);
    if (!on_strong && skeleton.size() > cell_note_count) {
      needs_pickup = true;
      cell_start_slot = 1;
    }
  }

  // Verify cell fits in skeleton.
  if (cell_start_slot + cell_note_count > skeleton.size()) {
    return {};
  }

  // Compute cell pitches from intervals, snapping to scale for diatonic integrity.
  // For ChromaticCell: preserve semitone intervals on weak beats.
  std::vector<int> cell_pitches(cell_note_count);
  cell_pitches[0] = snapToScale(anchors.start_pitch, anchors.key, anchors.scale,
                                anchors.pitch_floor, anchors.pitch_ceil);
  for (size_t idx = 0; idx < cell.intervals.size(); ++idx) {
    int raw = cell_pitches[idx] + cell.intervals[idx];
    bool is_chromatic_cell = (cell.type == KerngestaltType::ChromaticCell);
    bool is_semitone = (std::abs(cell.intervals[idx]) == 1);
    Tick cell_slot_tick = (cell_start_slot + idx + 1 < skeleton.size())
        ? skeleton[cell_start_slot + idx + 1].start_tick : 0;
    bool on_weak_beat = (cell_slot_tick % kTicksPerBeat != 0);

    if (is_chromatic_cell && is_semitone && on_weak_beat) {
      // Skip scale snapping to preserve chromatic interval.
      cell_pitches[idx + 1] = std::max(anchors.pitch_floor,
                                        std::min(anchors.pitch_ceil, raw));
    } else {
      cell_pitches[idx + 1] = snapToScale(raw, anchors.key, anchors.scale,
                                          anchors.pitch_floor, anchors.pitch_ceil);
    }
  }

  // Octave shift if any cell pitch is out of range.
  int cell_min = *std::min_element(cell_pitches.begin(), cell_pitches.end());
  int cell_max = *std::max_element(cell_pitches.begin(), cell_pitches.end());
  int octave_shift = 0;
  if (cell_min < anchors.pitch_floor) {
    octave_shift = ((anchors.pitch_floor - cell_min + 11) / 12) * 12;
  } else if (cell_max > anchors.pitch_ceil) {
    octave_shift = -((cell_max - anchors.pitch_ceil + 11) / 12) * 12;
  }
  if (octave_shift != 0) {
    for (auto& pch : cell_pitches) pch += octave_shift;
  }

  // Verify all cell pitches are in range after shift.
  for (int pch : cell_pitches) {
    if (pch < anchors.pitch_floor || pch > anchors.pitch_ceil) return {};
  }

  // Record cell window.
  out_cell_window.start_idx = cell_start_slot;
  out_cell_window.end_idx = cell_start_slot + cell_note_count - 1;
  out_cell_window.valid = true;

  // --- Main loop: pitch generation with cell placement ---
  bool needs_compensation = false;
  int compensation_direction = 0;
  int large_leap_count = 0;
  bool last_fluctuated = false;
  int actual_climax_pitch = anchors.climax_pitch;
  int actual_climax_abs_degree = anchors.climax_abs_degree;

  // Cell reappearance tracking for MotifB (inverted cell).
  bool cell_reappeared = false;
  std::vector<int> reappearance_pitches;
  size_t reappearance_start = 0;
  size_t reappearance_end = 0;

  for (size_t si = 0; si < skeleton.size(); ++si) {
    const auto& slot = skeleton[si];
    int pitch = 0;
    BachNoteSource source = BachNoteSource::FugueSubject;

    // --- Pickup note ---
    if (needs_pickup && si == 0) {
      pitch = anchors.start_pitch;
      // Keep default source (FugueSubject).
    }
    // --- Cell window notes ---
    else if (si >= cell_start_slot && si <= out_cell_window.end_idx) {
      size_t cell_idx = si - cell_start_slot;
      pitch = cell_pitches[cell_idx];
      source = BachNoteSource::SubjectCore;
    }
    // --- Cell reappearance notes (pre-computed) ---
    else if (cell_reappeared && si >= reappearance_start && si <= reappearance_end) {
      size_t reapp_idx = si - reappearance_start;
      if (reapp_idx < reappearance_pitches.size()) {
        pitch = reappearance_pitches[reapp_idx];
        // Mark as FugueSubject (not SubjectCore -- reappearance is not immutable).
      }
    }
    // --- Remaining slots: phase-based logic ---
    else {
      switch (slot.phase) {
        case SlotPhase::kMotifA: {
          int target_degree = anchors.start_degree + slot.degree_hint;
          int target_pitch =
              degreeToPitch(target_degree, kBaseNote, anchors.key_offset, anchors.scale);

          float progress =
              (anchors.climax_tick > 0)
                  ? static_cast<float>(slot.start_tick) /
                        static_cast<float>(anchors.climax_tick)
                  : 0.0f;
          int interp_pitch =
              anchors.start_pitch +
              static_cast<int>(
                  static_cast<float>(anchors.climax_pitch - anchors.start_pitch) *
                  progress);

          int degree_shift = 0;
          if (target_pitch < interp_pitch) {
            degree_shift = (interp_pitch - target_pitch + 1) / 2;
          }
          int adjusted_degree = target_degree + degree_shift;

          if (slot.phase_index > 0 &&
              rng::rollProbability(gen, anchors.fluctuation_rate)) {
            adjusted_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
          }

          if (needs_compensation) {
            adjusted_degree += compensation_direction;
            needs_compensation = false;
          }

          pitch = snapToScale(
              degreeToPitch(adjusted_degree, kBaseNote, anchors.key_offset, anchors.scale),
              anchors.key, anchors.scale, anchors.pitch_floor, anchors.climax_pitch);

          // Post-leap stepwise preference for Kopfmotiv (70%).
          if (slot.phase_index > 0 && slot.phase_index <= 2 && !result.empty()) {
            int prev_p = static_cast<int>(result.back().pitch);
            int head_interval = (result.size() >= 2)
                ? std::abs(static_cast<int>(result[1].pitch) -
                           static_cast<int>(result[0].pitch))
                : 0;
            if (head_interval >= 5 && rng::rollProbability(gen, 0.70f)) {
              int step_dir = (result.back().pitch > result[0].pitch) ? -1 : 1;
              int step_pitch = snapToScale(
                  prev_p + step_dir * 2, anchors.key, anchors.scale,
                  anchors.pitch_floor, anchors.pitch_ceil);
              pitch = step_pitch;
            }
          }

          int prev_pitch =
              result.empty() ? -1 : static_cast<int>(result.back().pitch);
          pitch = avoidUnison(pitch, prev_pitch, anchors.key, anchors.scale,
                              anchors.pitch_floor, anchors.climax_pitch);
          pitch = clampLeap(pitch, prev_pitch, anchors.character, anchors.key,
                            anchors.scale, anchors.pitch_floor, anchors.climax_pitch,
                            gen, &large_leap_count);

          if (prev_pitch >= 0 && std::abs(pitch - prev_pitch) >= 5) {
            needs_compensation = true;
            compensation_direction = (pitch > prev_pitch) ? -1 : 1;
          }
          break;
        }

        case SlotPhase::kClimax: {
          int clamped_climax = anchors.climax_pitch;
          if (!result.empty()) {
            int prev = static_cast<int>(result.back().pitch);
            clamped_climax =
                clampLeap(anchors.climax_pitch, prev, anchors.character, anchors.key,
                          anchors.scale, anchors.pitch_floor, anchors.pitch_ceil,
                          gen, &large_leap_count);
          }
          pitch = clamped_climax;
          actual_climax_pitch = clamped_climax;
          actual_climax_abs_degree = scale_util::pitchToAbsoluteDegree(
              clampPitch(clamped_climax, 0, 127),
              anchors.key, anchors.scale);

          needs_compensation = false;
          compensation_direction = 0;
          last_fluctuated = false;
          break;
        }

        case SlotPhase::kMotifB: {
          // Attempt cell reappearance (inverted) once.
          if (!cell_reappeared) {
            size_t remaining_motifb = 0;
            for (size_t fi = si; fi < skeleton.size(); ++fi) {
              if (skeleton[fi].phase == SlotPhase::kMotifB) ++remaining_motifb;
            }
            if (remaining_motifb >= cell_note_count) {
              int prev_p = result.empty() ? anchors.start_pitch
                                          : static_cast<int>(result.back().pitch);
              int max_leap = maxLeapForCharacter(anchors.character);

              // Try inverted intervals first, then original if inverted fails.
              for (int attempt = 0; attempt < 2; ++attempt) {
                std::vector<int> trial_pitches(cell_note_count);
                trial_pitches[0] = prev_p;
                bool valid = true;
                for (size_t ri = 0; ri < cell.intervals.size(); ++ri) {
                  int interval = (attempt == 0) ? -cell.intervals[ri]
                                                : cell.intervals[ri];
                  int raw = trial_pitches[ri] + interval;
                  trial_pitches[ri + 1] = snapToScale(
                      raw, anchors.key, anchors.scale,
                      anchors.pitch_floor, anchors.pitch_ceil);
                  if (trial_pitches[ri + 1] < anchors.pitch_floor ||
                      trial_pitches[ri + 1] > anchors.pitch_ceil) {
                    valid = false;
                    break;
                  }
                  if (std::abs(trial_pitches[ri + 1] - trial_pitches[ri]) > max_leap) {
                    valid = false;
                    break;
                  }
                }
                if (valid) {
                  reappearance_pitches = std::move(trial_pitches);
                  reappearance_start = si;
                  reappearance_end = si + cell_note_count - 1;
                  cell_reappeared = true;
                  pitch = reappearance_pitches[0];
                  break;
                }
              }
            }
          }

          // If not a reappearance note, use standard MotifB logic.
          if (!(cell_reappeared && si >= reappearance_start && si <= reappearance_end
                && si != reappearance_start)) {
            if (cell_reappeared && si == reappearance_start) {
              // pitch already set above; skip standard logic.
            } else {
              int offset = slot.degree_hint;
              float remaining_ratio =
                  (anchors.total_ticks > anchors.climax_tick)
                      ? static_cast<float>(slot.start_tick - anchors.climax_tick) /
                            static_cast<float>(anchors.total_ticks - anchors.climax_tick)
                      : 1.0f;
              int interp_pitch =
                  actual_climax_pitch +
                  static_cast<int>(
                      static_cast<float>(anchors.tonic_pitch - actual_climax_pitch) *
                      remaining_ratio);

              int target_degree_abs = actual_climax_abs_degree + offset;
              int target_pitch = static_cast<int>(
                  scale_util::absoluteDegreeToPitch(target_degree_abs, anchors.key,
                                                   anchors.scale));

              int degree_shift = 0;
              if (target_pitch > interp_pitch + 2) {
                degree_shift = -1;
              } else if (target_pitch < interp_pitch - 2) {
                degree_shift = 1;
              }
              int adjusted_abs_degree = target_degree_abs + degree_shift;

              bool fluctuated = false;
              if (slot.phase_index > 0 && !last_fluctuated &&
                  rng::rollProbability(gen, anchors.fluctuation_rate)) {
                adjusted_abs_degree += rng::rollProbability(gen, 0.5f) ? 1 : -1;
                fluctuated = true;
              }
              last_fluctuated = fluctuated;

              if (needs_compensation) {
                adjusted_abs_degree += compensation_direction;
                needs_compensation = false;
              }

              pitch = snapToScale(
                  static_cast<int>(scale_util::absoluteDegreeToPitch(
                      adjusted_abs_degree, anchors.key, anchors.scale)),
                  anchors.key, anchors.scale, anchors.pitch_floor, actual_climax_pitch);

              int prev_pitch =
                  result.empty() ? -1 : static_cast<int>(result.back().pitch);
              pitch = avoidUnison(pitch, prev_pitch, anchors.key, anchors.scale,
                                  anchors.pitch_floor, actual_climax_pitch);
              pitch = clampLeap(pitch, prev_pitch, anchors.character, anchors.key,
                                anchors.scale, anchors.pitch_floor, actual_climax_pitch,
                                gen, &large_leap_count);

              if (prev_pitch >= 0 && std::abs(pitch - prev_pitch) >= 5) {
                needs_compensation = true;
                compensation_direction = (pitch > prev_pitch) ? -1 : 1;
              }
            }
          }
          break;
        }

        case SlotPhase::kCadence: {
          pitch = snapToScale(
              static_cast<int>(scale_util::absoluteDegreeToPitch(
                  slot.degree_hint, anchors.key, anchors.scale)),
              anchors.key, anchors.scale, anchors.pitch_floor, actual_climax_pitch);

          int prev_pitch =
              result.empty() ? -1 : static_cast<int>(result.back().pitch);
          pitch = avoidUnison(pitch, prev_pitch, anchors.key, anchors.scale,
                              anchors.pitch_floor, actual_climax_pitch);
          pitch = clampLeap(pitch, prev_pitch, anchors.character, anchors.key,
                            anchors.scale, anchors.pitch_floor, actual_climax_pitch,
                            gen, &large_leap_count);
          break;
        }
      }
    }

    // Tritone avoidance: skip for cell window notes (reject if cell has tritone).
    if (!result.empty()) {
      bool in_cell = (si >= out_cell_window.start_idx && si <= out_cell_window.end_idx);
      int prev = static_cast<int>(result.back().pitch);
      int dist = std::abs(pitch - prev);
      int simple = interval_util::compoundToSimple(dist);
      if (simple == 6) {
        // Cell window notes with tritone: reject entire candidate.
        if (in_cell) return {};

        bool exempt = false;
        Tick prev_dur = result.back().duration;
        if (prev_dur <= 240 && slot.duration <= 240) exempt = true;

        if (!exempt) {
          int direction = (pitch > prev) ? 1 : -1;
          struct TritoneCandidate {
            int cand_pitch;
            int score;
          };
          TritoneCandidate candidates[3];
          candidates[0].cand_pitch = snapToScale(
              pitch + (-direction), anchors.key, anchors.scale,
              anchors.pitch_floor, anchors.pitch_ceil);
          candidates[1].cand_pitch = snapToScale(
              pitch + direction, anchors.key, anchors.scale,
              anchors.pitch_floor, anchors.pitch_ceil);
          candidates[2].cand_pitch = snapToScale(
              pitch + direction * 2, anchors.key, anchors.scale,
              anchors.pitch_floor, anchors.pitch_ceil);

          int best_idx = -1;
          int best_score = 9999;
          for (int cidx = 0; cidx < 3; ++cidx) {
            int cand = candidates[cidx].cand_pitch;
            int cand_simple =
                interval_util::compoundToSimple(std::abs(cand - prev));
            if (cand_simple == 6) continue;
            int leap_size = std::abs(cand - prev);
            int unison_penalty = (cand == prev) ? 30 : 0;
            int score = leap_size + unison_penalty;
            if (score < best_score) {
              best_score = score;
              best_idx = cidx;
            }
          }
          if (best_idx >= 0) {
            pitch = candidates[best_idx].cand_pitch;
          }
        }
      }
    }

    NoteEvent note;
    note.start_tick = slot.start_tick;
    note.duration = slot.duration;
    note.pitch = clampPitch(pitch, 0, 127);
    note.velocity = 80;
    note.voice = 0;
    note.source = source;
    result.push_back(note);
  }

  // Ending normalization (same as legacy, but skip if last note is in cell window).
  if (!result.empty()) {
    bool last_in_cell = (result.size() - 1 >= out_cell_window.start_idx &&
                         result.size() - 1 <= out_cell_window.end_idx);
    if (!last_in_cell) {
      int prev_pitch_for_ending =
          (result.size() >= 2)
              ? static_cast<int>(result[result.size() - 2].pitch)
              : static_cast<int>(result.back().pitch);
      int max_leap = maxLeapForCharacter(anchors.character);

      float dominant_rate = policy.dominant_ending_prob;
      if (rng::rollProbability(gen, dominant_rate)) {
        int dom_pc = getPitchClass(static_cast<uint8_t>(
            degreeToPitch(4, kBaseNote, anchors.key_offset, anchors.scale)));
        int ending = normalizeEndingPitch(dom_pc, prev_pitch_for_ending,
                                          max_leap, anchors.key, anchors.scale,
                                          anchors.pitch_floor, anchors.pitch_ceil);
        result.back().pitch = static_cast<uint8_t>(ending);
      } else {
        int tonic_pc =
            getPitchClass(static_cast<uint8_t>(anchors.tonic_pitch));
        int ending = normalizeEndingPitch(tonic_pc, prev_pitch_for_ending,
                                          max_leap, anchors.key, anchors.scale,
                                          anchors.pitch_floor, anchors.pitch_ceil);
        result.back().pitch = static_cast<uint8_t>(ending);
      }
    }
  }

  // Snap all start_ticks to 16th-note grid for metric integrity.
  constexpr Tick kTickQuantum = kTicksPerBeat / 4;  // 120
  for (auto& note : result) {
    note.start_tick = (note.start_tick / kTickQuantum) * kTickQuantum;
  }
  // Fix any overlaps introduced by quantization.
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    Tick next_start = result[idx + 1].start_tick;
    if (result[idx].start_tick + result[idx].duration > next_start) {
      result[idx].duration = next_start - result[idx].start_tick;
      if (result[idx].duration < kTickQuantum) {
        result[idx].duration = kTickQuantum;
      }
    }
  }

  // Post-processing leap enforcement: skip cell window notes.
  int post_max_leap = maxLeapForCharacter(anchors.character);
  for (size_t idx = 1; idx < result.size(); ++idx) {
    // Skip cell window notes; reject if they violate.
    if (idx >= out_cell_window.start_idx && idx <= out_cell_window.end_idx) {
      int prev_p = static_cast<int>(result[idx - 1].pitch);
      int cur_p = static_cast<int>(result[idx].pitch);
      if (std::abs(cur_p - prev_p) > post_max_leap) return {};
      continue;
    }
    // Also skip if prev note is the last cell note (don't modify the transition away
    // from cell unless it's outside the cell window itself).
    int prev_p = static_cast<int>(result[idx - 1].pitch);
    int cur_p = static_cast<int>(result[idx].pitch);
    int interval = cur_p - prev_p;
    if (std::abs(interval) > post_max_leap) {
      int direction = (interval > 0) ? 1 : -1;
      for (int attempt = post_max_leap; attempt >= 0; --attempt) {
        int candidate = prev_p + direction * attempt;
        candidate =
            std::max(anchors.pitch_floor, std::min(anchors.pitch_ceil, candidate));
        int snapped =
            snapToScale(candidate, anchors.key, anchors.scale,
                        anchors.pitch_floor, anchors.pitch_ceil);
        if (std::abs(snapped - prev_p) <= post_max_leap) {
          result[idx].pitch = clampPitch(snapped, 0, 127);
          break;
        }
      }
    }
  }

  // Post-processing: fix tritone intervals, skip cell window notes.
  for (int tritone_pass = 0; tritone_pass < 2; ++tritone_pass) {
    bool any_fixed = false;
    for (size_t idx = 1; idx < result.size(); ++idx) {
      // Skip cell window notes; reject if they have a tritone violation.
      if (idx >= out_cell_window.start_idx && idx <= out_cell_window.end_idx) {
        int prev_p = static_cast<int>(result[idx - 1].pitch);
        int cur_p = static_cast<int>(result[idx].pitch);
        int s = interval_util::compoundToSimple(std::abs(cur_p - prev_p));
        if (s == 6 && !(result[idx - 1].duration <= 240 && result[idx].duration <= 240)) {
          return {};
        }
        continue;
      }

      int prev_p = static_cast<int>(result[idx - 1].pitch);
      int cur_p = static_cast<int>(result[idx].pitch);
      int s = interval_util::compoundToSimple(std::abs(cur_p - prev_p));
      if (s != 6) continue;

      if (result[idx - 1].duration <= 240 && result[idx].duration <= 240) continue;

      int best_cand = cur_p;
      int best_cost = 9999;
      for (int delta : {1, -1, 2, -2}) {
        int shifted = cur_p + delta;
        int snapped = snapToScale(shifted, anchors.key, anchors.scale,
                                  anchors.pitch_floor, anchors.pitch_ceil);
        int new_simple =
            interval_util::compoundToSimple(std::abs(snapped - prev_p));
        if (new_simple == 6) continue;
        if (std::abs(snapped - prev_p) > post_max_leap) continue;
        if (idx + 1 < result.size()) {
          int next_p = static_cast<int>(result[idx + 1].pitch);
          int fwd_simple =
              interval_util::compoundToSimple(std::abs(snapped - next_p));
          if (fwd_simple == 6) continue;
        }
        int cost =
            std::abs(snapped - prev_p) + ((snapped == prev_p) ? 30 : 0);
        if (cost < best_cost) {
          best_cost = cost;
          best_cand = snapped;
        }
      }
      if (best_cand != cur_p) {
        result[idx].pitch = clampPitch(best_cand, 0, 127);
        any_fixed = true;
      }
    }
    if (!any_fixed) break;
  }

  return result;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// SubjectGenerator
// ---------------------------------------------------------------------------

Subject SubjectGenerator::generate(const FugueConfig& config,
                                   uint32_t seed) const {
  Subject subject;
  subject.key = config.key;
  subject.is_minor = config.is_minor;
  subject.character = config.character;

  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);

  uint8_t bars = config.subject_bars;
  if (bars < policy.min_subject_bars) bars = policy.min_subject_bars;
  if (bars > policy.max_subject_bars) bars = policy.max_subject_bars;
  if (bars < 1) bars = 1;
  if (bars > 4) bars = 4;

  // Determine anacrusis based on character type, clamped by archetype.
  std::mt19937 anacrusis_gen(seed ^ 0x41756674u);  // "Auft" in ASCII
  float anacrusis_prob = 0.0f;
  switch (config.character) {
    case SubjectCharacter::Severe:
      anacrusis_prob = rng::rollFloat(anacrusis_gen, 0.20f, 0.40f); break;
    case SubjectCharacter::Playful:
      anacrusis_prob = rng::rollFloat(anacrusis_gen, 0.60f, 0.80f); break;
    case SubjectCharacter::Noble:
      anacrusis_prob = rng::rollFloat(anacrusis_gen, 0.30f, 0.50f); break;
    case SubjectCharacter::Restless:
      anacrusis_prob = rng::rollFloat(anacrusis_gen, 0.50f, 0.70f); break;
  }
  // Clamp anacrusis probability to archetype bounds.
  anacrusis_prob = std::clamp(anacrusis_prob,
                               policy.min_anacrusis_prob,
                               policy.max_anacrusis_prob);
  bool has_anacrusis = rng::rollProbability(anacrusis_gen, anacrusis_prob);
  Tick anacrusis_ticks = 0;
  if (has_anacrusis) {
    if (config.character == SubjectCharacter::Playful ||
        config.character == SubjectCharacter::Restless) {
      anacrusis_ticks = rng::rollProbability(anacrusis_gen, 0.5f)
                            ? kTicksPerBeat
                            : kTicksPerBeat * 2;
    } else {
      anacrusis_ticks = kTicksPerBeat;
    }
  }

  // Generate with validation retry (archetype-weighted selection).
  constexpr int kMaxRetries = 4;
  SubjectValidator validator;
  ArchetypeScorer archetype_scorer;
  GenerateResult best_result;
  float best_composite = -1.0f;

  for (int retry = 0; retry < kMaxRetries; ++retry) {
    auto result = generateNotes(config, bars,
                                seed + static_cast<uint32_t>(retry));
    Subject candidate;
    candidate.notes = result.notes;
    candidate.key = config.key;
    candidate.is_minor = config.is_minor;
    candidate.character = config.character;

    float base_comp = validator.evaluate(candidate).composite();
    float arch_comp = archetype_scorer.evaluate(candidate, policy).composite();
    float w = policy.base_quality_weight;
    float comp;
    if (config.toccata_core_intervals.empty()) {
      comp = base_comp * w + arch_comp * (1.0f - w);
    } else {
      constexpr float kToccataAlpha = 0.12f;
      std::vector<int> subj_ivls;
      for (size_t i = 1; i < result.notes.size(); ++i) {
        subj_ivls.push_back(static_cast<int>(result.notes[i].pitch) -
                            static_cast<int>(result.notes[i - 1].pitch));
      }
      float toc = computeToccataAffinity(subj_ivls, config.toccata_core_intervals);
      comp = (base_comp * w + arch_comp * (1.0f - w)) * (1.0f - kToccataAlpha)
             + toc * kToccataAlpha;
    }

    if (comp > best_composite) {
      best_composite = comp;
      best_result = std::move(result);
    }
    if (comp >= 0.7f) break;
  }

  subject.notes = std::move(best_result.notes);
  subject.cell_window = best_result.cell_window;
  subject.length_ticks = static_cast<Tick>(bars) * kTicksPerBar;
  subject.anacrusis_ticks = anacrusis_ticks;

  if (anacrusis_ticks > 0 && !subject.notes.empty()) {
    Tick first_dur = subject.notes[0].duration;
    if (first_dur > anacrusis_ticks) {
      NoteEvent anacrusis_note = subject.notes[0];
      anacrusis_note.duration = anacrusis_ticks;
      subject.notes[0].start_tick = anacrusis_ticks;
      subject.notes[0].duration = first_dur - anacrusis_ticks;
      subject.notes.insert(subject.notes.begin(), anacrusis_note);
      // Shift cell_window indices if anacrusis note was inserted before it.
      if (subject.cell_window.valid) {
        subject.cell_window.start_idx += 1;
        subject.cell_window.end_idx += 1;
      }
    }
    subject.length_ticks += anacrusis_ticks;
  }

  // Build SubjectIdentity (Layers 1+2) from the final note sequence.
  subject.identity = buildSubjectIdentity(subject.notes, config.key, config.is_minor);
  // Preserve cell_window from generator in the identity.
  subject.identity.essential.cell_window = subject.cell_window;

  return subject;
}

SubjectGenerator::GenerateResult SubjectGenerator::generateNotes(
    const FugueConfig& config, uint8_t bars, uint32_t seed) const {
  SubjectCharacter character = config.character;
  Key key = config.key;
  bool is_minor = config.is_minor;

  // Dedicated anchor RNG (separate stream from rhythm and path RNGs).
  std::mt19937 anchor_rng(seed ^ 0x416E6368u);  // "Anch"
  int key_offset = static_cast<int>(key);
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = static_cast<Tick>(bars) * kTicksPerBar;

  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);

  // --- Phase 1: Compute anchors (once per seed) ---

  GoalTone goal = goalToneForCharacter(character, anchor_rng, policy);
  uint32_t template_idx = anchor_rng() % 5;
  auto [motif_a, motif_b] = motifTemplatesForCharacter(character, template_idx);

  constexpr int kBaseNote = 60;
  float tonic_prob = 0.6f;
  switch (character) {
    case SubjectCharacter::Severe:
      tonic_prob = rng::rollFloat(anchor_rng, 0.55f, 0.70f); break;
    case SubjectCharacter::Playful:
      tonic_prob = rng::rollFloat(anchor_rng, 0.40f, 0.55f); break;
    case SubjectCharacter::Noble:
      tonic_prob = rng::rollFloat(anchor_rng, 0.60f, 0.70f); break;
    case SubjectCharacter::Restless:
      tonic_prob = rng::rollFloat(anchor_rng, 0.45f, 0.60f); break;
  }
  int start_degree = rng::rollProbability(anchor_rng, tonic_prob) ? 0 : 4;

  CharacterParams params = getCharacterParams(character, anchor_rng);
  applyArchetypeConstraints(params, policy);

  int start_pitch = degreeToPitch(start_degree, kBaseNote, key_offset, scale);
  int tonic_base = degreeToPitch(0, kBaseNote, key_offset, scale);

  int tonic_abs = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(tonic_base), key, scale);
  int start_abs = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(start_pitch), key, scale);
  int floor_abs = std::min(start_abs, tonic_abs);
  int ceil_abs = floor_abs + params.max_range_degrees;

  int pitch_floor = static_cast<int>(
      scale_util::absoluteDegreeToPitch(floor_abs, key, scale));
  int pitch_ceil = static_cast<int>(
      scale_util::absoluteDegreeToPitch(ceil_abs, key, scale));
  pitch_floor = std::max(static_cast<int>(organ_range::kManual1Low), pitch_floor);
  pitch_ceil = std::min(static_cast<int>(organ_range::kManual1High), pitch_ceil);

  int climax_abs = floor_abs +
      static_cast<int>(static_cast<float>(ceil_abs - floor_abs) *
                        goal.pitch_ratio);
  if (climax_abs < start_abs + 3) {
    climax_abs = std::min(start_abs + 3, ceil_abs);
  }
  int climax_pitch = static_cast<int>(
      scale_util::absoluteDegreeToPitch(climax_abs, key, scale));
  climax_pitch = std::max(pitch_floor, std::min(pitch_ceil, climax_pitch));

  Tick raw_climax_tick =
      static_cast<Tick>(static_cast<float>(total_ticks) * goal.position_ratio);
  Tick climax_tick = quantizeToStrongBeat(raw_climax_tick, character, total_ticks);
  if (climax_tick >= total_ticks) {
    climax_tick = (total_ticks > kTicksPerBar) ? total_ticks - kTicksPerBar : 0;
  }

  float fluctuation_rate = 0.20f;
  switch (character) {
    case SubjectCharacter::Severe:
      fluctuation_rate = rng::rollFloat(anchor_rng, 0.10f, 0.25f); break;
    case SubjectCharacter::Playful:
      fluctuation_rate = rng::rollFloat(anchor_rng, 0.20f, 0.35f); break;
    case SubjectCharacter::Noble:
      fluctuation_rate = rng::rollFloat(anchor_rng, 0.10f, 0.20f); break;
    case SubjectCharacter::Restless:
      fluctuation_rate = rng::rollFloat(anchor_rng, 0.25f, 0.40f); break;
  }

  int tonic_pitch = degreeToPitch(0, kBaseNote, key_offset, scale);
  tonic_pitch = std::max(pitch_floor, std::min(pitch_ceil, tonic_pitch));

  int climax_abs_degree = scale_util::pitchToAbsoluteDegree(
      clampPitch(climax_pitch, 0, 127),
      key, scale);

  CadentialFormula cadence = getCadentialFormula(character);

  SubjectAnchors anchors{
      start_degree, start_pitch, tonic_pitch, climax_pitch,
      climax_tick, total_ticks,
      pitch_floor, pitch_ceil,
      climax_abs_degree,
      key, key_offset, scale,
      character, fluctuation_rate};

  // --- Phase 2: Build rhythm skeleton (once per seed) ---

  std::mt19937 rhythm_gen(seed ^ 0x52687974u);  // "Rhyt"
  AccelProfile accel_profile = getAccelProfile(character, config.archetype, rhythm_gen);
  auto skeleton =
      buildRhythmSkeleton(anchors, motif_a, motif_b, cadence, accel_profile, rhythm_gen);

  // --- Phase 2.5: Select Kerngestalt type and cell ---

  uint32_t arch_mix = static_cast<uint32_t>(config.archetype) * 0x41726368u;
  std::mt19937 type_rng(seed ^ 0x54797065u ^ arch_mix);  // "Type" + archetype
  KerngestaltType kern_type =
      selectKerngestaltType(character, config.archetype, type_rng);
  int cell_idx = static_cast<int>(type_rng() % 4);
  const KerngestaltCell& cell = getCoreCell(kern_type, cell_idx);

  // --- Phase 3: N-candidate Kerngestalt path selection with tiered fallback ---

  SubjectValidator validator;
  ArchetypeScorer archetype_scorer;

  struct CandidateResult {
    std::vector<NoteEvent> notes;
    float composite = -1.0f;
    int tier = 2;  // 0=best, 1=good, 2=fallback
    CellWindow cell_window;
  };
  CandidateResult best;

  int num_candidates = (policy.path_candidates > 0)
      ? policy.path_candidates : kDefaultPathCandidates;

  // Tier 0/1: Kerngestalt path candidates.
  for (int idx = 0; idx < num_candidates; ++idx) {
    uint32_t path_seed = seed ^ (static_cast<uint32_t>(idx) * 0x7A3B9C1Du) ^ arch_mix;

    CellWindow cw;
    auto candidate = generateKerngestaltPath(
        anchors, skeleton, policy, cell, path_seed, cw);

    if (candidate.empty()) continue;

    Subject sub;
    sub.notes = candidate;
    sub.key = key;
    sub.is_minor = is_minor;
    sub.character = character;

    if (!archetype_scorer.checkHardGate(sub, policy)) continue;

    // Build identity to check Kerngestalt validity.
    auto ident = buildEssentialIdentity(candidate, key, is_minor);
    ident.cell_window = cw;

    // Determine tier based on Kerngestalt validity and type match.
    int tier = 2;
    bool valid_kern = isValidKerngestalt(ident);
    if (valid_kern && ident.kerngestalt_type == kern_type) {
      tier = 0;
    } else if (valid_kern) {
      tier = 1;
    }

    // NoteFunction fitness bonus.
    float note_func_bonus = 0.0f;
    size_t eval_count = std::min(candidate.size(), skeleton.size());
    for (size_t ni = 0; ni < eval_count; ++ni) {
      int prev_p = (ni > 0) ? static_cast<int>(candidate[ni - 1].pitch) : -1;
      note_func_bonus += evaluateNoteFunctionFit(
          static_cast<int>(candidate[ni].pitch), prev_p,
          skeleton[ni].function, key, scale, skeleton.size());
    }
    note_func_bonus = std::clamp(note_func_bonus, 0.0f, 1.0f);

    float base_comp = validator.evaluate(sub).composite();
    float arch_comp = archetype_scorer.evaluate(sub, policy).composite();
    float comp;
    if (config.toccata_core_intervals.empty()) {
      comp = base_comp * 0.65f + arch_comp * 0.25f + note_func_bonus * 0.10f;
    } else {
      std::vector<int> subj_ivls;
      for (size_t ni2 = 1; ni2 < candidate.size(); ++ni2) {
        subj_ivls.push_back(static_cast<int>(candidate[ni2].pitch) -
                            static_cast<int>(candidate[ni2 - 1].pitch));
      }
      float toc = computeToccataAffinity(subj_ivls, config.toccata_core_intervals);
      comp = base_comp * 0.60f + arch_comp * 0.25f + note_func_bonus * 0.10f
             + toc * 0.05f;
    }

    // Lower tier wins; within same tier, higher composite wins.
    if (tier < best.tier || (tier == best.tier && comp > best.composite)) {
      best.notes = std::move(candidate);
      best.composite = comp;
      best.tier = tier;
      best.cell_window = cw;
    }

    if (tier == 0 && comp >= 0.85f) break;
  }

  // Tier 2 fallback: if no Kerngestalt candidates succeeded, try legacy path.
  if (best.tier == 2 || best.notes.empty()) {
    for (int idx = 0; idx < num_candidates; ++idx) {
      uint32_t path_seed = seed ^ (static_cast<uint32_t>(idx) * 0x7A3B9C1Du) ^ arch_mix;
      auto candidate =
          generateLegacyPitchPath(anchors, skeleton, policy, path_seed);

      if (candidate.empty()) continue;

      Subject sub;
      sub.notes = candidate;
      sub.key = key;
      sub.is_minor = is_minor;
      sub.character = character;

      if (!archetype_scorer.checkHardGate(sub, policy)) continue;

      float note_func_bonus = 0.0f;
      size_t eval_count = std::min(candidate.size(), skeleton.size());
      for (size_t ni = 0; ni < eval_count; ++ni) {
        int prev_p = (ni > 0) ? static_cast<int>(candidate[ni - 1].pitch) : -1;
        note_func_bonus += evaluateNoteFunctionFit(
            static_cast<int>(candidate[ni].pitch), prev_p,
            skeleton[ni].function, key, scale, skeleton.size());
      }
      note_func_bonus = std::clamp(note_func_bonus, 0.0f, 1.0f);

      float base_comp = validator.evaluate(sub).composite();
      float arch_comp = archetype_scorer.evaluate(sub, policy).composite();
      float comp;
      if (config.toccata_core_intervals.empty()) {
        comp = base_comp * 0.65f + arch_comp * 0.25f + note_func_bonus * 0.10f;
      } else {
        std::vector<int> subj_ivls;
        for (size_t ni2 = 1; ni2 < candidate.size(); ++ni2) {
          subj_ivls.push_back(static_cast<int>(candidate[ni2].pitch) -
                              static_cast<int>(candidate[ni2 - 1].pitch));
        }
        float toc = computeToccataAffinity(subj_ivls, config.toccata_core_intervals);
        comp = base_comp * 0.60f + arch_comp * 0.25f + note_func_bonus * 0.10f
               + toc * 0.05f;
      }

      if (comp > best.composite || best.notes.empty()) {
        best.notes = std::move(candidate);
        best.composite = comp;
        best.cell_window = {};  // No cell window for legacy path.
      }
      if (comp >= 0.85f) break;
    }
  }

  return {std::move(best.notes), best.cell_window};
}

}  // namespace bach
