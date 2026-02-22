// Phase 2: ConstraintState implementation.

#include "constraint/constraint_state.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/gravity_reference_data.inc"
#include "core/harmonic_bigram_data.inc"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "core/scale.h"
#include "fugue/cadence_insertion.h"

namespace bach {

// ---------------------------------------------------------------------------
// JSD computation
// ---------------------------------------------------------------------------

void normalizeDistribution(const uint16_t* counts, float* out, int n) {
  uint32_t total = 0;
  for (int i = 0; i < n; ++i) total += counts[i];
  if (total == 0) {
    float uniform = 1.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i) out[i] = uniform;
    return;
  }
  float inv = 1.0f / static_cast<float>(total);
  for (int i = 0; i < n; ++i) out[i] = static_cast<float>(counts[i]) * inv;
}

void normalizeDistribution(const int* counts, float* out, int n) {
  int total = 0;
  for (int i = 0; i < n; ++i) total += counts[i];
  if (total == 0) {
    float uniform = 1.0f / static_cast<float>(n);
    for (int i = 0; i < n; ++i) out[i] = uniform;
    return;
  }
  float inv = 1.0f / static_cast<float>(total);
  for (int i = 0; i < n; ++i) out[i] = static_cast<float>(counts[i]) * inv;
}

float computeJSD(const float* p, const float* q, int n) {
  // JSD = 0.5 * KL(p||m) + 0.5 * KL(q||m)  where m = 0.5*(p+q)
  // Using base-2 log so JSD in [0, 1].
  float jsd = 0.0f;
  for (int i = 0; i < n; ++i) {
    float m = 0.5f * (p[i] + q[i]);
    if (m < 1e-12f) continue;
    if (p[i] > 1e-12f) {
      jsd += 0.5f * p[i] * std::log2(p[i] / m);
    }
    if (q[i] > 1e-12f) {
      jsd += 0.5f * q[i] * std::log2(q[i] / m);
    }
  }
  return std::max(0.0f, std::min(1.0f, jsd));
}

// ---------------------------------------------------------------------------
// InvariantSet
// ---------------------------------------------------------------------------

