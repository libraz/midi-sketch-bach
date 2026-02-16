/// @file
/// @brief Implementation of CollisionResolver - 6-stage pitch conflict resolution cascade.

#include "counterpoint/collision_resolver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/interval.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "counterpoint/melodic_context.h"
#include "counterpoint/species_rules.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"

namespace bach {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// @brief Check if a tick falls near a cadence position.
/// @param tick The tick to check.
/// @param cadence_ticks Sorted cadence tick positions.
/// @return True if tick is within kTicksPerBeat of any cadence tick.
static bool isNearCadence(Tick tick, const std::vector<Tick>& cadence_ticks) {
  for (Tick cad_tick : cadence_ticks) {
    Tick distance = (tick >= cad_tick) ? (tick - cad_tick) : (cad_tick - tick);
    if (distance <= kTicksPerBeat) {
      return true;
    }
  }
  return false;
}

/// @brief Check if a tick falls on a strong beat (beats 0 or 2 in 4/4).
static bool isStrongBeat(Tick tick) {
  uint8_t beat = beatInBar(tick);
  return beat == 0 || beat == 2;
}

/// @brief Compute cross-relation penalty for a candidate pitch against other voices.
///
/// A cross-relation occurs when two voices use conflicting chromatic alterations
/// of the same pitch class (e.g., C-natural in one voice, C# in another).
/// Natural half-steps (E/F, B/C) are excluded.
///
/// @param state Current counterpoint state.
/// @param voice_id Voice being scored.
/// @param pitch Candidate pitch.
/// @param tick Current tick.
/// @return Penalty value (0.0 = no cross-relation, 0.3 = cross-relation detected).
static float crossRelationPenalty(const CounterpointState& state,
                                  VoiceId voice_id, uint8_t pitch, Tick tick) {
  int pitch_class = getPitchClass(pitch);
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    // Check the most recent note in the other voice within 1 beat.
    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) {
      // Also check the previous note if no note at tick.
      other_note = state.getLastNote(other);
      if (!other_note) continue;
      // Must be within 1 beat proximity.
      Tick dist = (tick >= other_note->start_tick)
                      ? (tick - other_note->start_tick)
                      : (other_note->start_tick - tick);
      if (dist > kTicksPerBeat) continue;
    }

    int other_pc = getPitchClass(other_note->pitch);
    int pc_diff = std::abs(pitch_class - other_pc);
    // Chromatic conflict: pitch classes differ by 1 semitone (or 11 wrapping).
    if (pc_diff == 1 || pc_diff == 11) {
      // Exclude natural half-steps E/F (4,5) and B/C (11,0).
      int low_pc = (pitch_class < other_pc) ? pitch_class : other_pc;
      int high_pc = (pitch_class < other_pc) ? other_pc : pitch_class;
      if (low_pc == 4 && high_pc == 5) continue;   // E/F natural
      if (low_pc == 0 && high_pc == 11) continue;   // B/C natural (wrapped)
      if (low_pc == 0 && high_pc == 1) continue;    // B#/C = enharmonic, skip
      return 1.0f;
    }
  }
  return 0.0f;
}

/// @brief Compute ascending tritone leap penalty for a candidate pitch.
/// Only penalizes ascending tritone (prev->cand = +6 semitones). Descending
/// tritone is common in Bach bass lines and countersubjects. Compound
/// intervals (abs > 12) are exempt.
static float tritonePenalty(const CounterpointState& state,
                            VoiceId voice_id, uint8_t pitch) {
  const NoteEvent* prev = state.getLastNote(voice_id);
  if (!prev) return 0.0f;
  int leap = static_cast<int>(pitch) - static_cast<int>(prev->pitch);
  if (std::abs(leap) > 12) return 0.0f;  // compound exempt
  if (leap == 6) return 1.0f;            // ascending tritone only
  return 0.0f;
}

/// @brief Count consecutive parallel imperfect consonances (3rds/6ths) between
/// two voices, looking backward from the current position.
///
/// Parallel 3rds and 6ths are natural in Bach, but extended runs (4+ beats)
/// reduce voice independence. This function counts how many consecutive
/// imperfect consonances have occurred in parallel motion.
///
/// @param state Current counterpoint state.
/// @param rules Rule evaluator for motion classification.
/// @param voice_id Voice being evaluated.
/// @param other Voice to compare against.
/// @param tick Current tick position.
/// @return Number of consecutive parallel imperfect consonances (0 if none).
static int countConsecutiveParallelImperfect(const CounterpointState& state,
                                             const IRuleEvaluator& rules,
                                             VoiceId voice_id, VoiceId other,
                                             Tick tick) {
  const auto& self_notes = state.getVoiceNotes(voice_id);
  const auto& other_notes = state.getVoiceNotes(other);
  if (self_notes.size() < 2 || other_notes.size() < 2) return 0;

  int count = 0;
  // Walk backward through recent notes to count parallel imperfect runs.
  // We check pairs of successive note-pairs for parallel motion with
  // imperfect consonant intervals.
  auto self_iter = self_notes.rbegin();
  auto other_iter = other_notes.rbegin();

  // Find latest other note before tick.
  while (other_iter != other_notes.rend() && other_iter->start_tick >= tick) {
    ++other_iter;
  }
  if (other_iter == other_notes.rend()) return 0;

  // Start from the latest self note.
  uint8_t curr_self = self_iter->pitch;
  uint8_t curr_other = other_iter->pitch;

  constexpr int kMaxLookback = 6;
  for (int step = 0; step < kMaxLookback; ++step) {
    ++self_iter;
    ++other_iter;
    if (self_iter == self_notes.rend() || other_iter == other_notes.rend()) break;

    uint8_t prev_self = self_iter->pitch;
    uint8_t prev_other = other_iter->pitch;

    // Check if the current interval is an imperfect consonance.
    int curr_ivl = absoluteInterval(curr_self, curr_other);
    int curr_reduced = interval_util::compoundToSimple(curr_ivl);
    IntervalQuality quality = classifyInterval(curr_reduced);
    if (quality != IntervalQuality::ImperfectConsonance) break;

    // Check if motion is parallel.
    MotionType motion = rules.classifyMotion(prev_self, curr_self,
                                              prev_other, curr_other);
    if (motion != MotionType::Parallel) break;

    ++count;
    curr_self = prev_self;
    curr_other = prev_other;
  }
  return count;
}

