// Constraint-driven episode generator (Phase 3).
//
// Core algorithm: ConstraintState x MotifOp x planFortspinnung.
// Each note candidate is evaluated via ConstraintState.evaluate() and the
// highest-scoring candidate is placed. Deadlock (is_dead()) returns
// success=false.

#include "constraint/episode_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "constraint/constraint_state.h"
#include "constraint/motif_constraint.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "core/vocabulary_data.inc"
#include "fugue/episode.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"
#include "fugue/thematic_plan.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum recent pitches tracked per voice for vocabulary scoring.
constexpr int kMaxRecentPitches = 8;

/// Number of candidate pitch offsets to evaluate around the original pitch.
/// Candidates: {-2, -1, 0, +1, +2} semitones from the motif's pitch.
constexpr int kCandidateOffsets[] = {0, -1, 1, -2, 2};
constexpr int kNumCandidateOffsets = 5;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Compute minimum note duration based on energy level.
///
/// Higher energy allows shorter notes. At energy=0 minimum is a quarter note,
/// at energy=1 minimum is approximately a 16th note.
/// Formula: kTicksPerBeat / (1 + energy * 3)
///
/// @param energy_level Energy in [0, 1].
/// @return Minimum duration in ticks.
Tick minDurationForEnergy(float energy_level) {
  float divisor = 1.0f + energy_level * 3.0f;
  Tick min_dur = static_cast<Tick>(kTicksPerBeat / divisor);
  return std::max(min_dur, static_cast<Tick>(duration::kSixteenthNote));
}

/// @brief Minimum note duration for Fortspinnung material by phase.
///
/// The global energy floor is useful for broad density control, but it
/// suppresses the running 16th-note vocabulary that defines Bach episodes.
/// Keep the Kernel conservative and let Sequence/Dissolution carry the
/// reference-like short-note motion.
Tick minDurationForFortPhase(FortPhase phase, Tick energy_floor) {
  switch (phase) {
    case FortPhase::Kernel:
      return std::min(energy_floor, static_cast<Tick>(duration::kEighthNote));
    case FortPhase::Sequence:
    case FortPhase::Dissolution:
      return duration::kSixteenthNote;
  }
  return energy_floor;
}

Tick durationBeforeBoundaryDissonance(Tick note_tick, Tick note_dur, uint8_t pitch,
                                      const HarmonicTimeline* timeline, Tick episode_start,
                                      Tick episode_duration);

/// @brief Compute chromatic key distance (semitones) between two keys.
/// @param from Source key.
/// @param to Target key.
/// @return Signed semitone distance (shortest path, range [-6, +6]).
int keyDistance(Key from, Key to) {
  int diff = static_cast<int>(to) - static_cast<int>(from);
  // Wrap to shortest path on the circle.
  if (diff > 6)
    diff -= 12;
  if (diff < -6)
    diff += 12;
  return diff;
}

ScaleType localScaleForKey(const EpisodeRequest& request, Key key) {
  if (!request.home_is_minor)
    return ScaleType::Major;

  KeySignature home{request.home_key, true};
  if (key == request.home_key || key == getSubdominant(home).tonic) {
    return ScaleType::HarmonicMinor;
  }
  return ScaleType::Major;
}

ScaleType localScaleForEvent(const HarmonicEvent& event) {
  return event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
}

/// @brief Build a VerticalSnapshot from currently placed notes at a tick.
///
/// Scans the placed notes to find the most recent sounding pitch per voice.
///
/// @param placed_notes All placed notes so far.
/// @param tick Current tick position.
/// @param num_voices Number of voices.
/// @return Snapshot with sounding pitches.
VerticalSnapshot buildSnapshot(const std::vector<NoteEvent>& placed_notes, Tick tick,
                               uint8_t num_voices) {
  VerticalSnapshot snap;
  snap.num_voices = num_voices;
  for (const auto& note : placed_notes) {
    if (note.voice < num_voices && note.start_tick <= tick &&
        note.start_tick + note.duration > tick) {
      snap.pitches[note.voice] = note.pitch;
    }
  }
  return snap;
}

/// @brief Build a MarkovContext for a candidate evaluation.
///
/// @param prev_pitch Previous pitch in the same voice (or 60 if none).
/// @param prev_duration Previous note duration (or kTicksPerBeat if none).
/// @param tick Current tick position.
/// @param current_key Current musical key.
/// @return Populated MarkovContext.
MarkovContext buildMarkovContext(uint8_t prev_pitch, Tick prev_duration, Tick tick, Key current_key,
                                 ScaleType scale = ScaleType::Major) {
  MarkovContext ctx;
  ctx.prev_pitch = prev_pitch;
  ctx.key = current_key;
  ctx.scale = scale;
  ctx.beat = tickToBeatPos(tick);
  ctx.prev_dur = ticksToDurCategory(prev_duration);
  ctx.prev_step = 0;
  ctx.deg_class = scaleDegreeToClass(static_cast<int>(prev_pitch) % 12);
  ctx.dir_class = DirIntervalClass::StepUp;
  return ctx;
}

/// @brief Compute vocabulary figure score from a recent pitch window.
///
/// Builds a 5-note window from recent_pitches + candidate, converts to
/// 4 directed degree intervals, and calls vocab_data::matchVocabulary().
///
/// @param recent Array of recent pitches (most recent last).
/// @param count Number of recent pitches available.
/// @param candidate Candidate pitch to append.
/// @return Vocabulary match score [0, 1].
float computeFigureScore(const uint8_t* recent, int count, uint8_t candidate) {
  // Need at least 4 recent pitches + candidate = 5-note window.
  if (count < 4)
    return 0.0f;

  uint8_t window[5];
  // Take last 4 from recent, append candidate.
  for (int idx = 0; idx < 4; ++idx) {
    window[idx] = recent[count - 4 + idx];
  }
  window[4] = candidate;

  int8_t intervals[4];
  for (int idx = 0; idx < 4; ++idx) {
    int diff = static_cast<int>(window[idx + 1]) - static_cast<int>(window[idx]);
    intervals[idx] = vocab_data::semitoneToDegree(diff);
  }
  return vocab_data::matchVocabulary(intervals);
}

float thematicReplyCellBonus(const EpisodeRequest& request, VoiceId voice, FortPhase phase,
                             uint8_t candidate, size_t motif_index) {
  if (request.thematic_plan == nullptr)
    return 0.0f;
  if (request.num_voices != 4 || voice != 1)
    return 0.0f;
  if (phase == FortPhase::Dissolution)
    return 0.0f;

  const PatternCandidate* reply =
      request.thematic_plan->episode_drawer.bestForIntent(VoiceIntent::RepeatedReplyCell);
  if (reply == nullptr || reply->notes.size() < 2)
    return 0.0f;

  const NoteEvent& target = reply->notes[motif_index % reply->notes.size()];
  if ((candidate % 12) != (target.pitch % 12))
    return 0.0f;

  BudgetDecision decision = decideBudgetUse(reply->intent, reply->protection,
                                            ViolationClass::AllowedExpressive, false, true);
  if (decision.action == BudgetAction::Reject)
    return 0.0f;

  return 0.005f * std::max(0.25f, reply->motif_identity);
}

bool isLowProtectionReplacementSlot(const EpisodeRequest& request, VoiceId voice, FortPhase phase) {
  if (request.thematic_plan == nullptr)
    return false;
  if (request.num_voices < 3)
    return false;
  if (phase == FortPhase::Kernel)
    return false;
  // Keep subject/reply voices and the final bass support intact.  Inner voices
  // are the replaceable layer when the local motif cannot be made legal.
  return voice >= 2 && voice + 1 < request.num_voices;
}

float episodeIntentHarmonyScore(const EpisodeRequest& request, VoiceId voice, FortPhase phase,
                                Tick tick, uint8_t candidate, const HarmonicEvent* event,
                                Key current_key, ScaleType current_scale) {
  if (event == nullptr)
    return 0.0f;

  bool strong = isStrongBeatInBar(tick);
  bool beat = tick % kTicksPerBeat == 0;
  if (!strong && !beat)
    return 0.0f;

  bool chord_tone = isChordTone(candidate, *event);
  bool local_tone = scale_util::isScaleTone(candidate, current_key, current_scale);
  bool low_layer = voice >= 2;
  bool replaceable = isLowProtectionReplacementSlot(request, voice, phase);
  bool bass_layer = request.num_voices > 0 && voice + 1 == request.num_voices;

  float score = 0.0f;
  if (strong) {
    if (chord_tone) {
      score += low_layer ? 0.85f : 0.25f;
    } else {
      score -= low_layer ? 1.45f : 0.55f;
      if (replaceable)
        score -= 0.35f;
    }

    if (!local_tone) {
      score -= low_layer ? 0.75f : 0.25f;
    }

    if (bass_layer) {
      int pc = getPitchClass(candidate);
      int root_pc = getPitchClass(event->chord.root_pitch);
      int fifth_pc = (root_pc + 7) % 12;
      if (pc == root_pc || pc == fifth_pc) {
        score += 0.35f;
      } else if (!chord_tone) {
        score -= 0.55f;
      }
    }

    if (phase == FortPhase::Dissolution && low_layer && !chord_tone) {
      score -= 0.35f;
    }
  } else if (beat && low_layer) {
    score += chord_tone ? 0.20f : -0.25f;
    if (!local_tone)
      score -= 0.20f;
  }

  return score;
}

const PatternCandidate* recoveryHeldConsonance(const ThematicPlan& plan) {
  for (const auto& candidate : plan.recovery_drawer.candidates()) {
    if (candidate.intent == VoiceIntent::Recovery &&
        candidate.kind == PatternKind::HeldConsonance && !candidate.notes.empty()) {
      return &candidate;
    }
  }
  return nullptr;
}

bool selectRecoveryConsonance(const EpisodeRequest& request, ConstraintState& state, VoiceId voice,
                              FortPhase phase, Tick note_tick, Tick note_dur, Key current_key,
                              const VerticalSnapshot& snap, const uint8_t* prev_pitch_per_voice,
                              const Tick* prev_dur_per_voice, const uint8_t* recent,
                              int recent_count, uint8_t voice_lo, uint8_t voice_hi,
                              uint8_t* recovery_pitch, Tick* recovery_duration) {
  if (!isLowProtectionReplacementSlot(request, voice, phase))
    return false;

  const PatternCandidate* pattern = recoveryHeldConsonance(*request.thematic_plan);
  if (pattern == nullptr)
    return false;

  BudgetDecision decision =
      decideBudgetUse(pattern->intent, pattern->protection, ViolationClass::Forbidden, true, true);
  if (decision.action != BudgetAction::ReplaceLowerLayer)
    return false;

  Tick remaining = request.start_tick + request.duration - note_tick;
  Tick dur = std::min(note_dur, remaining);
  if (dur < duration::kSixteenthNote)
    return false;

  uint8_t seed_pitch = pattern->notes.front().pitch;
  if (seed_pitch == 0)
    return false;

  uint8_t center =
      static_cast<uint8_t>((static_cast<int>(voice_lo) + static_cast<int>(voice_hi)) / 2);
  uint8_t folded_seed = seed_pitch;
  while (folded_seed > voice_hi && folded_seed >= 12)
    folded_seed -= 12;
  while (folded_seed < voice_lo && folded_seed <= 115)
    folded_seed += 12;
  folded_seed = clampPitch(folded_seed, voice_lo, voice_hi);

  uint8_t previous = prev_pitch_per_voice[voice] > 0
                         ? clampPitch(prev_pitch_per_voice[voice], voice_lo, voice_hi)
                         : center;
  ScaleType local_scale = localScaleForKey(request, current_key);
  uint8_t candidates[] = {
      previous,
      static_cast<uint8_t>(scale_util::nearestScaleTone(previous, current_key, local_scale)),
      folded_seed,
      static_cast<uint8_t>(scale_util::nearestScaleTone(center, current_key, local_scale)), center};

  float best_score = -std::numeric_limits<float>::infinity();
  uint8_t best_pitch = 0;
  for (uint8_t candidate : candidates) {
    candidate = clampPitch(candidate, voice_lo, voice_hi);
    float figure_score = computeFigureScore(recent, recent_count, candidate);
    MarkovContext ctx = buildMarkovContext(
        previous, prev_dur_per_voice[voice] > 0 ? prev_dur_per_voice[voice] : kTicksPerBeat,
        note_tick, current_key, local_scale);
    float score = state.evaluate(candidate, dur, voice, note_tick, ctx, snap, request.rule_eval,
                                 request.crossing_eval, request.cp_state_ctx, recent, recent_count,
                                 figure_score);
    const HarmonicEvent* event = nullptr;
    if (request.timeline != nullptr && request.timeline->size() > 0) {
      event = &request.timeline->getAt(note_tick);
    }
    score += episodeIntentHarmonyScore(request, voice, phase, note_tick, candidate, event,
                                       current_key, local_scale);
    if (score > best_score) {
      best_score = score;
      best_pitch = candidate;
    }
  }

  if (best_score <= -std::numeric_limits<float>::infinity())
    return false;
  *recovery_pitch = best_pitch;
  *recovery_duration = dur;
  return true;
}

/// @brief Apply modulation pitch shift for gradual key transition.
///
/// In the second half of the episode, gradually shift pitches toward
/// the target key using fractional chromatic transposition.
///
/// @param pitch Original pitch.
/// @param progress Fractional progress through the episode [0, 1].
/// @param total_shift Total semitone shift for key transition.
/// @return Adjusted pitch (clamped to MIDI range).
uint8_t applyModulationShift(uint8_t pitch, float progress, int total_shift) {
  if (progress <= 0.5f || total_shift == 0)
    return pitch;

  // Linear ramp from 0 to total_shift over the second half.
  float frac = (progress - 0.5f) * 2.0f;  // 0..1 in second half.
  int shift = static_cast<int>(frac * total_shift);
  return clampPitch(static_cast<int>(pitch) + shift, 0, 127);
}

/// @brief Move one diatonic step from a pitch toward a target pitch.
uint8_t stepTowardDiatonic(uint8_t from, uint8_t target, Key key, uint8_t lo, uint8_t hi,
                           ScaleType scale = ScaleType::Major) {
  int from_deg = scale_util::pitchToAbsoluteDegree(from, key, scale);
  int target_deg = scale_util::pitchToAbsoluteDegree(target, key, scale);
  if (target_deg > from_deg) {
    ++from_deg;
  } else if (target_deg < from_deg) {
    --from_deg;
  }
  uint8_t stepped = scale_util::absoluteDegreeToPitch(from_deg, key, scale);
  return clampPitch(stepped, lo, hi);
}

uint8_t foldPitchIntoRangeByOctave(uint8_t pitch, uint8_t lo, uint8_t hi) {
  int folded = static_cast<int>(pitch);
  while (folded > static_cast<int>(hi) && folded >= 12)
    folded -= 12;
  while (folded < static_cast<int>(lo) && folded <= 115)
    folded += 12;
  return clampPitch(folded, lo, hi);
}

