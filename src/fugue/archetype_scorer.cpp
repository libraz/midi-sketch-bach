/// @file
/// @brief Archetype-specific quality scoring for fugue subjects.

#include "fugue/archetype_scorer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

#include "fugue/stretto.h"
#include "fugue/subject_params.h"
#include "transform/motif_transform.h"

namespace bach {

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
  score.kopfmotiv_strength = scoreKopfmotivStrength(subject);
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
    float kopf = scoreKopfmotivStrength(subject);
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
    int last_pc = static_cast<int>(subject.notes.back().pitch) % 12;
    if (last_pc == dominant_pc) {
      // Scale bonus by how strongly the policy favors dominant (0.5-1.0 → 0.15-0.30).
      score += 0.15f + (policy.dominant_ending_prob - 0.5f) * 0.30f;
    }
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
    int orig_mod = ((orig_interval % 12) + 12) % 12;
    int prev_mod = ((prev_interval % 12) + 12) % 12;
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

float ArchetypeScorer::scoreKopfmotivStrength(const Subject& subject) const {
  if (subject.notes.size() < 3) return 0.0f;

  auto kopf = subject.extractKopfmotiv(4);
  if (kopf.size() < 2) return 0.0f;

  float score = 0.0f;

  // Criterion 1: interval variety in Kopfmotiv (unique intervals / count).
  std::set<int> unique_intervals;
  for (size_t i = 1; i < kopf.size(); ++i) {
    int interval = static_cast<int>(kopf[i].pitch) -
                   static_cast<int>(kopf[i - 1].pitch);
    unique_intervals.insert(interval);
  }
  float interval_ratio = static_cast<float>(unique_intervals.size()) /
                         static_cast<float>(kopf.size() - 1);
  score += interval_ratio * 0.4f;

  // Criterion 2: rhythmic variety (unique durations / count).
  std::set<Tick> unique_durations;
  for (const auto& n : kopf) {
    unique_durations.insert(n.duration);
  }
  float rhythm_ratio = static_cast<float>(unique_durations.size()) /
                       static_cast<float>(kopf.size());
  score += rhythm_ratio * 0.3f;

  // Criterion 3: presence of a clear opening gesture (leap or step pattern).
  if (kopf.size() >= 2) {
    int first_interval = std::abs(static_cast<int>(kopf[1].pitch) -
                                  static_cast<int>(kopf[0].pitch));
    // A clear gesture is either a notable leap (3+ semitones) or a
    // characteristic step pattern.
    if (first_interval >= 3) {
      score += 0.3f;  // Clear opening leap.
    } else if (first_interval >= 1) {
      score += 0.15f;  // Stepwise opening.
    }
  }

  return std::max(0.0f, std::min(1.0f, score));
}

}  // namespace bach
