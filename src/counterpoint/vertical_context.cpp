// Implementation of generation-time vertical safety checks.
// Delegates to checkVerticalConsonance() for strong beats; adds weak-beat
// m2/M7/TT rejection.

#include "counterpoint/vertical_context.h"

#include "core/interval.h"
#include "core/pitch_utils.h"
#include "counterpoint/vertical_safe.h"

namespace bach {

bool VerticalContext::isSafe(Tick tick, uint8_t voice, uint8_t pitch) const {
  if (!placed_notes || !timeline) return true;

  uint8_t beat = beatInBar(tick);

  // Strong beats (0, 2): delegate entirely to checkVerticalConsonance.
  if (beat == 0 || beat == 2) {
    return checkVerticalConsonance(pitch, voice, tick, *placed_notes, *timeline,
                                   num_voices);
  }

  // Weak beats: reject harsh dissonances (m2=1, TT=6, M7=11).
  uint8_t melodic_prev = findPrevPitch(voice, tick);
  for (const auto& note : *placed_notes) {
    if (note.voice == voice) continue;
    if (note.start_tick + note.duration <= tick || note.start_tick > tick) continue;
    int simple =
        interval_util::compoundToSimple(absoluteInterval(pitch, note.pitch));
    if (simple == 1 || simple == 6 || simple == 11) {
      // Check if weak_beat_allow permits this dissonance.
      if (weak_beat_allow &&
          weak_beat_allow(tick, voice, pitch, note.pitch, simple, melodic_prev)) {
        continue;
      }
      return false;
    }
  }
  return true;
}

float VerticalContext::score(Tick tick, uint8_t voice, uint8_t pitch) const {
  if (!isSafe(tick, voice, pitch)) return 0.0f;
  if (!placed_notes) return 1.0f;

  // Count sounding notes at this tick (excluding self).
  int consonance_count = 0;
  int perfect_count = 0;
  int imperfect_count = 0;
  int total_sounding = 0;

  for (const auto& note : *placed_notes) {
    if (note.voice == voice) continue;
    if (note.start_tick + note.duration <= tick || note.start_tick > tick) continue;
    ++total_sounding;

    int simple =
        interval_util::compoundToSimple(absoluteInterval(pitch, note.pitch));
    if (interval_util::isPerfectConsonance(simple)) {
      ++perfect_count;
      ++consonance_count;
    } else if (interval_util::isConsonance(simple)) {
      ++imperfect_count;
      ++consonance_count;
    }
  }

  if (total_sounding == 0) return 1.0f;

  // Graduated scoring based on consonance quality.
  if (consonance_count == total_sounding) {
    if (perfect_count == total_sounding) return 1.0f;
    if (imperfect_count > 0) return 0.8f;
    return 0.5f;  // P4 between upper voices.
  }
  // Some non-consonant intervals (allowed on weak beats).
  return 0.3f;
}

uint8_t VerticalContext::findPrevPitch(uint8_t voice, Tick before_tick) const {
  if (!placed_notes) return 0;
  uint8_t best_pitch = 0;
  Tick best_tick = 0;
  for (const auto& note : *placed_notes) {
    if (note.voice != voice) continue;
    if (note.start_tick < before_tick && note.start_tick >= best_tick) {
      best_tick = note.start_tick;
      best_pitch = note.pitch;
    }
  }
  return best_pitch;
}

}  // namespace bach