InvariantSet::SatisfiesResult InvariantSet::satisfies(
    uint8_t pitch, VoiceId voice_id, Tick tick,
    const VerticalSnapshot& snap, const IRuleEvaluator* rule_eval,
    const BachRuleEvaluator* crossing_eval, const CounterpointState* state,
    const uint8_t* recent_pitches, int recent_count) const {
  SatisfiesResult result;

  // Hard: voice range check.
  if (pitch < voice_range_lo || pitch > voice_range_hi) {
    result.hard_violations++;
    result.range_violation = true;
  }

  // Hard: parallel perfect consonance check.
  if (rule_eval && state) {
    for (int i = 0; i < snap.num_voices; ++i) {
      if (i == voice_id) continue;
      if (snap.pitches[i] == 0) continue;
      if (rule_eval->hasParallelPerfect(*state,
                                        voice_id, static_cast<VoiceId>(i),
                                        tick)) {
        result.hard_violations++;
        result.parallel_perfect = true;
        break;
      }
    }
  }

  // Crossing check (policy-dependent).
  if (crossing_eval && state) {
    for (int i = 0; i < snap.num_voices; ++i) {
      if (i == voice_id) continue;
      if (snap.pitches[i] == 0) continue;
      bool is_crossing = rule_eval && rule_eval->hasVoiceCrossing(
                                          *state, voice_id,
                                          static_cast<VoiceId>(i), tick);
      if (is_crossing) {
        if (crossing_policy == CrossingPolicy::Reject) {
          result.hard_violations++;
          result.crossing_violation = true;
          break;
        } else {
          // AllowTemporary: check if temporary.
          bool temp = crossing_eval->isCrossingTemporary(
              *state, voice_id, static_cast<VoiceId>(i), tick);
          if (!temp) {
            result.soft_violations++;
            result.crossing_violation = true;
          }
        }
      }
    }
  }

  // Hard: repeated note limit.
  if (recent_count >= hard_repeat_limit) {
    bool all_same = true;
    for (int i = 0; i < recent_count && i < hard_repeat_limit; ++i) {
      if (recent_pitches[i] != pitch) {
        all_same = false;
        break;
      }
    }
    if (all_same) {
      result.hard_violations++;
      result.repeat_violation = true;
    }
  }

  // Soft: adjacent voice spacing check (too wide).
  for (int i = 0; i < snap.num_voices; ++i) {
    if (i == voice_id) continue;
    if (snap.pitches[i] == 0) continue;
    int spacing = std::abs(static_cast<int>(pitch) -
                           static_cast<int>(snap.pitches[i]));
    if (spacing > max_adjacent_spacing) {
      result.soft_violations++;
      result.spacing_violation = true;
      break;
    }
  }

  // E1: Close spacing penalty — depth-dependent soft penalty prevents
  // voice clustering. Extended to 5-6st for 3-voice Develop/Conclude episodes.
  bool episode_context = (phase_ == FuguePhase::Develop ||
                          phase_ == FuguePhase::Conclude);
  int close_threshold = (active_voice_count_ <= 3 && episode_context) ? 7 : 5;
  for (int i = 0; i < snap.num_voices; ++i) {
    if (i == voice_id) continue;
    if (snap.pitches[i] == 0) continue;
    int spacing = std::abs(static_cast<int>(pitch) -
                           static_cast<int>(snap.pitches[i]));
    if (spacing > 0 && spacing < close_threshold) {
      if (spacing < 2) {
        result.additional_penalty += 0.40f;
        result.close_spacing_recovery = true;
      } else if (spacing < 3) {
        result.additional_penalty += 0.20f;
      } else if (spacing < 4) {
        result.additional_penalty += 0.15f;
      } else if (spacing < 5) {
        result.additional_penalty += 0.10f;
      } else {
        // 5-6st: mild penalty, only in 3-voice episode context.
        result.additional_penalty += 0.08f;
      }
      result.spacing_violation = true;
      break;  // One penalty per candidate (worst adjacent pair).
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// SectionAccumulator
// ---------------------------------------------------------------------------

namespace {

int durationToRhythmBin(Tick dur) {
  // 32nd, 16th, 8th, quarter, half, whole, longer
  if (dur <= 75) return 0;       // 32nd (~60 ticks)
  if (dur <= 180) return 1;      // 16th (~120 ticks)
  if (dur <= 360) return 2;      // 8th (~240 ticks)
  if (dur <= 720) return 3;      // quarter (~480 ticks)
  if (dur <= 1440) return 4;     // half (~960 ticks)
  if (dur <= 2400) return 5;     // whole (~1920 ticks)
  return 6;                      // longer
}

/// @brief Fold 12-entry harmony degree targets into 7 scale degrees.
/// Maps: [V, I, IV, i, VI, III, II, vi, iv, V+, v, I+] ->
///   degree 0(I/i/I+)=1661+922+442, 1(II)=572, 2(III)=714,
///   3(IV/iv)=1052+520, 4(V/v/V+)=1687+520+468, 5(VI/vi)=896+546, 6(VII)=0
void getReferenceDegreeDistribution(float* out) {
  // Raw counts from gravity_reference_data.inc (12 entries → 7 degrees).
  int counts[7] = {
      1661 + 922 + 442,  // I + i + I+
      572,               // II
      714,               // III
      1052 + 520,        // IV + iv
      1687 + 520 + 468,  // V + V+ + v
      896 + 546,         // VI + vi
      0                  // VII (not in top-12)
  };
  int total = 0;
  for (int i = 0; i < 7; ++i) total += counts[i];
  float inv = 1.0f / static_cast<float>(total);
  for (int i = 0; i < 7; ++i) out[i] = static_cast<float>(counts[i]) * inv;
}

}  // namespace

void SectionAccumulator::recordNote(Tick duration, int degree) {
  int rbin = durationToRhythmBin(duration);
  if (rbin >= 0 && rbin < kRhythmBins) {
    rhythm_counts[rbin]++;
    total_rhythm++;
  }
  int d = ((degree % 7) + 7) % 7;
  if (d >= 0 && d < kHarmonyDegrees) {
    harmony_counts[d]++;
    total_harmony++;
  }
}

void SectionAccumulator::reset() {
  rhythm_counts.fill(0);
  harmony_counts.fill(0);
  total_rhythm = 0;
  total_harmony = 0;
}

float SectionAccumulator::rhythm_jsd() const {
  float ref[kRhythmBins];
  normalizeDistribution(gravity_data::kRhythmTarget, ref, kRhythmBins);
  float obs[kRhythmBins];
  normalizeDistribution(rhythm_counts.data(), obs, kRhythmBins);
  return computeJSD(obs, ref, kRhythmBins);
}

float SectionAccumulator::harmony_jsd() const {
  float ref[kHarmonyDegrees];
  getReferenceDegreeDistribution(ref);
  float obs[kHarmonyDegrees];
  normalizeDistribution(harmony_counts.data(), obs, kHarmonyDegrees);
  return computeJSD(obs, ref, kHarmonyDegrees);
}

// ---------------------------------------------------------------------------
// Phase weights
// ---------------------------------------------------------------------------

static constexpr PhaseWeights kEstablishWeights = {0.40f, 0.30f, 0.15f, 0.15f};
static constexpr PhaseWeights kDevelopWeights = {0.35f, 0.25f, 0.25f, 0.15f};
static constexpr PhaseWeights kResolveWeights = {0.30f, 0.35f, 0.20f, 0.15f};
static constexpr PhaseWeights kConcludeWeights = {0.25f, 0.40f, 0.20f, 0.15f};

const PhaseWeights& getPhaseWeights(FuguePhase phase) {
  switch (phase) {
    case FuguePhase::Establish: return kEstablishWeights;
    case FuguePhase::Develop:   return kDevelopWeights;
    case FuguePhase::Resolve:   return kResolveWeights;
    case FuguePhase::Conclude:  return kConcludeWeights;
  }
  return kDevelopWeights;
}

// ---------------------------------------------------------------------------
// jsd_decay_factor
// ---------------------------------------------------------------------------

float jsd_decay_factor(Tick tick, Tick total_duration,
                       const std::vector<Tick>& cadence_ticks,
                       float energy) {
  float factor = 1.0f;

  // Cadence zone decay: within 2 beats of a cadence, reduce JSD strictness.
  if (isInCadenceZone(tick, cadence_ticks, 2)) {
    factor *= 0.5f;
  }

  // High energy decay: energy > 0.8 reduces JSD strictness.
  if (energy > 0.8f) {
    float excess = (energy - 0.8f) / 0.2f;  // 0..1
    factor *= (1.0f - 0.4f * excess);        // -> 0.6 at energy=1.0
  }

  // Phrase boundary decay: near end of fugue.
  if (total_duration > 0) {
    float pos = static_cast<float>(tick) / static_cast<float>(total_duration);
    if (pos > 0.95f) {
      float tail = (pos - 0.95f) / 0.05f;
      factor *= (1.0f - 0.3f * tail);
    }
  }

  return std::max(0.3f, factor);
}

// ---------------------------------------------------------------------------
// GravityConfig::score
// ---------------------------------------------------------------------------

float GravityConfig::score(uint8_t pitch, Tick duration,
                           const MarkovContext& ctx,
                           const VerticalSnapshot& snap,
                           const SectionAccumulator& accum,
                           float decay, float figure_score) const {
  const PhaseWeights& w = getPhaseWeights(phase);

  // Layer 1: Melodic (Markov pitch + duration).
  float melodic_score = 0.0f;
  if (melodic_model) {
    DegreeStep next_step = computeDegreeStep(ctx.prev_pitch, pitch,
                                              ctx.key, ctx.scale);
    melodic_score += scoreMarkovPitch(*melodic_model, ctx.prev_step,
                                       ctx.deg_class, ctx.beat, next_step);

    DurCategory next_dur = ticksToDurCategory(duration);
    DirIntervalClass dir = toDirIvlClass(next_step);
    melodic_score += scoreMarkovDuration(*melodic_model, ctx.prev_dur,
                                          dir, next_dur) *
                     0.5f;  // Duration weight within melodic layer.
  }

  // Layer 2: Vertical interval.
  float vertical_score = 0.0f;
  if (vertical_table && snap.num_voices >= 2) {
    // Score against each sounding voice.
    int scored = 0;
    for (int i = 0; i < snap.num_voices; ++i) {
      if (snap.pitches[i] == 0) continue;
      int pc_offset = ((pitch - snap.pitches[i]) % 12 + 12) % 12;
      // Use bass degree 0 as default (simplified; full impl uses harmony).
      int bass_deg = 0;
      int vbin = voiceCountToBin(snap.num_voices);
      HarmFunc hf = HarmFunc::Tonic;
      vertical_score += scoreVerticalInterval(*vertical_table, bass_deg,
                                               ctx.beat, vbin, hf, pc_offset);
      scored++;
    }
    if (scored > 0) {
      vertical_score /= static_cast<float>(scored);
    }
  }

  // Layer 3: Rhythm/Harmony JSD penalty.
  float jsd_penalty = 0.0f;
  {
    float r_jsd = accum.rhythm_jsd();
    float h_jsd = accum.harmony_jsd();
    jsd_penalty = -(r_jsd + h_jsd) * decay;
  }

  // Layer 4: Vocabulary figure match.
  float vocab_score = figure_score * 0.5f;  // Scale to reasonable range.

  // Composite: phase-weighted sum.
  float composite = w.melodic * melodic_score +
                    w.vertical * vertical_score +
                    w.rhythm * jsd_penalty +
                    w.vocabulary * vocab_score;

  // Energy modulation: higher energy amplifies score magnitude.
  float energy_mult = 0.8f + 0.4f * energy;  // [0.8, 1.2]
  return composite * energy_mult;
}

// ---------------------------------------------------------------------------
// ConstraintState
// ---------------------------------------------------------------------------

float ConstraintState::evaluate(
    uint8_t pitch, Tick duration, VoiceId voice_id, Tick tick,
    const MarkovContext& ctx, const VerticalSnapshot& snap,
    const IRuleEvaluator* rule_eval,
    const BachRuleEvaluator* crossing_eval,
    const CounterpointState* cp_state,
    const uint8_t* recent_pitches, int recent_count,
    float figure_score) {
  // Layer 2: Invariant check.
  auto inv_result = invariants.satisfies(
      pitch, voice_id, tick, snap, rule_eval, crossing_eval, cp_state,
      recent_pitches, recent_count);

  if (inv_result.hard_violations > 0) {
    return -std::numeric_limits<float>::infinity();
  }

  // Layer 1: Obligation compatibility (simple: check if placing this note
  // would help resolve active obligations).
  float obligation_bonus = 0.0f;
  for (const auto& ob : active_obligations) {
    if (!ob.is_active_at(tick)) continue;
    if (ob.voice_mask != 0 && !(ob.voice_mask & (1 << voice_id))) continue;

    // Check if this pitch helps resolve the obligation.
    if (ob.direction != 0) {
      int dir = (pitch > ctx.prev_pitch) ? 1 : (pitch < ctx.prev_pitch ? -1 : 0);
      if (dir == ob.direction) {
        int interval = std::abs(static_cast<int>(pitch) -
                                static_cast<int>(ctx.prev_pitch));
        if (ob.required_interval_semitones == 0 ||
            interval == std::abs(ob.required_interval_semitones)) {
          obligation_bonus += (ob.strength == ObligationStrength::Structural)
                                  ? 0.3f
                                  : 0.15f;
        }
      }
    }
  }

  // Layer 3: Gravity score.
  float decay = jsd_decay_factor(tick, total_duration, cadence_ticks,
                                 gravity.energy);
  float gravity_score = gravity.score(pitch, duration, ctx, snap, accumulator,
                                      decay, figure_score);

  // Soft violation penalty + recovery obligation for soft violations.
  float soft_penalty = inv_result.soft_violations * -0.1f;
  if (inv_result.soft_violations > 0) {
    addRecoveryObligation(
        ObligationType::LeapResolve,
        tick,
        tick + kTicksPerBar * 2,
        voice_id);
  }

  // E1: Depth-dependent close-spacing penalty (score only, no recovery
  // obligation — close spacing is common in dense fugue texture and should
  // not inflate soft_violation_count toward the is_dead threshold).
  soft_penalty -= inv_result.additional_penalty;

  return gravity_score + obligation_bonus + soft_penalty;
}

void ConstraintState::advance(Tick tick, uint8_t placed_pitch,
                              VoiceId placed_voice,
                              Tick duration, Key key) {
  total_note_count++;

  // Resolve obligations that match.
  auto it = active_obligations.begin();
  while (it != active_obligations.end()) {
    if (!it->is_active_at(tick)) {
      // Expired.
      if (tick > it->deadline &&
          it->strength == ObligationStrength::Structural) {
        // Structural obligation expired unresolved -- this is bad but we
        // track it rather than crash.
      }
      if (tick > it->deadline) {
        it = active_obligations.erase(it);
        continue;
      }
    }

    // Check if placed note resolves this obligation.
    bool resolved = false;
    if (it->voice_mask == 0 || (it->voice_mask & (1 << placed_voice))) {
      if (it->is_active_at(tick)) {
        if (it->direction != 0) {
          // Direction-based resolution check is approximate here;
          // full check would need previous pitch context.
          resolved = true;  // Simplified: mark as resolved when in window.
        } else if (it->type == ObligationType::StrongBeatHarm) {
          // Gate: auto-pass if we got here.
          resolved = true;
        } else if (it->type == ObligationType::InvariantRecovery) {
          resolved = true;
        }
      }
    }

    if (resolved) {
      // Also resolve any obligations in the satisfies list.
      for (uint16_t sat_id : it->satisfies) {
        active_obligations.erase(
            std::remove_if(active_obligations.begin(),
                           active_obligations.end(),
                           [sat_id](const ObligationNode& o) {
                             return o.id == sat_id;
                           }),
            active_obligations.end());
      }
      it = active_obligations.erase(it);
    } else {
      ++it;
    }
  }

  // Expire overdue obligations.
  active_obligations.erase(
      std::remove_if(active_obligations.begin(), active_obligations.end(),
                     [tick](const ObligationNode& o) {
                       return tick > o.deadline;
                     }),
      active_obligations.end());

  // Record placement in accumulator for distribution tracking.
  if (duration > 0) {
    int degree = 0;
    if (!scale_util::pitchToScaleDegree(placed_pitch, key,
                                         ScaleType::Major, degree)) {
      // Non-scale pitch: use nearest scale degree approximation.
      degree = (static_cast<int>(placed_pitch) % 12) * 7 / 12;
    }
    accumulator.recordNote(duration, degree);
  }
}

void ConstraintState::addRecoveryObligation(ObligationType type, Tick origin,
                                            Tick deadline, VoiceId voice) {
  ObligationNode ob;
  ob.id = static_cast<uint16_t>(active_obligations.size() + 1000);
  ob.type = type;
  ob.origin = origin;
  ob.start_tick = origin;
  ob.deadline = deadline;
  ob.voice_mask = (1 << voice);
  ob.strength = ObligationStrength::Soft;
  active_obligations.push_back(ob);
  soft_violation_count++;
}

bool ConstraintState::is_dead(Tick current_tick) const {
  // Check if any Structural obligation has passed its deadline.
  for (const auto& obl : active_obligations) {
    if (obl.strength == ObligationStrength::Structural) {
      if (current_tick > obl.deadline) {
        return true;  // Structural deadline exceeded, generation is stuck.
      }
    }
  }
  // Also check soft violation ratio -- too many soft violations means the
  // generation quality has degraded beyond recovery.
  return soft_violation_ratio() > 0.15f;
}

}  // namespace bach
