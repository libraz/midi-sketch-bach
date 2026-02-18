// Feasibility harness implementation: micro-exposition simulation,
// voice assignment search, pair verification, and solvability testing.

#include "constraint/feasibility_harness.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/counterpoint_state.h"
#include "fugue/answer.h"
#include "fugue/countersubject.h"
#include "fugue/exposition.h"
#include "fugue/voice_registers.h"

namespace bach {
namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// @brief Sampling resolution for density computation (16th note).
constexpr Tick kDensitySampleStep = kTicksPerBeat / 4;

/// @brief Strong beat interval: ticks divisible by kTicksPerBeat * 2 (half bar).
constexpr Tick kStrongBeatInterval = kTicksPerBeat * 2;

/// @brief Number of top candidates to run precise simulation on (g2).
constexpr int kTopK = 3;

/// @brief Number of MicroSim trials for precise evaluation in g2.
constexpr int kPreciseTrials = 3;

/// @brief Critical violation rule names from BachRuleEvaluator.
constexpr const char* kParallelFifths = "parallel_fifths";
constexpr const char* kParallelOctaves = "parallel_octaves";

// ---------------------------------------------------------------------------
// P1.g1 helpers: MicroExposition Simulator
// ---------------------------------------------------------------------------

/// @brief Compute register overlap between voice pitch ranges in an exposition.
///
/// For each pair of voices, compute the intersection / union of their pitch
/// ranges. Returns the maximum overlap across all pairs.
///
/// @param expo The exposition with voice notes.
/// @param num_voices Number of voices.
/// @return Maximum register overlap ratio [0, 1].
float computeRegisterOverlap(const Exposition& expo, uint8_t num_voices) {
  // Collect pitch min/max per voice.
  struct VoicePitchRange {
    uint8_t min_pitch = 127;
    uint8_t max_pitch = 0;
    bool has_notes = false;
  };

  std::vector<VoicePitchRange> ranges(num_voices);
  for (const auto& [vid, notes] : expo.voice_notes) {
    if (vid >= num_voices) continue;
    for (const auto& note : notes) {
      ranges[vid].min_pitch = std::min(ranges[vid].min_pitch, note.pitch);
      ranges[vid].max_pitch = std::max(ranges[vid].max_pitch, note.pitch);
      ranges[vid].has_notes = true;
    }
  }

  float max_overlap = 0.0f;
  for (int idx_a = 0; idx_a < num_voices; ++idx_a) {
    for (int idx_b = idx_a + 1; idx_b < num_voices; ++idx_b) {
      if (!ranges[idx_a].has_notes || !ranges[idx_b].has_notes) continue;

      int intersect_lo = std::max(static_cast<int>(ranges[idx_a].min_pitch),
                                  static_cast<int>(ranges[idx_b].min_pitch));
      int intersect_hi = std::min(static_cast<int>(ranges[idx_a].max_pitch),
                                  static_cast<int>(ranges[idx_b].max_pitch));
      int intersection = std::max(0, intersect_hi - intersect_lo);

      int union_lo = std::min(static_cast<int>(ranges[idx_a].min_pitch),
                              static_cast<int>(ranges[idx_b].min_pitch));
      int union_hi = std::max(static_cast<int>(ranges[idx_a].max_pitch),
                              static_cast<int>(ranges[idx_b].max_pitch));
      int union_range = std::max(1, union_hi - union_lo);

      float overlap = static_cast<float>(intersection) / static_cast<float>(union_range);
      max_overlap = std::max(max_overlap, overlap);
    }
  }

  return max_overlap;
}

/// @brief Compute accent collision ratio between voices at strong beats.
///
/// At each strong beat (bar start), count how many voices have a note
/// onset. If all voices have simultaneous onsets, that is an accent
/// collision. Returns the max collision ratio across all strong beats.
///
/// @param expo The exposition with voice notes.
/// @param num_voices Number of voices.
/// @return Maximum accent collision ratio [0, 1].
float computeAccentCollision(const Exposition& expo, uint8_t num_voices) {
  if (num_voices <= 1 || expo.total_ticks == 0) return 0.0f;

  float max_ratio = 0.0f;

  for (Tick tick = 0; tick < expo.total_ticks; tick += kTicksPerBar) {
    int onsets = 0;
    for (const auto& [vid, notes] : expo.voice_notes) {
      for (const auto& note : notes) {
        if (note.start_tick == tick) {
          onsets++;
          break;
        }
      }
    }
    float ratio = static_cast<float>(onsets) / static_cast<float>(num_voices);
    max_ratio = std::max(max_ratio, ratio);
  }

  return max_ratio;
}

// ---------------------------------------------------------------------------
// P1.g2 helpers: VoiceAssignmentSearch
// ---------------------------------------------------------------------------

/// @brief Score voice separation for a given octave offset.
///
/// Evaluates how well the shifted subject fits across voice registers by
/// computing the average distance between the subject's center pitch and
/// each voice's center pitch (normalized by range width).
///
/// @param subject_low Lowest pitch in the shifted subject.
/// @param subject_high Highest pitch in the shifted subject.
/// @param num_voices Number of voices.
/// @return Separation score [0, 1] where 1 = excellent separation.
float scoreVoiceSeparation(uint8_t subject_low, uint8_t subject_high,
                           uint8_t num_voices) {
  float subject_center = (static_cast<float>(subject_low) +
                          static_cast<float>(subject_high)) / 2.0f;
  float total_distance = 0.0f;
  int pairs = 0;

  for (uint8_t vid = 0; vid < num_voices; ++vid) {
    auto [range_lo, range_hi] = getFugueVoiceRange(vid, num_voices);
    float voice_center = (static_cast<float>(range_lo) +
                          static_cast<float>(range_hi)) / 2.0f;
    // Distance from subject center to voice center.
    float dist = std::abs(subject_center - voice_center);
    total_distance += dist;
    pairs++;
  }

  if (pairs == 0) return 0.0f;

  // Average distance, normalized. 48 semitones (4 octaves) as max reasonable spread.
  float avg_dist = total_distance / static_cast<float>(pairs);
  constexpr float kMaxReasonableSpread = 48.0f;
  return std::min(1.0f, avg_dist / kMaxReasonableSpread);
}

/// @brief Score register clarity: how well the subject sits in voice 0's
/// characteristic range.
///
/// @param subject_center Center pitch of the shifted subject.
/// @param num_voices Number of voices.
/// @return Clarity score [0, 1] where 1 = perfectly centered.
float scoreRegisterClarity(float subject_center, uint8_t num_voices) {
  auto [range_lo, range_hi] = getFugueVoiceRange(0, num_voices);
  float voice_center = (static_cast<float>(range_lo) +
                        static_cast<float>(range_hi)) / 2.0f;
  float half_range = (static_cast<float>(range_hi) -
                      static_cast<float>(range_lo)) / 2.0f;
  if (half_range < 1.0f) return 0.0f;

  float dist = std::abs(subject_center - voice_center);
  return std::max(0.0f, 1.0f - dist / half_range);
}

/// @brief Score accent contour alignment for a given octave offset.
///
/// Uses the profile's accent_contour front/mid/tail weights. Front-weighted
/// subjects score better at higher registers (closer to voice 0).
///
/// @param profile The constraint profile with accent contour.
/// @param subject_center Center pitch of the shifted subject.
/// @param num_voices Number of voices.
/// @return Accent score [0, 1].
float scoreAccentContour(const SubjectConstraintProfile& profile,
                         float subject_center, uint8_t num_voices) {
  auto [v0_lo, v0_hi] = getFugueVoiceRange(0, num_voices);
  float v0_center = (static_cast<float>(v0_lo) + static_cast<float>(v0_hi)) / 2.0f;

  // Front-weighted accent favors placement near the top voice (more projection).
  // Tail-weighted accent is more neutral.
  float front_bias = profile.accent_contour.front_weight -
                     profile.accent_contour.tail_weight;

  // If front-weighted, closer to v0 center is better.
  float dist_to_v0 = std::abs(subject_center - v0_center);
  constexpr float kMaxDist = 36.0f;  // 3 octaves
  float proximity = std::max(0.0f, 1.0f - dist_to_v0 / kMaxDist);

  // Blend: if front-weighted, proximity matters more; if tail-weighted, less.
  float bias_weight = 0.5f + front_bias * 0.3f;
  return proximity * bias_weight + (1.0f - bias_weight) * 0.5f;
}

/// @brief Create a copy of the subject with all pitches shifted by an offset.
///
/// @param subject Original subject.
/// @param semitone_offset Offset in semitones (octave_offset * 12).
/// @return Subject copy with shifted pitches.
Subject shiftSubjectPitches(const Subject& subject, int semitone_offset) {
  Subject shifted = subject;
  for (auto& note : shifted.notes) {
    int new_pitch = static_cast<int>(note.pitch) + semitone_offset;
    new_pitch = std::max(0, std::min(127, new_pitch));
    note.pitch = static_cast<uint8_t>(new_pitch);
  }
  return shifted;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// P1.g1: runMicroSim
// ---------------------------------------------------------------------------

MicroSimResult runMicroSim(
    const Subject& subject,
    const FugueConfig& config,
    int num_trials) {
  MicroSimResult result;
  result.num_attempts = num_trials;

  if (subject.notes.empty() || num_trials <= 0) {
    return result;
  }

  float total_overlap = 0.0f;

  for (int trial = 0; trial < num_trials; ++trial) {
    uint32_t trial_seed = config.seed + static_cast<uint32_t>(trial) * 7919;

    // Step 1: Generate answer and countersubject.
    Answer answer = generateAnswer(subject);
    Countersubject counter = generateCountersubject(subject, trial_seed);

    // Step 2: Build unvalidated exposition.
    Exposition expo = buildExposition(
        subject, answer, counter, config, trial_seed);

    // Step 3: Create BachRuleEvaluator and CounterpointState.
    BachRuleEvaluator evaluator(config.num_voices);
    CounterpointState cp_state;

    // Register voices using getFugueVoiceRange().
    for (uint8_t vid = 0; vid < config.num_voices; ++vid) {
      auto [range_lo, range_hi] = getFugueVoiceRange(vid, config.num_voices);
      cp_state.registerVoice(vid, range_lo, range_hi);
    }

    // Populate counterpoint state with exposition notes.
    for (const auto& [vid, notes] : expo.voice_notes) {
      for (const auto& note : notes) {
        cp_state.addNote(vid, note);
      }
    }

    // Step 4: Validate with BachRuleEvaluator.
    auto violations = evaluator.validate(cp_state, 0, expo.total_ticks);

    // Step 5: Count critical violations.
    int critical_count = 0;
    for (const auto& viol : violations) {
      if (viol.rule == kParallelFifths || viol.rule == kParallelOctaves) {
        critical_count++;
      }
    }

    // A trial is successful if no critical violations.
    bool trial_success = (critical_count == 0);
    if (trial_success) {
      result.num_success++;
    }
    result.num_critical_violations += critical_count;

    // Step 6: Track register overlap.
    float overlap = computeRegisterOverlap(expo, config.num_voices);
    total_overlap += overlap;

    // Track accent collision.
    float collision = computeAccentCollision(expo, config.num_voices);
    result.max_accent_collision = std::max(result.max_accent_collision, collision);

    // A bottleneck is when register overlap is excessive (>= 0.80).
    if (overlap >= 0.80f) {
      result.num_bottleneck++;
    }
  }

  result.avg_register_overlap = total_overlap / static_cast<float>(num_trials);

  return result;
}

// ---------------------------------------------------------------------------
// P1.g2: findBestAssignment
// ---------------------------------------------------------------------------

VoiceAssignment findBestAssignment(
    const Subject& subject,
    const SubjectConstraintProfile& profile,
    const FugueConfig& config) {
  // Phase 1: Coarse evaluation of all 5 octave offsets.
  struct Candidate {
    int8_t offset;
    float coarse_score;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(5);

  for (int8_t offset = -2; offset <= 2; ++offset) {
    int semitone_shift = static_cast<int>(offset) * 12;
    Subject shifted = shiftSubjectPitches(subject, semitone_shift);

    if (shifted.notes.empty()) {
      candidates.push_back({offset, 0.0f});
      continue;
    }

    // Compute shifted subject pitch stats.
    uint8_t shifted_low = 127;
    uint8_t shifted_high = 0;
    for (const auto& note : shifted.notes) {
      shifted_low = std::min(shifted_low, note.pitch);
      shifted_high = std::max(shifted_high, note.pitch);
    }

    float shifted_center = (static_cast<float>(shifted_low) +
                            static_cast<float>(shifted_high)) / 2.0f;

    // Check if the shifted subject is within any voice's range.
    bool in_range = false;
    auto [v0_lo, v0_hi] = getFugueVoiceRange(0, config.num_voices);
    if (shifted_low >= v0_lo && shifted_high <= v0_hi) {
      in_range = true;
    }
    // Even if not perfectly in voice 0, check other voices.
    if (!in_range) {
      for (uint8_t vid = 1; vid < config.num_voices; ++vid) {
        auto [rng_lo, rng_hi] = getFugueVoiceRange(vid, config.num_voices);
        if (shifted_low >= rng_lo && shifted_high <= rng_hi) {
          in_range = true;
          break;
        }
      }
    }

    // Score components.
    float separation = scoreVoiceSeparation(shifted_low, shifted_high,
                                            config.num_voices);
    float clarity = scoreRegisterClarity(shifted_center, config.num_voices);
    float accent = scoreAccentContour(profile, shifted_center, config.num_voices);

    // Coarse score: weighted combination. Penalize out-of-range.
    float range_penalty = in_range ? 1.0f : 0.3f;
    float coarse = (separation * 0.4f + clarity * 0.35f + accent * 0.25f) *
                   range_penalty;

    candidates.push_back({offset, coarse});
  }

  // Sort by coarse score descending.
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              return lhs.coarse_score > rhs.coarse_score;
            });

  // Phase 2: Run MicroSim on top K candidates.
  VoiceAssignment best;
  best.final_score = -1.0f;

  int num_to_test = std::min(kTopK, static_cast<int>(candidates.size()));
  for (int idx = 0; idx < num_to_test; ++idx) {
    int8_t offset = candidates[idx].offset;
    int semitone_shift = static_cast<int>(offset) * 12;
    Subject shifted = shiftSubjectPitches(subject, semitone_shift);

    MicroSimResult sim = runMicroSim(shifted, config, kPreciseTrials);

    float sim_rate = sim.success_rate();
    float final = sim_rate * candidates[idx].coarse_score;

    if (final > best.final_score) {
      best.start_octave_offset = offset;
      best.separation_score = candidates[idx].coarse_score;
      best.sim_score = sim_rate;
      best.final_score = final;
    }
  }

  // If no candidate was evaluated, return a default.
  if (best.final_score < 0.0f) {
    best.final_score = 0.0f;
  }

  return best;
}

namespace {

// ---------------------------------------------------------------------------
// P1.g3 helpers: Pair verification
// ---------------------------------------------------------------------------

/// @brief Check if two obligation types form a conflicting pair.
///
/// Conflicting pairs:
/// - LeadingTone vs StrongBeatHarm: LT demands resolution while gate demands
///   harmonic tone -- the resolution pitch may not satisfy the gate.
/// - Seventh vs LeapResolve: both demand stepwise resolution in potentially
///   opposite directions at the same tick.
bool isConflictingPair(ObligationType type_a, ObligationType type_b) {
  // LT vs StrongBeatHarm
  if ((type_a == ObligationType::LeadingTone &&
       type_b == ObligationType::StrongBeatHarm) ||
      (type_a == ObligationType::StrongBeatHarm &&
       type_b == ObligationType::LeadingTone)) {
    return true;
  }
  // Seventh vs LeapResolve
  if ((type_a == ObligationType::Seventh &&
       type_b == ObligationType::LeapResolve) ||
      (type_a == ObligationType::LeapResolve &&
       type_b == ObligationType::Seventh)) {
    return true;
  }
  return false;
}

/// @brief Check if an obligation is cadence-related.
bool isCadenceObligation(ObligationType type) {
  return type == ObligationType::CadenceApproach ||
         type == ObligationType::CadenceStable;
}

/// @brief Find the pitch sounding in a note sequence at a given tick.
///
/// Returns the pitch of the note active at the tick, or 0 if no note is active.
/// If multiple notes overlap (unusual in a single voice), returns the first found.
uint8_t findPitchAtTick(const std::vector<NoteEvent>& notes, Tick tick) {
  for (const auto& note : notes) {
    if (note.start_tick <= tick &&
        note.start_tick + note.duration > tick) {
      return note.pitch;
    }
  }
  return 0;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// P1.g3: verifyPair
// ---------------------------------------------------------------------------

PairVerificationResult verifyPair(
    const SubjectConstraintProfile& subject_prof,
    const SubjectConstraintProfile& answer_prof,
    int offset_ticks) {
  PairVerificationResult result;
  result.tonal_answer_feasible = answer_prof.tonal_answer_feasible;
  result.pair_peak_density = 0.0f;
  result.cadence_conflict_score = 0.0f;

  // Step 1: Time-shift answer obligations by offset_ticks and merge.
  // Create shifted copies of answer obligations.
  std::vector<ObligationNode> shifted_answer;
  shifted_answer.reserve(answer_prof.obligations.size());
  for (const auto& obl : answer_prof.obligations) {
    ObligationNode shifted = obl;
    shifted.origin += static_cast<Tick>(offset_ticks);
    shifted.start_tick += static_cast<Tick>(offset_ticks);
    shifted.deadline += static_cast<Tick>(offset_ticks);
    shifted_answer.push_back(shifted);
  }

  // Determine the combined timeline span.
  Tick min_tick = UINT32_MAX;
  Tick max_tick = 0;

  for (const auto& obl : subject_prof.obligations) {
    min_tick = std::min(min_tick, obl.start_tick);
    max_tick = std::max(max_tick, obl.deadline);
  }
  for (const auto& obl : shifted_answer) {
    min_tick = std::min(min_tick, obl.start_tick);
    max_tick = std::max(max_tick, obl.deadline);
  }

  if (min_tick >= max_tick) {
    // No temporal overlap -- trivially feasible.
    return result;
  }

  // Step 2-3: Compute pair peak density by sampling the combined timeline.
  float peak_density = 0.0f;

  for (Tick sample = min_tick; sample <= max_tick; sample += kDensitySampleStep) {
    int active_debt = 0;

    for (const auto& obl : subject_prof.obligations) {
      if (obl.is_debt() && obl.is_active_at(sample)) {
        active_debt++;
      }
    }
    for (const auto& obl : shifted_answer) {
      if (obl.is_debt() && obl.is_active_at(sample)) {
        active_debt++;
      }
    }

    peak_density = std::max(peak_density, static_cast<float>(active_debt));
  }

  result.pair_peak_density = peak_density;

  // Step 4: Detect obligation conflicts between subject and answer obligations.
  // Check all pairs of (subject_obligation, shifted_answer_obligation) for
  // conflicting types that are simultaneously active.
  for (const auto& obl_s : subject_prof.obligations) {
    for (const auto& obl_a : shifted_answer) {
      if (!isConflictingPair(obl_s.type, obl_a.type)) continue;

      // Find the overlap region.
      Tick overlap_start = std::max(obl_s.start_tick, obl_a.start_tick);
      Tick overlap_end = std::min(obl_s.deadline, obl_a.deadline);

      if (overlap_start <= overlap_end) {
        ObligationConflict conflict;
        conflict.obligation_a_id = obl_s.id;
        conflict.obligation_b_id = obl_a.id;
        conflict.type_a = obl_s.type;
        conflict.type_b = obl_a.type;
        conflict.conflict_tick = overlap_start;
        result.conflicts.push_back(conflict);
      }
    }
  }

  // Step 5: Compute cadence conflict score.
  // Both subject and answer having CadenceApproach/CadenceStable active simultaneously.
  int cadence_overlap_samples = 0;
  int total_samples = 0;

  for (Tick sample = min_tick; sample <= max_tick; sample += kDensitySampleStep) {
    total_samples++;

    bool subject_cadence_active = false;
    bool answer_cadence_active = false;

    for (const auto& obl : subject_prof.obligations) {
      if (isCadenceObligation(obl.type) && obl.is_active_at(sample)) {
        subject_cadence_active = true;
        break;
      }
    }
    for (const auto& obl : shifted_answer) {
      if (isCadenceObligation(obl.type) && obl.is_active_at(sample)) {
        answer_cadence_active = true;
        break;
      }
    }

    if (subject_cadence_active && answer_cadence_active) {
      cadence_overlap_samples++;
    }
  }

  if (total_samples > 0) {
    result.cadence_conflict_score =
        static_cast<float>(cadence_overlap_samples) /
        static_cast<float>(total_samples);
  }

  return result;
}

// ---------------------------------------------------------------------------
// P1.g4: testSolvability
// ---------------------------------------------------------------------------

SolvabilityResult testSolvability(
    const std::vector<NoteEvent>& subject_notes,
    const std::vector<NoteEvent>& cs_notes,
    Key /*key*/, bool /*is_minor*/) {
  SolvabilityResult result;
  result.vertical_clash_rate = 0.0f;
  result.strong_beat_dissonance_rate = 0.0f;
  result.register_overlap = 0.0f;

  if (subject_notes.empty() || cs_notes.empty()) {
    return result;
  }

  // Determine the combined time span.
  Tick earliest = std::min(subject_notes.front().start_tick,
                           cs_notes.front().start_tick);
  Tick latest_subject = subject_notes.back().start_tick +
                        subject_notes.back().duration;
  Tick latest_cs = cs_notes.back().start_tick + cs_notes.back().duration;
  Tick latest = std::max(latest_subject, latest_cs);

  // Step 1-3: At each strong beat, check vertical interval.
  int total_strong_beats = 0;
  int dissonant_strong_beats = 0;
  int non_chord_tone_strong_beats = 0;

  for (Tick tick = earliest; tick < latest; tick += kStrongBeatInterval) {
    uint8_t subject_pitch = findPitchAtTick(subject_notes, tick);
    uint8_t cs_pitch = findPitchAtTick(cs_notes, tick);

    // Both voices must have a note sounding at this tick.
    if (subject_pitch == 0 || cs_pitch == 0) continue;

    total_strong_beats++;

    // Compute the vertical interval.
    int semitones = absoluteInterval(subject_pitch, cs_pitch);
    int simple = interval_util::compoundToSimple(semitones);
    IntervalQuality quality = classifyInterval(simple);

    if (quality == IntervalQuality::Dissonance) {
      dissonant_strong_beats++;

      // Strong-beat dissonance: seconds (1, 2), tritone (6), sevenths (10, 11)
      // are particularly problematic as non-chord tones on strong beats.
      if (simple == interval::kMinor2nd || simple == interval::kMajor2nd ||
          simple == interval::kTritone ||
          simple == interval::kMinor7th || simple == interval::kMajor7th) {
        non_chord_tone_strong_beats++;
      }
    }
  }

  // Step 4: Compute rates.
  if (total_strong_beats > 0) {
    result.vertical_clash_rate =
        static_cast<float>(dissonant_strong_beats) /
        static_cast<float>(total_strong_beats);
    result.strong_beat_dissonance_rate =
        static_cast<float>(non_chord_tone_strong_beats) /
        static_cast<float>(total_strong_beats);
  }

  // Step 5: Compute register overlap.
  // Find pitch ranges for subject and countersubject.
  uint8_t subject_min = 127, subject_max = 0;
  for (const auto& note : subject_notes) {
    subject_min = std::min(subject_min, note.pitch);
    subject_max = std::max(subject_max, note.pitch);
  }

  uint8_t cs_min = 127, cs_max = 0;
  for (const auto& note : cs_notes) {
    cs_min = std::min(cs_min, note.pitch);
    cs_max = std::max(cs_max, note.pitch);
  }

  // Intersection of pitch ranges.
  int intersect_low = std::max(static_cast<int>(subject_min),
                               static_cast<int>(cs_min));
  int intersect_high = std::min(static_cast<int>(subject_max),
                                static_cast<int>(cs_max));
  int intersection = std::max(0, intersect_high - intersect_low);

  // Union of pitch ranges.
  int union_low = std::min(static_cast<int>(subject_min),
                           static_cast<int>(cs_min));
  int union_high = std::max(static_cast<int>(subject_max),
                            static_cast<int>(cs_max));
  int union_range = std::max(1, union_high - union_low);

  result.register_overlap =
      static_cast<float>(intersection) / static_cast<float>(union_range);

  return result;
}

}  // namespace bach
