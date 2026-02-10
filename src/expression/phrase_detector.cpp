// Implementation of phrase boundary detection from harmonic analysis.

#include "expression/phrase_detector.h"

namespace bach {

std::vector<PhraseBoundary> detectPhraseBoundaries(const HarmonicTimeline& timeline) {
  std::vector<PhraseBoundary> boundaries;
  const auto& events = timeline.events();

  if (events.size() < 2) {
    return boundaries;
  }

  for (size_t idx = 1; idx < events.size(); ++idx) {
    const auto& prev = events[idx - 1];
    const auto& curr = events[idx];

    // Key change: strong boundary.
    if (curr.key != prev.key || curr.is_minor != prev.is_minor) {
      PhraseBoundary boundary;
      boundary.tick = curr.tick;
      boundary.cadence = CadenceType::Perfect;  // Key changes are strong boundaries.
      boundaries.push_back(boundary);
      continue;  // Do not double-count if a cadence also occurs at a key change.
    }

    // Perfect cadence: V -> I.
    if (prev.chord.degree == ChordDegree::V && curr.chord.degree == ChordDegree::I) {
      PhraseBoundary boundary;
      boundary.tick = curr.tick;
      boundary.cadence = CadenceType::Perfect;
      boundaries.push_back(boundary);
      continue;
    }

    // Deceptive cadence: V -> vi.
    if (prev.chord.degree == ChordDegree::V && curr.chord.degree == ChordDegree::vi) {
      PhraseBoundary boundary;
      boundary.tick = curr.tick;
      boundary.cadence = CadenceType::Deceptive;
      boundaries.push_back(boundary);
      continue;
    }

    // Half cadence: arrival on V from a non-V chord.
    // Only flag as a half cadence if the previous chord was not also V
    // (avoid flagging sustained dominant pedals).
    if (curr.chord.degree == ChordDegree::V && prev.chord.degree != ChordDegree::V) {
      PhraseBoundary boundary;
      boundary.tick = curr.tick;
      boundary.cadence = CadenceType::Half;
      boundaries.push_back(boundary);
      continue;
    }
  }

  return boundaries;
}

}  // namespace bach