float directPerfectMotionPenalty(uint8_t candidate, VoiceId voice, const VerticalSnapshot& snap,
                                 const uint8_t* prev_pitch_per_voice) {
  float penalty = 0.0f;
  uint8_t prev_self = prev_pitch_per_voice[voice];
  if (prev_self == 0)
    return 0.0f;

  for (int vi = 0; vi < snap.num_voices && vi < 6; ++vi) {
    if (vi == voice || snap.pitches[vi] == 0)
      continue;
    uint8_t prev_other = prev_pitch_per_voice[vi];
    if (prev_other == 0)
      continue;

    int curr_interval =
        interval_util::compoundToSimple(absoluteInterval(candidate, snap.pitches[vi]));
    if (!interval_util::isPerfectConsonance(curr_interval))
      continue;

    int prev_interval = interval_util::compoundToSimple(absoluteInterval(prev_self, prev_other));
    int self_motion = static_cast<int>(candidate) - static_cast<int>(prev_self);
    int other_motion = static_cast<int>(snap.pitches[vi]) - static_cast<int>(prev_other);
    bool same_direction =
        self_motion != 0 && other_motion != 0 && (self_motion > 0) == (other_motion > 0);
    if (!same_direction)
      continue;

    if (prev_interval == curr_interval) {
      penalty += 1.8f;
    } else if (std::abs(self_motion) > 2 && std::abs(other_motion) > 2) {
      penalty += 0.9f;
    }
  }
  return penalty;
}

float strongBeatDissonancePenalty(uint8_t candidate, VoiceId voice, Tick tick,
                                  const VerticalSnapshot& snap) {
  if (tick % kTicksPerBeat != 0)
    return 0.0f;

  float penalty = 0.0f;
  for (int vi = 0; vi < snap.num_voices && vi < 6; ++vi) {
    if (vi == voice || snap.pitches[vi] == 0)
      continue;
    int ivl = interval_util::compoundToSimple(absoluteInterval(candidate, snap.pitches[vi]));
    if (ivl == 1 || ivl == 2 || ivl == 6 || ivl == 10 || ivl == 11) {
      penalty += 1.2f;
    }
  }
  return penalty;
}

float sustainedStrongBeatDissonancePenalty(const std::vector<NoteEvent>& notes, uint8_t num_voices,
                                           uint8_t candidate, VoiceId voice, Tick tick,
                                           Tick duration) {
  if (duration == 0)
    return 0.0f;
  Tick end = tick + duration;
  Tick probe = ((tick / kTicksPerBeat) + 1) * kTicksPerBeat;
  if (probe <= tick || probe >= end)
    return 0.0f;

  VerticalSnapshot snap = buildSnapshot(notes, probe, num_voices);
  float penalty = 0.0f;
  for (int vi = 0; vi < snap.num_voices && vi < 6; ++vi) {
    if (vi == voice || snap.pitches[vi] == 0)
      continue;
    int ivl = interval_util::compoundToSimple(absoluteInterval(candidate, snap.pitches[vi]));
    if (ivl == 1 || ivl == 2 || ivl == 6 || ivl == 10 || ivl == 11) {
      penalty += (ivl == 10 || ivl == 11) ? 2.0f : 1.5f;
    }
  }
  return penalty;
}

float protectedDialogueOverlapPenalty(const std::vector<NoteEvent>& notes, uint8_t num_voices,
                                      uint8_t candidate, VoiceId voice, Tick tick, Tick duration) {
  if (voice > 1 || duration == 0)
    return 0.0f;
  Tick end = tick + duration;
  float penalty = 0.0f;

  for (Tick probe = tick; probe < end; probe += duration::kEighthNote) {
    VerticalSnapshot snap = buildSnapshot(notes, probe, num_voices);
    for (int vi = 0; vi < snap.num_voices && vi <= 1; ++vi) {
      if (vi == voice || snap.pitches[vi] == 0)
        continue;
      int ivl = interval_util::compoundToSimple(absoluteInterval(candidate, snap.pitches[vi]));
      if (ivl == 0) {
        penalty += 2.8f;
      } else if (ivl == 1 || ivl == 2 || ivl == 6 || ivl == 10 || ivl == 11) {
        penalty += 1.8f;
      }
    }
  }

  return penalty;
}