/// @brief Compute voice-motion penalty/bonus for a candidate pitch.
///
/// Evaluates the motion type between the candidate voice and each other
/// sounding voice, applying Bach-style preferences:
/// - Parallel P5/P8: heavy penalty (+0.5) -- already rejected by isSafeToPlace,
///   but adds extra scoring for near-miss candidates
/// - Contrary motion: bonus (-0.15) -- most desirable for voice independence
/// - Oblique motion: small bonus (-0.05) -- one voice holds, acceptable
/// - Parallel 3rd/6th: small bonus (-0.1) for first 3 beats, then
///   penalty (+0.1) for extended runs (4+ consecutive)
///
/// @param state Current counterpoint state.
/// @param rules Rule evaluator for motion and interval classification.
/// @param voice_id Voice placing the candidate pitch.
/// @param cand_pitch Candidate MIDI pitch.
/// @param tick Current tick position.
/// @return Aggregate penalty (negative = bonus, positive = penalty).
static float voiceMotionPenalty(const CounterpointState& state,
                                const IRuleEvaluator& rules,
                                VoiceId voice_id, uint8_t cand_pitch,
                                Tick tick) {
  const NoteEvent* prev_self = state.getLastNote(voice_id);
  if (!prev_self) return 0.0f;

  float total_penalty = 0.0f;
  int voice_pair_count = 0;
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    // Find previous note for the other voice before tick.
    const NoteEvent* prev_other = nullptr;
    const auto& other_notes = state.getVoiceNotes(other);
    for (auto iter = other_notes.rbegin(); iter != other_notes.rend(); ++iter) {
      if (iter->start_tick < tick) {
        prev_other = &(*iter);
        break;
      }
    }
    if (!prev_other) continue;

    ++voice_pair_count;

    // Classify the motion this candidate would create.
    MotionType motion = rules.classifyMotion(
        prev_self->pitch, cand_pitch, prev_other->pitch, other_note->pitch);

    // Check the resulting interval.
    int curr_ivl = absoluteInterval(cand_pitch, other_note->pitch);
    int curr_reduced = interval_util::compoundToSimple(curr_ivl);

    switch (motion) {
      case MotionType::Contrary:
        // Contrary motion: most desirable for voice independence.
        total_penalty -= 0.15f;
        break;

      case MotionType::Oblique:
        // Oblique motion: one voice holds -- acceptable.
        total_penalty -= 0.05f;
        break;

      case MotionType::Parallel: {
        // Check if the resulting interval is a perfect consonance.
        if (interval_util::isPerfectConsonance(curr_reduced)) {
          // Parallel P5/P8: heavy penalty (isSafeToPlace rejects these,
          // but this catches near-miss candidates in scoring).
          total_penalty += 0.50f;
        } else {
          IntervalQuality quality = classifyInterval(curr_reduced);
          if (quality == IntervalQuality::ImperfectConsonance) {
            // Parallel 3rds/6ths: check run length.
            int run = countConsecutiveParallelImperfect(
                state, rules, voice_id, other, tick);
            if (run < 3) {
              // Short run: acceptable, small bonus for harmonic flow.
              total_penalty -= 0.10f;
            } else {
              // Extended run (4+ including this note): penalize to
              // encourage voice independence.
              total_penalty += 0.10f;
            }
          }
          // Similar intervals that are dissonant: no special handling
          // (already penalized by consonance checks elsewhere).
        }
        break;
      }

      case MotionType::Similar:
        // Similar motion: same direction, different interval.
        // Neutral -- no bonus or penalty (unless approaching a perfect
        // consonance, which is handled by isSafeToPlace's hidden-perfect check).
        break;
    }
  }

  // Average over voice pairs to avoid over-accumulation in 4-voice textures.
  if (voice_pair_count > 1) {
    total_penalty /= static_cast<float>(voice_pair_count);
  }

  return total_penalty;
}

// ---------------------------------------------------------------------------
// Free-standing parallel / P4-bass checker
// ---------------------------------------------------------------------------

