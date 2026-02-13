// Harmonic time warp implementation.

#include "harmony/harmonic_time_warp.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bach {

namespace {

/// @brief Simple hash for seed → float derivation.
float hashToFloat(uint32_t seed) {
  uint32_t h = seed;
  h = (h ^ (h >> 16)) * 0x45d9f3bu;
  h = (h ^ (h >> 16)) * 0x45d9f3bu;
  h = h ^ (h >> 16);
  return static_cast<float>(h) / static_cast<float>(UINT32_MAX);
}

/// @brief Compute curvature from character and seed.
///
/// Curvature controls the overall warp intensity. Mapped to a range per
/// character, interpolated by seed hash.
///
/// @return Curvature value (higher = more warp).
float computeCurvature(GoldbergTempoCharacter character, uint32_t seed) {
  float t = hashToFloat(seed);
  float lo, hi;
  switch (character) {
    case GoldbergTempoCharacter::Stable:     lo = 0.1f; hi = 0.3f; break;
    case GoldbergTempoCharacter::Dance:      lo = 0.4f; hi = 0.8f; break;
    case GoldbergTempoCharacter::Expressive: lo = 0.7f; hi = 1.2f; break;
    case GoldbergTempoCharacter::Virtuosic:  lo = 0.8f; hi = 1.4f; break;
    case GoldbergTempoCharacter::Lament:     lo = 1.0f; hi = 1.8f; break;
  }
  return lo + (hi - lo) * t;
}

/// @brief Compute max delta fraction from character and curvature.
///
/// The max delta is the maximum fractional change applied to a single beat.
/// E.g. 0.05 = ±5% of beat_ticks.
///
/// @return Max delta fraction (0.005 to 0.08).
float computeMaxDelta(GoldbergTempoCharacter character, float curvature) {
  float min_c, max_c, min_d, max_d;
  switch (character) {
    case GoldbergTempoCharacter::Stable:
      min_c = 0.1f; max_c = 0.3f; min_d = 0.005f; max_d = 0.01f; break;
    case GoldbergTempoCharacter::Dance:
      min_c = 0.4f; max_c = 0.8f; min_d = 0.015f; max_d = 0.03f; break;
    case GoldbergTempoCharacter::Expressive:
      min_c = 0.7f; max_c = 1.2f; min_d = 0.03f;  max_d = 0.05f; break;
    case GoldbergTempoCharacter::Virtuosic:
      min_c = 0.8f; max_c = 1.4f; min_d = 0.04f;  max_d = 0.06f; break;
    case GoldbergTempoCharacter::Lament:
      min_c = 1.0f; max_c = 1.8f; min_d = 0.05f;  max_d = 0.08f; break;
  }
  float range = max_c - min_c;
  float t = (range > 0.0f) ? std::clamp((curvature - min_c) / range, 0.0f, 1.0f) : 0.5f;
  return min_d + (max_d - min_d) * t;
}

/// @brief Compute per-phrase asymmetry from seed.
///
/// Asymmetry biases the second half of the phrase to be heavier or lighter.
/// Range: [-0.15, +0.15]. Varies per phrase for binary repeat differentiation.
///
/// @return Asymmetry value.
float computeAsymmetry(uint32_t phrase_seed) {
  float t = hashToFloat(phrase_seed);
  return -0.15f + 0.3f * t;
}

/// @brief Compute beat-level weight from harmonic + metric data.
///
/// Combines harmonic tension gradient, cadence presence, bass resolution
/// with metric emphasis (Strong/Medium/Weak).
float computeBeatWeight(int grid_bar, int beat_in_bar,
                        const GoldbergStructuralGrid& grid,
                        MeterProfile meter) {
  const auto& bar = grid.getBar(grid_bar);

  // Harmonic gradient: tension change from previous bar.
  float prev_h = (grid_bar > 0) ? grid.getBar(grid_bar - 1).tension.harmonic : 0.0f;
  float harmonic_gradient = std::abs(bar.tension.harmonic - prev_h);

  // Cadence / resolution weights.
  float cadence_w = bar.cadence.has_value() ? 0.3f : 0.0f;
  float resolution_w = bar.bass_motion.resolution_pitch.has_value() ? 0.15f : 0.0f;

  // Bar harmonic weight.
  float bar_weight = harmonic_gradient * 0.5f + cadence_w + resolution_w;

  // Beat metrical emphasis.
  auto strength = getMetricalStrength(beat_in_bar, meter);
  float beat_emphasis = (strength == MetricalStrength::Strong)  ? 1.0f
                      : (strength == MetricalStrength::Medium) ? 0.7f
                                                                : 0.4f;
  return bar_weight * beat_emphasis;
}

/// @brief Compute per-beat deltas for a single 4-bar phrase.
///
/// Weights are computed from the grid, asymmetry-biased, mean-centered, and
/// normalized so that max |delta| = max_delta_fraction. Σδ=0 by construction.
///
/// @param phrase_idx Phrase index in the variation (0-based).
/// @param beats_per_bar Pulses per bar (pulsesPerBar).
/// @param grid Structural grid.
/// @param meter Meter profile.
/// @param max_delta Max fractional delta.
/// @param asymmetry Per-phrase asymmetry bias.
/// @return Vector of deltas, one per beat in the phrase.
std::vector<float> computePhraseDeltas(
    int phrase_idx, int beats_per_bar,
    const GoldbergStructuralGrid& grid,
    MeterProfile meter,
    float max_delta, float asymmetry) {
  const int beats_per_phrase = 4 * beats_per_bar;
  std::vector<float> weights(beats_per_phrase);

  // Compute raw weights.
  for (int b = 0; b < beats_per_phrase; ++b) {
    int bar_in_phrase = b / beats_per_bar;
    int beat_in_bar = b % beats_per_bar;
    int grid_bar = ((phrase_idx * 4) + bar_in_phrase) % 32;
    weights[b] = computeBeatWeight(grid_bar, beat_in_bar, grid, meter);
  }

  // Apply asymmetry: bias second half heavier/lighter.
  int half = beats_per_phrase / 2;
  for (int b = 0; b < beats_per_phrase; ++b) {
    float bias = (b < half) ? (1.0f - asymmetry) : (1.0f + asymmetry);
    weights[b] *= bias;
  }

  // Mean-center.
  float sum = 0.0f;
  for (float w : weights) sum += w;
  float mean = sum / static_cast<float>(beats_per_phrase);

  std::vector<float> deltas(beats_per_phrase);
  for (int b = 0; b < beats_per_phrase; ++b) {
    deltas[b] = weights[b] - mean;
  }

  // Normalize so max |delta| = max_delta_fraction.
  float max_abs = 0.0f;
  for (float d : deltas) {
    max_abs = std::max(max_abs, std::abs(d));
  }
  if (max_abs > 1e-6f) {
    float scale = max_delta / max_abs;
    for (auto& d : deltas) d *= scale;
  }

  return deltas;
}

}  // namespace