/// @brief Place bass fragments for voice 2+ with constraint validation.
///
/// Reuses the pattern from fortspinnung.cpp: extract tail motif, augment,
/// place in lower register. Each bass note is validated through
/// state.evaluate().
///
/// @param result Output note vector (appended to).
/// @param state Constraint state (modified via advance).
/// @param placed_notes Notes placed by voices 0-1 (for tail extraction and snapshots).
/// @param start_tick Episode start tick.
/// @param duration Episode duration.
/// @param num_voices Total voice count.
/// @param current_key Current key.
/// @param rng RNG for probabilistic emission.
/// @param timeline Harmonic timeline for bass pitch selection (nullable).
/// @param grammar Fortspinnung grammar for phase boundary calculation.
/// @param rule_eval Rule evaluator for parallel perfect checks (nullable).
/// @param crossing_eval Bach evaluator for crossing checks (nullable).
/// @param cp_state_ctx Counterpoint state context (nullable).
void placeBassFragments(std::vector<NoteEvent>& result, ConstraintState& state,
                        const std::vector<NoteEvent>& placed_notes, Tick start_tick, Tick duration,
                        uint8_t num_voices, Key current_key, std::mt19937& rng,
                        const HarmonicTimeline* timeline, const FortspinnungGrammar& grammar,
                        const IRuleEvaluator* rule_eval, const BachRuleEvaluator* crossing_eval,
                        const CounterpointState* cp_state_ctx) {
  if (num_voices < 3 || placed_notes.empty())
    return;

  // Collect voice 0 notes for tail extraction.
  std::vector<NoteEvent> voice0_notes;
  for (const auto& note : placed_notes) {
    if (note.voice == 0)
      voice0_notes.push_back(note);
  }
  if (voice0_notes.empty())
    return;

  // Extract tail motif and augment for bass.
  auto tail = extractTailMotif(voice0_notes, 3);
  if (tail.size() > 3)
    tail.resize(3);
  auto bass_fragment = tail;  // Preserve original rhythm (no augment).

  // Get voice 2 range.
  auto [v2_lo, v2_hi] = getFugueVoiceRange(2, num_voices);
  int v2_lo_int = static_cast<int>(v2_lo);
  int v2_hi_int = static_cast<int>(v2_hi);

  // Map fragment pitches to bass register.
  for (auto& note : bass_fragment) {
    int mapped = static_cast<int>(note.pitch);
    if (mapped < v2_lo_int)
      mapped += 12;
    if (mapped > v2_hi_int)
      mapped -= 12;
    note.pitch = clampPitch(mapped, v2_lo, v2_hi);
  }

  // Add duration variety to bass fragment notes (augment creates uniform durations).
  // Randomly stretch/compress individual notes for rhythmic interest.
  if (bass_fragment.size() >= 2) {
    std::mt19937 dur_rng(rng());
    for (size_t idx = 0; idx < bass_fragment.size(); ++idx) {
      float factor = rng::rollFloat(dur_rng, 0.6f, 1.6f);
      bass_fragment[idx].duration =
          quantizeToGrid(std::max(static_cast<Tick>(bass_fragment[idx].duration * factor),
                                  static_cast<Tick>(kTicksPerBeat / 2)));
    }
  }

  Tick frag_dur = motifDuration(bass_fragment);
  if (frag_dur == 0)
    frag_dur = kTicksPerBeat * 2;

  // Emission probability: keep the lower inner voice present through the
  // sequence. Candidates are still passed through ConstraintState, so raising
  // this value mainly reduces empty spans rather than forcing unsafe notes.
  float emit_prob = rng::rollFloat(rng, 0.85f, 0.95f);

  Tick bass_tick = start_tick;
  bool use_fragment = true;
  uint8_t bass_prev_pitch = bass_fragment.empty() ? 48 : bass_fragment[0].pitch;

  while (bass_tick < start_tick + duration) {
    if (!rng::rollProbability(rng, emit_prob)) {
      bass_tick += use_fragment ? frag_dur : kTicksPerBar;
      use_fragment = !use_fragment;
      continue;
    }

    if (use_fragment && !bass_fragment.empty()) {
      for (const auto& frag_note : bass_fragment) {
        Tick note_tick = frag_note.start_tick + bass_tick;
        if (note_tick >= start_tick + duration)
          break;

        // Evaluate through constraint state.
        VerticalSnapshot snap = buildSnapshot(result, note_tick, num_voices);
        MarkovContext ctx =
            buildMarkovContext(bass_prev_pitch, kTicksPerBeat, note_tick, current_key);
        float score = state.evaluate(frag_note.pitch, frag_note.duration, 2, note_tick, ctx, snap,
                                     rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

        if (score > -std::numeric_limits<float>::infinity()) {
          NoteEvent evt = frag_note;
          evt.start_tick = note_tick;
          evt.voice = 2;
          evt.source = BachNoteSource::EpisodeMaterial;
          Tick remaining = start_tick + duration - note_tick;
          if (evt.duration > remaining)
            evt.duration = remaining;
          evt.duration = durationBeforeBoundaryDissonance(evt.start_tick, evt.duration, evt.pitch,
                                                          timeline, start_tick, duration);
          // Skip notes shorter than a sixteenth to avoid rhythmic debris.
          if (evt.duration < duration::kSixteenthNote)
            continue;
          result.push_back(evt);
          state.advance(note_tick, evt.pitch, 2, evt.duration, current_key);
          bass_prev_pitch = evt.pitch;
        }
      }
      bass_tick += frag_dur;
    } else {
      // Anchor note: pitch from timeline, or descending circle-of-fifths fallback.
      //
      // Bach's episodes use 2-bar sequential patterns descending through the
      // circle of fifths: I -> IV -> vii -> iii -> vi -> ii -> V.
      // The vii step (offset=11) is replaced by V (offset=7) in the bass for
      // stability — upper voices provide the diminished function.
      //
      // Strong beats (beat 1 of each bar) get the sequence root pitch;
      // weak beats use diatonic passing motion between current and next step.
      int anchor_pitch;
      if (timeline) {
        const Chord& chord = timeline->getChordAt(bass_tick);
        anchor_pitch = static_cast<int>(chord.root_pitch);
        // Map to bass register (octave fold).
        while (anchor_pitch > v2_hi_int)
          anchor_pitch -= 12;
        while (anchor_pitch < v2_lo_int)
          anchor_pitch += 12;
      } else {
        // Descending circle-of-fifths: I -> IV -> vii(->V) -> iii -> vi -> ii -> V.
        constexpr int kCircleOfFifths[] = {0, 5, 7, 4, 9, 2, 7};
        constexpr int kNumCircleSteps = 7;
        constexpr int kMaxSteps = 5;  // Stop at step 4 (vi); full cycle too long.

        // 2-bar unit stepping from episode start.
        int raw_step = static_cast<int>((bass_tick - start_tick) / (kTicksPerBar * 2));
        int step_idx = std::min(raw_step % kNumCircleSteps, kMaxSteps - 1);

        // Strong beat: sequence root pitch.
        // Weak beat: diatonic passing tone between current and next step.
        bool is_strong_beat = (bass_tick % kTicksPerBar) < static_cast<Tick>(kTicksPerBeat);
        int root_offset = kCircleOfFifths[step_idx];
        int base_pitch = 48 + static_cast<int>(current_key) + root_offset;

        if (is_strong_beat) {
          anchor_pitch = base_pitch;
        } else {
          // Interpolate toward next step via diatonic passing motion.
          int next_idx = std::min(step_idx + 1, kMaxSteps - 1);
          int next_offset = kCircleOfFifths[next_idx];
          int next_pitch = 48 + static_cast<int>(current_key) + next_offset;

          // Use absolute scale degrees for smooth diatonic interpolation.
          int curr_deg = scale_util::pitchToAbsoluteDegree(
              static_cast<uint8_t>(clampPitch(base_pitch, 0, 127)), current_key, ScaleType::Major);
          int next_deg = scale_util::pitchToAbsoluteDegree(
              static_cast<uint8_t>(clampPitch(next_pitch, 0, 127)), current_key, ScaleType::Major);

          // Calculate how far into the 2-bar unit we are (fractional).
          Tick unit_offset = (bass_tick - start_tick) % (kTicksPerBar * 2);
          float frac = static_cast<float>(unit_offset) / static_cast<float>(kTicksPerBar * 2);

          int passing_deg = curr_deg + static_cast<int>((next_deg - curr_deg) * frac);
          anchor_pitch = static_cast<int>(
              scale_util::absoluteDegreeToPitch(passing_deg, current_key, ScaleType::Major));
        }

        while (anchor_pitch > v2_hi_int)
          anchor_pitch -= 12;
        while (anchor_pitch < v2_lo_int)
          anchor_pitch += 12;
      }
      anchor_pitch = clampPitch(anchor_pitch, v2_lo, v2_hi);

      // Phase-dependent bass anchor duration distribution.
      // Sequence: shorter (includes 16ths) for rhythmic drive.
      // Kernel/Dissolution: longer for harmonic stability.
      Tick base_dur;
      float bass_progress = static_cast<float>(bass_tick - start_tick) /
                            static_cast<float>(std::max(duration, static_cast<Tick>(1)));
      bool is_sequence_phase = bass_progress >= grammar.kernel_ratio &&
                               bass_progress < grammar.kernel_ratio + grammar.sequence_ratio;
      float dur_roll = rng::rollFloat(rng, 0.0f, 1.0f);
      if (is_sequence_phase) {
        // Sequence: favor short Fortspinnung motion; organ fugue references
        // use far more 16th/8th activity than half-bar anchors.
        if (dur_roll < 0.25f)
          base_dur = duration::kSixteenthNote;
        else if (dur_roll < 0.70f)
          base_dur = kTicksPerBeat / 2;
        else if (dur_roll < 0.95f)
          base_dur = kTicksPerBeat;
        else
          base_dur = kTicksPerBeat * 2;
      } else {
        // Kernel/Dissolution: still anchoring, but avoid over-weighting
        // half/bar notes that make the generated episode rhythm too static.
        if (dur_roll < 0.45f)
          base_dur = kTicksPerBeat / 2;
        else if (dur_roll < 0.85f)
          base_dur = kTicksPerBeat;
        else if (dur_roll < 0.97f)
          base_dur = kTicksPerBeat * 2;
        else
          base_dur = kTicksPerBar;
      }

      // Bass floor: sustain when upper voices have sustained figuration (>=2 beats).
      {
        int short_note_count = 0;
        for (const auto& note : result) {
          if (note.voice >= 2)
            continue;
          if (note.start_tick + note.duration > bass_tick - kTicksPerBeat * 2 &&
              note.start_tick <= bass_tick && note.duration <= duration::kEighthNote) {
            ++short_note_count;
          }
        }
        if (!is_sequence_phase && short_note_count >= 4 && base_dur < duration::kEighthNote) {
          base_dur = duration::kEighthNote;
        }
      }
      Tick anchor_dur = std::min(base_dur, start_tick + duration - bass_tick);
      anchor_dur = durationBeforeBoundaryDissonance(bass_tick, anchor_dur,
                                                    static_cast<uint8_t>(anchor_pitch), timeline,
                                                    start_tick, duration);
      if (anchor_dur >= duration::kSixteenthNote) {
        VerticalSnapshot snap = buildSnapshot(result, bass_tick, num_voices);
        MarkovContext ctx =
            buildMarkovContext(bass_prev_pitch, kTicksPerBeat, bass_tick, current_key);
        float score =
            state.evaluate(static_cast<uint8_t>(anchor_pitch), anchor_dur, 2, bass_tick, ctx, snap,
                           rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

        if (score > -std::numeric_limits<float>::infinity()) {
          NoteEvent anchor;
          anchor.start_tick = bass_tick;
          anchor.duration = anchor_dur;
          anchor.pitch = static_cast<uint8_t>(anchor_pitch);
          anchor.velocity = 80;
          anchor.voice = 2;
          anchor.source = BachNoteSource::EpisodeMaterial;
          result.push_back(anchor);
          state.advance(bass_tick, anchor.pitch, 2, anchor_dur, current_key);
          bass_prev_pitch = anchor.pitch;
        }
      }
      bass_tick += kTicksPerBeat;  // Beat advance keeps the inner voice active.
    }
    use_fragment = !use_fragment;
  }

  // Post-sweep: voice 2 notes exceeding range ceiling octave-folded.
  for (auto& note : result) {
    if (note.voice != 2)
      continue;
    if (note.start_tick < start_tick || note.start_tick >= start_tick + duration)
      continue;
    int p = static_cast<int>(note.pitch);
    while (p > v2_hi_int && p - 12 >= v2_lo_int)
      p -= 12;
    while (p < v2_lo_int && p + 12 <= v2_hi_int)
      p += 12;
    note.pitch = clampPitch(p, v2_lo, v2_hi);
  }
}

/// @brief Place pedal voice (voice 3+) with tonic/dominant anchor notes.
///
/// For 4+ voice fugues, voice 3 (or the last voice) alternates between
/// tonic and dominant anchor notes in the pedal register. Each note is
/// validated through state.evaluate() before placement.
///
/// @param result Output note vector (appended to).
/// @param state Constraint state (modified via advance).
/// @param start_tick Episode start tick.
/// @param duration Episode duration.
/// @param num_voices Total voice count.
/// @param current_key Current key.
/// @param rng RNG for probabilistic emission.
/// @param timeline Harmonic timeline for boundary-aware durations (nullable).
/// @param rule_eval Rule evaluator for parallel perfect checks (nullable).
/// @param crossing_eval Bach evaluator for crossing checks (nullable).
/// @param cp_state_ctx Counterpoint state context (nullable).
void placePedalVoice(std::vector<NoteEvent>& result, ConstraintState& state, Tick start_tick,
                     Tick duration, uint8_t num_voices, Key current_key, std::mt19937& rng,
                     const HarmonicTimeline* timeline, const IRuleEvaluator* rule_eval,
                     const BachRuleEvaluator* crossing_eval,
                     const CounterpointState* cp_state_ctx) {
  if (num_voices < 4)
    return;

  VoiceId pedal_voice = num_voices - 1;
  auto [pedal_lo, pedal_hi] = getFugueVoiceRange(pedal_voice, num_voices);

  float emit_prob = rng::rollFloat(rng, 0.50f, 0.70f);
  constexpr int kMaxSilentBars = 4;
  int consecutive_silent_bars = 0;
  Tick pedal_tick = start_tick;
  bool use_tonic = true;
  int initial_tonic = 36 + static_cast<int>(current_key);
  initial_tonic = clampPitch(initial_tonic, pedal_lo, pedal_hi);
  uint8_t pedal_prev_pitch = static_cast<uint8_t>(initial_tonic);

  while (pedal_tick < start_tick + duration) {
    Key local_key = current_key;
    ScaleType local_scale = ScaleType::Major;
    if (timeline != nullptr && timeline->size() > 0) {
      const HarmonicEvent& event = timeline->getAt(pedal_tick);
      local_key = event.key;
      local_scale = localScaleForEvent(event);
    }
    int tonic_bass = 36 + static_cast<int>(local_key);
    tonic_bass = clampPitch(tonic_bass, pedal_lo, pedal_hi);
    int dominant_bass = tonic_bass + 7;
    if (dominant_bass > static_cast<int>(pedal_hi))
      dominant_bass -= 12;
    dominant_bass = clampPitch(dominant_bass, pedal_lo, pedal_hi);

    bool force_emit = (consecutive_silent_bars >= kMaxSilentBars);
    bool emit = force_emit || rng::rollProbability(rng, emit_prob);

    if (!emit) {
      consecutive_silent_bars++;
      pedal_tick += kTicksPerBeat * 2;  // Half-bar advance.
      use_tonic = !use_tonic;
      continue;
    }

    consecutive_silent_bars = 0;
    // Tonic/dominant/subdominant distribution shifts toward dominant near episode end.
    float progress = static_cast<float>(pedal_tick - start_tick) /
                     static_cast<float>(std::max(duration, static_cast<Tick>(1)));
    float t_prob = (progress >= 0.75f) ? 0.25f : 0.50f;
    float d_prob = (progress >= 0.75f) ? 0.60f : 0.35f;
    int anchor;
    float pedal_roll = rng::rollFloat(rng, 0.0f, 1.0f);
    if (pedal_roll < t_prob) {
      anchor = tonic_bass;
    } else if (pedal_roll < t_prob + d_prob) {
      anchor = dominant_bass;
    } else {
      // Subdominant (IV): tonic + 5 semitones.
      int subdominant_bass = tonic_bass + 5;
      if (subdominant_bass > static_cast<int>(pedal_hi))
        subdominant_bass -= 12;
      anchor = clampPitch(subdominant_bass, pedal_lo, pedal_hi);
    }
    Tick remaining_in_episode = start_tick + duration - pedal_tick;
    Tick pattern_span = std::min(kTicksPerBeat * 2, remaining_in_episode);
    Tick pattern_durs[8] = {};
    int pattern_len = 1;
    float rhythm_roll = rng::rollFloat(rng, 0.0f, 1.0f);
    bool cadence_zone = progress >= 0.75f;
    if (!cadence_zone && rhythm_roll < 0.05f && pattern_span >= kTicksPerBeat * 2) {
      pattern_len = 8;
      for (int idx = 0; idx < pattern_len; ++idx) {
        pattern_durs[idx] = duration::kSixteenthNote;
      }
    } else if (!cadence_zone && rhythm_roll < 0.20f && pattern_span >= kTicksPerBeat * 2) {
      // Organ-fugue pedal reference: 4x8th is the most common pedal n-gram.
      pattern_len = 4;
      for (int idx = 0; idx < pattern_len; ++idx)
        pattern_durs[idx] = kTicksPerBeat / 2;
    } else if (rhythm_roll < (cadence_zone ? 0.20f : 0.45f) && pattern_span >= kTicksPerBeat * 2) {
      pattern_len = 2;
      pattern_durs[0] = kTicksPerBeat;
      pattern_durs[1] = kTicksPerBeat;
    } else {
      pattern_len = 1;
      pattern_durs[0] = pattern_span;
    }

    int next_anchor = use_tonic ? dominant_bass : tonic_bass;
    uint8_t anchor_pitch = static_cast<uint8_t>(anchor);
    uint8_t next_pitch = static_cast<uint8_t>(next_anchor);
    for (int idx = 0; idx < pattern_len; ++idx) {
      Tick sub_tick = pedal_tick;
      for (int prev = 0; prev < idx; ++prev)
        sub_tick += pattern_durs[prev];
      if (sub_tick >= start_tick + duration)
        break;

      Tick sub_dur = std::min(pattern_durs[idx], start_tick + duration - sub_tick);
      if (sub_dur == 0)
        continue;

      uint8_t target_pitch = anchor_pitch;
      if (pattern_len == 2) {
        target_pitch = (idx == 0) ? anchor_pitch
                                  : stepTowardDiatonic(anchor_pitch, next_pitch, local_key,
                                                       pedal_lo, pedal_hi, local_scale);
      } else if (pattern_len == 4 || pattern_len == 8) {
        if (idx == 0) {
          target_pitch = anchor_pitch;
        } else if (idx == pattern_len / 2) {
          target_pitch = next_pitch;
        } else if (idx < pattern_len / 2) {
          target_pitch = stepTowardDiatonic(anchor_pitch, next_pitch, local_key, pedal_lo, pedal_hi,
                                            local_scale);
        } else {
          target_pitch = stepTowardDiatonic(next_pitch, anchor_pitch, local_key, pedal_lo, pedal_hi,
                                            local_scale);
        }
      }

      // On beat starts, keep the bass harmonically anchored; offbeats may pass.
      if (sub_tick % kTicksPerBeat == 0 && idx != 0 && pattern_len >= 4) {
        target_pitch = (idx >= pattern_len / 2) ? next_pitch : anchor_pitch;
      }

      // Avoid handing repeated pedal anchors to the late repair sweep.  If the
      // anchor would repeat the previous pedal pitch, make the intended support
      // line move toward the next structural anchor now.
      if (target_pitch == pedal_prev_pitch && sub_tick > start_tick) {
        uint8_t toward_next = stepTowardDiatonic(pedal_prev_pitch, next_pitch, local_key, pedal_lo,
                                                 pedal_hi, local_scale);
        if (toward_next != pedal_prev_pitch) {
          target_pitch = toward_next;
        } else {
          int upward = scale_util::nearestScaleTone(
              static_cast<uint8_t>(std::min<int>(pedal_hi, pedal_prev_pitch + 2)), local_key,
              local_scale);
          int downward = scale_util::nearestScaleTone(
              static_cast<uint8_t>(std::max<int>(pedal_lo, pedal_prev_pitch - 2)), local_key,
              local_scale);
          if (upward != pedal_prev_pitch && upward <= pedal_hi) {
            target_pitch = static_cast<uint8_t>(upward);
          } else if (downward != pedal_prev_pitch && downward >= pedal_lo) {
            target_pitch = static_cast<uint8_t>(downward);
          }
        }
      }

      sub_dur = durationBeforeBoundaryDissonance(sub_tick, sub_dur, target_pitch, timeline,
                                                 start_tick, duration);
      if (sub_dur < duration::kSixteenthNote)
        continue;

      VerticalSnapshot snap = buildSnapshot(result, sub_tick, num_voices);
      MarkovContext ctx = buildMarkovContext(pedal_prev_pitch, sub_dur, sub_tick, local_key);
      float score = state.evaluate(target_pitch, sub_dur, pedal_voice, sub_tick, ctx, snap,
                                   rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

      if (score > -std::numeric_limits<float>::infinity()) {
        NoteEvent note;
        note.start_tick = sub_tick;
        note.duration = sub_dur;
        note.pitch = target_pitch;
        note.velocity = 80;
        note.voice = pedal_voice;
        note.source = BachNoteSource::EpisodeMaterial;
        result.push_back(note);
        state.advance(sub_tick, note.pitch, pedal_voice, note.duration, local_key);
        pedal_prev_pitch = note.pitch;
      }
    }
    pedal_tick += kTicksPerBeat * 2;  // Half-bar advance.
    use_tonic = !use_tonic;
  }
}

/// @brief Place held tones on resting voices.
///
/// In 4+ voice episodes, one inner voice "rests" by holding sustained
/// notes (whole notes) while other voices have active material.
/// The resting voice rotates based on episode_index. Each held tone is
/// validated through state.evaluate() before placement.
///
/// @param result Output note vector (appended to).
/// @param placed_notes Existing notes (for pitch reference).
/// @param state Constraint state (modified via advance).
/// @param start_tick Episode start tick.
/// @param duration Episode duration.
/// @param num_voices Total voice count.
/// @param episode_index Episode ordinal (determines resting voice).
/// @param current_key Current key.
/// @param rule_eval Rule evaluator for parallel perfect checks (nullable).
/// @param crossing_eval Bach evaluator for crossing checks (nullable).
/// @param cp_state_ctx Counterpoint state context (nullable).
void placeHeldTones(std::vector<NoteEvent>& result, const std::vector<NoteEvent>& placed_notes,
                    ConstraintState& state, Tick start_tick, Tick duration, uint8_t num_voices,
                    int episode_index, Key current_key, const IRuleEvaluator* rule_eval,
                    const BachRuleEvaluator* crossing_eval, const CounterpointState* cp_state_ctx) {
  if (num_voices < 4)
    return;

  // Resting voice rotates through inner voices (not voice 0/1 active, not bass).
  // For 4 voices: only voice 2 can rest (voice 0/1 active, voice 3 = bass).
  // For 5 voices: voices 2-3 can rest (voice 0/1 active, voice 4 = bass).
  uint8_t first_inner = 2;
  uint8_t last_inner = num_voices - 2;
  if (first_inner > last_inner)
    return;

  uint8_t inner_count = last_inner - first_inner + 1;
  VoiceId resting_voice = first_inner + static_cast<VoiceId>(episode_index % inner_count);

  // Check if the resting voice already has notes.
  bool has_notes = false;
  for (const auto& note : placed_notes) {
    if (note.voice == resting_voice) {
      has_notes = true;
      break;
    }
  }
  if (has_notes)
    return;  // Voice already active, skip.

  // Get voice range for held tone pitch.
  auto [v_lo, v_hi] = getFugueVoiceRange(resting_voice, num_voices);
  int held_pitch = (static_cast<int>(v_lo) + static_cast<int>(v_hi)) / 2;
  // Snap to scale tone.
  held_pitch =
      scale_util::nearestScaleTone(static_cast<uint8_t>(held_pitch), current_key, ScaleType::Major);

  // Place half-note held tones across the episode.
  Tick held_tick = start_tick;
  int held_step_count = 0;
  while (held_tick < start_tick + duration) {
    Tick held_dur = std::min(kTicksPerBeat * 2,  // Half note.
                             start_tick + duration - held_tick);
    if (held_dur == 0)
      break;

    VerticalSnapshot snap = buildSnapshot(result, held_tick, num_voices);
    MarkovContext ctx =
        buildMarkovContext(static_cast<uint8_t>(held_pitch), kTicksPerBeat, held_tick, current_key);
    float score =
        state.evaluate(static_cast<uint8_t>(held_pitch), held_dur, resting_voice, held_tick, ctx,
                       snap, rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

    if (score > -std::numeric_limits<float>::infinity()) {
      NoteEvent held;
      held.start_tick = held_tick;
      held.duration = held_dur;
      held.pitch = static_cast<uint8_t>(held_pitch);
      held.velocity = 80;
      held.voice = resting_voice;
      held.source = BachNoteSource::EpisodeMaterial;
      result.push_back(held);
      state.advance(held_tick, held.pitch, resting_voice, held_dur, current_key);
    }

    held_tick += kTicksPerBeat * 2;  // Half-bar advance.
    ++held_step_count;
    // Subtle motion: move pitch by one step every 2 steps.
    if (held_step_count % 2 == 0) {
      held_pitch = clampPitch(held_pitch - 1, v_lo, v_hi);
      held_pitch = scale_util::nearestScaleTone(static_cast<uint8_t>(held_pitch), current_key,
                                                ScaleType::Major);
    }
  }
}

bool isConsonantWithSoundingVoices(const std::vector<NoteEvent>& notes, size_t skip_idx, Tick tick,
                                   VoiceId voice, uint8_t pitch) {
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    if (idx == skip_idx)
      continue;
    const auto& other = notes[idx];
    if (other.voice == voice)
      continue;
    if (other.start_tick > tick || other.start_tick + other.duration <= tick) {
      continue;
    }
    int simple = interval_util::compoundToSimple(absoluteInterval(pitch, other.pitch));
    if (!interval_util::isConsonance(simple)) {
      return false;
    }
  }
  return true;
}

bool hasVoiceSoundingAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick) {
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
      return true;
    }
  }
  return false;
}

