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
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/key.h"
#include "core/vocabulary_data.inc"
#include "fugue/episode.h"
#include "harmony/harmonic_timeline.h"
#include "fugue/fortspinnung.h"
#include "fugue/motif_pool.h"
#include "fugue/voice_registers.h"
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

/// @brief Compute chromatic key distance (semitones) between two keys.
/// @param from Source key.
/// @param to Target key.
/// @return Signed semitone distance (shortest path, range [-6, +6]).
int keyDistance(Key from, Key to) {
  int diff = static_cast<int>(to) - static_cast<int>(from);
  // Wrap to shortest path on the circle.
  if (diff > 6) diff -= 12;
  if (diff < -6) diff += 12;
  return diff;
}

/// @brief Build a VerticalSnapshot from currently placed notes at a tick.
///
/// Scans the placed notes to find the most recent sounding pitch per voice.
///
/// @param placed_notes All placed notes so far.
/// @param tick Current tick position.
/// @param num_voices Number of voices.
/// @return Snapshot with sounding pitches.
VerticalSnapshot buildSnapshot(const std::vector<NoteEvent>& placed_notes,
                               Tick tick, uint8_t num_voices) {
  VerticalSnapshot snap;
  snap.num_voices = num_voices;
  for (const auto& note : placed_notes) {
    if (note.voice < num_voices &&
        note.start_tick <= tick &&
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
MarkovContext buildMarkovContext(uint8_t prev_pitch, Tick prev_duration,
                                   Tick tick, Key current_key) {
  MarkovContext ctx;
  ctx.prev_pitch = prev_pitch;
  ctx.key = current_key;
  ctx.scale = ScaleType::Major;
  ctx.beat = tickToBeatPos(tick);
  ctx.prev_dur = ticksToDurCategory(prev_duration);
  ctx.prev_step = 0;
  ctx.deg_class = scaleDegreeToClass(
      static_cast<int>(prev_pitch) % 12);
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
float computeFigureScore(const uint8_t* recent, int count,
                         uint8_t candidate) {
  // Need at least 4 recent pitches + candidate = 5-note window.
  if (count < 4) return 0.0f;

  uint8_t window[5];
  // Take last 4 from recent, append candidate.
  for (int idx = 0; idx < 4; ++idx) {
    window[idx] = recent[count - 4 + idx];
  }
  window[4] = candidate;

  int8_t intervals[4];
  for (int idx = 0; idx < 4; ++idx) {
    int diff = static_cast<int>(window[idx + 1]) -
               static_cast<int>(window[idx]);
    intervals[idx] = vocab_data::semitoneToDegree(diff);
  }
  return vocab_data::matchVocabulary(intervals);
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
  if (progress <= 0.5f || total_shift == 0) return pitch;

  // Linear ramp from 0 to total_shift over the second half.
  float frac = (progress - 0.5f) * 2.0f;  // 0..1 in second half.
  int shift = static_cast<int>(frac * total_shift);
  return clampPitch(static_cast<int>(pitch) + shift, 0, 127);
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
void placeBassFragments(std::vector<NoteEvent>& result,
                        ConstraintState& state,
                        const std::vector<NoteEvent>& placed_notes,
                        Tick start_tick, Tick duration,
                        uint8_t num_voices, Key current_key,
                        std::mt19937& rng,
                        const HarmonicTimeline* timeline,
                        const FortspinnungGrammar& grammar,
                        const IRuleEvaluator* rule_eval,
                        const BachRuleEvaluator* crossing_eval,
                        const CounterpointState* cp_state_ctx) {
  if (num_voices < 3 || placed_notes.empty()) return;

  // Collect voice 0 notes for tail extraction.
  std::vector<NoteEvent> voice0_notes;
  for (const auto& note : placed_notes) {
    if (note.voice == 0) voice0_notes.push_back(note);
  }
  if (voice0_notes.empty()) return;

  // Extract tail motif and augment for bass.
  auto tail = extractTailMotif(voice0_notes, 3);
  if (tail.size() > 3) tail.resize(3);
  auto bass_fragment = tail;  // Preserve original rhythm (no augment).

  // Get voice 2 range.
  auto [v2_lo, v2_hi] = getFugueVoiceRange(2, num_voices);
  int v2_lo_int = static_cast<int>(v2_lo);
  int v2_hi_int = static_cast<int>(v2_hi);

  // Map fragment pitches to bass register.
  for (auto& note : bass_fragment) {
    int mapped = static_cast<int>(note.pitch);
    if (mapped < v2_lo_int) mapped += 12;
    if (mapped > v2_hi_int) mapped -= 12;
    note.pitch = clampPitch(mapped, v2_lo, v2_hi);
  }

  // Add duration variety to bass fragment notes (augment creates uniform durations).
  // Randomly stretch/compress individual notes for rhythmic interest.
  if (bass_fragment.size() >= 2) {
    std::mt19937 dur_rng(rng());
    for (size_t idx = 0; idx < bass_fragment.size(); ++idx) {
      float factor = rng::rollFloat(dur_rng, 0.6f, 1.6f);
      bass_fragment[idx].duration = quantizeToGrid(std::max(
          static_cast<Tick>(bass_fragment[idx].duration * factor),
          static_cast<Tick>(kTicksPerBeat / 2)));
    }
  }

  Tick frag_dur = motifDuration(bass_fragment);
  if (frag_dur == 0) frag_dur = kTicksPerBeat * 2;

  // Emission probability: 70-85% per-seed variation.
  float emit_prob = rng::rollFloat(rng, 0.70f, 0.85f);

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
        if (note_tick >= start_tick + duration) break;

        // Evaluate through constraint state.
        VerticalSnapshot snap = buildSnapshot(result, note_tick, num_voices);
        MarkovContext ctx = buildMarkovContext(
            bass_prev_pitch, kTicksPerBeat, note_tick, current_key);
        float score = state.evaluate(
            frag_note.pitch, frag_note.duration, 2, note_tick,
            ctx, snap, rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

        if (score > -std::numeric_limits<float>::infinity()) {
          NoteEvent evt = frag_note;
          evt.start_tick = note_tick;
          evt.voice = 2;
          evt.source = BachNoteSource::EpisodeMaterial;
          Tick remaining = start_tick + duration - note_tick;
          if (evt.duration > remaining) evt.duration = remaining;
          // Skip notes shorter than a sixteenth to avoid rhythmic debris.
          if (evt.duration < duration::kSixteenthNote) continue;
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
        while (anchor_pitch > v2_hi_int) anchor_pitch -= 12;
        while (anchor_pitch < v2_lo_int) anchor_pitch += 12;
      } else {
        // Descending circle-of-fifths: I -> IV -> vii(->V) -> iii -> vi -> ii -> V.
        constexpr int kCircleOfFifths[] = {0, 5, 7, 4, 9, 2, 7};
        constexpr int kNumCircleSteps = 7;
        constexpr int kMaxSteps = 5;  // Stop at step 4 (vi); full cycle too long.

        // 2-bar unit stepping from episode start.
        int raw_step = static_cast<int>(
            (bass_tick - start_tick) / (kTicksPerBar * 2));
        int step_idx = std::min(raw_step % kNumCircleSteps, kMaxSteps - 1);

        // Strong beat: sequence root pitch.
        // Weak beat: diatonic passing tone between current and next step.
        bool is_strong_beat =
            (bass_tick % kTicksPerBar) < static_cast<Tick>(kTicksPerBeat);
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
              static_cast<uint8_t>(clampPitch(base_pitch, 0, 127)),
              current_key, ScaleType::Major);
          int next_deg = scale_util::pitchToAbsoluteDegree(
              static_cast<uint8_t>(clampPitch(next_pitch, 0, 127)),
              current_key, ScaleType::Major);

          // Calculate how far into the 2-bar unit we are (fractional).
          Tick unit_offset = (bass_tick - start_tick) % (kTicksPerBar * 2);
          float frac = static_cast<float>(unit_offset) /
                       static_cast<float>(kTicksPerBar * 2);

          int passing_deg = curr_deg +
              static_cast<int>((next_deg - curr_deg) * frac);
          anchor_pitch = static_cast<int>(
              scale_util::absoluteDegreeToPitch(passing_deg, current_key,
                                                ScaleType::Major));
        }

        while (anchor_pitch > v2_hi_int) anchor_pitch -= 12;
        while (anchor_pitch < v2_lo_int) anchor_pitch += 12;
      }
      anchor_pitch = clampPitch(anchor_pitch, v2_lo, v2_hi);

      // Phase-dependent bass anchor duration distribution.
      // Sequence: shorter (includes 16ths) for rhythmic drive.
      // Kernel/Dissolution: longer for harmonic stability.
      Tick base_dur;
      float bass_progress = static_cast<float>(bass_tick - start_tick) /
                             static_cast<float>(std::max(duration, static_cast<Tick>(1)));
      bool is_sequence_phase =
          bass_progress >= grammar.kernel_ratio &&
          bass_progress < grammar.kernel_ratio + grammar.sequence_ratio;
      float dur_roll = rng::rollFloat(rng, 0.0f, 1.0f);
      if (is_sequence_phase) {
        // Sequence: 16th(20%), 8th(40%), quarter(30%), half(10%).
        if (dur_roll < 0.20f) base_dur = duration::kSixteenthNote;
        else if (dur_roll < 0.60f) base_dur = kTicksPerBeat / 2;
        else if (dur_roll < 0.90f) base_dur = kTicksPerBeat;
        else base_dur = kTicksPerBeat * 2;
      } else {
        // Kernel/Dissolution: 8th(30%), quarter(40%), half(20%), bar(10%).
        if (dur_roll < 0.30f) base_dur = kTicksPerBeat / 2;
        else if (dur_roll < 0.70f) base_dur = kTicksPerBeat;
        else if (dur_roll < 0.90f) base_dur = kTicksPerBeat * 2;
        else base_dur = kTicksPerBar;
      }

      // Bass floor: sustain when upper voices have sustained figuration (>=2 beats).
      {
        int short_note_count = 0;
        for (const auto& note : result) {
          if (note.voice >= 2) continue;
          if (note.start_tick + note.duration > bass_tick - kTicksPerBeat * 2 &&
              note.start_tick <= bass_tick &&
              note.duration <= duration::kEighthNote) {
            ++short_note_count;
          }
        }
        if (short_note_count >= 4 && base_dur < kTicksPerBeat) {
          base_dur = kTicksPerBeat;
        }
      }
      Tick anchor_dur = std::min(base_dur,
                                 start_tick + duration - bass_tick);
      if (anchor_dur >= duration::kSixteenthNote) {
        VerticalSnapshot snap = buildSnapshot(result, bass_tick, num_voices);
        MarkovContext ctx = buildMarkovContext(
            bass_prev_pitch, kTicksPerBeat, bass_tick, current_key);
        float score = state.evaluate(
            static_cast<uint8_t>(anchor_pitch), anchor_dur, 2, bass_tick,
            ctx, snap, rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

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
      bass_tick += kTicksPerBeat * 2;  // Half-bar advance.
    }
    use_fragment = !use_fragment;
  }

  // Post-sweep: voice 2 notes exceeding range ceiling octave-folded.
  for (auto& note : result) {
    if (note.voice != 2) continue;
    if (note.start_tick < start_tick || note.start_tick >= start_tick + duration) continue;
    int p = static_cast<int>(note.pitch);
    while (p > v2_hi_int && p - 12 >= v2_lo_int) p -= 12;
    while (p < v2_lo_int && p + 12 <= v2_hi_int) p += 12;
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
/// @param rule_eval Rule evaluator for parallel perfect checks (nullable).
/// @param crossing_eval Bach evaluator for crossing checks (nullable).
/// @param cp_state_ctx Counterpoint state context (nullable).
void placePedalVoice(std::vector<NoteEvent>& result,
                     ConstraintState& state,
                     Tick start_tick, Tick duration,
                     uint8_t num_voices, Key current_key,
                     std::mt19937& rng,
                     const IRuleEvaluator* rule_eval,
                     const BachRuleEvaluator* crossing_eval,
                     const CounterpointState* cp_state_ctx) {
  if (num_voices < 4) return;

  VoiceId pedal_voice = num_voices - 1;
  auto [pedal_lo, pedal_hi] = getFugueVoiceRange(pedal_voice, num_voices);

  // Pedal anchor pitches from key.
  int tonic_bass = 36 + static_cast<int>(current_key);
  tonic_bass = clampPitch(tonic_bass, pedal_lo, pedal_hi);
  int dominant_bass = tonic_bass + 7;
  if (dominant_bass > static_cast<int>(pedal_hi)) dominant_bass -= 12;
  dominant_bass = clampPitch(dominant_bass, pedal_lo, pedal_hi);

  float emit_prob = rng::rollFloat(rng, 0.50f, 0.70f);
  constexpr int kMaxSilentBars = 4;
  int consecutive_silent_bars = 0;
  Tick pedal_tick = start_tick;
  bool use_tonic = true;

  while (pedal_tick < start_tick + duration) {
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
      if (subdominant_bass > static_cast<int>(pedal_hi)) subdominant_bass -= 12;
      anchor = clampPitch(subdominant_bass, pedal_lo, pedal_hi);
    }
    Tick anchor_dur = std::min(kTicksPerBeat * 2,  // Half note.
                               start_tick + duration - pedal_tick);
    if (anchor_dur > 0) {
      VerticalSnapshot snap = buildSnapshot(result, pedal_tick, num_voices);
      MarkovContext ctx = buildMarkovContext(
          0, kTicksPerBeat, pedal_tick, current_key);
      float score = state.evaluate(
          static_cast<uint8_t>(anchor), anchor_dur, pedal_voice, pedal_tick,
          ctx, snap, rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

      if (score > -std::numeric_limits<float>::infinity()) {
        NoteEvent note;
        note.start_tick = pedal_tick;
        note.duration = anchor_dur;
        note.pitch = static_cast<uint8_t>(anchor);
        note.velocity = 80;
        note.voice = pedal_voice;
        note.source = BachNoteSource::EpisodeMaterial;
        result.push_back(note);
        state.advance(pedal_tick, note.pitch, pedal_voice, note.duration, current_key);
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
void placeHeldTones(std::vector<NoteEvent>& result,
                    const std::vector<NoteEvent>& placed_notes,
                    ConstraintState& state,
                    Tick start_tick, Tick duration,
                    uint8_t num_voices, int episode_index,
                    Key current_key,
                    const IRuleEvaluator* rule_eval,
                    const BachRuleEvaluator* crossing_eval,
                    const CounterpointState* cp_state_ctx) {
  if (num_voices < 4) return;

  // Resting voice rotates through inner voices (not voice 0/1 active, not bass).
  // For 4 voices: only voice 2 can rest (voice 0/1 active, voice 3 = bass).
  // For 5 voices: voices 2-3 can rest (voice 0/1 active, voice 4 = bass).
  uint8_t first_inner = 2;
  uint8_t last_inner = num_voices - 2;
  if (first_inner > last_inner) return;

  uint8_t inner_count = last_inner - first_inner + 1;
  VoiceId resting_voice = first_inner +
      static_cast<VoiceId>(episode_index % inner_count);

  // Check if the resting voice already has notes.
  bool has_notes = false;
  for (const auto& note : placed_notes) {
    if (note.voice == resting_voice) {
      has_notes = true;
      break;
    }
  }
  if (has_notes) return;  // Voice already active, skip.

  // Get voice range for held tone pitch.
  auto [v_lo, v_hi] = getFugueVoiceRange(resting_voice, num_voices);
  int held_pitch = (static_cast<int>(v_lo) + static_cast<int>(v_hi)) / 2;
  // Snap to scale tone.
  held_pitch = scale_util::nearestScaleTone(
      static_cast<uint8_t>(held_pitch), current_key, ScaleType::Major);

  // Place half-note held tones across the episode.
  Tick held_tick = start_tick;
  int held_step_count = 0;
  while (held_tick < start_tick + duration) {
    Tick held_dur = std::min(kTicksPerBeat * 2,  // Half note.
                             start_tick + duration - held_tick);
    if (held_dur == 0) break;

    VerticalSnapshot snap = buildSnapshot(result, held_tick, num_voices);
    MarkovContext ctx = buildMarkovContext(
        static_cast<uint8_t>(held_pitch), kTicksPerBeat, held_tick, current_key);
    float score = state.evaluate(
        static_cast<uint8_t>(held_pitch), held_dur, resting_voice, held_tick,
        ctx, snap, rule_eval, crossing_eval, cp_state_ctx, nullptr, 0, 0.0f);

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
      held_pitch = scale_util::nearestScaleTone(
          static_cast<uint8_t>(held_pitch), current_key, ScaleType::Major);
    }
  }
}

/// @brief Apply invertible counterpoint (voice 0 <-> voice 1 swap).
///
/// For odd episode indices with num_voices >= 2, swap voice IDs 0 and 1
/// for all notes. This implements double counterpoint at the octave.
///
/// @param notes Notes to modify in place.
/// @param episode_index Episode ordinal.
/// @param num_voices Number of active voices.
void applyInvertibleCounterpoint(std::vector<NoteEvent>& notes,
                                 int episode_index, uint8_t num_voices) {
  if (episode_index % 2 == 0 || num_voices < 2) return;

  for (auto& note : notes) {
    if (note.voice == 0) {
      note.voice = 1;
    } else if (note.voice == 1) {
      note.voice = 0;
    }
  }
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
  if (request.motif_pool == nullptr || request.motif_pool->empty() ||
      request.duration == 0) {
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
  std::vector<NoteEvent> v0_transformed = applyMotifOp(
      base_motif->notes, params.voice0_initial, request.start_key);
  std::vector<NoteEvent> v1_transformed = applyMotifOp(
      base_motif->notes, params.voice1_initial, request.start_key);

  // Apply secondary transformation for voice 1 (Noble: Retrograde after Augment).
  if (params.voice1_secondary != MotifOp::Original) {
    v1_transformed = applyMotifOp(
        v1_transformed, params.voice1_secondary, request.start_key);
  }

  // 3. Plan Fortspinnung arc.
  std::vector<FortspinnungStep> steps = planFortspinnung(
      pool, request.grammar, request.start_tick, request.duration,
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
    if (vdx < request.num_voices &&
        vdx < EpisodeRequest::kMaxRequestVoices &&
        request.last_pitches[vdx] > 0) {
      prev_pitch_per_voice[vdx] = request.last_pitches[vdx];
    } else if (vdx == 0 && !v0_transformed.empty()) {
      prev_pitch_per_voice[vdx] = v0_transformed[0].pitch;
    } else if (vdx == 1 && !v1_transformed.empty()) {
      prev_pitch_per_voice[vdx] = v1_transformed[0].pitch;
    } else {
      // Re-entry or unknown: use voice center as neutral starting point.
      auto [vlo, vhi] = getFugueVoiceRange(
          static_cast<uint8_t>(vdx), request.num_voices);
      prev_pitch_per_voice[vdx] =
          static_cast<uint8_t>((static_cast<int>(vlo) + static_cast<int>(vhi)) / 2);
    }
    prev_dur_per_voice[vdx] = kTicksPerBeat;
  }

  for (const auto& step : steps) {
    VoiceId voice = step.voice;
    if (voice >= kMaxVoices) continue;

    // Get the transformed motif notes for this step.
    const PooledMotif* step_motif = pool.getByRank(step.pool_rank);
    if (step_motif == nullptr || step_motif->notes.empty()) continue;

    // Apply the step's operation.
    std::vector<NoteEvent> motif_notes = applyMotifOp(
        step_motif->notes, step.op, request.start_key,
        ScaleType::Major, params.sequence_step);

    // Noble character: voice 1 uses augmented motif transposed down an octave
    // for bass-like behavior (Bach's actual practice for Noble episodes).
    if (voice == 1 && request.character == SubjectCharacter::Noble) {
      motif_notes = transposeMelody(motif_notes, -12);
    }

    // Get voice range for pitch clamping.
    auto [voice_lo, voice_hi] = getFugueVoiceRange(voice, request.num_voices);

    // Place each note in the motif with candidate evaluation.
    Tick note_tick = step.tick;
    for (const auto& motif_note : motif_notes) {
      if (note_tick >= request.start_tick + request.duration) break;

      // Rhythm density: phase-controlled diminution with motif preservation.
      // B1: Kernel=0% (theme rhythm fully preserved), Sequence=25% (limited),
      // Dissolution=40-55% (main rhythmic density site).
      Tick base_dur = motif_note.duration;
      {
        constexpr Tick kDimFloor = duration::kSixteenthNote;  // 120 ticks
        FortPhase phase = step.phase;

        // B1: Phase-dependent diminution probability.
        // Reference: organ fugue episodes (BWV578, BWV532) use running 16th motion.
        // Kernel preserves theme rhythm; Sequence/Dissolution drive toward density.
        float diminish_prob;
        switch (phase) {
          case FortPhase::Kernel:
            diminish_prob = 0.0f;
            break;
          case FortPhase::Sequence:
            diminish_prob = 0.50f;
            break;
          case FortPhase::Dissolution:
            diminish_prob = 0.55f + request.energy_level * 0.15f;
            break;
        }

        // B2: Strong beat guard — preserve durations on strong beats
        // (beat 1 or 3) except in Dissolution phase.
        if (phase != FortPhase::Dissolution && isStrongBeatInBar(note_tick)) {
          diminish_prob = 0.0f;
        }

        // B3: Resolution note protection — if previous note was dissonant
        // with other voices and current motif pitch is consonant, skip
        // diminution to preserve suspension-resolution voice leading.
        if (diminish_prob > 0.0f) {
          uint8_t prev_p = prev_pitch_per_voice[voice];
          if (prev_p > 0) {
            bool prev_dissonant = false;
            for (int vi = 0; vi < request.num_voices && vi < kMaxVoices; ++vi) {
              if (vi == voice) continue;
              uint8_t other_p = prev_pitch_per_voice[vi];
              if (other_p == 0) continue;
              int ivl = std::abs(static_cast<int>(prev_p) -
                                 static_cast<int>(other_p)) % 12;
              // Dissonant: 2nds (1,2), 7ths (10,11), tritone (6).
              if (ivl == 1 || ivl == 2 || ivl == 6 ||
                  ivl == 10 || ivl == 11) {
                prev_dissonant = true;
                break;
              }
            }
            if (prev_dissonant) {
              uint8_t mp = motif_note.pitch;
              for (int vi = 0; vi < request.num_voices && vi < kMaxVoices;
                   ++vi) {
                if (vi == voice) continue;
                uint8_t other_p = prev_pitch_per_voice[vi];
                if (other_p == 0) continue;
                int ivl = std::abs(static_cast<int>(mp) -
                                   static_cast<int>(other_p)) % 12;
                // Consonant: unison(0), 3rds(3,4), 5th(7), 6ths(8,9).
                if (ivl == 0 || ivl == 3 || ivl == 4 ||
                    ivl == 7 || ivl == 8 || ivl == 9) {
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
          DurCategory post_diminish_dc = ticksToDurCategory(
              std::max(base_dur / 2, kDimFloor));
          if (prev_dc == DurCategory::S8 && post_diminish_dc == DurCategory::S16) {
            diminish_prob *= 0.5f;  // Softened: allow more 8th->16th transitions.
          }
        }

        if (base_dur > kDimFloor) {
          if (rng::rollProbability(rng, diminish_prob)) {
            base_dur = std::max(base_dur / 2, kDimFloor);
            // Second halving: Dissolution at half prob, Sequence at lower prob
            // to create quarter->8th->16th chains for running episode motion.
            float second_prob = (phase == FortPhase::Dissolution)
                                    ? diminish_prob * 0.5f
                                    : 0.25f;
            if (base_dur > kDimFloor &&
                rng::rollProbability(rng, second_prob)) {
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
          } else if (prev_dc == DurCategory::S8 &&
                     base_dur < duration::kEighthNote) {
            base_dur = duration::kEighthNote;
          }
        }
      }

      // Apply minimum duration constraint.
      Tick note_dur = std::max(base_dur, min_dur);
      Tick remaining = request.start_tick + request.duration - note_tick;
      if (note_dur > remaining) note_dur = remaining;
      if (note_dur == 0) continue;

      // Compute progress for modulation.
      float progress = static_cast<float>(note_tick - request.start_tick) /
                       static_cast<float>(std::max(request.duration,
                                                   static_cast<Tick>(1)));

      // Determine current key based on modulation progress.
      // Switch to target key at 60% to ensure second-half diatonicism.
      Key current_key = (progress > 0.6f) ? request.end_key : request.start_key;

      // Build context for evaluation.
      VerticalSnapshot snap = buildSnapshot(
          result.notes, note_tick, request.num_voices);
      MarkovContext ctx = buildMarkovContext(
          prev_pitch_per_voice[voice], prev_dur_per_voice[voice],
          note_tick, current_key);

      // Evaluate candidates: phase-dependent search width.
      uint8_t base_pitch = applyModulationShift(
          motif_note.pitch, progress, total_shift);

      float best_score = -std::numeric_limits<float>::infinity();
      uint8_t best_pitch = base_pitch;

      // A1: Kernel phase uses 3 candidates (original ± 1 semitone) to
      // preserve motif pitch identity. Other phases use full 5 candidates.
      constexpr int kKernelOffsets[] = {0, -1, 1};
      constexpr int kKernelNumOffsets = 3;
      const int* offsets = (step.phase == FortPhase::Kernel)
                               ? kKernelOffsets : kCandidateOffsets;
      int num_offsets = (step.phase == FortPhase::Kernel)
                            ? kKernelNumOffsets : kNumCandidateOffsets;

      for (int offset_idx = 0; offset_idx < num_offsets; ++offset_idx) {
        int candidate_int = static_cast<int>(base_pitch) +
                            offsets[offset_idx];

        // Range check.
        if (candidate_int < static_cast<int>(voice_lo) ||
            candidate_int > static_cast<int>(voice_hi)) {
          continue;
        }
        // Snap to nearest diatonic scale tone (C major internal convention).
        uint8_t candidate = scale_util::nearestScaleTone(
            static_cast<uint8_t>(candidate_int), current_key, ScaleType::Major);

        // Compute figure score from recent pitch window.
        float figure_score = computeFigureScore(
            recent_per_voice[voice], recent_count_per_voice[voice],
            candidate);

        // Evaluate via ConstraintState with rule evaluators from request.
        float score = state.evaluate(
            candidate, note_dur, voice, note_tick,
            ctx, snap,
            request.rule_eval,
            request.crossing_eval,
            request.cp_state_ctx,
            recent_per_voice[voice],
            recent_count_per_voice[voice],
            figure_score);

        // Same-pitch repetition penalty: discourage consecutive identical pitches
        // to reduce monotonous patterns (target: ≤10% repetition rate).
        // Moderate penalty (0.40) balances diversity against consonance.
        if (candidate == prev_pitch_per_voice[voice]) {
          score -= 0.40f;
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
            if (vi == voice || snap.pitches[vi] == 0) continue;
            int sp = std::abs(static_cast<int>(candidate) -
                              static_cast<int>(snap.pitches[vi]));
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

        // Pedal consonance bonus: on strong beats, prefer intervals consonant
        // with the active pedal pitch (3rd, 5th, 6th, octave).
        if (request.pedal_pitch > 0 &&
            note_tick % kTicksPerBeat == 0) {
          int ivl = absoluteInterval(candidate, request.pedal_pitch) % 12;
          bool consonant = (ivl == 0 || ivl == 3 || ivl == 4 ||
                            ivl == 7 || ivl == 8 || ivl == 9);
          score += consonant ? 0.30f : -0.25f;
        }

        // Wave 4: Episode spacing bonus — encourage wider voice separation.
        // Phase-dependent cap prevents over-spacing while ensuring clarity.
        {
          float spacing_cap;
          switch (step.phase) {
            case FortPhase::Kernel:      spacing_cap = 0.50f; break;
            case FortPhase::Sequence:    spacing_cap = 0.40f; break;
            case FortPhase::Dissolution: spacing_cap = 0.35f; break;
          }
          int min_spacing = 127;
          for (int vi = 0; vi < snap.num_voices; ++vi) {
            if (vi == voice || snap.pitches[vi] == 0) continue;
            int sp = std::abs(static_cast<int>(candidate) -
                              static_cast<int>(snap.pitches[vi]));
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
        }
      }

      // Skip note if all candidates are hard-violated.
      if (best_score <= -std::numeric_limits<float>::infinity()) {
        note_tick += motif_note.duration;
        continue;
      }

      // Place the winning candidate.
      NoteEvent placed;
      placed.start_tick = note_tick;
      placed.duration = note_dur;
      placed.pitch = best_pitch;
      placed.velocity = 80;
      placed.voice = voice;
      placed.source = BachNoteSource::EpisodeMaterial;
      result.notes.push_back(placed);

      // Advance constraint state.
      state.advance(note_tick, best_pitch, voice, note_dur, current_key);

      // Update per-voice tracking.
      prev_pitch_per_voice[voice] = best_pitch;
      prev_dur_per_voice[voice] = note_dur;
      int& count = recent_count_per_voice[voice];
      if (count < kMaxRecentPitches) {
        recent_per_voice[voice][count] = best_pitch;
        count++;
      } else {
        // Shift window left by 1.
        for (int rdx = 0; rdx < kMaxRecentPitches - 1; ++rdx) {
          recent_per_voice[voice][rdx] = recent_per_voice[voice][rdx + 1];
        }
        recent_per_voice[voice][kMaxRecentPitches - 1] = best_pitch;
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

  // 5. Determine resting voice (4+ voices) BEFORE placing lower voices.
  //    For 4 voices: only voice 2 can rest. For 5: voices 2-3 rotate.
  VoiceId resting_voice = 255;  // Sentinel: no resting voice.
  if (request.num_voices >= 4) {
    uint8_t first_inner = 2;
    uint8_t last_inner = request.num_voices - 2;
    if (first_inner <= last_inner) {
      uint8_t inner_count = last_inner - first_inner + 1;
      resting_voice = first_inner +
          static_cast<VoiceId>(request.episode_index % inner_count);
    }
  }

  // 5a. Place held tones on resting voice first (so bass skips it).
  if (resting_voice != 255) {
    placeHeldTones(result.notes, result.notes,
                   state,
                   request.start_tick, request.duration,
                   request.num_voices, request.episode_index,
                   request.start_key,
                   request.rule_eval, request.crossing_eval,
                   request.cp_state_ctx);
  }

  // 5b. Place bass fragments for voice 2 (if not resting).
  if (request.num_voices >= 3 && resting_voice != 2) {
    std::mt19937 bass_rng(request.seed ^ 0xBA550002u);
    Key bass_key = request.end_key;
    std::vector<NoteEvent> placed_copy = result.notes;
    placeBassFragments(result.notes, state, placed_copy,
                       request.start_tick, request.duration,
                       request.num_voices, bass_key, bass_rng,
                       request.timeline, request.grammar,
                       request.rule_eval, request.crossing_eval,
                       request.cp_state_ctx);
  }

  // 5c. Place pedal voice (last voice) for 4+ voice fugues.
  if (request.num_voices >= 4) {
    std::mt19937 pedal_rng(request.seed ^ 0xBA550003u);
    placePedalVoice(result.notes, state,
                    request.start_tick, request.duration,
                    request.num_voices, request.end_key, pedal_rng,
                    request.rule_eval, request.crossing_eval,
                    request.cp_state_ctx);
  }

  // 6. Apply invertible counterpoint (odd episode_index, num_voices >= 2).
  applyInvertibleCounterpoint(
      result.notes, request.episode_index, request.num_voices);

  // 7. Sort notes by start_tick for clean output.
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