void applyHarmonicTimeWarp(
    std::vector<NoteEvent>& notes,
    const GoldbergStructuralGrid& grid,
    const GoldbergVariationDescriptor& desc,
    uint32_t seed) {
  if (notes.empty()) return;

  // Time signature parameters.
  const Tick bar_ticks = desc.time_sig.ticksPerBar();
  const int beats_per_bar = desc.time_sig.pulsesPerBar();
  const Tick beat_ticks = bar_ticks / static_cast<Tick>(beats_per_bar);
  const Tick phrase_ticks = 4 * bar_ticks;

  if (beat_ticks == 0 || phrase_ticks == 0) return;

  // Seed-derived global parameters.
  float curvature = computeCurvature(desc.tempo_character, seed);
  float max_delta = computeMaxDelta(desc.tempo_character, curvature);

  // Find max tick to determine phrase count.
  Tick max_tick = 0;
  for (const auto& note : notes) {
    Tick end = note.start_tick + note.duration;
    if (end > max_tick) max_tick = end;
  }
  int total_phrases = static_cast<int>((max_tick + phrase_ticks - 1) / phrase_ticks);

  // Pre-compute deltas for all phrases.
  std::vector<std::vector<float>> all_deltas(total_phrases);
  for (int p = 0; p < total_phrases; ++p) {
    // Per-phrase seed: vary asymmetry across repeated sections.
    uint32_t phrase_seed = seed ^ (static_cast<uint32_t>(p) * 0x9E3779B9u);
    float asymmetry = computeAsymmetry(phrase_seed);
    all_deltas[p] = computePhraseDeltas(p, beats_per_bar, grid, desc.meter_profile,
                                        max_delta, asymmetry);
  }

  // Apply warp to each note.
  for (auto& note : notes) {
    int phrase_idx = static_cast<int>(note.start_tick / phrase_ticks);
    if (phrase_idx >= total_phrases) phrase_idx = total_phrases - 1;

    Tick phrase_start = static_cast<Tick>(phrase_idx) * phrase_ticks;
    Tick tick_in_phrase = note.start_tick - phrase_start;

    int beats_per_phrase = 4 * beats_per_bar;
    int beat_in_phrase = static_cast<int>(tick_in_phrase / beat_ticks);
    if (beat_in_phrase >= beats_per_phrase) beat_in_phrase = beats_per_phrase - 1;

    Tick beat_start = static_cast<Tick>(beat_in_phrase) * beat_ticks;
    Tick offset_in_beat = tick_in_phrase - beat_start;

    const auto& deltas = all_deltas[phrase_idx];

    // Cumulative delta up to this beat.
    float cum_delta = 0.0f;
    for (int j = 0; j < beat_in_phrase; ++j) {
      cum_delta += deltas[j];
    }

    // Displacement: beat boundary shift + intra-beat scaling.
    float displacement = static_cast<float>(beat_ticks) * cum_delta
                       + static_cast<float>(offset_in_beat) * deltas[beat_in_phrase];

    // Apply tick displacement.
    int64_t new_tick = static_cast<int64_t>(note.start_tick)
                     + static_cast<int64_t>(std::round(displacement));
    // Clamp to phrase bounds (should not cross phrase boundary).
    if (new_tick < static_cast<int64_t>(phrase_start)) {
      new_tick = static_cast<int64_t>(phrase_start);
    }
    note.start_tick = static_cast<Tick>(new_tick);

    // Scale duration proportionally.
    float dur_scale = 1.0f + deltas[beat_in_phrase];
    Tick new_dur = static_cast<Tick>(std::max(1.0f, std::round(
        static_cast<float>(note.duration) * dur_scale)));
    note.duration = new_dur;
  }
}

}  // namespace bach
