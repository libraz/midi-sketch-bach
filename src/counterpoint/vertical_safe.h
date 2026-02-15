// Vertical safety callback for resolveLeaps / repairRepeatedNotes.

#ifndef BACH_COUNTERPOINT_VERTICAL_SAFE_H
#define BACH_COUNTERPOINT_VERTICAL_SAFE_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/basic_types.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

namespace {

/// Returns false if the candidate pitch creates a dissonance with any sounding
/// note at the given tick (accented-beat-only check with P4 upper-voice rule).
/// Returns true if the candidate passes the consonance check (or is on a weak
/// beat, or is a chord tone).
inline bool checkVerticalConsonance(uint8_t cand_pitch, uint8_t voice, Tick tick,
                                    const std::vector<NoteEvent>& notes,
                                    const HarmonicTimeline& timeline,
                                    uint8_t num_voices) {
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
}

}  // namespace

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
    return checkVerticalConsonance(cand_pitch, voice, tick, notes, timeline, num_voices);
  };
}

/// Creates a vertical_safe callback with parallel P5/P8 detection.
/// Includes all checks from makeVerticalSafeCallback plus:
/// - Rejects candidates that create parallel P5 or P8 with any sounding voice
/// - Uses per-voice sorted index for O(log N) pitch lookup at previous beat
/// - P4 is NOT checked for parallels (only consonance check applies)
/// - Oblique motion (one voice stationary) is allowed
/// - Checks candidate voice vs other voices only (third-party pairs ignored)
///
/// @param timeline Harmonic timeline for chord-tone checks.
/// @param notes Note events (must remain valid for callback lifetime).
/// @param num_voices Number of voices.
inline std::function<bool(Tick, uint8_t, uint8_t)>
makeVerticalSafeWithParallelCheck(const HarmonicTimeline& timeline,
                                  const std::vector<NoteEvent>& notes,
                                  uint8_t num_voices) {
  // Per-voice note index entry: reference into notes vector with time range.
  struct VN {
    size_t idx;
    Tick start;
    Tick end;
  };

  // Build per-voice sorted index at construction time.
  auto voice_index = std::make_shared<std::vector<std::vector<VN>>>(num_voices);
  for (size_t note_idx = 0; note_idx < notes.size(); ++note_idx) {
    const auto& note = notes[note_idx];
    if (note.voice < num_voices) {
      (*voice_index)[note.voice].push_back({note_idx, note.start_tick,
                                            note.start_tick + note.duration});
    }
  }
  for (auto& voice_notes : *voice_index) {
    std::sort(voice_notes.begin(), voice_notes.end(),
              [](const VN& lhs, const VN& rhs) { return lhs.start < rhs.start; });
  }

  // O(log N) pitch lookup: find the pitch sounding in a voice at a given tick.
  // Returns -1 if the voice is silent at that tick.
  auto pitchAt = [&notes_ref = notes, voice_idx = voice_index](uint8_t voice,
                                                                Tick tick) -> int {
    auto& vnv = (*voice_idx)[voice];
    if (vnv.empty()) return -1;
    auto iter = std::upper_bound(vnv.begin(), vnv.end(), tick,
                                 [](Tick tck, const VN& vn) { return tck < vn.start; });
    if (iter == vnv.begin()) return -1;
    --iter;
    if (tick >= iter->start && tick < iter->end) {
      return static_cast<int>(notes_ref[iter->idx].pitch);
    }
    return -1;
  };

  return [&timeline, &notes, num_voices, pitchAt](Tick tick, uint8_t voice,
                                                   uint8_t cand_pitch) -> bool {
    // Weak beats and chord tones bypass all checks (including parallel).
    uint8_t beat = beatInBar(tick);
    if (beat != 0 && beat != 2) return true;
    if (isChordTone(cand_pitch, timeline.getAt(tick))) return true;

    // --- Consonance check ---
    if (!checkVerticalConsonance(cand_pitch, voice, tick, notes, timeline, num_voices))
      return false;

    // --- Parallel P5/P8 check: candidate voice vs each other voice ---

    // Find the previous beat tick.
    Tick prev_tick = (tick >= kTicksPerBeat) ? tick - kTicksPerBeat : 0;
    if (prev_tick == tick) return true;  // No previous beat data.

    for (uint8_t other_voice = 0; other_voice < num_voices; ++other_voice) {
      if (other_voice == voice) continue;

      // Get pitches at current tick and previous tick.
      int other_curr = pitchAt(other_voice, tick);
      if (other_curr < 0) continue;  // Voice silent at current tick.

      int other_prev = pitchAt(other_voice, prev_tick);
      if (other_prev < 0) continue;  // No previous data.

      // Get candidate voice's pitch at previous tick.
      int cand_prev = pitchAt(voice, prev_tick);
      if (cand_prev < 0) continue;  // No previous data for candidate voice.

      // Current interval (using candidate pitch).
      int curr_interval = std::abs(static_cast<int>(cand_pitch) - other_curr);
      // Previous interval.
      int prev_interval = std::abs(cand_prev - other_prev);

      // Both must be perfect consonances (P1, P5, P8 -- P4 excluded).
      if (!interval_util::isPerfectConsonance(curr_interval) ||
          !interval_util::isPerfectConsonance(prev_interval)) {
        continue;
      }

      // Same simple interval type (e.g., both P5 or both P8).
      int curr_simple = interval_util::compoundToSimple(curr_interval);
      int prev_simple = interval_util::compoundToSimple(prev_interval);
      if (curr_simple != prev_simple) continue;

      // Similar motion check: both voices move in the same direction.
      // Delta = 0 means oblique motion -- not parallel -- allowed.
      int motion_cand = static_cast<int>(cand_pitch) - cand_prev;
      int motion_other = other_curr - other_prev;
      if (motion_cand != 0 && motion_other != 0 &&
          (motion_cand > 0) == (motion_other > 0)) {
        return false;  // Parallel perfect consonance detected.
      }
    }

    return true;
  };
}

}  // namespace bach

#endif  // BACH_COUNTERPOINT_VERTICAL_SAFE_H