uint8_t findNextVoicePitch(const std::vector<NoteEvent>& notes, size_t note_idx,
                           Tick original_end) {
  const auto& note = notes[note_idx];
  Tick best_tick = std::numeric_limits<Tick>::max();
  uint8_t best_pitch = note.pitch;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    if (idx == note_idx)
      continue;
    const auto& cand = notes[idx];
    if (cand.voice != note.voice || cand.start_tick < original_end)
      continue;
    if (cand.start_tick < best_tick) {
      best_tick = cand.start_tick;
      best_pitch = cand.pitch;
    }
  }
  return best_pitch;
}

bool findPrevNextVoiceNotes(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick,
                            NoteEvent& prev_note, NoteEvent& next_note) {
  bool found_prev = false;
  bool found_next = false;
  Tick prev_start = 0;
  Tick next_start = std::numeric_limits<Tick>::max();
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick + note.duration <= tick && note.start_tick >= prev_start) {
      prev_note = note;
      prev_start = note.start_tick;
      found_prev = true;
    } else if (note.start_tick >= tick && note.start_tick < next_start) {
      next_note = note;
      next_start = note.start_tick;
      found_next = true;
    }
  }
  return found_prev && found_next;
}

bool findPrevVoiceNote(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick,
                       NoteEvent& prev_note) {
  bool found_prev = false;
  Tick prev_start = 0;
  for (const auto& note : notes) {
    if (note.voice != voice)
      continue;
    if (note.start_tick + note.duration <= tick && note.start_tick >= prev_start) {
      prev_note = note;
      prev_start = note.start_tick;
      found_prev = true;
    }
  }
  return found_prev;
}

int insertEpisodePassingContinuations(std::vector<NoteEvent>& notes, Tick episode_start,
                                      Tick episode_duration, uint8_t num_voices, Key key) {
  int inserted = 0;
  const Tick kernel_end = episode_start + episode_duration / 4;
  const size_t original_size = notes.size();
  for (size_t idx = 0; idx < original_size; ++idx) {
    auto& note = notes[idx];
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    uint8_t max_running_voice = num_voices == 4 ? 2 : 1;
    if (note.voice > max_running_voice)
      continue;
    if (note.start_tick < kernel_end)
      continue;
    if (note.duration < duration::kQuarterNote)
      continue;

    Tick first_dur = note.duration / 2;
    first_dur = quantizeToGrid(first_dur);
    Tick second_tick = note.start_tick + first_dur;
    Tick original_end = note.start_tick + note.duration;
    if (first_dur < duration::kEighthNote || second_tick >= original_end ||
        second_tick >= episode_start + episode_duration) {
      continue;
    }
    Tick second_dur = original_end - second_tick;
    if (second_dur < duration::kEighthNote)
      continue;

    auto voice_range = getFugueVoiceRange(note.voice, num_voices);
    uint8_t voice_lo = voice_range.first;
    uint8_t voice_hi = voice_range.second;
    uint8_t next_pitch = findNextVoicePitch(notes, idx, original_end);
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(note.pitch)) > 7) {
      continue;
    }
    uint8_t candidate = stepTowardDiatonic(note.pitch, next_pitch, key, voice_lo, voice_hi);
    if (candidate == note.pitch && next_pitch == note.pitch) {
      uint8_t up = stepTowardDiatonic(note.pitch, clampPitch(note.pitch + 4, 0, 127), key, voice_lo,
                                      voice_hi);
      uint8_t down = stepTowardDiatonic(note.pitch, clampPitch(note.pitch - 4, 0, 127), key,
                                        voice_lo, voice_hi);
      candidate = (up != note.pitch) ? up : down;
    }
    if (candidate == note.pitch)
      continue;
    if (candidate < voice_lo || candidate > voice_hi)
      continue;
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(candidate)) > 7) {
      continue;
    }
    if (!isConsonantWithSoundingVoices(notes, idx, second_tick, note.voice, candidate)) {
      continue;
    }

    NoteEvent continuation = note;
    continuation.start_tick = second_tick;
    continuation.duration = second_dur;
    continuation.pitch = candidate;
    note.duration = first_dur;
    notes.push_back(continuation);
    ++inserted;
  }
  return inserted;
}

int fillSparseInnerVoiceGaps(std::vector<NoteEvent>& notes, Tick episode_start,
                             Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  std::vector<NoteEvent> inner;
  for (const auto& note : notes) {
    if (note.source == BachNoteSource::EpisodeMaterial && note.voice == kInnerVoice &&
        note.start_tick >= episode_start && note.start_tick < episode_start + episode_duration) {
      inner.push_back(note);
    }
  }
  if (inner.size() < 2)
    return 0;

  std::sort(inner.begin(), inner.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  int inserted = 0;
  auto voice_range = getFugueVoiceRange(kInnerVoice, num_voices);
  uint8_t voice_lo = voice_range.first;
  uint8_t voice_hi = voice_range.second;
  for (size_t idx = 0; idx + 1 < inner.size(); ++idx) {
    const NoteEvent& prev = inner[idx];
    const NoteEvent& next = inner[idx + 1];
    Tick gap_start = prev.start_tick + prev.duration;
    Tick gap_end = next.start_tick;
    if (gap_end <= gap_start)
      continue;
    Tick gap = gap_end - gap_start;
    if (gap < duration::kQuarterNote)
      continue;
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(prev.pitch)) > 12) {
      continue;
    }

    uint8_t candidate = stepTowardDiatonic(prev.pitch, next.pitch, key, voice_lo, voice_hi);
    if (candidate == prev.pitch) {
      candidate =
          stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch + (idx % 2 == 0 ? 4 : -4), 0, 127),
                             key, voice_lo, voice_hi);
    }
    if (candidate == prev.pitch)
      continue;
    if (!isConsonantWithSoundingVoices(notes, notes.size(), gap_start, kInnerVoice, candidate)) {
      continue;
    }

    NoteEvent fill = prev;
    fill.start_tick = gap_start;
    fill.duration = duration::kEighthNote;
    if (fill.start_tick + fill.duration > gap_end) {
      fill.duration = gap_end - fill.start_tick;
    }
    if (fill.duration < duration::kEighthNote)
      continue;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;

    if (gap < duration::kHalfNote)
      continue;
    Tick second_tick = gap_start + duration::kEighthNote;
    if (second_tick + duration::kEighthNote > gap_end)
      continue;
    uint8_t second = stepTowardDiatonic(candidate, next.pitch, key, voice_lo, voice_hi);
    if (second == candidate || second == prev.pitch)
      continue;
    if (!isConsonantWithSoundingVoices(notes, notes.size(), second_tick, kInnerVoice, second)) {
      continue;
    }
    NoteEvent fill2 = fill;
    fill2.start_tick = second_tick;
    fill2.pitch = second;
    notes.push_back(fill2);
    ++inserted;
  }

  return inserted;
}

int fillSparseUpperVoiceGaps(std::vector<NoteEvent>& notes, Tick episode_start,
                             Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices < 3)
    return 0;

  constexpr VoiceId kUpperVoice = 0;
  std::vector<NoteEvent> upper;
  for (const auto& note : notes) {
    if (note.source == BachNoteSource::EpisodeMaterial && note.voice == kUpperVoice &&
        note.start_tick >= episode_start && note.start_tick < episode_start + episode_duration) {
      upper.push_back(note);
    }
  }
  if (upper.size() < 2)
    return 0;

  std::sort(upper.begin(), upper.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  int inserted = 0;
  constexpr int kMaxInserted = 12;
  auto [voice_lo, voice_hi] = getFugueVoiceRange(kUpperVoice, num_voices);
  for (size_t idx = 0; idx + 1 < upper.size(); ++idx) {
    const NoteEvent& prev = upper[idx];
    const NoteEvent& next = upper[idx + 1];
    Tick gap_start = prev.start_tick + prev.duration;
    Tick gap_end = next.start_tick;
    if (gap_end <= gap_start)
      continue;
    Tick gap = gap_end - gap_start;
    if (gap < kTicksPerBeat)
      continue;
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(prev.pitch)) > 12) {
      continue;
    }

    uint8_t last_pitch = prev.pitch;
    for (Tick tick = gap_start; tick + duration::kEighthNote <= gap_end;
         tick += duration::kEighthNote) {
      if (inserted >= kMaxInserted)
        return inserted;
      if (tick % kTicksPerBeat == 0)
        continue;
      if (hasVoiceSoundingAt(notes, kUpperVoice, tick))
        continue;

      uint8_t candidate = stepTowardDiatonic(last_pitch, next.pitch, key, voice_lo, voice_hi);
      if (candidate == last_pitch) {
        uint8_t up = stepTowardDiatonic(last_pitch, clampPitch(last_pitch + 3, 0, 127), key,
                                        voice_lo, voice_hi);
        uint8_t down = stepTowardDiatonic(last_pitch, clampPitch(last_pitch - 3, 0, 127), key,
                                          voice_lo, voice_hi);
        bool up_ok = up != last_pitch &&
                     std::abs(static_cast<int>(up) - static_cast<int>(last_pitch)) <= 2 &&
                     isConsonantWithSoundingVoices(notes, notes.size(), tick, kUpperVoice, up);
        bool down_ok = down != last_pitch &&
                       std::abs(static_cast<int>(down) - static_cast<int>(last_pitch)) <= 2 &&
                       isConsonantWithSoundingVoices(notes, notes.size(), tick, kUpperVoice, down);
        if (down_ok) {
          candidate = down;
        } else if (up_ok) {
          candidate = up;
        }
      }
      if (candidate == last_pitch)
        continue;
      if (std::abs(static_cast<int>(candidate) - static_cast<int>(last_pitch)) > 2) {
        continue;
      }
      if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(candidate)) > 9) {
        continue;
      }
      if (!isConsonantWithSoundingVoices(notes, notes.size(), tick, kUpperVoice, candidate)) {
        continue;
      }

      NoteEvent fill = prev;
      fill.start_tick = tick;
      fill.duration = duration::kEighthNote;
      fill.pitch = candidate;
      fill.source = BachNoteSource::EpisodeMaterial;
      notes.push_back(fill);
      last_pitch = candidate;
      ++inserted;
    }
  }

  return inserted;
}

int addInnerWeakBeatPassingLine(std::vector<NoteEvent>& notes, Tick episode_start,
                                Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  const Tick kernel_end = episode_start + episode_duration / 4;
  auto voice_range = getFugueVoiceRange(kInnerVoice, num_voices);
  uint8_t voice_lo = voice_range.first;
  uint8_t voice_hi = voice_range.second;

  int inserted = 0;
  for (Tick tick = kernel_end + duration::kEighthNote;
       tick + duration::kEighthNote <= episode_start + episode_duration;
       tick += duration::kEighthNote) {
    // Weak eighths only; inserted notes end before the next beat.
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;

    NoteEvent prev;
    NoteEvent next;
    if (!findPrevNextVoiceNotes(notes, kInnerVoice, tick, prev, next))
      continue;
    Tick gap = next.start_tick - (prev.start_tick + prev.duration);
    if (gap < kTicksPerBeat)
      continue;
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(prev.pitch)) > 9) {
      continue;
    }

    uint8_t candidate = stepTowardDiatonic(prev.pitch, next.pitch, key, voice_lo, voice_hi);
    if (candidate == prev.pitch)
      continue;
    if (std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch)) > 2) {
      continue;
    }
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(candidate)) > 7) {
      continue;
    }
    if (!isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, candidate)) {
      continue;
    }

    NoteEvent fill = prev;
    fill.start_tick = tick;
    fill.duration = duration::kEighthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;
  }
  return inserted;
}

int extendInnerWeakBeatLine(std::vector<NoteEvent>& notes, Tick episode_start,
                            Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  const Tick kernel_end = episode_start + episode_duration / 4;
  auto edge_voice_range = getFugueVoiceRange(kInnerVoice, num_voices);
  uint8_t voice_lo = edge_voice_range.first;
  uint8_t voice_hi = edge_voice_range.second;

  int inserted = 0;
  for (Tick tick = kernel_end + duration::kEighthNote;
       tick + duration::kEighthNote <= episode_start + episode_duration;
       tick += duration::kEighthNote) {
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;

    NoteEvent prev;
    if (!findPrevVoiceNote(notes, kInnerVoice, tick, prev))
      continue;
    if (tick - (prev.start_tick + prev.duration) > kTicksPerBar)
      continue;

    uint8_t up =
        stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch + 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t down =
        stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch - 3, 0, 127), key, voice_lo, voice_hi);
    bool up_ok = up != prev.pitch &&
                 std::abs(static_cast<int>(up) - static_cast<int>(prev.pitch)) <= 2 &&
                 isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, up);
    bool down_ok = down != prev.pitch &&
                   std::abs(static_cast<int>(down) - static_cast<int>(prev.pitch)) <= 2 &&
                   isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, down);
    if (!up_ok && !down_ok)
      continue;

    uint8_t candidate = down_ok ? down : up;
    if (up_ok && down_ok) {
      candidate = ((tick / duration::kEighthNote) % 2 == 0) ? up : down;
    }

    NoteEvent fill = prev;
    fill.start_tick = tick;
    fill.duration = duration::kEighthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;
  }

  return inserted;
}