ParallelCheckResult checkParallelsAndP4Bass(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t pitch, Tick tick, uint8_t num_voices) {
  ParallelCheckResult result;
  const auto& voices = state.getActiveVoices();
  VoiceId bass_voice = (num_voices > 0) ? (num_voices - 1) : 0;

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    int ivl = absoluteInterval(pitch, other_note->pitch);

    // P4-bass check.
    int reduced = interval_util::compoundToSimple(ivl);
    if (reduced == interval::kPerfect4th) {
      if (voice_id == bass_voice || other == bass_voice) {
        result.has_p4_bass = true;
        result.conflicting_voice = other;
      }
    }

    // Parallel perfect consonance check.
    const NoteEvent* prev_self = state.getLastNote(voice_id);
    const NoteEvent* prev_other = nullptr;
    const auto& other_notes = state.getVoiceNotes(other);
    for (auto iter = other_notes.rbegin(); iter != other_notes.rend(); ++iter) {
      if (iter->start_tick < tick) {
        prev_other = &(*iter);
        break;
      }
    }

    if (prev_self && prev_other) {
      int prev_ivl = absoluteInterval(prev_self->pitch, prev_other->pitch);
      int prev_reduced = interval_util::compoundToSimple(prev_ivl);
      int curr_reduced = interval_util::compoundToSimple(ivl);

      bool prev_perfect = interval_util::isPerfectConsonance(prev_reduced);
      bool curr_perfect = interval_util::isPerfectConsonance(curr_reduced);

      if (prev_perfect && curr_perfect && prev_reduced == curr_reduced) {
        MotionType motion = rules.classifyMotion(
            prev_self->pitch, pitch,
            prev_other->pitch, other_note->pitch);
        if (motion == MotionType::Parallel || motion == MotionType::Similar) {
          result.has_parallel_perfect = true;
          result.conflicting_voice = other;
        }
      }
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Voice reentry detection
// ---------------------------------------------------------------------------

/// @brief Detect voice reentry: a voice returning after a rest gap.
///
/// A voice reentry occurs when:
/// - The voice has prior notes (not first entry)
/// - There is a gap of at least one full bar between the last note's end
///   and the current tick
/// - The current tick is on a beat boundary
///
/// @param state Counterpoint state.
/// @param voice_id Voice to check.
/// @param tick Current tick.
/// @return True if this is a voice reentry moment.
static bool isVoiceReentry(const CounterpointState& state,
                           VoiceId voice_id, Tick tick) {
  const NoteEvent* last = state.getLastNote(voice_id);
  if (!last) return true;  // First entry ever.
  // Check that current tick is on a beat boundary.
  if (tick % kTicksPerBeat != 0) return false;
  // Eighth-note rests and shorter are normal articulation.
  // Quarter-rest or longer signals a voice gap where melodic context is lost.
  Tick last_end = last->start_tick + last->duration;
  return (last_end + kTicksPerBeat <= tick);
}

// ---------------------------------------------------------------------------
// Safety check
// ---------------------------------------------------------------------------

bool CollisionResolver::isSafeToPlace(const CounterpointState& state,
                                      const IRuleEvaluator& rules,
                                      VoiceId voice_id, uint8_t pitch,
                                      Tick tick, Tick /*duration*/,
                                      std::optional<uint8_t> next_pitch,
                                      int adjacent_spacing_limit) const {
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    // Tiered consonance check by metric position.
    // Bar start (beat 1): reject all dissonances.
    // Beats 2-4: reject only harsh dissonances (m2/TT/M7) to allow the natural
    // ~25-34% dissonance ratio found in Bach organ fugues (reference mean=0.746).
    bool is_on_beat = (tick % kTicksPerBeat == 0);
    bool is_bar_start = (tick % kTicksPerBar == 0);
    int ivl = absoluteInterval(pitch, other_note->pitch);
    if (is_on_beat && !rules.isIntervalConsonant(ivl, is_bar_start)) {
      if (is_bar_start) {
        // Beat 1: reject all dissonances unconditionally.
        return false;
      }
      // Beats 2-4: reject only harsh dissonances (m2=1, TT=6, M7=11).
      int reduced = interval_util::compoundToSimple(ivl);
      if (reduced == 1 || reduced == 6 || reduced == 11) {
        return false;
      }
      // Mild dissonances (M2=2, P4=5, m7=10) are allowed on beats 2-4 as
      // they occur naturally in Bach's counterpoint.
    }

    // P4 involving the bass voice is dissonant in Baroque practice, even when
    // the rule evaluator treats P4 as consonant (3+ voice context).  Only P4
    // between two upper voices is acceptable.
    if (is_on_beat && rules.isIntervalConsonant(ivl, is_bar_start)) {
      int reduced = interval_util::compoundToSimple(ivl);
      if (reduced == interval::kPerfect4th) {
        const auto& active = state.getActiveVoices();
        // Bass voice = highest voice_id (last in registration order).
        VoiceId bass_voice = active.empty() ? 0 : active.back();
        if (voice_id == bass_voice || other == bass_voice) {
          // Reject unless justified as a non-harmonic tone (passing/neighbor).
          bool p4_bass_allowed = false;
          // Voice reentry: no NHT exemption for P4-bass.
          // Reuse downbeat logic: no NHT exemption on beat 1.
          Tick p4_beat_in_bar = (tick % kTicksPerBar) / kTicksPerBeat;
          bool p4_is_downbeat = (p4_beat_in_bar == 0);
          if (!p4_is_downbeat && !isVoiceReentry(state, voice_id, tick)) {
            if (next_pitch.has_value()) {
              const NoteEvent* prev_self = state.getLastNote(voice_id);
              if (prev_self) {
                SpeciesRules species_rules(SpeciesType::Fifth);
                bool is_passing = species_rules.isValidPassingTone(
                    prev_self->pitch, pitch, *next_pitch);
                bool is_neighbor = species_rules.isValidNeighborTone(
                    prev_self->pitch, pitch, *next_pitch);
                if (is_passing || is_neighbor) p4_bass_allowed = true;
              }
            }
          }
          if (!p4_bass_allowed) return false;
        }
      }
    }

    // Build a temporary state to check parallels.
    // Simulate adding the note and check for parallel perfects.
    const NoteEvent* prev_self = state.getLastNote(voice_id);
    const NoteEvent* prev_other = nullptr;

    // Find previous note for the other voice before tick.
    const auto& other_notes = state.getVoiceNotes(other);
    for (auto iter = other_notes.rbegin(); iter != other_notes.rend();
         ++iter) {
      if (iter->start_tick < tick) {
        prev_other = &(*iter);
        break;
      }
    }

    if (prev_self && prev_other) {
      int prev_ivl = absoluteInterval(prev_self->pitch, prev_other->pitch);
      int curr_ivl = ivl;

      // Check parallel perfect consonances.
      int prev_reduced = interval_util::compoundToSimple(prev_ivl);
      int curr_reduced = interval_util::compoundToSimple(curr_ivl);

      bool prev_perfect = interval_util::isPerfectConsonance(prev_reduced);
      bool curr_perfect = interval_util::isPerfectConsonance(curr_reduced);

      if (prev_perfect && curr_perfect && prev_reduced == curr_reduced) {
        MotionType motion = rules.classifyMotion(
            prev_self->pitch, pitch,
            prev_other->pitch, other_note->pitch);
        if (motion == MotionType::Parallel ||
            motion == MotionType::Similar) {
          return false;
        }
      }

      // Hidden perfect: approaching a perfect consonance via similar motion.
      // Outer voice pair on strong beat: flag if either voice leaps (>2st).
      // Inner voice pairs or weak beat: keep original threshold (>7st both).
      if (curr_perfect) {
        int dir_a = static_cast<int>(pitch) - static_cast<int>(prev_self->pitch);
        int dir_b = static_cast<int>(other_note->pitch) -
                    static_cast<int>(prev_other->pitch);
        if (dir_a != 0 && dir_b != 0 && (dir_a > 0) == (dir_b > 0)) {
          bool a_outer =
              voices.empty() || voice_id == voices.front() || voice_id == voices.back();
          bool b_outer =
              voices.empty() || other == voices.front() || other == voices.back();
          bool landing_on_strong = isStrongBeat(tick);

          if (a_outer && b_outer && landing_on_strong) {
            // Outer voice pair on strong beat: flag if either voice leaps (>2st).
            int leading_leap = std::max(std::abs(dir_a), std::abs(dir_b));
            if (leading_leap > 2) {
              return false;
            }
          } else {
            // Inner voices or weak beat: keep original threshold (>7st both).
            if (std::abs(dir_a) > 7 && std::abs(dir_b) > 7) {
              return false;
            }
          }
        }
      }
    }
  }

  // Melodic leap constraint (Bach's practice):
  // - Max leap = octave (12 semitones)
  // - Tritone (6 semitones) forbidden in melodic context
  const NoteEvent* melodic_prev = state.getLastNote(voice_id);
  if (melodic_prev) {
    int leap = absoluteInterval(pitch, melodic_prev->pitch);
    if (leap > 12) return false;  // > octave forbidden
    int leap_class = leap % 12;
    if (leap_class == 6) return false;  // tritone forbidden
  }

  // Check voice crossing.
  if (wouldCrossVoice(state, voice_id, pitch, tick)) {
    return false;
  }

  // Minimum adjacent voice spacing on strong beats (Baroque practice).
  // BachRuleEvaluator treats this as a soft penalty (not rejection).
  // FuxRuleEvaluator treats this as a hard rejection.
  if (rules.isStrictSpacing()) {
    bool is_strong_beat = (tick % kTicksPerBeat == 0);
    if (is_strong_beat) {
      for (VoiceId other : voices) {
        if (other == voice_id) continue;
        const NoteEvent* other_note = state.getNoteAt(other, tick);
        if (!other_note) continue;
        // Only check adjacent voices (voice IDs differ by 1).
        int voice_dist = std::abs(static_cast<int>(voice_id) -
                                  static_cast<int>(other));
        if (voice_dist != 1) continue;
        int pitch_dist = absoluteInterval(pitch, other_note->pitch);
        if (pitch_dist > 0 && pitch_dist < 3) return false;  // < minor 3rd
      }
    }
  }

  // Maximum spacing for adjacent manual voices (all evaluators).
  // Does not apply to pedal (last voice) since pedal-manual gap is naturally
  // wide in organ writing. Default threshold: octave + M2 = 14 semitones for
  // adjacent voices. Immutable/Structural notes use 19 (octave + P5) to avoid
  // dropping structural material.
  {
    VoiceId bass_voice = voices.empty() ? 0 : voices.back();
    for (VoiceId other : voices) {
      if (other == voice_id) continue;
      int voice_dist = std::abs(static_cast<int>(voice_id) -
                                static_cast<int>(other));
      if (voice_dist != 1) continue;
      // Skip pedal-manual pairs.
      if (voice_id == bass_voice || other == bass_voice) continue;
      const NoteEvent* adj_note = state.getNoteAt(other, tick);
      if (!adj_note) continue;
      int pitch_dist = absoluteInterval(pitch, adj_note->pitch);
      if (pitch_dist > adjacent_spacing_limit) return false;
    }
  }

  // Check voice range.
  const auto* range = state.getVoiceRange(voice_id);
  if (range && (pitch < range->low || pitch > range->high)) {
    // Outside range is allowed (soft penalty) but we flag it as unsafe
    // if it is more than an octave outside.
    int distance = 0;
    if (pitch < range->low) {
      distance = range->low - pitch;
    } else {
      distance = pitch - range->high;
    }
    if (distance > range_tolerance_) return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Strategy cascade
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::tryStrategy(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    const std::string& strategy, std::optional<uint8_t> next_pitch) const {
  PlacementResult result;
  result.strategy = strategy;

  if (strategy == "original") {
    if (isSafeToPlace(state, rules, voice_id, desired_pitch, tick,
                      duration, next_pitch)) {
      // Soft cross-relation gate: if the original pitch creates a chromatic
      // conflict with another voice, skip it so that chord_tone / step_shift
      // can find a non-conflicting alternative.
      if (crossRelationPenalty(state, voice_id, desired_pitch, tick) > 0.0f) {
        return result;  // result.accepted remains false → cascade continues
      }
      result.pitch = desired_pitch;
      result.penalty = 0.0f;
      result.accepted = true;
    }
    return result;
  }

  if (strategy == "chord_tone") {
    float best_penalty = 2.0f;

    // Helper lambda: evaluate a candidate pitch and update best result.
    // Applies Bach-style consonance priority: imperfect consonances (3rd, 6th)
    // are preferred over perfect consonances (5th, octave) to encourage richer
    // harmonic writing and reduce parallel-perfect risk.
    auto evaluateCandidate = [&](uint8_t cand_pitch, float base_penalty) {
      if (!isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                         duration, next_pitch))
        return;

      float penalty = base_penalty;
      penalty += crossRelationPenalty(state, voice_id, cand_pitch, tick);

      // Consonance quality scoring: prefer imperfect consonances over perfect.
      // Bach's counterpoint uses 3rds and 6ths far more than 5ths and octaves.
      // This bonus/penalty applies to vertical intervals with all sounding voices.
      bool strong_beat = isStrongBeat(tick);
      for (VoiceId other_v : state.getActiveVoices()) {
        if (other_v == voice_id) continue;
        const NoteEvent* sounding = state.getNoteAt(other_v, tick);
        if (!sounding) continue;
        int sounding_ivl = absoluteInterval(cand_pitch, sounding->pitch);
        int reduced_ivl = interval_util::compoundToSimple(sounding_ivl);
        IntervalQuality quality = classifyInterval(reduced_ivl);
        if (quality == IntervalQuality::ImperfectConsonance) {
          // Imperfect consonances (m3, M3, m6, M6): preferred in Bach style.
          penalty -= 0.15f;
        } else if (quality == IntervalQuality::PerfectConsonance && reduced_ivl != 0) {
          // Perfect 5th / octave: acceptable but carries parallel-motion risk.
          // Unison (0) is excluded from penalty since it has its own constraints.
          if (strong_beat) {
            penalty += 0.08f;
          }
        }
      }

      // Voice spacing penalty: penalize wide gaps between adjacent manual voices.
      for (VoiceId adj_v = 0; adj_v < state.voiceCount(); ++adj_v) {
        if (adj_v == voice_id) continue;
        int vdist = std::abs(static_cast<int>(voice_id) - static_cast<int>(adj_v));
        if (vdist != 1) continue;
        if (adj_v == state.voiceCount() - 1 && state.voiceCount() >= 3) continue;
        if (voice_id == state.voiceCount() - 1 && state.voiceCount() >= 3) continue;
        const NoteEvent* adj_note = state.getNoteAt(adj_v, tick);
        if (!adj_note) adj_note = state.getLastNote(adj_v);
        if (!adj_note) continue;
        int spacing = absoluteInterval(cand_pitch, adj_note->pitch);
        if (spacing > 12) {
          penalty += static_cast<float>(spacing - 12) / 7.0f * 0.3f;
        }
      }

      penalty += tritonePenalty(state, voice_id, cand_pitch);

      // Voice motion scoring: reward contrary/oblique motion, penalize
      // parallel perfect consonances and extended parallel imperfect runs.
      penalty += voiceMotionPenalty(state, rules, voice_id, cand_pitch, tick);

      // Clamp penalty floor to prevent negative penalties from over-accumulating.
      if (penalty < 0.0f) penalty = 0.0f;

      if (penalty < best_penalty) {
        best_penalty = penalty;
        result.pitch = cand_pitch;
        result.penalty = penalty;
        result.accepted = true;
      }
    };

    if (harmonic_timeline_) {
      // Chord-tone-aware strategy: use actual chord tones from the timeline.
      const HarmonicEvent& event = harmonic_timeline_->getAt(tick);

      // 1. Try nearestChordTone(desired_pitch) -- minimal displacement.
      uint8_t nearest = nearestChordTone(desired_pitch, event);
      float dist_penalty = static_cast<float>(
          std::abs(static_cast<int>(nearest) - static_cast<int>(desired_pitch))) /
          12.0f * 0.3f;
      evaluateCandidate(nearest, dist_penalty);

      // 2. Try all chord tones across octaves (closest to desired_pitch first).
      int root_pc = getPitchClass(event.chord.root_pitch);
      // Build chord pitch classes (root, 3rd, 5th, optional 7th).
      std::vector<int> chord_pcs;
      chord_pcs.push_back(root_pc);
      // Use isChordTone to determine the full set, but build it from root.
      for (int i = 1; i < 12; ++i) {
        int pc = (root_pc + i) % 12;
        // Build a test pitch in octave 5 to check.
        uint8_t test_pitch = static_cast<uint8_t>(60 + pc);
        if (isChordTone(test_pitch, event)) {
          chord_pcs.push_back(pc);
        }
      }

      // Generate candidates sorted by distance to desired_pitch.
      struct ChordCandidate {
        uint8_t pitch;
        int distance;
      };
      std::vector<ChordCandidate> candidates;
      int base_octave = (static_cast<int>(desired_pitch) / 12) * 12;
      for (int pc : chord_pcs) {
        for (int oct_offset = -24; oct_offset <= 24; oct_offset += 12) {
          int cand = base_octave + pc + oct_offset;
          if (cand < 0 || cand > 127) continue;
          int dist = std::abs(cand - static_cast<int>(desired_pitch));
          candidates.push_back({static_cast<uint8_t>(cand), dist});
        }
      }
      std::sort(candidates.begin(), candidates.end(),
                [](const ChordCandidate& a, const ChordCandidate& b) {
                  return a.distance < b.distance;
                });

      for (const auto& c : candidates) {
        float penalty = static_cast<float>(c.distance) / 12.0f * 0.3f;
        evaluateCandidate(c.pitch, penalty);
      }

      // 3. Try diatonic tones (non-chord but scale tones).
      if (!result.accepted) {
        for (int delta = 1; delta <= 7; ++delta) {
          for (int sign = -1; sign <= 1; sign += 2) {
            int cand = static_cast<int>(desired_pitch) + delta * sign;
            if (cand < 0 || cand > 127) continue;
            if (isDiatonicInKey(cand, event.key, event.is_minor) &&
                !isChordTone(static_cast<uint8_t>(cand), event)) {
              float penalty = static_cast<float>(delta) / 12.0f * 0.3f + 0.1f;
              evaluateCandidate(static_cast<uint8_t>(cand), penalty);
            }
          }
          if (result.accepted) break;
        }
      }
    }

    // 4. Fallback: consonant intervals (legacy behavior, or if timeline not set).
    // Imperfect consonances (3rd, 6th) are tried before perfect consonances
    // (5th, octave, unison) to match Bach's preference for richer harmony.
    if (!result.accepted) {
      // Imperfect consonances first: m3, M3, m6, M6.
      static constexpr int kImperfectFirst[] = {3, 4, 8, 9, 0, 7, 12};
      std::vector<int> consonant_offsets;
      for (int ivl : kImperfectFirst) {
        consonant_offsets.push_back(ivl);
        if (ivl != 0) consonant_offsets.push_back(-ivl);
      }
      for (int offset : consonant_offsets) {
        int candidate = static_cast<int>(desired_pitch) + offset;
        if (candidate < 0 || candidate > 127) continue;
        auto cand_pitch = static_cast<uint8_t>(candidate);
        float penalty = static_cast<float>(std::abs(offset)) / 12.0f * 0.3f;
        evaluateCandidate(cand_pitch, penalty);
      }
    }

    return result;
  }

  if (strategy == "step_shift") {
    float best_penalty = 2.0f;

    // Cadence-aware voice leading: when near a cadence tick, compute a bonus
    // for leading-tone resolution (pitch % 12 == 11 resolving up by semitone)
    // or dominant 7th resolution (resolving stepwise down).
    bool near_cadence = !cadence_ticks_.empty() && isNearCadence(tick, cadence_ticks_);
    const NoteEvent* prev_note = state.getLastNote(voice_id);
    int prev_pitch_class = -1;
    if (prev_note) {
      prev_pitch_class = getPitchClass(prev_note->pitch);
    }

    // Harmonic context for chord-tone and diatonic awareness.
    bool strong_beat = isStrongBeat(tick);
    const HarmonicEvent* h_event = nullptr;
    if (harmonic_timeline_) {
      h_event = &harmonic_timeline_->getAt(tick);
    }

    for (int delta = 1; delta <= max_search_range_; ++delta) {
      // Try both directions: up and down.
      for (int sign = -1; sign <= 1; sign += 2) {
        int candidate = static_cast<int>(desired_pitch) + delta * sign;
        if (candidate < 0 || candidate > 127) continue;

        auto cand_pitch = static_cast<uint8_t>(candidate);
        if (isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                          duration, next_pitch)) {
          float penalty = static_cast<float>(delta) / 12.0f * 0.5f;

          // Apply cross-relation penalty.
          penalty += crossRelationPenalty(state, voice_id, cand_pitch, tick);

          // Apply ascending tritone penalty.
          penalty += tritonePenalty(state, voice_id, cand_pitch);

          // Chord-tone and diatonic penalties when harmonic context is available.
          if (h_event) {
            if (strong_beat && isChordTone(cand_pitch, *h_event)) {
              // Strong beat chord tone bonus.
              penalty -= 0.3f;
            }
            if (!isDiatonicInKey(cand_pitch, h_event->key, h_event->is_minor)) {
              // Non-diatonic penalty: stronger on strong beats.
              penalty += strong_beat ? 0.3f : 0.1f;
            }
          }

          // Consonance quality preference: favor imperfect consonances (3rd, 6th)
          // over perfect consonances (5th, octave) in step_shift candidates.
          // This encourages richer harmonic writing characteristic of Bach.
          for (VoiceId other_v : state.getActiveVoices()) {
            if (other_v == voice_id) continue;
            const NoteEvent* sounding = state.getNoteAt(other_v, tick);
            if (!sounding) continue;
            int sounding_ivl = absoluteInterval(cand_pitch, sounding->pitch);
            int reduced_ivl = interval_util::compoundToSimple(sounding_ivl);
            IntervalQuality quality = classifyInterval(reduced_ivl);
            if (quality == IntervalQuality::ImperfectConsonance) {
              penalty -= 0.1f;  // Imperfect consonances preferred.
            } else if (quality == IntervalQuality::PerfectConsonance &&
                       reduced_ivl != 0 && strong_beat) {
              penalty += 0.05f;  // Perfect consonances carry parallel risk on strong beats.
            }
          }

          // Voice motion scoring: reward contrary/oblique motion, penalize
          // parallel perfect consonances and extended parallel imperfect runs.
          penalty += voiceMotionPenalty(state, rules, voice_id, cand_pitch, tick);

          // Apply melodic quality penalty via MelodicContext scoring.
          if (prev_note) {
            MelodicContext mel_ctx = buildMelodicContextFromState(state, voice_id);
            float melodic_score = MelodicContext::scoreMelodicQuality(
                mel_ctx, cand_pitch);
            // Invert: high melodic quality -> low penalty.
            penalty += (1.0f - melodic_score) * 0.3f;

            // Leap resolution priority: favor candidates that resolve a pending leap.
            if (mel_ctx.leap_needs_resolution) {
              int cand_interval = absoluteInterval(cand_pitch, prev_note->pitch);
              int cand_dir = (cand_pitch > prev_note->pitch) ? 1 : -1;
              if (cand_interval >= 1 && cand_interval <= 2 &&
                  cand_dir != mel_ctx.prev_direction) {
                penalty -= 0.25f;
              }
            }

            // Voice spacing penalty: penalize wide gaps between adjacent manual voices.
            for (VoiceId adj_v = 0; adj_v < state.voiceCount(); ++adj_v) {
              if (adj_v == voice_id) continue;
              int vdist =
                  std::abs(static_cast<int>(voice_id) - static_cast<int>(adj_v));
              if (vdist != 1) continue;
              if (adj_v == state.voiceCount() - 1 && state.voiceCount() >= 3)
                continue;
              if (voice_id == state.voiceCount() - 1 && state.voiceCount() >= 3)
                continue;
              const NoteEvent* adj_note = state.getNoteAt(adj_v, tick);
              if (!adj_note) adj_note = state.getLastNote(adj_v);
              if (!adj_note) continue;
              int spacing = absoluteInterval(cand_pitch, adj_note->pitch);
              if (spacing > 12) {
                penalty += static_cast<float>(spacing - 12) / 7.0f * 0.3f;
              }
            }
          }

          // Clamp penalty floor.
          if (penalty < 0.0f) penalty = 0.0f;

          // Apply cadence voice-leading bonus.
          if (near_cadence && prev_note) {
            int directed = static_cast<int>(cand_pitch) - static_cast<int>(prev_note->pitch);

            // Leading tone (B in C major, pitch class 11) resolving up by semitone.
            if (prev_pitch_class == 11 && directed == 1) {
              penalty -= 0.3f;
              if (penalty < 0.0f) penalty = 0.0f;
            }

            // 7th of V7 chord resolving stepwise downward (-1 or -2 semitones).
            // The 7th of a V7 in C major is F (pitch class 5).
            // More generally, any pitch that was the 7th resolving down.
            if (prev_pitch_class == 5 && (directed == -1 || directed == -2)) {
              penalty -= 0.3f;
              if (penalty < 0.0f) penalty = 0.0f;
            }
          }

          if (penalty < best_penalty) {
            best_penalty = penalty;
            result.pitch = cand_pitch;
            result.penalty = penalty;
            result.accepted = true;
          }
        }
      }
      // Stop as soon as we find a valid pitch at this distance.
      if (result.accepted) return result;
    }
    return result;
  }

  if (strategy == "octave_shift") {
    // Try octave shifts, preferring the direction that minimizes adjacent voice spacing.
    auto computeMinAdjacentSpacing = [&](uint8_t pitch) -> int {
      int min_spacing = 127;
      for (VoiceId adj_v = 0; adj_v < state.voiceCount(); ++adj_v) {
        if (adj_v == voice_id) continue;
        int vdist =
            std::abs(static_cast<int>(voice_id) - static_cast<int>(adj_v));
        if (vdist != 1) continue;
        const NoteEvent* adj_note = state.getLastNote(adj_v);
        if (!adj_note) continue;
        int spc = absoluteInterval(pitch, adj_note->pitch);
        if (spc < min_spacing) min_spacing = spc;
      }
      return min_spacing;
    };

    int up_pitch = static_cast<int>(desired_pitch) + 12;
    int dn_pitch = static_cast<int>(desired_pitch) - 12;
    int up_spacing =
        (up_pitch <= 127)
            ? computeMinAdjacentSpacing(static_cast<uint8_t>(up_pitch))
            : 127;
    int dn_spacing =
        (dn_pitch >= 0)
            ? computeMinAdjacentSpacing(static_cast<uint8_t>(dn_pitch))
            : 127;
    int first = (up_spacing <= dn_spacing) ? 12 : -12;
    int second = (first == 12) ? -12 : 12;

    for (int shift : {first, second}) {
      int candidate = static_cast<int>(desired_pitch) + shift;
      if (candidate < 0 || candidate > 127) continue;

      auto cand_pitch = static_cast<uint8_t>(candidate);
      if (!isSafeToPlace(state, rules, voice_id, cand_pitch, tick, duration,
                         next_pitch))
        continue;

      // Reject if the octave shift would cross an adjacent voice.
      if (wouldCrossVoice(state, voice_id, cand_pitch, tick)) continue;

      result.pitch = cand_pitch;
      result.penalty = 0.7f;
      result.accepted = true;
      return result;
    }
    return result;
  }

  if (strategy == "rest") {
    // Last resort: give up.
    result.pitch = 0;
    result.penalty = 1.0f;
    result.accepted = false;
    return result;
  }

  return result;
}

PlacementResult CollisionResolver::findSafePitch(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick,
    Tick duration, std::optional<uint8_t> next_pitch) const {
  // 6-stage strategy cascade (suspension inserted between chord_tone and step_shift).
  static const char* kStrategies[] = {
      "original", "chord_tone", "suspension", "step_shift", "octave_shift", "rest"};

  for (const char* strategy : kStrategies) {
    // The suspension strategy uses a dedicated method.
    if (std::string(strategy) == "suspension") {
      PlacementResult result =
          trySuspension(state, rules, voice_id, desired_pitch, tick, duration);
      if (result.accepted) return result;
      continue;
    }

    PlacementResult result = tryStrategy(state, rules, voice_id,
                                         desired_pitch, tick, duration,
                                         strategy, next_pitch);
    if (result.accepted) return result;
  }

  // If all strategies fail, return a rest result.
  PlacementResult rest;
  rest.pitch = 0;
  rest.penalty = 1.0f;
  rest.strategy = "rest";
  rest.accepted = false;
  return rest;
}

// ---------------------------------------------------------------------------
// Source-aware strategy cascade
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::findSafePitch(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    BachNoteSource source, std::optional<uint8_t> next_pitch) const {
  ProtectionLevel level = getProtectionLevel(source);

  // Select allowed strategies based on protection level.
  struct StrategyList {
    const char** strategies;
    int count;
  };

  static const char* kImmutable[] = {"original", "rest"};
  static const char* kStructural[] = {"original", "octave_shift", "rest"};
  static const char* kFlexible[] = {
      "original", "chord_tone", "suspension", "step_shift", "octave_shift", "rest"};

  StrategyList list;
  switch (level) {
    case ProtectionLevel::Immutable:
      list = {kImmutable, 2};
      break;
    case ProtectionLevel::SemiImmutable:
      list = {kStructural, 3};
      break;
    case ProtectionLevel::Structural:
      list = {kStructural, 3};
      break;
    case ProtectionLevel::Flexible:
      list = {kFlexible, 6};
      break;
  }

  // Immutable/Structural notes use relaxed adjacent spacing (19 semitones)
  // to prevent dropping structural material like fugue subjects.
  int spacing_limit = (level != ProtectionLevel::Flexible) ? 19 : 14;

  // Leap resolution soft gate: when the previous interval was a leap (>=5
  // semitones) and the original candidate does not resolve it by step, defer
  // to the cascade (chord_tone/step_shift) to find a resolving alternative.
  // Falls back to original if no cascade candidate resolves the leap.
  bool apply_leap_gate = false;
  if (level == ProtectionLevel::Flexible &&
      source != BachNoteSource::EpisodeMaterial &&
      source != BachNoteSource::ArpeggioFlow) {
    // One-sided cadence exemption: only exempt ticks in [cadence - kTicksPerBeat, cadence).
    bool near_cadence_before = false;
    for (Tick cad : cadence_ticks_) {
      if (tick < cad && cad - tick <= kTicksPerBeat) {
        near_cadence_before = true;
        break;
      }
    }
    if (!near_cadence_before) {
      const auto& voice_notes = state.getVoiceNotes(voice_id);
      if (voice_notes.size() >= 2) {
        auto iter = voice_notes.rbegin();
        uint8_t last_pitch = iter->pitch;
        ++iter;
        int prev_leap = static_cast<int>(last_pitch) -
                        static_cast<int>(iter->pitch);
        if (std::abs(prev_leap) >= 5) {
          int res = static_cast<int>(desired_pitch) -
                    static_cast<int>(last_pitch);
          // Step (1-2 semitones) in any direction counts as resolution.
          bool resolves = (std::abs(res) >= 1 && std::abs(res) <= 2);
          apply_leap_gate = !resolves;
        }
      }
    }
  }

  PlacementResult soft_fallback;
  bool has_soft_fallback = false;

  for (int idx = 0; idx < list.count; ++idx) {
    // The suspension strategy uses a dedicated method.
    if (std::string(list.strategies[idx]) == "suspension") {
      PlacementResult result =
          trySuspension(state, rules, voice_id, desired_pitch, tick, duration);
      if (result.accepted) return result;
      continue;
    }

    // For "original" strategy with Immutable/Structural sources, call
    // isSafeToPlace directly with relaxed spacing to avoid note drops.
    if (std::string(list.strategies[idx]) == "original" &&
        level != ProtectionLevel::Flexible) {
      if (isSafeToPlace(state, rules, voice_id, desired_pitch, tick,
                         duration, next_pitch, spacing_limit)) {
        PlacementResult result;
        result.pitch = desired_pitch;
        result.penalty = 0.0f;
        result.strategy = "original";
        result.accepted = true;
        return result;
      }
      continue;
    }

    PlacementResult result = tryStrategy(state, rules, voice_id,
                                         desired_pitch, tick, duration,
                                         list.strategies[idx], next_pitch);
    if (result.accepted) {
      // Leap resolution gate: when the previous note was a leap and the
      // original pitch does not resolve it, save as fallback and let the
      // cascade try to find a resolving alternative.
      if (apply_leap_gate && std::string(list.strategies[idx]) == "original") {
        soft_fallback = result;
        has_soft_fallback = true;
        leap_gate_triggered_++;
        continue;  // Try cascade strategies for leap resolution.
      }

      // Repetition gate: reject "original" if it would create a run of 3+
      // same-pitch notes for FreeCounterpoint. Forces cascade fallthrough to
      // chord_tone / step_shift for a melodic alternative.
      if (std::string(list.strategies[idx]) == "original" &&
          source == BachNoteSource::FreeCounterpoint) {
        const auto& voice_notes = state.getVoiceNotes(voice_id);
        constexpr Tick kRunGapThreshold = 2 * kTicksPerBar;
        int run_length = 0;
        Tick ref_tick = tick;
        for (auto it = voice_notes.rbegin(); it != voice_notes.rend(); ++it) {
          if (it->pitch != result.pitch) break;
          Tick note_end = it->start_tick + it->duration;
          if (ref_tick > note_end && (ref_tick - note_end) > kRunGapThreshold)
            break;
          ++run_length;
          ref_tick = it->start_tick;
        }
        if (run_length >= 2) continue;  // Would be 3+ same pitch; reject.
      }
      return result;
    }
  }

  // Cascade failed to find a resolving alternative. Use the original pitch
  // as fallback to prevent note drops.
  if (has_soft_fallback) {
    leap_gate_fallback_used_++;
    return soft_fallback;
  }

  PlacementResult rest;
  rest.pitch = 0;
  rest.penalty = 1.0f;
  rest.strategy = "rest";
  rest.accepted = false;
  return rest;
}

// ---------------------------------------------------------------------------
// Suspension strategy
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::trySuspension(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t /*desired_pitch*/, Tick tick,
    Tick /*duration*/) const {
  PlacementResult result;
  result.strategy = "suspension";

  // A suspension requires a previous pitch to hold.
  if (tick < kTicksPerBeat) return result;

  // Find the pitch this voice had on the previous beat.
  Tick prev_tick = tick - kTicksPerBeat;
  const NoteEvent* prev_note = state.getNoteAt(voice_id, prev_tick);
  if (!prev_note) return result;

  // Guard: reject suspension if it would create 3+ consecutive same pitches.
  // PedalPoint is exempt -- pedal points sustain the same pitch by design.
  {
    const auto& vn = state.getVoiceNotes(voice_id);
    if (vn.size() >= 2) {
      auto iter = vn.rbegin();
      uint8_t last = iter->pitch;
      ++iter;
      uint8_t second_last = iter->pitch;
      if (second_last == last && last == prev_note->pitch) {
        // Already 2 consecutive same pitches matching the suspension pitch;
        // holding would create a 3rd. Reject unless this is a pedal voice.
        // Check if any of the recent notes are PedalPoint source.
        bool is_pedal = false;
        for (auto pit = vn.rbegin(); pit != vn.rend(); ++pit) {
          if (pit->source == BachNoteSource::PedalPoint) {
            is_pedal = true;
            break;
          }
          // Only check last few notes.
          if (std::distance(vn.rbegin(), pit) >= 3) break;
        }
        if (!is_pedal) return result;
      }
    }
  }

  uint8_t held_pitch = prev_note->pitch;

  // --- Preparation check ---
  // A valid suspension requires the held pitch to have been consonant on the
  // previous beat (the preparation phase).  Without consonant preparation,
  // the dissonance is unprepared and violates Baroque counterpoint practice.
  {
    const auto& voices_prep = state.getActiveVoices();
    bool preparation_consonant = true;
    bool has_other_voice_at_prev = false;
    for (VoiceId other : voices_prep) {
      if (other == voice_id) continue;
      const NoteEvent* other_at_prev = state.getNoteAt(other, prev_tick);
      if (!other_at_prev) continue;
      has_other_voice_at_prev = true;
      int prep_ivl = absoluteInterval(held_pitch, other_at_prev->pitch);
      int prep_reduced = interval_util::compoundToSimple(prep_ivl);
      if (!interval_util::isConsonance(prep_reduced)) {
        preparation_consonant = false;
        break;
      }
    }
    // If no other voice was sounding at prev_tick, we cannot verify preparation.
    // Reject: a suspension without audible harmonic context is meaningless.
    if (!has_other_voice_at_prev || !preparation_consonant) return result;
  }

  // Check if holding this pitch creates a dissonance with other voices,
  // and whether stepping down by 1 or 2 semitones resolves it consonantly.
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    int ivl = absoluteInterval(held_pitch, other_note->pitch);
    bool is_strong = (tick % kTicksPerBeat == 0);
    if (!rules.isIntervalConsonant(ivl, is_strong)) {
      // Classify the suspension type by the dissonant interval (simple).
      // Bach's standard suspensions: 4-3 (reduced 5), 7-6 (reduced 10),
      // 9-8 (reduced 2/14). Other dissonant intervals are less idiomatic.
      int reduced_ivl = interval_util::compoundToSimple(ivl);
      float type_penalty = 0.0f;
      if (reduced_ivl == interval::kPerfect4th ||   // 4-3 suspension
          reduced_ivl == interval::kMinor2nd ||      // 9-8 suspension (compound)
          reduced_ivl == interval::kMajor2nd) {      // 9-8 or 2-1 suspension
        type_penalty = 0.0f;  // Standard suspension types: no extra penalty.
      } else if (reduced_ivl == interval::kMinor7th ||  // 7-6 suspension
                 reduced_ivl == interval::kMajor7th) {
        type_penalty = 0.0f;  // 7-6 is also standard.
      } else {
        type_penalty = 0.1f;  // Non-standard suspension: small extra penalty.
      }

      // --- Resolution check ---
      // Verify the resolution: step down from held_pitch should be consonant.
      // Standard resolution is downward by step (1-2 semitones).
      for (int step = 1; step <= 2; ++step) {
        int resolution_pitch = static_cast<int>(held_pitch) - step;
        if (resolution_pitch < 0) continue;

        int res_ivl = std::abs(resolution_pitch -
                               static_cast<int>(other_note->pitch));
        // Resolution interval must be consonant (check as if on a weak beat
        // since resolutions typically land on the weak part of the beat).
        if (rules.isIntervalConsonant(res_ivl, false)) {
          result.pitch = held_pitch;
          result.penalty = 0.15f + type_penalty;
          result.accepted = true;
          return result;
        }
      }
    }
  }

  // Even without dissonance, do not use suspension strategy --
  // if the held pitch is consonant, "original" or "chord_tone" suffices.
  return result;
}

