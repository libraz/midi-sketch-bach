// Implementation of gesture template utilities for Stylus Phantasticus.

#include "forms/gesture_template.h"

#include <algorithm>

namespace bach {

void tagGestureNotes(std::vector<NoteEvent>& notes, uint16_t gesture_id) {
  for (auto& n : notes) {
    n.gesture_id = gesture_id;
    n.source = BachNoteSource::ToccataGesture;
    // Voice 0 = Leader, voice 1 = OctaveEcho, voice 2+ = PedalHit.
    switch (n.voice) {
      case 0:
        n.gesture_role = GestureRole::Leader;
        break;
      case 1:
        n.gesture_role = GestureRole::OctaveEcho;
        break;
      default:
        n.gesture_role = GestureRole::PedalHit;
        break;
    }
  }
}

std::vector<int> extractGestureCoreIntervals(
    const std::vector<NoteEvent>& notes, uint16_t gesture_id) {
  // Collect Leader notes from this gesture, sorted by onset.
  struct LP {
    Tick tick;
    uint8_t pitch;
  };
  std::vector<LP> leaders;
  for (const auto& n : notes) {
    if (n.gesture_id == gesture_id &&
        n.gesture_role == GestureRole::Leader) {
      leaders.push_back({n.start_tick, n.pitch});
    }
  }
  std::sort(leaders.begin(), leaders.end(),
            [](const LP& a, const LP& b) { return a.tick < b.tick; });

  if (leaders.size() < 2) return {};

  // Compute all directed intervals.
  std::vector<int> all_intervals;
  all_intervals.reserve(leaders.size() - 1);
  for (size_t i = 1; i < leaders.size(); ++i) {
    all_intervals.push_back(static_cast<int>(leaders[i].pitch) -
                            static_cast<int>(leaders[i - 1].pitch));
  }

  // Extract descent portion: find the longest contiguous run of non-positive
  // intervals (descent + unison), excluding ornament patterns (short positive
  // intervals immediately followed by negative ones, e.g., mordent +1/-1).
  // Strategy: skip intervals that are part of ornament triplets (positive
  // followed by same-magnitude negative within 2 intervals).
  std::vector<int> descent;
  for (size_t i = 0; i < all_intervals.size(); ++i) {
    int ivl = all_intervals[i];
    if (ivl <= 0) {
      // Descending or unison — core descent.
      descent.push_back(ivl);
    } else {
      // Positive interval: check if it's an ornament (returns to original pitch).
      // Ornament pattern: +N followed by -N (or -N-1).
      if (i + 1 < all_intervals.size()) {
        int next = all_intervals[i + 1];
        if (next <= -ivl + 1 && next < 0) {
          // Skip this ornament pair (don't add either to descent).
          ++i;
          continue;
        }
      }
      // Non-ornament ascending: descent run has ended.
      if (!descent.empty()) break;
    }
  }

  return descent;
}

}  // namespace bach