int addInnerWeakSixteenthPassingLine(std::vector<NoteEvent>& notes, Tick episode_start,
                                     Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  const Tick kernel_end = episode_start + episode_duration / 4;
  auto [voice_lo, voice_hi] = getFugueVoiceRange(kInnerVoice, num_voices);

  int inserted = 0;
  constexpr int kMaxInserted = 16;
  for (Tick tick = kernel_end + duration::kSixteenthNote;
       tick + duration::kSixteenthNote <= episode_start + episode_duration;
       tick += duration::kSixteenthNote) {
    if (inserted >= kMaxInserted)
      break;
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;

    NoteEvent prev;
    NoteEvent next;
    if (!findPrevNextVoiceNotes(notes, kInnerVoice, tick, prev, next))
      continue;
    if (prev.source != BachNoteSource::EpisodeMaterial ||
        next.source != BachNoteSource::EpisodeMaterial) {
      continue;
    }
    if (tick - (prev.start_tick + prev.duration) > duration::kHalfNote)
      continue;
    if (next.start_tick - tick > duration::kHalfNote)
      continue;
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(prev.pitch)) > 9) {
      continue;
    }

    uint8_t candidate = stepTowardDiatonic(prev.pitch, next.pitch, key, voice_lo, voice_hi);
    if (candidate == prev.pitch) {
      uint8_t up = stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch + 3, 0, 127), key, voice_lo,
                                      voice_hi);
      uint8_t down = stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch - 3, 0, 127), key,
                                        voice_lo, voice_hi);
      candidate = ((tick / duration::kSixteenthNote) % 2 == 0) ? up : down;
    }
    if (candidate == prev.pitch)
      continue;
    if (std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch)) > 2) {
      continue;
    }
    if (std::abs(static_cast<int>(next.pitch) - static_cast<int>(candidate)) > 7) {
      continue;
    }
    if (!isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, candidate)) {
      continue;
    }

    NoteEvent fill = prev;
    fill.start_tick = tick;
    fill.duration = duration::kSixteenthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;
  }

  return inserted;
}

int splitSafeUpperEighths(std::vector<NoteEvent>& notes, Tick episode_start, Tick episode_duration,
                          uint8_t num_voices, Key key) {
  int inserted = 0;
  const Tick kernel_end = episode_start + episode_duration / 4;
  const size_t original_size = notes.size();
  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = notes[idx];
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.voice > 1 && !(num_voices == 4 && note.voice == 2))
      continue;
    if (note.start_tick < kernel_end)
      continue;
    if (note.duration != duration::kEighthNote)
      continue;

    Tick original_end = note.start_tick + note.duration;
    Tick second_tick = note.start_tick + duration::kSixteenthNote;
    if (second_tick >= original_end || second_tick >= episode_start + episode_duration) {
      continue;
    }

    uint8_t next_pitch = findNextVoicePitch(notes, idx, original_end);
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(note.pitch)) > 12) {
      continue;
    }

    auto voice_range = getFugueVoiceRange(note.voice, num_voices);
    uint8_t voice_lo = voice_range.first;
    uint8_t voice_hi = voice_range.second;
    auto weak_passing_ok = [&](uint8_t cand) {
      if (second_tick % kTicksPerBeat == 0)
        return false;
      int motion = std::abs(static_cast<int>(cand) - static_cast<int>(note.pitch));
      if (motion == 0 || motion > 2)
        return false;
      if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(cand)) > 7) {
        return false;
      }
      return true;
    };
    uint8_t candidate = stepTowardDiatonic(note.pitch, next_pitch, key, voice_lo, voice_hi);
    if (candidate == note.pitch) {
      uint8_t up = stepTowardDiatonic(note.pitch, clampPitch(note.pitch + 3, 0, 127), key, voice_lo,
                                      voice_hi);
      uint8_t down = stepTowardDiatonic(note.pitch, clampPitch(note.pitch - 3, 0, 127), key,
                                        voice_lo, voice_hi);
      bool up_ok = up != note.pitch &&
                   (isConsonantWithSoundingVoices(notes, idx, second_tick, note.voice, up) ||
                    weak_passing_ok(up));
      bool down_ok = down != note.pitch &&
                     (isConsonantWithSoundingVoices(notes, idx, second_tick, note.voice, down) ||
                      weak_passing_ok(down));
      if (up_ok && !down_ok) {
        candidate = up;
      } else if (down_ok) {
        candidate = down;
      }
    }
    if (candidate == note.pitch)
      continue;
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(candidate)) > 12) {
      continue;
    }
    if (!isConsonantWithSoundingVoices(notes, idx, second_tick, note.voice, candidate) &&
        !weak_passing_ok(candidate)) {
      continue;
    }

    NoteEvent continuation = note;
    continuation.start_tick = second_tick;
    continuation.duration = duration::kSixteenthNote;
    continuation.pitch = candidate;
    note.duration = duration::kSixteenthNote;
    notes.push_back(continuation);
    ++inserted;
  }
  return inserted;
}

int splitSafeEpisodeQuartersToSixteenths(std::vector<NoteEvent>& notes, Tick episode_start,
                                         Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices < 3)
    return 0;

  int inserted = 0;
  const Tick kernel_end = episode_start + episode_duration / 4;
  const size_t original_size = notes.size();
  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = notes[idx];
    if (note.source != BachNoteSource::EpisodeMaterial)
      continue;
    if (note.voice > 2)
      continue;
    if (note.start_tick < kernel_end)
      continue;
    if (note.duration != duration::kQuarterNote)
      continue;

    Tick t1 = note.start_tick + duration::kSixteenthNote;
    Tick t2 = note.start_tick + duration::kEighthNote;
    Tick t3 = t2 + duration::kSixteenthNote;
    if (t3 + duration::kSixteenthNote > episode_start + episode_duration) {
      continue;
    }

    auto quarter_voice_range = getFugueVoiceRange(note.voice, num_voices);
    uint8_t voice_lo = quarter_voice_range.first;
    uint8_t voice_hi = quarter_voice_range.second;
    uint8_t next_pitch = findNextVoicePitch(notes, idx, note.start_tick + note.duration);

    uint8_t up =
        stepTowardDiatonic(note.pitch, clampPitch(note.pitch + 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t down =
        stepTowardDiatonic(note.pitch, clampPitch(note.pitch - 3, 0, 127), key, voice_lo, voice_hi);

    auto candidate_ok = [&](uint8_t cand, Tick tick) {
      if (cand == note.pitch)
        return false;
      if (std::abs(static_cast<int>(cand) - static_cast<int>(note.pitch)) > 2) {
        return false;
      }
      if (isConsonantWithSoundingVoices(notes, idx, tick, note.voice, cand)) {
        return true;
      }
      // Weak sixteenth passing/neighbor tones are idiomatic in running Bach
      // episode figuration; keep beat-level consonance strict.
      return tick % kTicksPerBeat != 0;
    };

    bool up_ok = candidate_ok(up, t1) && candidate_ok(up, t3);
    bool down_ok = candidate_ok(down, t1) && candidate_ok(down, t3);
    if (!up_ok && !down_ok)
      continue;

    uint8_t neighbor = note.pitch;
    if (up_ok && down_ok) {
      int up_next = std::abs(static_cast<int>(next_pitch) - static_cast<int>(up));
      int down_next = std::abs(static_cast<int>(next_pitch) - static_cast<int>(down));
      neighbor = (down_next <= up_next) ? down : up;
    } else {
      neighbor = up_ok ? up : down;
    }
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(neighbor)) > 12) {
      continue;
    }

    NoteEvent second = note;
    NoteEvent third = note;
    NoteEvent fourth = note;
    note.duration = duration::kSixteenthNote;

    second.start_tick = t1;
    second.duration = duration::kSixteenthNote;
    second.pitch = neighbor;

    third.start_tick = t2;
    third.duration = duration::kSixteenthNote;

    fourth.start_tick = t3;
    fourth.duration = duration::kSixteenthNote;
    fourth.pitch = neighbor;

    notes.push_back(second);
    notes.push_back(third);
    notes.push_back(fourth);
    inserted += 3;
  }
  return inserted;
}

int splitLongInnerEpisodeNotes(std::vector<NoteEvent>& notes, Tick episode_start,
                               Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  int inserted = 0;
  constexpr int kMaxInserted = 18;
  const Tick kernel_end = episode_start + episode_duration / 4;
  const size_t original_size = notes.size();
  auto [voice_lo, voice_hi] = getFugueVoiceRange(kInnerVoice, num_voices);

  for (size_t idx = 0; idx < original_size; ++idx) {
    NoteEvent& note = notes[idx];
    if (inserted >= kMaxInserted)
      break;
    if (note.source != BachNoteSource::EpisodeMaterial || note.voice != kInnerVoice) {
      continue;
    }
    if (note.start_tick < kernel_end)
      continue;
    if (note.duration < duration::kHalfNote)
      continue;

    Tick original_end = note.start_tick + note.duration;
    if (original_end > episode_start + episode_duration) {
      original_end = episode_start + episode_duration;
    }
    Tick first_follow = note.start_tick + duration::kEighthNote;
    if (first_follow + duration::kEighthNote > original_end)
      continue;

    uint8_t next_pitch = findNextVoicePitch(notes, idx, original_end);
    if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(note.pitch)) > 12) {
      continue;
    }

    uint8_t up =
        stepTowardDiatonic(note.pitch, clampPitch(note.pitch + 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t down =
        stepTowardDiatonic(note.pitch, clampPitch(note.pitch - 3, 0, 127), key, voice_lo, voice_hi);
    auto neighbor_ok = [&](uint8_t cand, Tick tick) {
      if (cand == note.pitch)
        return false;
      if (std::abs(static_cast<int>(cand) - static_cast<int>(note.pitch)) > 2) {
        return false;
      }
      if (std::abs(static_cast<int>(next_pitch) - static_cast<int>(cand)) > 12) {
        return false;
      }
      return isConsonantWithSoundingVoices(notes, idx, tick, kInnerVoice, cand) ||
             tick % kTicksPerBeat != 0;
    };

    bool up_ok = neighbor_ok(up, first_follow);
    bool down_ok = neighbor_ok(down, first_follow);
    if (!up_ok && !down_ok)
      continue;
    uint8_t neighbor = down_ok ? down : up;
    if (up_ok && down_ok) {
      int up_next = std::abs(static_cast<int>(next_pitch) - static_cast<int>(up));
      int down_next = std::abs(static_cast<int>(next_pitch) - static_cast<int>(down));
      neighbor = (down_next <= up_next) ? down : up;
    }

    note.duration = duration::kEighthNote;
    int slot = 1;
    for (Tick tick = first_follow;
         tick + duration::kEighthNote <= original_end && inserted < kMaxInserted;
         tick += duration::kEighthNote, ++slot) {
      NoteEvent fill = note;
      fill.start_tick = tick;
      fill.duration = duration::kEighthNote;
      fill.pitch = (slot % 2 == 1) ? neighbor : note.pitch;
      if (fill.pitch != note.pitch && !neighbor_ok(fill.pitch, tick)) {
        continue;
      }
      notes.push_back(fill);
      ++inserted;
    }
  }

  return inserted;
}

int seedInnerWeakBeatContinuity(std::vector<NoteEvent>& notes, Tick episode_start,
                                Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  constexpr int kMaxInserted = 32;
  const Tick kernel_end = episode_start + episode_duration / 4;
  auto [voice_lo, voice_hi] = getFugueVoiceRange(kInnerVoice, num_voices);

  int inserted = 0;
  for (Tick tick = kernel_end + duration::kEighthNote;
       tick + duration::kEighthNote <= episode_start + episode_duration;
       tick += duration::kEighthNote) {
    if (inserted >= kMaxInserted)
      break;
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;

    NoteEvent prev;
    if (!findPrevVoiceNote(notes, kInnerVoice, tick, prev))
      continue;
    if (tick - (prev.start_tick + prev.duration) > kTicksPerBeat * 4)
      continue;

    uint8_t up =
        stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch + 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t down =
        stepTowardDiatonic(prev.pitch, clampPitch(prev.pitch - 3, 0, 127), key, voice_lo, voice_hi);
    auto candidate_ok = [&](uint8_t cand) {
      if (cand == prev.pitch)
        return false;
      if (std::abs(static_cast<int>(cand) - static_cast<int>(prev.pitch)) > 2) {
        return false;
      }
      return isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, cand);
    };

    bool up_ok = candidate_ok(up);
    bool down_ok = candidate_ok(down);
    if (!up_ok && !down_ok)
      continue;
    uint8_t candidate = down_ok ? down : up;
    if (up_ok && down_ok) {
      candidate = ((tick / duration::kEighthNote) % 2 == 0) ? up : down;
    }

    NoteEvent fill = prev;
    fill.start_tick = tick;
    fill.duration = duration::kEighthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;
  }

  return inserted;
}

