// Implementation of implied voice analysis for monophonic lines.

#include "analysis/implied_voice_analyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace bach {

namespace {

/// @brief Count parallel perfect intervals between two note sequences.
///
/// Checks consecutive note pairs in each voice for parallel motion
/// arriving at perfect intervals (unison, perfect 5th, octave).
///
/// @param upper Upper voice notes (sorted by start_tick).
/// @param lower Lower voice notes (sorted by start_tick).
/// @return Number of parallel perfect interval violations.
uint32_t countImpliedParallels(const std::vector<NoteEvent>& upper,
                                const std::vector<NoteEvent>& lower) {
  uint32_t count = 0;

  // Build a tick-aligned comparison using the smaller voice's tick positions.
  size_t ui = 0, li = 0;
  int prev_interval = -1;

  while (ui < upper.size() && li < lower.size()) {
    // Advance to the nearest aligned pair.
    while (ui < upper.size() && li < lower.size() &&
           upper[ui].start_tick != lower[li].start_tick) {
      if (upper[ui].start_tick < lower[li].start_tick) {
        ++ui;
      } else {
        ++li;
      }
    }
    if (ui >= upper.size() || li >= lower.size()) break;

    int interval = std::abs(static_cast<int>(upper[ui].pitch) -
                            static_cast<int>(lower[li].pitch)) % 12;

    bool is_perfect = (interval == 0 || interval == 7);

    if (prev_interval >= 0 && is_perfect) {
      bool prev_perfect = (prev_interval == 0 || prev_interval == 7);
      if (prev_perfect && interval == prev_interval) {
        ++count;
      }
    }

    prev_interval = interval;
    ++ui;
    ++li;
  }

  return count;
}

/// @brief Calculate standard deviation of a pitch set.
float pitchStdDev(const std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return 0.0f;

  float sum = 0.0f;
  for (const auto& note : notes) sum += static_cast<float>(note.pitch);
  float mean = sum / static_cast<float>(notes.size());

  float var_sum = 0.0f;
  for (const auto& note : notes) {
    float diff = static_cast<float>(note.pitch) - mean;
    var_sum += diff * diff;
  }
  return std::sqrt(var_sum / static_cast<float>(notes.size()));
}

}  // namespace

ImpliedVoiceAnalysisResult ImpliedVoiceAnalyzer::analyze(
    const std::vector<NoteEvent>& melody,
    uint8_t register_split_pitch) {
  ImpliedVoiceAnalysisResult result;

  if (melody.size() < 4) {
    result.implied_voice_count = 1.0f;
    return result;
  }

  // Separate into upper and lower voices by register.
  std::vector<NoteEvent> upper_voice;
  std::vector<NoteEvent> lower_voice;

  for (const auto& note : melody) {
    if (note.pitch >= register_split_pitch) {
      upper_voice.push_back(note);
    } else {
      lower_voice.push_back(note);
    }
  }

  // Calculate implied voice count based on register distribution.
  float upper_ratio = static_cast<float>(upper_voice.size()) /
                       static_cast<float>(melody.size());

  // Voice count estimate: base 1.0 + contribution from each register.
  // Even distribution (50/50) implies 2 voices; skewed implies fewer.
  float balance = 1.0f - std::abs(upper_ratio - (1.0f - upper_ratio));
  result.implied_voice_count = 1.0f + balance * 1.8f;

  // Register consistency: inverse of standard deviation within each voice.
  float upper_std = pitchStdDev(upper_voice);
  float lower_std = pitchStdDev(lower_voice);
  float avg_std = (upper_std + lower_std) / 2.0f;

  // Normalize: std of 0 = perfect consistency (1.0), std of 12+ = poor (0.0).
  result.register_consistency = std::max(0.0f, 1.0f - avg_std / 12.0f);

  // Count parallel perfect intervals between implied voices.
  result.implied_parallel_count = countImpliedParallels(upper_voice, lower_voice);

  // Quality gate: voice count in [2.3, 2.8] and no more than 2 parallel violations.
  result.passes_quality_gate =
      result.implied_voice_count >= 2.3f &&
      result.implied_voice_count <= 2.8f &&
      result.implied_parallel_count <= 2;

  return result;
}

uint8_t ImpliedVoiceAnalyzer::estimateSplitPitch(
    const std::vector<NoteEvent>& melody) {
  if (melody.empty()) return 60;  // Default to C4.

  std::vector<uint8_t> pitches;
  pitches.reserve(melody.size());
  for (const auto& note : melody) {
    pitches.push_back(note.pitch);
  }

  std::sort(pitches.begin(), pitches.end());
  return pitches[pitches.size() / 2];
}

}  // namespace bach