// ---------------------------------------------------------------------------
// Voice crossing check for octave shift
// ---------------------------------------------------------------------------

bool CollisionResolver::wouldCrossVoice(const CounterpointState& state,
                                        VoiceId voice_id, uint8_t pitch,
                                        Tick tick) const {
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    // Determine voice ordering from registration order.
    // Lower voice_id = higher voice (soprano=0, alto=1, tenor=2, bass=3).
    if (voice_id < other) {
      // We are a higher voice; our pitch must not go below the other.
      if (pitch < other_note->pitch) return true;
    } else {
      // We are a lower voice; our pitch must not go above the other.
      if (pitch > other_note->pitch) return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Pedal resolver
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::resolvePedal(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick,
    Tick duration) const {
  // Same cascade as findSafePitch, but add voice-range penalty.
  PlacementResult result =
      findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);

  if (result.accepted) {
    // Add range penalty if the final pitch is outside the voice range.
    const auto* range = state.getVoiceRange(voice_id);
    if (range) {
      float range_penalty = 0.0f;
      if (result.pitch < range->low) {
        range_penalty =
            static_cast<float>(range->low - result.pitch) * 0.05f;
      } else if (result.pitch > range->high) {
        range_penalty =
            static_cast<float>(result.pitch - range->high) * 0.05f;
      }
      result.penalty += range_penalty;
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// 2-beat lookahead resolution
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::findSafePitchWithLookahead(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    uint8_t next_desired_pitch) const {
  // If no lookahead info, fall back to regular resolution.
  if (next_desired_pitch == 0) {
    return findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);
  }

  // Generate candidates: desired pitch +/- 0..max_search_range_.
  struct Candidate {
    uint8_t pitch;
    float current_penalty;
    float next_penalty;
    float total_score;
  };

  static constexpr size_t kMaxCandidates = 50;
  std::vector<Candidate> candidates;
  candidates.reserve(kMaxCandidates);

  for (int delta = 0; delta <= max_search_range_; ++delta) {
    for (int sign = (delta == 0 ? 1 : -1); sign <= 1; sign += 2) {
      int candidate_pitch = static_cast<int>(desired_pitch) + delta * sign;
      if (candidate_pitch < 0 || candidate_pitch > 127) continue;

      auto cand = static_cast<uint8_t>(candidate_pitch);
      if (!isSafeToPlace(state, rules, voice_id, cand, tick, duration)) {
        continue;
      }

      float current_penalty =
          static_cast<float>(delta) / 12.0f * 0.3f;
      current_penalty += crossRelationPenalty(state, voice_id, cand, tick);

      // Voice motion scoring: reward contrary/oblique, penalize parallel runs.
      current_penalty += voiceMotionPenalty(state, rules, voice_id, cand, tick);

      // Evaluate next-beat approach quality.
      // Stepwise motion (1-2 semitones) is ideal; small leaps (3-4) are
      // acceptable; larger leaps receive increasing penalty.
      int interval_to_next = std::abs(static_cast<int>(cand) -
                                      static_cast<int>(next_desired_pitch));
      float next_penalty = 0.0f;
      if (interval_to_next <= 2) {
        next_penalty = 0.0f;  // Stepwise -- ideal approach.
      } else if (interval_to_next <= 4) {
        next_penalty = 0.1f;  // Small leap -- acceptable.
      } else {
        next_penalty = static_cast<float>(interval_to_next) / 12.0f * 0.5f;
      }

      float total = current_penalty + 0.5f * next_penalty;
      candidates.push_back({cand, current_penalty, next_penalty, total});

      if (candidates.size() >= kMaxCandidates) break;
    }
    if (candidates.size() >= kMaxCandidates) break;
  }

  if (candidates.empty()) {
    // No safe candidates found -- fall back to the regular cascade.
    return findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);
  }

  // Select the candidate with the lowest total score.
  auto best = std::min_element(
      candidates.begin(), candidates.end(),
      [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.total_score < rhs.total_score;
      });

  PlacementResult result;
  result.pitch = best->pitch;
  result.penalty = best->current_penalty;
  result.strategy = "lookahead";
  result.accepted = true;
  return result;
}

void CollisionResolver::setMaxSearchRange(int semitones) {
  if (semitones > 0) {
    max_search_range_ = semitones;
  }
}

void CollisionResolver::setRangeTolerance(int semitones) {
  if (semitones > 0) {
    range_tolerance_ = semitones;
  }
}

// ---------------------------------------------------------------------------
// Cadence-aware voice leading
// ---------------------------------------------------------------------------

void CollisionResolver::setCadenceTicks(const std::vector<Tick>& ticks) {
  cadence_ticks_ = ticks;
}

void CollisionResolver::setHarmonicTimeline(const HarmonicTimeline* timeline) {
  harmonic_timeline_ = timeline;
}

}  // namespace bach