int seedInnerEpisodeEdgeContinuity(std::vector<NoteEvent>& notes, Tick episode_start,
                                   Tick episode_duration, uint8_t num_voices, Key key) {
  if (num_voices != 4)
    return 0;

  constexpr VoiceId kInnerVoice = 2;
  constexpr int kMaxInserted = 16;
  const Tick episode_end = episode_start + episode_duration;
  const Tick kernel_end = episode_start + episode_duration / 4;
  auto edge_voice_range = getFugueVoiceRange(kInnerVoice, num_voices);
  uint8_t voice_lo = edge_voice_range.first;
  uint8_t voice_hi = edge_voice_range.second;

  std::vector<NoteEvent> inner;
  for (const auto& note : notes) {
    if (note.source == BachNoteSource::EpisodeMaterial && note.voice == kInnerVoice &&
        note.start_tick >= episode_start && note.start_tick < episode_end) {
      inner.push_back(note);
    }
  }
  if (inner.empty())
    return 0;
  std::sort(inner.begin(), inner.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  auto neighborCandidate = [&](uint8_t anchor, Tick tick) -> uint8_t {
    uint8_t up =
        stepTowardDiatonic(anchor, clampPitch(anchor + 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t down =
        stepTowardDiatonic(anchor, clampPitch(anchor - 3, 0, 127), key, voice_lo, voice_hi);
    uint8_t first = ((tick / duration::kEighthNote) % 2 == 0) ? up : down;
    uint8_t second = (first == up) ? down : up;
    for (uint8_t cand : {first, second}) {
      if (cand == anchor)
        continue;
      if (std::abs(static_cast<int>(cand) - static_cast<int>(anchor)) > 2) {
        continue;
      }
      if (isConsonantWithSoundingVoices(notes, notes.size(), tick, kInnerVoice, cand)) {
        return cand;
      }
    }
    return anchor;
  };

  int inserted = 0;
  const NoteEvent& first_note = inner.front();
  Tick lead_start =
      std::max(kernel_end + duration::kEighthNote, first_note.start_tick > kTicksPerBar * 2
                                                       ? first_note.start_tick - kTicksPerBar * 2
                                                       : episode_start);
  for (Tick tick = lead_start;
       tick + duration::kEighthNote <= first_note.start_tick && inserted < kMaxInserted;
       tick += duration::kEighthNote) {
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;
    uint8_t candidate = neighborCandidate(first_note.pitch, tick);
    if (candidate == first_note.pitch)
      continue;

    NoteEvent fill = first_note;
    fill.start_tick = tick;
    fill.duration = duration::kEighthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    ++inserted;
  }

  const NoteEvent& last_note = inner.back();
  Tick tail_start = last_note.start_tick + last_note.duration;
  Tick tail_end = std::min(episode_end, tail_start + kTicksPerBar * 2);
  uint8_t anchor = last_note.pitch;
  for (Tick tick = tail_start; tick + duration::kEighthNote <= tail_end && inserted < kMaxInserted;
       tick += duration::kEighthNote) {
    if (tick % kTicksPerBeat == 0)
      continue;
    if (hasVoiceSoundingAt(notes, kInnerVoice, tick))
      continue;
    uint8_t candidate = neighborCandidate(anchor, tick);
    if (candidate == anchor)
      continue;

    NoteEvent fill = last_note;
    fill.start_tick = tick;
    fill.duration = duration::kEighthNote;
    fill.pitch = candidate;
    fill.source = BachNoteSource::EpisodeMaterial;
    notes.push_back(fill);
    anchor = candidate;
    ++inserted;
  }

  return inserted;
}

/// @brief Apply invertible counterpoint (voice 0 <-> voice 1 swap).
///
/// For odd episode indices with num_voices >= 2, swap voice IDs 0 and 1
/// for all notes. This implements double counterpoint at the octave.
///
/// @param notes Notes to modify in place.
/// @param episode_index Episode ordinal.
/// @param num_voices Number of active voices.
void applyInvertibleCounterpoint(std::vector<NoteEvent>& notes, int episode_index,
                                 uint8_t num_voices) {
  if (episode_index % 2 == 0 || num_voices < 2)
    return;

  for (auto& note : notes) {
    if (note.voice == 0) {
      note.voice = 1;
    } else if (note.voice == 1) {
      note.voice = 0;
    }
  }
}

BachNoteSource episodeSourceForIntent(const EpisodeRequest& request, VoiceId voice,
                                      FortPhase phase) {
  if (request.thematic_plan == nullptr)
    return BachNoteSource::EpisodeMaterial;
  if (voice == 0 || (voice == 1 && phase != FortPhase::Dissolution)) {
    return BachNoteSource::SequenceNote;
  }
  return BachNoteSource::EpisodeMaterial;
}

bool isEpisodeGeneratedSource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote;
}

const PatternCandidate* thematicEpisodeCandidateForStep(const EpisodeRequest& request,
                                                        VoiceId voice, FortPhase phase) {
  if (request.thematic_plan == nullptr)
    return nullptr;

  if (voice == 0) {
    const PatternCandidate* sequential =
        request.thematic_plan->episode_drawer.bestForIntent(VoiceIntent::SequentialCounterline);
    if (sequential != nullptr && !sequential->notes.empty()) {
      return sequential;
    }
    return request.thematic_plan->subject_drawer.bestForIntent(VoiceIntent::SequentialCounterline);
  }

  if (voice == 1 && phase != FortPhase::Dissolution) {
    const PatternCandidate* reply =
        request.thematic_plan->episode_drawer.bestForIntent(VoiceIntent::RepeatedReplyCell);
    if (reply != nullptr && !reply->notes.empty())
      return reply;
  }

  if (request.num_voices == 4 && voice == 2 && phase != FortPhase::Kernel) {
    const PatternCandidate* inner =
        request.thematic_plan->episode_drawer.bestForIntent(VoiceIntent::SequentialCounterline);
    if (inner != nullptr && !inner->notes.empty())
      return inner;
  }

  return nullptr;
}

MotifOp thematicOpForStep(VoiceId voice, FortPhase phase, MotifOp fallback) {
  if (voice == 1 && phase == FortPhase::Kernel)
    return MotifOp::Original;
  switch (phase) {
    case FortPhase::Kernel:
      return MotifOp::Original;
    case FortPhase::Sequence:
      return MotifOp::Sequence;
    case FortPhase::Dissolution:
      return MotifOp::Fragment;
  }
  return fallback;
}

int truncateSustainedBoundaryDissonances(std::vector<NoteEvent>& notes,
                                         const HarmonicTimeline* timeline, Tick episode_start,
                                         Tick episode_duration) {
  if (timeline == nullptr || timeline->events().empty())
    return 0;

  const Tick episode_end = episode_start + episode_duration;
  int changed = 0;
  for (auto& note : notes) {
    if (!isEpisodeGeneratedSource(note.source))
      continue;
    if (note.start_tick < episode_start || note.start_tick >= episode_end) {
      continue;
    }
    Tick note_end = note.start_tick + note.duration;
    for (const auto& event : timeline->events()) {
      Tick boundary = event.tick;
      if (boundary <= note.start_tick || boundary >= note_end)
        continue;
      if (boundary < episode_start || boundary >= episode_end)
        continue;
      Tick shortened = boundary - note.start_tick;
      if (shortened < duration::kSixteenthNote)
        continue;
      if (isChordTone(note.pitch, event))
        continue;

      note.duration = shortened;
      ++changed;
      break;
    }
  }
  return changed;
}

bool isFlexibleEpisodeContourSource(BachNoteSource source) {
  return source == BachNoteSource::EpisodeMaterial || source == BachNoteSource::SequenceNote ||
         source == BachNoteSource::FreeCounterpoint;
}

int smoothLocalEpisodeMaterialLeaps(std::vector<NoteEvent>& notes, const HarmonicTimeline* timeline,
                                    Tick episode_start, Tick episode_duration, uint8_t num_voices,
                                    Key key, Key home_key, bool home_is_minor) {
  const Tick episode_end = episode_start + episode_duration;
  int changed = 0;

  for (VoiceId voice = 0; voice < num_voices; ++voice) {
    if (voice < 2)
      continue;

    std::vector<size_t> ordered;
    for (size_t idx = 0; idx < notes.size(); ++idx) {
      const auto& note = notes[idx];
      if (note.voice != voice)
        continue;
      if (note.start_tick < episode_start || note.start_tick >= episode_end)
        continue;
      if (!isFlexibleEpisodeContourSource(note.source))
        continue;
      ordered.push_back(idx);
    }
    if (ordered.size() < 2)
      continue;

    std::sort(ordered.begin(), ordered.end(), [&](size_t lhs, size_t rhs) {
      if (notes[lhs].start_tick != notes[rhs].start_tick) {
        return notes[lhs].start_tick < notes[rhs].start_tick;
      }
      return lhs < rhs;
    });

    auto [voice_lo, voice_hi] = getFugueVoiceRange(voice, num_voices);
    if (num_voices == 4 && voice == 2) {
      voice_hi = std::min<uint8_t>(voice_hi, 69);
    }

    for (size_t pos = 1; pos < ordered.size(); ++pos) {
      NoteEvent& note = notes[ordered[pos]];
      const NoteEvent& prev = notes[ordered[pos - 1]];
      if (note.source != BachNoteSource::EpisodeMaterial)
        continue;
      Tick gap = note.start_tick > prev.start_tick + prev.duration
                     ? note.start_tick - (prev.start_tick + prev.duration)
                     : 0;
      if (gap > duration::kHalfNote)
        continue;

      int original_leap = std::abs(static_cast<int>(note.pitch) - static_cast<int>(prev.pitch));
      if (original_leap <= interval::kPerfect5th)
        continue;

      uint8_t next_pitch = note.pitch;
      bool has_next = false;
      if (pos + 1 < ordered.size()) {
        const NoteEvent& next = notes[ordered[pos + 1]];
        Tick next_gap = next.start_tick - (note.start_tick + note.duration);
        if (next_gap <= duration::kHalfNote) {
          next_pitch = next.pitch;
          has_next = true;
        }
      }

      EpisodeRequest scale_request;
      scale_request.home_key = home_key;
      scale_request.home_is_minor = home_is_minor;
      ScaleType local_scale = localScaleForKey(scale_request, key);
      if (timeline != nullptr && timeline->size() > 0) {
        local_scale = localScaleForEvent(timeline->getAt(note.start_tick));
      }

      uint8_t step =
          stepTowardDiatonic(prev.pitch, note.pitch, key, voice_lo, voice_hi, local_scale);
      std::vector<uint8_t> candidates;
      candidates.push_back(step);
      candidates.push_back(
          static_cast<uint8_t>(scale_util::nearestScaleTone(prev.pitch, key, local_scale)));
      for (int shift = -12; shift <= 12; shift += 12) {
        int folded = static_cast<int>(note.pitch) + shift;
        if (folded >= static_cast<int>(voice_lo) && folded <= static_cast<int>(voice_hi)) {
          candidates.push_back(static_cast<uint8_t>(folded));
        }
      }

      float best_score = -std::numeric_limits<float>::infinity();
      uint8_t best_pitch = note.pitch;
      for (uint8_t candidate : candidates) {
        candidate = clampPitch(candidate, voice_lo, voice_hi);
        int prev_leap = std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch));
        if (prev_leap == 0 || prev_leap > interval::kPerfect5th)
          continue;
        int next_leap =
            has_next ? std::abs(static_cast<int>(next_pitch) - static_cast<int>(candidate)) : 0;
        if (has_next && next_leap > interval::kPerfect5th)
          continue;
        bool weak_neighbor =
            note.start_tick % kTicksPerBeat != 0 && has_next &&
            std::abs(static_cast<int>(candidate) - static_cast<int>(prev.pitch)) <= 2 &&
            std::abs(static_cast<int>(next_pitch) - static_cast<int>(candidate)) <= 2;
        if (!isConsonantWithSoundingVoices(notes, ordered[pos], note.start_tick, voice,
                                           candidate) &&
            !weak_neighbor) {
          continue;
        }
        if (timeline != nullptr && timeline->size() > 0 && note.start_tick % kTicksPerBeat == 0 &&
            !isChordTone(candidate, timeline->getAt(note.start_tick))) {
          continue;
        }

        float score = -static_cast<float>(prev_leap);
        if (has_next)
          score -= static_cast<float>(next_leap) * 0.5f;
        if (scale_util::isScaleTone(candidate, key, local_scale))
          score += 0.2f;
        if (timeline != nullptr && timeline->size() > 0 &&
            isChordTone(candidate, timeline->getAt(note.start_tick))) {
          score += 0.4f;
        }
        if (score > best_score) {
          best_score = score;
          best_pitch = candidate;
        }
      }

      if (best_pitch != note.pitch) {
        note.pitch = best_pitch;
        note.modified_by |= static_cast<uint8_t>(NoteModifiedBy::LeapResolution);
        ++changed;
      }
    }
  }

  return changed;
}

Tick durationBeforeBoundaryDissonance(Tick note_tick, Tick note_dur, uint8_t pitch,
                                      const HarmonicTimeline* timeline, Tick episode_start,
                                      Tick episode_duration) {
  if (timeline == nullptr || timeline->events().empty())
    return note_dur;
  if (note_dur <= duration::kSixteenthNote)
    return note_dur;

  const Tick episode_end = episode_start + episode_duration;
  Tick note_end = note_tick + note_dur;
  for (const auto& event : timeline->events()) {
    Tick boundary = event.tick;
    if (boundary <= note_tick || boundary >= note_end)
      continue;
    if (boundary < episode_start || boundary >= episode_end)
      continue;
    if (isChordTone(pitch, event))
      continue;

    Tick shortened = boundary - note_tick;
    if (shortened >= duration::kSixteenthNote) {
      return shortened;
    }
  }
  return note_dur;
}

}  // namespace

// ---------------------------------------------------------------------------
// generateConstraintEpisode
// ---------------------------------------------------------------------------

