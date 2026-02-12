/// @file
/// @brief Fugue subject generation with character-driven note and rhythm selection.

#include "fugue/subject.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "fugue/archetype_policy.h"
#include "fugue/archetype_scorer.h"
#include "fugue/motif_template.h"
#include "fugue/subject_params.h"
#include "fugue/subject_validator.h"

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
  int best = -1;
  int best_dist = 999;
  for (int octave = 0; octave < 11; ++octave) {
    int candidate = target_pitch_class + octave * 12;
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

    // Pair substitution on even-indexed pairs only.
    if (idx % 2 == 0 && idx + 1 < motif_a.durations.size() &&
        idx + 1 < motif_a.degree_offsets.size()) {
      Tick next_dur = motif_a.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, a.character, rhythm_gen, new_a, new_b);
      duration = new_a;
    }

    if (current_tick + duration > a.climax_tick) {
      duration = a.climax_tick - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    slots.push_back({current_tick, duration, SlotPhase::kMotifA,
                      motif_a.degree_offsets[idx], motif_a_count});
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
    slots.push_back({current_tick, climax_dur, SlotPhase::kClimax, 0, 0});
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

    if (idx % 2 == 0 && idx + 1 < motif_b.durations.size()) {
      Tick next_dur = motif_b.durations[idx + 1];
      Tick new_a = duration, new_b = next_dur;
      varyDurationPair(duration, next_dur, a.character, rhythm_gen, new_a, new_b);
      duration = new_a;
    }

    if (current_tick + duration > a.total_ticks) {
      duration = a.total_ticks - current_tick;
      if (duration < kTicksPerBeat / 4) break;
    }

    slots.push_back({current_tick, duration, SlotPhase::kMotifB,
                      motif_b.degree_offsets[idx], motif_b_count});
    current_tick += duration;
    ++motif_b_count;
  }

  // --- Cadence slots ---
  if (current_tick < a.total_ticks) {
    int tonic_abs_degree = scale_util::pitchToAbsoluteDegree(
        static_cast<uint8_t>(std::max(0, std::min(127, a.tonic_pitch))),
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
                        degree_abs, cadence_count});
      current_tick += dur;
      ++cadence_count;
    }
  }

  return slots;
}

/// Generate a pitch path for the given skeleton using a candidate-specific seed.
///
/// Anchors and rhythm skeleton are fixed; only pitch decisions vary per candidate.
/// This implements the same pitch logic as the original generateNotes but operates
/// on pre-computed skeleton slots.
std::vector<NoteEvent> generatePitchPath(
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
            static_cast<uint8_t>(std::max(0, std::min(127, clamped_climax))),
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
          result[i].pitch = static_cast<uint8_t>(
              std::max(0, std::min(127, snapped)));
          break;
        }
      }
    }
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

  // Generate with validation retry.
  constexpr int kMaxRetries = 4;
  SubjectValidator validator;
  std::vector<NoteEvent> best_notes;
  float best_composite = -1.0f;

  for (int retry = 0; retry < kMaxRetries; ++retry) {
    auto notes = generateNotes(config, bars,
                               seed + static_cast<uint32_t>(retry));
    Subject candidate;
    candidate.notes = notes;
    candidate.key = config.key;
    candidate.is_minor = config.is_minor;
    candidate.character = config.character;
    SubjectScore score = validator.evaluate(candidate);
    float comp = score.composite();
    if (comp > best_composite) {
      best_composite = comp;
      best_notes = std::move(notes);
    }
    if (score.isAcceptable()) break;
  }

  subject.notes = std::move(best_notes);
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
    }
    subject.length_ticks += anacrusis_ticks;
  }

  return subject;
}

std::vector<NoteEvent> SubjectGenerator::generateNotes(
    const FugueConfig& config, uint8_t bars, uint32_t seed) const {
  SubjectCharacter character = config.character;
  Key key = config.key;
  bool is_minor = config.is_minor;

  std::mt19937 gen(seed);
  int key_offset = static_cast<int>(key);
  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  Tick total_ticks = static_cast<Tick>(bars) * kTicksPerBar;

  const ArchetypePolicy& policy = getArchetypePolicy(config.archetype);

  // --- Phase 1: Compute anchors (once per seed) ---

  GoalTone goal = goalToneForCharacter(character, gen, policy);
  uint32_t template_idx = gen() % 4;
  auto [motif_a, motif_b] = motifTemplatesForCharacter(character, template_idx);

  constexpr int kBaseNote = 60;
  float tonic_prob = 0.6f;
  switch (character) {
    case SubjectCharacter::Severe:
      tonic_prob = rng::rollFloat(gen, 0.55f, 0.70f); break;
    case SubjectCharacter::Playful:
      tonic_prob = rng::rollFloat(gen, 0.40f, 0.55f); break;
    case SubjectCharacter::Noble:
      tonic_prob = rng::rollFloat(gen, 0.60f, 0.70f); break;
    case SubjectCharacter::Restless:
      tonic_prob = rng::rollFloat(gen, 0.45f, 0.60f); break;
  }
  int start_degree = rng::rollProbability(gen, tonic_prob) ? 0 : 4;

  CharacterParams params = getCharacterParams(character, gen);
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
  pitch_floor = std::max(36, pitch_floor);
  pitch_ceil = std::min(96, pitch_ceil);

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
      fluctuation_rate = rng::rollFloat(gen, 0.10f, 0.25f); break;
    case SubjectCharacter::Playful:
      fluctuation_rate = rng::rollFloat(gen, 0.20f, 0.35f); break;
    case SubjectCharacter::Noble:
      fluctuation_rate = rng::rollFloat(gen, 0.10f, 0.20f); break;
    case SubjectCharacter::Restless:
      fluctuation_rate = rng::rollFloat(gen, 0.25f, 0.40f); break;
  }

  int tonic_pitch = degreeToPitch(0, kBaseNote, key_offset, scale);
  tonic_pitch = std::max(pitch_floor, std::min(pitch_ceil, tonic_pitch));

  int climax_abs_degree = scale_util::pitchToAbsoluteDegree(
      static_cast<uint8_t>(std::max(0, std::min(127, climax_pitch))),
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
  auto skeleton =
      buildRhythmSkeleton(anchors, motif_a, motif_b, cadence, rhythm_gen);

  // --- Phase 3: N-candidate pitch path selection with archetype scoring ---

  SubjectValidator validator;
  ArchetypeScorer archetype_scorer;
  std::vector<NoteEvent> best_notes;
  float best_composite = -1.0f;

  for (int i = 0; i < kDefaultPathCandidates; ++i) {
    uint32_t path_seed = seed ^ (static_cast<uint32_t>(i) * 0x7A3B9C1Du);
    auto candidate = generatePitchPath(anchors, skeleton, policy, path_seed);

    Subject s;
    s.notes = candidate;
    s.key = key;
    s.is_minor = is_minor;
    s.character = character;

    // Hard gate: reject candidates that violate archetype requirements.
    if (!archetype_scorer.checkHardGate(s, policy)) continue;

    // Combined score: base quality (70%) + archetype fitness (30%).
    float base_comp = validator.evaluate(s).composite();
    float arch_comp = archetype_scorer.evaluate(s, policy).composite();
    float comp = base_comp * 0.7f + arch_comp * 0.3f;

    if (comp > best_composite) {
      best_composite = comp;
      best_notes = std::move(candidate);
    }
    // Early stop for high-quality candidates.
    if (comp >= 0.85f) break;
  }

  return best_notes;
}

}  // namespace bach
