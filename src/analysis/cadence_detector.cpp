/// @file
/// @brief Implementation of cadence detection -- read-only harmonic timeline analysis.

#include "analysis/cadence_detector.h"

#include <algorithm>
#include <cmath>

#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// @brief Check if a chord degree is V or V7 (dominant function).
static bool isDominant(const Chord& chord) {
  return chord.degree == ChordDegree::V;
}

/// @brief Check if a chord has seventh quality (V7-style).
static bool hasSeventh(const Chord& chord) {
  return chord.quality == ChordQuality::Dominant7 ||
         chord.quality == ChordQuality::Minor7 ||
         chord.quality == ChordQuality::MajorMajor7 ||
         chord.quality == ChordQuality::Diminished7 ||
         chord.quality == ChordQuality::HalfDiminished7;
}

/// @brief Check if a chord is iv in first inversion (Phrygian cadence precondition).
static bool isIV6(const Chord& chord, bool is_minor) {
  return chord.degree == ChordDegree::IV && chord.inversion == 1 && is_minor;
}

// ---------------------------------------------------------------------------
// detectCadences
// ---------------------------------------------------------------------------

std::vector<DetectedCadence> detectCadences(const HarmonicTimeline& timeline) {
  std::vector<DetectedCadence> results;

  const auto& events = timeline.events();
  if (events.size() < 2) {
    return results;
  }

  for (size_t idx = 1; idx < events.size(); ++idx) {
    const auto& prev = events[idx - 1];
    const auto& curr = events[idx];

    // Perfect cadence: V(7) -> I
    if (isDominant(prev.chord) && curr.chord.degree == ChordDegree::I) {
      DetectedCadence cadence;
      cadence.type = CadenceType::Perfect;
      cadence.tick = curr.tick;
      // Higher confidence when V has a 7th.
      cadence.confidence = hasSeventh(prev.chord) ? 0.95f : 0.9f;
      results.push_back(cadence);
      continue;
    }

    // Deceptive cadence: V -> vi
    if (isDominant(prev.chord) && curr.chord.degree == ChordDegree::vi) {
      DetectedCadence cadence;
      cadence.type = CadenceType::Deceptive;
      cadence.tick = curr.tick;
      cadence.confidence = 0.8f;
      results.push_back(cadence);
      continue;
    }

    // Phrygian cadence: iv6 -> V (in minor key)
    if (isIV6(prev.chord, prev.is_minor) && isDominant(curr.chord)) {
      DetectedCadence cadence;
      cadence.type = CadenceType::Phrygian;
      cadence.tick = curr.tick;
      cadence.confidence = 0.85f;
      results.push_back(cadence);
      continue;
    }

    // Half cadence: * -> V (any chord moving to V)
    // Only detect if the previous chord is NOT V itself (avoid V->V).
    if (curr.chord.degree == ChordDegree::V && !isDominant(prev.chord)) {
      DetectedCadence cadence;
      cadence.type = CadenceType::Half;
      cadence.tick = curr.tick;
      cadence.confidence = 0.7f;
      results.push_back(cadence);
      continue;
    }
  }

  // Results are already sorted by tick since events are chronological.
  return results;
}

// ---------------------------------------------------------------------------
// cadenceDetectionRate
// ---------------------------------------------------------------------------

float cadenceDetectionRate(const std::vector<DetectedCadence>& detected,
                           const std::vector<Tick>& planned,
                           Tick tolerance_ticks) {
  if (planned.empty()) {
    return 0.0f;
  }

  int matched = 0;
  for (Tick planned_tick : planned) {
    for (const auto& det : detected) {
      Tick distance = (det.tick >= planned_tick) ? (det.tick - planned_tick)
                                                : (planned_tick - det.tick);
      if (distance <= tolerance_ticks) {
        ++matched;
        break;
      }
    }
  }

  return static_cast<float>(matched) / static_cast<float>(planned.size());
}

}  // namespace bach