EpisodeResult generateConstraintEpisode(const EpisodeRequest& request) {
  EpisodeResult result;
  result.exit_state = request.entry_state;
  result.achieved_key = request.start_key;

  // Validate inputs.
  if (request.motif_pool == nullptr || request.motif_pool->empty() || request.duration == 0) {
    result.success = false;
    return result;
  }

  const MotifPool& pool = *request.motif_pool;
  CharacterEpisodeParams params = getCharacterParams(request.character);
  std::mt19937 rng(request.seed);

  // 1. Select base motif from pool using character's voice0_initial op.
  const PooledMotif* base_motif = pool.getForOp(params.voice0_initial);
  if (base_motif == nullptr || base_motif->notes.empty()) {
    result.success = false;
    return result;
  }

  // 2. Apply voice0/voice1 transformations.
  ScaleType start_scale = localScaleForKey(request, request.start_key);
  std::vector<NoteEvent> v0_transformed =
      applyMotifOp(base_motif->notes, params.voice0_initial, request.start_key, start_scale);
  std::vector<NoteEvent> v1_transformed =
      applyMotifOp(base_motif->notes, params.voice1_initial, request.start_key, start_scale);

  // Apply secondary transformation for voice 1 (Noble: Retrograde after Augment).
  if (params.voice1_secondary != MotifOp::Original) {
    v1_transformed =
        applyMotifOp(v1_transformed, params.voice1_secondary, request.start_key, start_scale);
  }

  // 3. Plan Fortspinnung arc.
  std::vector<FortspinnungStep> steps =
      planFortspinnung(pool, request.grammar, request.start_tick, request.duration,
                       request.num_voices, request.character, request.seed);

  if (steps.empty()) {
    result.success = false;
    return result;
  }

  // 4. Main generation loop: place notes with constraint evaluation.
  ConstraintState& state = result.exit_state;

  // Import pipeline accumulator if provided.
  if (request.pipeline_accumulator) {
    state.accumulator = *request.pipeline_accumulator;
  }

  state.accumulator.current_phase = FuguePhase::Develop;
  Tick min_dur = minDurationForEnergy(request.energy_level);
  int total_shift = keyDistance(request.start_key, request.end_key);

  // Per-voice state tracking.
  constexpr int kMaxVoices = 6;
  uint8_t recent_per_voice[kMaxVoices][kMaxRecentPitches] = {};
  int recent_count_per_voice[kMaxVoices] = {};
  uint8_t prev_pitch_per_voice[kMaxVoices] = {};
  int prev_direction_per_voice[kMaxVoices] = {};
  Tick prev_dur_per_voice[kMaxVoices] = {};

  // Per-voice-per-bar sixteenth note cap: limit to 75% of bar (12 out of 16).
  constexpr int kMaxSixteenthsPerBar = 12;
  int sixteenth_count[kMaxVoices] = {};
  int current_bar_idx[kMaxVoices] = {};  // Track which bar each voice is in.
  for (int vdx = 0; vdx < kMaxVoices; ++vdx) {
    current_bar_idx[vdx] = -1;  // Sentinel: no bar seen yet.
  }
  // Initialize previous pitches: prefer last_pitches from previous section
  // (voice-leading continuity), fall back to transformed motif's first pitch.
  for (int vdx = 0; vdx < kMaxVoices; ++vdx) {
    if (vdx < request.num_voices && vdx < EpisodeRequest::kMaxRequestVoices &&
        request.last_pitches[vdx] > 0) {
      prev_pitch_per_voice[vdx] = request.last_pitches[vdx];
    } else if (vdx == 0 && !v0_transformed.empty()) {
      prev_pitch_per_voice[vdx] = v0_transformed[0].pitch;
    } else if (vdx == 1 && !v1_transformed.empty()) {
      prev_pitch_per_voice[vdx] = v1_transformed[0].pitch;
    } else {
      // Re-entry or unknown: use voice center as neutral starting point.
      auto [vlo, vhi] = getFugueVoiceRange(static_cast<uint8_t>(vdx), request.num_voices);
      prev_pitch_per_voice[vdx] =
          static_cast<uint8_t>((static_cast<int>(vlo) + static_cast<int>(vhi)) / 2);
    }
    prev_dur_per_voice[vdx] = kTicksPerBeat;
  }

  for (const auto& step : steps) {
    VoiceId voice = step.voice;
    if (voice >= kMaxVoices)
      continue;

    // Get the transformed motif notes for this step.
    const PooledMotif* step_motif = pool.getByRank(step.pool_rank);
    if (step_motif == nullptr || step_motif->notes.empty())
      continue;

    const PatternCandidate* thematic_candidate =
        thematicEpisodeCandidateForStep(request, voice, step.phase);
    const std::vector<NoteEvent>& source_notes =
        (thematic_candidate != nullptr && !thematic_candidate->notes.empty())
            ? thematic_candidate->notes
            : step_motif->notes;
    MotifOp step_op =
        (thematic_candidate != nullptr) ? thematicOpForStep(voice, step.phase, step.op) : step.op;

    std::vector<NoteEvent> motif_notes =
        applyMotifOp(source_notes, step_op, request.start_key, start_scale, params.sequence_step);

    // Noble character: voice 1 uses augmented motif transposed down an octave
    // for bass-like behavior (Bach's actual practice for Noble episodes).
    if (voice == 1 && request.character == SubjectCharacter::Noble) {
      motif_notes = transposeMelody(motif_notes, -12);
    }

    // Get voice range for pitch clamping.
    auto [voice_lo, voice_hi] = getFugueVoiceRange(voice, request.num_voices);
    if (request.num_voices == 4 && voice <= 1) {
      voice_hi = std::min<uint8_t>(voice_hi, 84);
    }
    if (request.num_voices == 4 && voice == 2) {
      voice_hi = std::min<uint8_t>(voice_hi, 69);
    }
    if (voice >= 2) {
      for (auto& note : motif_notes) {
        int pitch = static_cast<int>(note.pitch);
        while (pitch > static_cast<int>(voice_hi) && pitch - 12 >= static_cast<int>(voice_lo)) {
          pitch -= 12;
        }
        while (pitch < static_cast<int>(voice_lo) && pitch + 12 <= static_cast<int>(voice_hi)) {
          pitch += 12;
        }
        note.pitch = clampPitch(pitch, voice_lo, voice_hi);
      }
    }

    // Place each note in the motif with candidate evaluation.
    Tick note_tick = step.tick;
    for (size_t motif_idx = 0; motif_idx < motif_notes.size(); ++motif_idx) {
      const auto& motif_note = motif_notes[motif_idx];
      if (note_tick >= request.start_tick + request.duration)
        break;

      // Rhythm density: phase-controlled diminution with motif preservation.
      // B1: Kernel=0% (theme rhythm fully preserved), Sequence=25% (limited),
      // Dissolution=40-55% (main rhythmic density site).
      Tick base_dur = motif_note.duration;
      {
        constexpr Tick kDimFloor = duration::kSixteenthNote;  // 120 ticks
        FortPhase phase = step.phase;
        const bool protected_thematic = thematic_candidate != nullptr &&
                                        static_cast<uint8_t>(thematic_candidate->protection) <=
                                            static_cast<uint8_t>(ThematicProtectionLevel::Dialogue);

        // B1: Phase-dependent diminution probability.
        // Reference: organ fugue episodes (BWV578, BWV532) use running 16th motion.
        // Kernel preserves theme rhythm; Sequence/Dissolution drive toward density.
        float diminish_prob;
        switch (phase) {
          case FortPhase::Kernel:
            diminish_prob = 0.0f;
            break;
          case FortPhase::Sequence:
            diminish_prob = 0.56f;
            break;
          case FortPhase::Dissolution:
            diminish_prob = 0.65f + request.energy_level * 0.15f;
            break;
        }

        // B2: Strong beat guard. Keep Kernel stable on beats 1/3 and preserve
        // the Sequence bar head, but allow beat-3 diminution for running lines.
        bool sequence_bar_head = phase == FortPhase::Sequence && note_tick % kTicksPerBar == 0;
        bool kernel_strong = phase == FortPhase::Kernel && isStrongBeatInBar(note_tick);
        if (kernel_strong || sequence_bar_head) {
          diminish_prob = 0.0f;
        }
        if (protected_thematic && phase != FortPhase::Dissolution) {
          diminish_prob = 0.0f;
        } else if (protected_thematic) {
          diminish_prob *= 0.35f;
        }

        // B3: Resolution note protection — if previous note was dissonant
        // with other voices and current motif pitch is consonant, skip
        // diminution to preserve suspension-resolution voice leading.
        if (diminish_prob > 0.0f) {
          uint8_t prev_p = prev_pitch_per_voice[voice];
          if (prev_p > 0) {
            bool prev_dissonant = false;
            for (int vi = 0; vi < request.num_voices && vi < kMaxVoices; ++vi) {
              if (vi == voice)
                continue;
              uint8_t other_p = prev_pitch_per_voice[vi];
              if (other_p == 0)
                continue;
              int ivl = std::abs(static_cast<int>(prev_p) - static_cast<int>(other_p)) % 12;
              // Dissonant: 2nds (1,2), 7ths (10,11), tritone (6).
              if (ivl == 1 || ivl == 2 || ivl == 6 || ivl == 10 || ivl == 11) {
                prev_dissonant = true;
                break;
              }
            }
            if (prev_dissonant) {
              uint8_t mp = motif_note.pitch;
              for (int vi = 0; vi < request.num_voices && vi < kMaxVoices; ++vi) {
                if (vi == voice)
                  continue;
                uint8_t other_p = prev_pitch_per_voice[vi];
                if (other_p == 0)
                  continue;
                int ivl = std::abs(static_cast<int>(mp) - static_cast<int>(other_p)) % 12;
                // Consonant: unison(0), 3rds(3,4), 5th(7), 6ths(8,9).
                if (ivl == 0 || ivl == 3 || ivl == 4 || ivl == 7 || ivl == 8 || ivl == 9) {
                  diminish_prob = 0.0f;
                  break;
                }
              }
            }
          }
        }

        // B4: Intra-voice rhythm consistency — soften 8th->16th transitions.
        // Bach's episodes do use 8th->16th transitions in Fortspinnung, but
        // excessive switching creates a restless texture. Light suppression.
        if (diminish_prob > 0.0f) {
          DurCategory prev_dc = ticksToDurCategory(prev_dur_per_voice[voice]);
          DurCategory post_diminish_dc = ticksToDurCategory(std::max(base_dur / 2, kDimFloor));
          if (prev_dc == DurCategory::S8 && post_diminish_dc == DurCategory::S16) {
            diminish_prob *= 0.65f;  // Allow Bach-like 8th->16th running figures.
          }
        }

        if (base_dur > kDimFloor) {
          if (rng::rollProbability(rng, diminish_prob)) {
            base_dur = std::max(base_dur / 2, kDimFloor);
            // Second halving: Dissolution at half prob, Sequence at lower prob
            // to create quarter->8th->16th chains for running episode motion.
            float second_prob = (phase == FortPhase::Dissolution) ? diminish_prob * 0.5f : 0.25f;
            if (base_dur > kDimFloor && rng::rollProbability(rng, second_prob)) {
              base_dur = std::max(base_dur / 2, kDimFloor);
            }
          }
        }
      }

      // Sixteenth note cap: if this voice already has 75% of the bar as 16ths,
      // force remaining notes to 8th note minimum to prevent mechanical texture.
      {
        int bar_idx = static_cast<int>(note_tick / kTicksPerBar);
        if (bar_idx != current_bar_idx[voice]) {
          current_bar_idx[voice] = bar_idx;
          sixteenth_count[voice] = 0;
        }
        if (base_dur <= duration::kSixteenthNote) {
          if (sixteenth_count[voice] >= kMaxSixteenthsPerBar) {
            base_dur = duration::kEighthNote;
          } else {
            sixteenth_count[voice]++;
          }
        }
      }

      // B5: Intra-voice rhythm consistency — suppress mixed patterns.
      // BWV578 reference: upper voices have 74-83% 16th notes, uniform figuration.
      // When previous note in this voice was short (16th/8th), prefer same category.
      {
        DurCategory prev_dc = ticksToDurCategory(prev_dur_per_voice[voice]);
        DurCategory cand_dc = ticksToDurCategory(base_dur);
        bool prev_short = (prev_dc == DurCategory::S16 || prev_dc == DurCategory::S8);
        bool cand_short = (cand_dc == DurCategory::S16 || cand_dc == DurCategory::S8);
        if (prev_short && cand_short && prev_dc != cand_dc) {
          // Snap to previous duration category for consistent figuration.
          if (prev_dc == DurCategory::S16 && base_dur > duration::kSixteenthNote) {
            base_dur = duration::kSixteenthNote;
          } else if (prev_dc == DurCategory::S8 && base_dur < duration::kEighthNote &&
                     step.phase == FortPhase::Kernel) {
            base_dur = duration::kEighthNote;
          }
        }
      }

      // Apply phase-aware minimum duration. Sequence/Dissolution must be able
      // to produce running 16ths; otherwise the energy floor turns most
      // diminished episode figures back into 8ths/quarters.
      Tick phase_min_dur = minDurationForFortPhase(step.phase, min_dur);
      Tick note_dur = std::max(base_dur, phase_min_dur);
      Tick remaining = request.start_tick + request.duration - note_tick;
      if (note_dur > remaining)
        note_dur = remaining;
      if (note_dur == 0)
        continue;

      // Compute progress for modulation.
      float progress = static_cast<float>(note_tick - request.start_tick) /
                       static_cast<float>(std::max(request.duration, static_cast<Tick>(1)));

      // Determine current key from the shared harmonic plan when available.
      // Falling back to progress-based modulation is kept for unit tests and
      // callers that do not yet provide a timeline.
      Key current_key = (progress > 0.6f) ? request.end_key : request.start_key;
      ScaleType current_scale = localScaleForKey(request, current_key);
      if (request.timeline != nullptr && request.timeline->size() > 0) {
        const HarmonicEvent& event = request.timeline->getAt(note_tick);
        current_key = event.key;
        current_scale = localScaleForEvent(event);
      }
      const HarmonicEvent* current_event = nullptr;
      if (request.timeline != nullptr && request.timeline->size() > 0) {
        current_event = &request.timeline->getAt(note_tick);
      }

      // Build context for evaluation.
      VerticalSnapshot snap = buildSnapshot(result.notes, note_tick, request.num_voices);
      MarkovContext ctx = buildMarkovContext(prev_pitch_per_voice[voice], prev_dur_per_voice[voice],
                                             note_tick, current_key, current_scale);

      // Evaluate candidates: phase-dependent search width.
      uint8_t base_pitch = applyModulationShift(motif_note.pitch, progress, total_shift);

      float best_score = -std::numeric_limits<float>::infinity();
      uint8_t best_pitch = base_pitch;
      float best_figure_score = 0.0f;

      // A1: Kernel phase uses 3 candidates (original ± 1 semitone) to
      // preserve motif pitch identity. Other phases use full 5 candidates.
      constexpr int kKernelOffsets[] = {0, -1, 1};
      constexpr int kKernelNumOffsets = 3;
      const int* offsets = (step.phase == FortPhase::Kernel) ? kKernelOffsets : kCandidateOffsets;
      int num_offsets =
          (step.phase == FortPhase::Kernel) ? kKernelNumOffsets : kNumCandidateOffsets;
      if (thematic_candidate != nullptr &&
          static_cast<uint8_t>(thematic_candidate->protection) <=
              static_cast<uint8_t>(ThematicProtectionLevel::Dialogue)) {
        offsets = kKernelOffsets;
        num_offsets = kKernelNumOffsets;
      }

      bool add_harmony_candidate =
          current_event != nullptr && voice >= 2 && step.phase != FortPhase::Kernel;
      uint8_t harmony_candidate = base_pitch;
      if (add_harmony_candidate) {
        harmony_candidate = foldPitchIntoRangeByOctave(nearestChordTone(base_pitch, *current_event),
                                                       voice_lo, voice_hi);
      }
      bool add_contour_candidate =
          prev_pitch_per_voice[voice] > 0 && step.phase != FortPhase::Kernel &&
          std::abs(static_cast<int>(base_pitch) - static_cast<int>(prev_pitch_per_voice[voice])) >
              interval::kPerfect5th;
      uint8_t contour_candidate = base_pitch;
      if (add_contour_candidate) {
        contour_candidate = stepTowardDiatonic(prev_pitch_per_voice[voice], base_pitch, current_key,
                                               voice_lo, voice_hi, current_scale);
      }
      bool add_lower_octave_candidate =
          request.num_voices >= 4 && voice == 1 && thematic_candidate != nullptr &&
          step.phase != FortPhase::Kernel &&
          static_cast<int>(base_pitch) - 12 >= static_cast<int>(voice_lo);
      uint8_t lower_octave_candidate =
          static_cast<uint8_t>(add_lower_octave_candidate ? base_pitch - 12 : base_pitch);

      for (int offset_idx = 0;
           offset_idx < num_offsets + (add_contour_candidate ? 1 : 0) +
                            (add_harmony_candidate ? 1 : 0) + (add_lower_octave_candidate ? 1 : 0);
           ++offset_idx) {
        bool is_contour_candidate = add_contour_candidate && offset_idx == num_offsets;
        bool is_harmony_candidate = offset_idx >= num_offsets + (add_contour_candidate ? 1 : 0) &&
                                    offset_idx < num_offsets + (add_contour_candidate ? 1 : 0) +
                                                     (add_harmony_candidate ? 1 : 0);
        bool is_lower_octave_candidate =
            add_lower_octave_candidate && offset_idx >= num_offsets +
                                                            (add_contour_candidate ? 1 : 0) +
                                                            (add_harmony_candidate ? 1 : 0);
        int candidate_int = is_contour_candidate   ? static_cast<int>(contour_candidate)
                            : is_harmony_candidate ? static_cast<int>(harmony_candidate)
                            : is_lower_octave_candidate
                                ? static_cast<int>(lower_octave_candidate)
                                : static_cast<int>(base_pitch) + offsets[offset_idx];

        // Range check.
        if (candidate_int < static_cast<int>(voice_lo) ||
            candidate_int > static_cast<int>(voice_hi)) {
          continue;
        }
        // Snap to the local diatonic collection.
        uint8_t candidate = (is_harmony_candidate || is_contour_candidate)
                                ? static_cast<uint8_t>(candidate_int)
                                : scale_util::nearestScaleTone(static_cast<uint8_t>(candidate_int),
                                                               current_key, current_scale);

        // Compute figure score from recent pitch window.
        float figure_score =
            computeFigureScore(recent_per_voice[voice], recent_count_per_voice[voice], candidate);

        // Evaluate via ConstraintState with rule evaluators from request.
        float score =
            state.evaluate(candidate, note_dur, voice, note_tick, ctx, snap, request.rule_eval,
                           request.crossing_eval, request.cp_state_ctx, recent_per_voice[voice],
                           recent_count_per_voice[voice], figure_score, false);

        score -= directPerfectMotionPenalty(candidate, voice, snap, prev_pitch_per_voice);
        score -= strongBeatDissonancePenalty(candidate, voice, note_tick, snap);
        score -= sustainedStrongBeatDissonancePenalty(result.notes, request.num_voices, candidate,
                                                      voice, note_tick, note_dur);
        if (request.thematic_plan != nullptr) {
          score -= protectedDialogueOverlapPenalty(result.notes, request.num_voices, candidate,
                                                   voice, note_tick, note_dur);
        }

        // Same-pitch repetition penalty: discourage consecutive identical pitches
        // to reduce monotonous patterns (target: ≤10% repetition rate).
        // Moderate penalty (0.40) balances diversity against consonance.
        if (candidate == prev_pitch_per_voice[voice]) {
          score -= 0.40f;
        }

        if (prev_pitch_per_voice[voice] > 0) {
          int motion = static_cast<int>(candidate) - static_cast<int>(prev_pitch_per_voice[voice]);
          int direction = (motion > 0) ? 1 : (motion < 0) ? -1 : 0;
          int prev_direction = prev_direction_per_voice[voice];
          if (direction != 0 && prev_direction != 0) {
            if (direction != prev_direction) {
              float flip_penalty = (voice == 0) ? 2.20f : 0.85f;
              if (voice == 0 && request.character == SubjectCharacter::Playful) {
                flip_penalty = 3.00f;
              }
              score -= flip_penalty;
            } else {
              score += (voice == 0) ? 0.35f : 0.10f;
            }
          }
        }

        // A1: Kernel original-pitch bonus — strongly prefer the motif's own
        // pitch to preserve thematic identity. If no hard violation on offset 0,
        // the original pitch wins unless another candidate is dramatically better.
        if (step.phase == FortPhase::Kernel && offset_idx == 0) {
          score += 0.50f;
        }

        // A1: Kernel spacing bonus — prefer wider voice separation over
        // consonance to prevent dense clustering when pitches are locked.
        if (step.phase == FortPhase::Kernel) {
          int min_spacing = 127;
          for (int vi = 0; vi < snap.num_voices; ++vi) {
            if (vi == voice || snap.pitches[vi] == 0)
              continue;
            int sp = std::abs(static_cast<int>(candidate) - static_cast<int>(snap.pitches[vi]));
            min_spacing = std::min(min_spacing, sp);
          }
          if (min_spacing < 127) {
            score += std::min(static_cast<float>(min_spacing) / 24.0f, 0.40f);
          }
        }

        // A2: Sequence motif-pitch bonus — prefer the motif's original pitch
        // to maintain sequential pattern coherence.
        if (step.phase == FortPhase::Sequence && offset_idx == 0) {
          score += 0.30f;
        }

        if (thematic_candidate != nullptr) {
          float identity = std::max(0.25f, thematic_candidate->motif_identity);
          if (offset_idx == 0) {
            score += 0.70f * identity;
          }
          BudgetDecision decision =
              decideBudgetUse(thematic_candidate->intent, thematic_candidate->protection,
                              ViolationClass::AllowedExpressive, false, true);
          if (decision.action == BudgetAction::Reject) {
            score -= 1.0f;
          }
        }

        score += thematicReplyCellBonus(request, voice, step.phase, candidate, motif_idx);
        score += episodeIntentHarmonyScore(request, voice, step.phase, note_tick, candidate,
                                           current_event, current_key, current_scale);
        if (current_event != nullptr && step.phase != FortPhase::Kernel && voice >= 2 &&
            !isChordTone(candidate, *current_event)) {
          score -= (note_tick % kTicksPerBeat == 0) ? 1.20f : 0.35f;
        }

        // Strict four-voice organ fugues need the lower manual inner line to
        // carry BWV578-like stepwise cells.  Large local jumps in Manual III
        // are the weakest vocabulary region in the current audit, so bias this
        // voice toward attested close motion during sequence/dissolution while
        // leaving kernel identity and other voices intact.
        if (request.num_voices == 4 && voice == 2 && step.phase != FortPhase::Kernel) {
          int local_motion =
              std::abs(static_cast<int>(candidate) - static_cast<int>(prev_pitch_per_voice[voice]));
          if (local_motion <= 2) {
            score += 0.20f;
          } else if (local_motion > 4) {
            score -= static_cast<float>(local_motion - 4) * 0.18f;
          }
          score += figure_score * 0.35f;
        }

        // Keep episode figuration within the validator's melodic ceiling.
        // Penalize, rather than forbid, so motif identity still wins when no
        // safer nearby candidate exists.
        int melodic_leap =
            std::abs(static_cast<int>(candidate) - static_cast<int>(prev_pitch_per_voice[voice]));
        if (melodic_leap > interval::kOctave) {
          score -= 16.0f + static_cast<float>(melodic_leap - interval::kOctave);
        } else if (melodic_leap > interval::kPerfect5th) {
          score -= static_cast<float>(melodic_leap - interval::kPerfect5th) * 0.85f;
        }
        if (is_contour_candidate) {
          score += 0.45f;
        }
        if (is_lower_octave_candidate) {
          score += 0.35f;
          if (prev_pitch_per_voice[voice] > 0) {
            int lower_motion = std::abs(static_cast<int>(candidate) -
                                        static_cast<int>(prev_pitch_per_voice[voice]));
            if (lower_motion <= interval::kPerfect4th) {
              score += 0.35f;
            }
          }
        }

        // Pedal consonance bonus: on strong beats, prefer intervals consonant
        // with the active pedal pitch (3rd, 5th, 6th, octave).
        if (request.pedal_pitch > 0 && note_tick % kTicksPerBeat == 0) {
          int ivl = absoluteInterval(candidate, request.pedal_pitch) % 12;
          bool consonant = (ivl == 0 || ivl == 3 || ivl == 4 || ivl == 7 || ivl == 8 || ivl == 9);
          score += consonant ? 0.30f : -0.25f;
        }

        // Wave 4: Episode spacing bonus — encourage wider voice separation.
        // Phase-dependent cap prevents over-spacing while ensuring clarity.
        {
          float spacing_cap;
          switch (step.phase) {
            case FortPhase::Kernel:
              spacing_cap = 0.50f;
              break;
            case FortPhase::Sequence:
              spacing_cap = 0.40f;
              break;
            case FortPhase::Dissolution:
              spacing_cap = 0.35f;
              break;
          }
          int min_spacing = 127;
          for (int vi = 0; vi < snap.num_voices; ++vi) {
            if (vi == voice || snap.pitches[vi] == 0)
              continue;
            int sp = std::abs(static_cast<int>(candidate) - static_cast<int>(snap.pitches[vi]));
            min_spacing = std::min(min_spacing, sp);
          }
          if (min_spacing < 127 && min_spacing > 0) {
            float bonus = std::sqrt(static_cast<float>(min_spacing) / 24.0f);
            score += std::min(bonus, spacing_cap);
          }
        }

        if (score > best_score) {
          best_score = score;
          best_pitch = candidate;
          best_figure_score = figure_score;
        }
      }

      uint8_t place_pitch = best_pitch;
      Tick place_dur = durationBeforeBoundaryDissonance(
          note_tick, note_dur, place_pitch, request.timeline, request.start_tick, request.duration);

      // If the intended motif cannot be committed, replace only the
      // low-protection inner layer with a recovery pattern.  Subject, reply,
      // and bass-support layers still fail closed.
      if (best_score <= -std::numeric_limits<float>::infinity()) {
        if (!selectRecoveryConsonance(request, state, voice, step.phase, note_tick, note_dur,
                                      current_key, snap, prev_pitch_per_voice, prev_dur_per_voice,
                                      recent_per_voice[voice], recent_count_per_voice[voice],
                                      voice_lo, voice_hi, &place_pitch, &place_dur)) {
          note_tick += motif_note.duration;
          continue;
        }
      } else {
        place_dur =
            durationBeforeBoundaryDissonance(note_tick, note_dur, best_pitch, request.timeline,
                                             request.start_tick, request.duration);
        float committed_score =
            state.evaluate(best_pitch, place_dur, voice, note_tick, ctx, snap, request.rule_eval,
                           request.crossing_eval, request.cp_state_ctx, recent_per_voice[voice],
                           recent_count_per_voice[voice], best_figure_score);
        if (committed_score <= -std::numeric_limits<float>::infinity()) {
          if (!selectRecoveryConsonance(request, state, voice, step.phase, note_tick, note_dur,
                                        current_key, snap, prev_pitch_per_voice, prev_dur_per_voice,
                                        recent_per_voice[voice], recent_count_per_voice[voice],
                                        voice_lo, voice_hi, &place_pitch, &place_dur)) {
            note_tick += motif_note.duration;
            continue;
          }
        }
      }

      // Place the winning candidate.
      NoteEvent placed;
      placed.start_tick = note_tick;
      placed.duration = place_dur;
      placed.pitch = place_pitch;
      placed.velocity = 80;
      placed.voice = voice;
      placed.source = episodeSourceForIntent(request, voice, step.phase);
      result.notes.push_back(placed);

      // Advance constraint state.
      state.advance(note_tick, place_pitch, voice, place_dur, current_key);

      // Update per-voice tracking.
      if (prev_pitch_per_voice[voice] > 0) {
        int motion = static_cast<int>(place_pitch) - static_cast<int>(prev_pitch_per_voice[voice]);
        if (motion > 0) {
          prev_direction_per_voice[voice] = 1;
        } else if (motion < 0) {
          prev_direction_per_voice[voice] = -1;
        }
      }
      prev_pitch_per_voice[voice] = place_pitch;
      prev_dur_per_voice[voice] = place_dur;
      int& count = recent_count_per_voice[voice];
      if (count < kMaxRecentPitches) {
        recent_per_voice[voice][count] = place_pitch;
        count++;
      } else {
        // Shift window left by 1.
        for (int rdx = 0; rdx < kMaxRecentPitches - 1; ++rdx) {
          recent_per_voice[voice][rdx] = recent_per_voice[voice][rdx + 1];
        }
        recent_per_voice[voice][kMaxRecentPitches - 1] = place_pitch;
      }

      // Check for deadlock.
      if (state.is_dead(note_tick)) {
        result.success = false;
        result.achieved_key = current_key;
        return result;
      }

      note_tick += note_dur;
    }
  }

  // 5. Determine resting voice (5+ voices) BEFORE placing lower voices.
  //    In 4-voice organ fugues, voice 2 should carry inner/bass fragments
  //    rather than becoming a near-continuous held-tone layer.
  VoiceId resting_voice = 255;  // Sentinel: no resting voice.
  if (request.num_voices >= 5) {
    uint8_t first_inner = 2;
    uint8_t last_inner = request.num_voices - 2;
    if (first_inner <= last_inner) {
      uint8_t inner_count = last_inner - first_inner + 1;
      resting_voice = first_inner + static_cast<VoiceId>(request.episode_index % inner_count);
    }
  }

  // 5a. Place held tones on resting voice first (so bass skips it).
  if (resting_voice != 255) {
    placeHeldTones(result.notes, result.notes, state, request.start_tick, request.duration,
                   request.num_voices, request.episode_index, request.start_key, request.rule_eval,
                   request.crossing_eval, request.cp_state_ctx);
  }

  // 5b. Place bass fragments for voice 2 (if not resting).
  if (request.num_voices >= 3 && resting_voice != 2) {
    std::mt19937 bass_rng(request.seed ^ 0xBA550002u);
    Key bass_key = request.end_key;
    std::vector<NoteEvent> placed_copy = result.notes;
    placeBassFragments(result.notes, state, placed_copy, request.start_tick, request.duration,
                       request.num_voices, bass_key, bass_rng, request.timeline, request.grammar,
                       request.rule_eval, request.crossing_eval, request.cp_state_ctx);
  }

  // 5c. Place pedal voice (last voice) for 4+ voice fugues.
  if (request.num_voices >= 4) {
    std::mt19937 pedal_rng(request.seed ^ 0xBA550003u);
    placePedalVoice(result.notes, state, request.start_tick, request.duration, request.num_voices,
                    request.end_key, pedal_rng, request.timeline, request.rule_eval,
                    request.crossing_eval, request.cp_state_ctx);
  }

  // 6. Apply invertible counterpoint (odd episode_index, num_voices >= 2).
  applyInvertibleCounterpoint(result.notes, request.episode_index, request.num_voices);

  // 7. Add safe passing/neighbor continuations to sparse upper episode lines.
  insertEpisodePassingContinuations(result.notes, request.start_tick, request.duration,
                                    request.num_voices, request.end_key);
  fillSparseInnerVoiceGaps(result.notes, request.start_tick, request.duration, request.num_voices,
                           request.end_key);
  fillSparseUpperVoiceGaps(result.notes, request.start_tick, request.duration, request.num_voices,
                           request.end_key);
  addInnerWeakBeatPassingLine(result.notes, request.start_tick, request.duration,
                              request.num_voices, request.end_key);
  extendInnerWeakBeatLine(result.notes, request.start_tick, request.duration, request.num_voices,
                          request.end_key);
  addInnerWeakSixteenthPassingLine(result.notes, request.start_tick, request.duration,
                                   request.num_voices, request.end_key);
  splitSafeUpperEighths(result.notes, request.start_tick, request.duration, request.num_voices,
                        request.end_key);
  splitSafeEpisodeQuartersToSixteenths(result.notes, request.start_tick, request.duration,
                                       request.num_voices, request.end_key);
  splitLongInnerEpisodeNotes(result.notes, request.start_tick, request.duration, request.num_voices,
                             request.end_key);
  seedInnerWeakBeatContinuity(result.notes, request.start_tick, request.duration,
                              request.num_voices, request.end_key);
  seedInnerEpisodeEdgeContinuity(result.notes, request.start_tick, request.duration,
                                 request.num_voices, request.end_key);
  smoothLocalEpisodeMaterialLeaps(result.notes, request.timeline, request.start_tick,
                                  request.duration, request.num_voices, request.end_key,
                                  request.home_key, request.home_is_minor);
  truncateSustainedBoundaryDissonances(result.notes, request.timeline, request.start_tick,
                                       request.duration);

  // 8. Sort notes by start_tick for clean output.
  std::sort(result.notes.begin(), result.notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.start_tick != rhs.start_tick)
                return lhs.start_tick < rhs.start_tick;
              return lhs.voice < rhs.voice;
            });

  result.achieved_key = request.end_key;
  result.success = true;
  return result;
}

}  // namespace bach
