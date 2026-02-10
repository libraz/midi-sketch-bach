// Implementation of phrase structure analysis and breathing point generation.

#include "core/phrase_structure.h"

#include <algorithm>
#include <cstdlib>

namespace bach {

namespace {

/// @brief Standard breathing point duration (1/4 beat = 120 ticks at 480 TPB).
constexpr Tick kDefaultBreathDuration = kTicksPerBeat / 4;

/// @brief Minimum phrase length to be considered valid (1 bar).
constexpr Tick kMinPhraseLength = kTicksPerBar;

}  // namespace

PhraseStructure PhraseStructure::fromCadenceTicks(
    const std::vector<Tick>& cadence_ticks, Tick total_duration) {
  PhraseStructure result;

  if (total_duration == 0) return result;

  // Build phrase boundaries from cadence positions.
  std::vector<Tick> boundaries;
  boundaries.push_back(0);  // Start of piece.
  for (Tick ct : cadence_ticks) {
    if (ct > 0 && ct < total_duration) {
      boundaries.push_back(ct);
    }
  }
  boundaries.push_back(total_duration);

  // Remove duplicates and sort.
  std::sort(boundaries.begin(), boundaries.end());
  boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                   boundaries.end());

  // Create phrases from boundary pairs.
  int pair_counter = 0;
  for (size_t idx = 0; idx + 1 < boundaries.size(); ++idx) {
    Tick start = boundaries[idx];
    Tick end = boundaries[idx + 1];

    // Skip very short segments.
    if (end - start < kMinPhraseLength / 2) continue;

    Phrase phrase;
    phrase.start_tick = start;
    phrase.end_tick = end;

    // Assign roles in pairs: Antecedent, Consequent, Antecedent, ...
    // The last phrase (if odd) is Free.
    if (idx + 2 < boundaries.size()) {
      phrase.role = (pair_counter % 2 == 0) ? Phrase::Role::Antecedent
                                             : Phrase::Role::Consequent;
      ++pair_counter;
    } else {
      phrase.role = Phrase::Role::Free;
    }

    result.phrases_.push_back(phrase);

    // Add breathing point at phrase boundary (except at the very end).
    if (idx + 2 < boundaries.size()) {
      BreathingPoint bp;
      bp.tick = end;
      bp.duration = kDefaultBreathDuration;
      result.breathing_points_.push_back(bp);
    }
  }

  return result;
}

Tick PhraseStructure::distanceToPhraseBoundary(Tick tick) const {
  Tick min_distance = static_cast<Tick>(-1);  // Max value.

  for (const auto& phrase : phrases_) {
    Tick d_start = static_cast<Tick>(
        std::abs(static_cast<int64_t>(tick) - static_cast<int64_t>(phrase.start_tick)));
    Tick d_end = static_cast<Tick>(
        std::abs(static_cast<int64_t>(tick) - static_cast<int64_t>(phrase.end_tick)));

    if (d_start < min_distance) min_distance = d_start;
    if (d_end < min_distance) min_distance = d_end;
  }

  return min_distance;
}

}  // namespace bach
