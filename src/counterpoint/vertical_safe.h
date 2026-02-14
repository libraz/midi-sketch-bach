// Vertical safety callback for resolveLeaps / repairRepeatedNotes.

#ifndef BACH_COUNTERPOINT_VERTICAL_SAFE_H
#define BACH_COUNTERPOINT_VERTICAL_SAFE_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// Creates a vertical_safe callback for resolveLeaps / repairRepeatedNotes.
/// Rejects candidates that create dissonance on accented beats (0, 2 in 4/4)
/// against sounding voices. Chord tones are always safe.
/// P4 is acceptable only between upper voices (bass-relative check).
///
/// NOTE: This currently rejects all strong-beat dissonances. If suspension
/// generation (4-3, 7-6, 9-8) is added in the future, this callback should
/// be extended with a preparation/resolution pattern check to allow valid
/// suspensions on accented beats.
inline std::function<bool(Tick, uint8_t, uint8_t)>
makeVerticalSafeCallback(const HarmonicTimeline& timeline,
                         const std::vector<NoteEvent>& notes,
                         uint8_t num_voices) {
  return [&timeline, &notes, num_voices](
             Tick tick, uint8_t voice, uint8_t cand_pitch) -> bool {
    // Only check on accented beats (0, 2 in 4/4). Weak beats always safe.
    uint8_t beat = beatInBar(tick);
    if (beat != 0 && beat != 2) return true;

    // Chord tone is always safe (consonant with harmonic context).
    if (isChordTone(cand_pitch, timeline.getAt(tick))) return true;

    // Find lowest sounding pitch at tick for bass-relative P4 judgment.
    // Include the candidate itself (it may become the new bass).
    uint8_t lowest = cand_pitch;
    for (const auto& n : notes) {
      if (n.voice == voice) continue;
      if (n.start_tick + n.duration <= tick || n.start_tick > tick) continue;
      if (n.pitch < lowest) lowest = n.pitch;
    }

    // Check consonance with each sounding voice.
    for (const auto& n : notes) {
      if (n.voice == voice) continue;
      if (n.start_tick + n.duration <= tick || n.start_tick > tick) continue;
      int reduced = interval_util::compoundToSimple(
          absoluteInterval(cand_pitch, n.pitch));
      if (!interval_util::isConsonance(reduced)) {
        // P4 acceptable between upper voices only (bass-relative rule).
        // In 3+ voice texture, allow P4 when neither pitch is the bass.
        if (num_voices >= 3 && reduced == interval::kPerfect4th) {
          uint8_t lower = std::min(cand_pitch, n.pitch);
          if (lower > lowest) continue;  // Neither is bass: OK.
        }
        return false;
      }
    }
    return true;
  };
}

}  // namespace bach

#endif  // BACH_COUNTERPOINT_VERTICAL_SAFE_H
