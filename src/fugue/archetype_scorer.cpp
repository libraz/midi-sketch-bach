/// @file
/// @brief Archetype-specific quality scoring for fugue subjects.

#include "fugue/archetype_scorer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/scale.h"
#include "fugue/stretto.h"
#include "fugue/subject_params.h"
#include "transform/motif_transform.h"

namespace bach {
namespace {

/// @brief Evaluate fragment reusability of a Kopfmotiv.
/// @return Score in [0, 1].
float evaluateFragmentReusability(const std::vector<NoteEvent>& kopf,
                                  Key /*key*/, ScaleType /*scale*/) {
  if (kopf.size() < 2) return 0.0f;
  float score = 0.0f;

  // (a) Range within one octave: narrower = more reusable.
  uint8_t lowest = 127, highest = 0;
  for (const auto& note : kopf) {
    if (note.pitch < lowest) lowest = note.pitch;
    if (note.pitch > highest) highest = note.pitch;
  }
  int range = static_cast<int>(highest) - static_cast<int>(lowest);
  if (range <= 12) {
    score += 0.3f * (1.0f - static_cast<float>(range) / 12.0f);
  }

  // (b) Start/end pitch proximity: same or adjacent degree = more circular.
  int start_end_dist = std::abs(static_cast<int>(kopf.front().pitch) -
                                static_cast<int>(kopf.back().pitch));
  if (start_end_dist <= 2) {
    score += 0.3f;
  } else if (start_end_dist <= 4) {
    score += 0.15f;
  }

  // (c) Rhythmic repetition: repeated duration patterns = more memorable.
  std::map<Tick, int> dur_counts;
  for (const auto& note : kopf) {
    dur_counts[note.duration]++;
  }
  int max_repeat = 0;
  for (const auto& [dur, cnt] : dur_counts) {
    if (cnt > max_repeat) max_repeat = cnt;
  }
  float repeat_ratio =
      static_cast<float>(max_repeat) / static_cast<float>(kopf.size());
  score += 0.4f * repeat_ratio;

  return std::clamp(score, 0.0f, 1.0f);
}

/// @brief Evaluate sequence (Sequenz) potential of a Kopfmotiv.
/// @return Score in [0, 1].
float evaluateSequencePotential(const std::vector<NoteEvent>& kopf,
                                Key key, ScaleType scale) {
  if (kopf.size() < 2) return 0.0f;

  // Extract interval pattern from kopf.
  std::vector<int> intervals;
  for (size_t idx = 1; idx < kopf.size(); ++idx) {
    intervals.push_back(static_cast<int>(kopf[idx].pitch) -
                        static_cast<int>(kopf[idx - 1].pitch));
  }

  // Transpose the interval pattern up and down by 2 scale degrees
  // and count tritones (augmented 4th = 6 semitones) in the result.
  int tritone_count = 0;
  for (int shift : {2, -2}) {
    // Shift each note by 'shift' scale degrees from the first note.
    uint8_t base_pitch = kopf[0].pitch;
    int base_abs = scale_util::pitchToAbsoluteDegree(base_pitch, key, scale);
    int shifted_abs = base_abs + shift;
    int shifted_pitch = static_cast<int>(
        scale_util::absoluteDegreeToPitch(shifted_abs, key, scale));

    // Reconstruct the transposed sequence using original intervals mapped
    // through the scale.
    int prev = shifted_pitch;
    for (size_t idx = 0; idx < intervals.size(); ++idx) {
      int orig_from = static_cast<int>(kopf[idx].pitch);
      int orig_to = static_cast<int>(kopf[idx + 1].pitch);
      int orig_from_abs = scale_util::pitchToAbsoluteDegree(
          clampPitch(orig_from, 0, 127), key, scale);
      int orig_to_abs = scale_util::pitchToAbsoluteDegree(
          clampPitch(orig_to, 0, 127), key, scale);
      int degree_interval = orig_to_abs - orig_from_abs;

      int next_abs = scale_util::pitchToAbsoluteDegree(
          clampPitch(prev, 0, 127), key, scale) +
          degree_interval;
      int next = static_cast<int>(
          scale_util::absoluteDegreeToPitch(next_abs, key, scale));

      int semitone_interval = std::abs(next - prev);
      int simple = interval_util::compoundToSimple(semitone_interval);
      if (simple == 6) ++tritone_count;  // Tritone

      prev = next;
    }
  }

  // Score: 0 tritones = 1.0, 1 = 0.6, 2+ = 0.2.
  if (tritone_count == 0) return 1.0f;
  if (tritone_count == 1) return 0.6f;
  return 0.2f;
}

/// @brief Evaluate contour symmetry of a subject.
/// @return Score in [0, 1]. 1.0 = perfectly symmetric contour.
float evaluateContourSymmetry(const Subject& subject) {
  if (subject.notes.size() < 3) return 0.5f;

  // Extract ascending and descending interval magnitudes.
  std::vector<int> ascending_intervals;
  std::vector<int> descending_intervals;
  for (size_t idx = 1; idx < subject.notes.size(); ++idx) {
    int interval = static_cast<int>(subject.notes[idx].pitch) -
                   static_cast<int>(subject.notes[idx - 1].pitch);
    if (interval > 0) ascending_intervals.push_back(interval);
    else if (interval < 0) descending_intervals.push_back(-interval);
  }

  if (ascending_intervals.empty() || descending_intervals.empty()) return 0.0f;

  // Compare distributions: sum of absolute intervals.
  int asc_sum = 0, desc_sum = 0;
  for (int val : ascending_intervals) asc_sum += val;
  for (int val : descending_intervals) desc_sum += val;

  // Size balance.
  size_t min_size =
      std::min(ascending_intervals.size(), descending_intervals.size());
  size_t max_size =
      std::max(ascending_intervals.size(), descending_intervals.size());
  float size_balance =
      static_cast<float>(min_size) / static_cast<float>(max_size);

  // Magnitude balance.
  int max_sum = std::max(asc_sum, desc_sum);
  float magnitude_balance =
      (max_sum > 0)
          ? static_cast<float>(std::min(asc_sum, desc_sum)) /
                static_cast<float>(max_sum)
          : 0.0f;

  return std::clamp(size_balance * 0.5f + magnitude_balance * 0.5f, 0.0f,
                    1.0f);
}

}  // namespace

// ---------------------------------------------------------------------------
// ArchetypeScore
// ---------------------------------------------------------------------------

float ArchetypeScore::composite() const {
  return archetype_fitness * 0.30f +
         inversion_quality * 0.25f +
         stretto_potential * 0.25f +
         kopfmotiv_strength * 0.20f;
}

// ---------------------------------------------------------------------------
// ArchetypeScorer
// ---------------------------------------------------------------------------

ArchetypeScore ArchetypeScorer::evaluate(const Subject& subject,
                                          const ArchetypePolicy& policy) const {
  ArchetypeScore score;
  score.archetype_fitness = scoreArchetypeFitness(subject, policy);
  score.inversion_quality = scoreInversionQuality(subject);
  score.stretto_potential = scoreStrettoPotential(subject);
  score.kopfmotiv_strength = scoreKopfmotivStrength(subject, policy);
  return score;
}

bool ArchetypeScorer::checkHardGate(const Subject& subject,
                                     const ArchetypePolicy& policy) const {
  if (subject.notes.size() < 2) return false;

  // Hard gate: require_invertible — inversion must be reasonable quality.
  if (policy.require_invertible) {
    float inv = scoreInversionQuality(subject);
    if (inv < 0.40f) return false;
  }

  // Hard gate: require_fragmentable — Kopfmotiv must be distinctive.
  if (policy.require_fragmentable) {
    float kopf = scoreKopfmotivStrength(subject, policy);
    if (kopf < 0.40f) return false;
  }

  // Hard gate: require_contour_symmetry — ascending and descending motion
  // should be roughly balanced (within 70:30 ratio).
  if (policy.require_contour_symmetry) {
    int ascending = 0, descending = 0;
    for (size_t i = 1; i < subject.notes.size(); ++i) {
      int interval = static_cast<int>(subject.notes[i].pitch) -
                     static_cast<int>(subject.notes[i - 1].pitch);
      if (interval > 0) ++ascending;
      else if (interval < 0) ++descending;
    }
    int total = ascending + descending;
    if (total > 0) {
      float ratio = static_cast<float>(std::min(ascending, descending)) /
                    static_cast<float>(total);
      if (ratio < 0.25f) return false;  // Less than 25:75 = reject.
    }
  }

  // Hard gate: require_functional_resolution — chromatic steps must resolve.
  if (policy.require_functional_resolution) {
    int unresolved_chromatic = 0;
    for (size_t i = 1; i < subject.notes.size(); ++i) {
      int interval = std::abs(static_cast<int>(subject.notes[i].pitch) -
                              static_cast<int>(subject.notes[i - 1].pitch));
      if (interval == 1) {
        // Chromatic step: check if next note resolves (continues direction
        // or returns, i.e., next interval is 1 or 2 semitones).
        if (i + 1 < subject.notes.size()) {
          int next_interval =
              std::abs(static_cast<int>(subject.notes[i + 1].pitch) -
                       static_cast<int>(subject.notes[i].pitch));
          if (next_interval > 3) ++unresolved_chromatic;
        }
      }
    }
    if (unresolved_chromatic > policy.max_consecutive_chromatic) return false;
  }

  // Hard gate: require_axis_stability — inversion must stay within range.
  if (policy.require_axis_stability) {
    uint8_t pivot = subject.notes[0].pitch;
    ScaleType scale =
        subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

    // 1) Max deviation: M6 (9 semitones) from pivot causes extreme register
    //    displacement when inverted.
    int max_dev = 0;
    for (const auto& note : subject.notes) {
      int dev = std::abs(static_cast<int>(note.pitch) - static_cast<int>(pivot));
      if (dev > max_dev) max_dev = dev;
    }
    if (max_dev > 9) return false;

    // 2) Inversion simulation: inverted melody must fit within C2-C7.
    auto inverted = invertMelodyDiatonic(subject.notes, pivot, subject.key, scale);
    if (!inverted.empty()) {
      for (const auto& inv_note : inverted) {
        if (inv_note.pitch < organ_range::kManual1Low ||
            inv_note.pitch > organ_range::kManual1High) return false;
      }
    }
  }

  return true;
}

float ArchetypeScorer::scoreArchetypeFitness(
    const Subject& subject, const ArchetypePolicy& policy) const {
  if (subject.notes.empty()) return 0.0f;

  float score = 1.0f;

  // Range fitness: is the subject's range within archetype bounds?
  int range_semitones = subject.range();
  // Approximate semitones-to-degrees: ~1.7 semitones per scale degree.
  float range_degrees_approx = static_cast<float>(range_semitones) / 1.7f;
  if (range_degrees_approx < static_cast<float>(policy.min_range_degrees)) {
    float deficit = static_cast<float>(policy.min_range_degrees) - range_degrees_approx;
    score -= deficit * 0.10f;
  }
  if (range_degrees_approx > static_cast<float>(policy.max_range_degrees)) {
    float excess = range_degrees_approx - static_cast<float>(policy.max_range_degrees);
    score -= excess * 0.10f;
  }

  // Step motion ratio fitness.
  int steps = 0, total_intervals = 0;
  for (size_t i = 1; i < subject.notes.size(); ++i) {
    int interval = std::abs(static_cast<int>(subject.notes[i].pitch) -
                            static_cast<int>(subject.notes[i - 1].pitch));
    if (interval <= 2) ++steps;
    ++total_intervals;
  }
  if (total_intervals > 0) {
    float step_ratio = static_cast<float>(steps) /
                       static_cast<float>(total_intervals);
    if (step_ratio < policy.min_step_ratio) {
      score -= (policy.min_step_ratio - step_ratio) * 0.5f;
    }
  }

  // Sixteenth note density fitness.
  int sixteenth_count = 0;
  for (const auto& note : subject.notes) {
    if (note.duration <= kSixteenthNote) ++sixteenth_count;
  }
  float sixteenth_density = static_cast<float>(sixteenth_count) /
                            static_cast<float>(subject.notes.size());
  if (sixteenth_density > policy.max_sixteenth_density) {
    score -= (sixteenth_density - policy.max_sixteenth_density) * 0.5f;
  }

  // Ending pitch preference: bonus for dominant ending when policy favors it.
  // Bach subjects typically end on the dominant to facilitate the answer entry.
  // This counterbalances the base validator's tonal_stability bonus for tonic endings.
  if (policy.dominant_ending_prob >= 0.5f && subject.notes.size() >= 2) {
    int key_root = static_cast<int>(subject.key);
    int dominant_pc = (key_root + 7) % 12;
    int last_pc = getPitchClass(subject.notes.back().pitch);
    if (last_pc == dominant_pc) {
      // Scale bonus by how strongly the policy favors dominant (0.5-1.0 → 0.15-0.30).
      score += 0.15f + (policy.dominant_ending_prob - 0.5f) * 0.30f;
    }
  }

  // Symmetry scoring: blend existing score with contour symmetry.
  if (policy.symmetry_score_weight > 0.0f) {
    float symmetry = evaluateContourSymmetry(subject);
    score = score * (1.0f - policy.symmetry_score_weight) +
            symmetry * policy.symmetry_score_weight;
  }

  return std::max(0.0f, std::min(1.0f, score));
}

float ArchetypeScorer::scoreInversionQuality(const Subject& subject) const {
  if (subject.notes.size() < 3) return 0.5f;

  // Find the pivot pitch (first note is typical for Bach inversions).
  uint8_t pivot = subject.notes[0].pitch;
  ScaleType scale = subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Invert the melody diatonically.
  auto inverted = invertMelodyDiatonic(subject.notes, pivot, subject.key, scale);
  if (inverted.empty()) return 0.0f;

  // Quality check 1: inverted range should be similar to original.
  uint8_t inv_lowest = 127, inv_highest = 0;
  for (const auto& n : inverted) {
    if (n.pitch < inv_lowest) inv_lowest = n.pitch;
    if (n.pitch > inv_highest) inv_highest = n.pitch;
  }
  int inv_range = static_cast<int>(inv_highest) - static_cast<int>(inv_lowest);
  int orig_range = subject.range();
  float range_similarity = 1.0f - std::abs(inv_range - orig_range) / 12.0f;
  range_similarity = std::max(0.0f, range_similarity);

  // Quality check 2: count parallel perfect intervals between original and
  // inverted (when played simultaneously at same tick offset).
  int parallel_perfects = 0;
  int checked_pairs = 0;
  for (size_t i = 1; i < subject.notes.size() && i < inverted.size(); ++i) {
    int orig_interval = static_cast<int>(subject.notes[i].pitch) -
                        static_cast<int>(inverted[i].pitch);
    int prev_interval = static_cast<int>(subject.notes[i - 1].pitch) -
                        static_cast<int>(inverted[i - 1].pitch);
    int orig_mod = getPitchClassSigned(orig_interval);
    int prev_mod = getPitchClassSigned(prev_interval);
    // Check for parallel unisons, 5ths, or octaves.
    bool is_perfect = (orig_mod == 0 || orig_mod == 7);
    bool was_perfect = (prev_mod == 0 || prev_mod == 7);
    if (is_perfect && was_perfect && orig_interval != prev_interval) {
      ++parallel_perfects;
    }
    ++checked_pairs;
  }
  float parallel_penalty = (checked_pairs > 0)
      ? static_cast<float>(parallel_perfects) /
            static_cast<float>(checked_pairs)
      : 0.0f;

  // Combined: range similarity weighted with parallel avoidance.
  float quality = range_similarity * 0.4f + (1.0f - parallel_penalty) * 0.6f;
  return std::max(0.0f, std::min(1.0f, quality));
}

float ArchetypeScorer::scoreStrettoPotential(const Subject& subject) const {
  if (subject.notes.size() < 3) return 0.0f;

  // Use half the subject length as max offset for stretto search.
  Tick max_offset = subject.length_ticks / 2;
  if (max_offset < kTicksPerBar) max_offset = kTicksPerBar;

  auto intervals = findValidStrettoIntervals(subject.notes, max_offset);

  // Score based on count: 0 intervals = 0.0, 1 = 0.5, 2+ = 0.75+.
  if (intervals.empty()) return 0.2f;  // Base score for any well-formed subject.
  if (intervals.size() == 1) return 0.6f;
  if (intervals.size() == 2) return 0.8f;
  return 1.0f;
}

float ArchetypeScorer::scoreKopfmotivStrength(
    const Subject& subject, const ArchetypePolicy& policy) const {
  if (subject.notes.size() < 3) return 0.0f;

  auto kopf = subject.extractKopfmotiv(4);
  if (kopf.size() < 2) return 0.0f;

  // Base score (existing logic): interval variety, rhythmic variety, opening gesture.
  float base_score = 0.0f;

  // Criterion 1: interval variety in Kopfmotiv.
  std::set<int> unique_intervals;
  for (size_t idx = 1; idx < kopf.size(); ++idx) {
    int interval = static_cast<int>(kopf[idx].pitch) -
                   static_cast<int>(kopf[idx - 1].pitch);
    unique_intervals.insert(interval);
  }
  float interval_ratio = static_cast<float>(unique_intervals.size()) /
                         static_cast<float>(kopf.size() - 1);
  base_score += interval_ratio * 0.4f;

  // Criterion 2: rhythmic variety.
  std::set<Tick> unique_durations;
  for (const auto& note : kopf) {
    unique_durations.insert(note.duration);
  }
  float rhythm_ratio = static_cast<float>(unique_durations.size()) /
                       static_cast<float>(kopf.size());
  base_score += rhythm_ratio * 0.3f;

  // Criterion 3: opening gesture.
  if (kopf.size() >= 2) {
    int first_interval = std::abs(static_cast<int>(kopf[1].pitch) -
                                  static_cast<int>(kopf[0].pitch));
    if (first_interval >= 3) {
      base_score += 0.3f;
    } else if (first_interval >= 1) {
      base_score += 0.15f;
    }
  }
  // P4/P5 opening leap bonus (soft, +0.04 to +0.06).
  if (kopf.size() >= 2) {
    int opening_interval = std::abs(static_cast<int>(kopf[1].pitch) -
                                     static_cast<int>(kopf[0].pitch));
    int simple = interval_util::compoundToSimple(opening_interval);
    if (simple == 7) {        // P5
      base_score += 0.06f;
    } else if (simple == 5) { // P4
      base_score += 0.04f;
    }
  }
  base_score = std::clamp(base_score, 0.0f, 1.0f);

  // Policy-weighted sub-scores.
  float w_frag = policy.fragment_reusability_weight;
  float w_seq = policy.sequence_potential_weight;
  float w_base = 1.0f - w_frag - w_seq;

  // Guard against w_frag + w_seq > 1.0.
  if (w_base < 0.0f) {
    w_base = 0.0f;
    w_frag = 0.5f;
    w_seq = 0.5f;
  }

  // If no policy weights, return base score directly (backward compatible).
  if (w_frag <= 0.0f && w_seq <= 0.0f) {
    return base_score;
  }

  ScaleType scale =
      subject.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  float reusability = evaluateFragmentReusability(kopf, subject.key, scale);
  float seq_potential = evaluateSequencePotential(kopf, subject.key, scale);

  return std::clamp(
      w_base * base_score + w_frag * reusability + w_seq * seq_potential,
      0.0f, 1.0f);
}

}  // namespace bach
