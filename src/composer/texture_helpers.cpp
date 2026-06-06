#include "composer/texture_helpers.h"

#include <algorithm>

namespace bach::composer {

void ThemeToneRegistry::record(Tick tick, VoiceId voice, int pitch, Tick duration) {
  tones_.push_back(ThemeTone{tick, duration, voice, pitch});
}

int ThemeToneRegistry::soundingPitchInVoice(VoiceId voice, Tick tick) const {
  int pitch = -1;
  // Signed sentinel: Tick is unsigned, so a Tick(-1) start would wrap to the
  // maximum value and never lose the "latest onset wins" comparison.
  long long best_start = -1;
  for (const ThemeTone& tone : tones_) {
    if (tone.voice != voice) {
      continue;
    }
    if (tick >= tone.tick &&
        tick<tone.tick + tone.duration&& static_cast<long long>(tone.tick)> best_start) {
      best_start = static_cast<long long>(tone.tick);
      pitch = tone.pitch;
    }
  }
  return pitch;
}

void ThemeToneRegistry::concurrentThemePitches(Tick tick, VoiceId voice,
                                               std::vector<int>& out_pitches) const {
  out_pitches.clear();
  for (const ThemeTone& tone : tones_) {
    if (tone.voice == voice) {
      continue;
    }
    if (tick >= tone.tick && tick < tone.tick + tone.duration) {
      out_pitches.push_back(tone.pitch);
    }
  }
}

void ThemeToneRegistry::concurrentMotions(Tick prev_tick, Tick tick, VoiceId voice,
                                          VoiceId num_voices,
                                          std::vector<ConcurrentMotion>& out_motions) const {
  out_motions.clear();
  for (VoiceId other = 0; other < num_voices; ++other) {
    if (other == voice) {
      continue;
    }
    const int curr = soundingPitchInVoice(other, tick);
    if (curr < 0) {
      continue;
    }
    const int prev = (prev_tick >= 0) ? soundingPitchInVoice(other, prev_tick) : -1;
    out_motions.push_back(ConcurrentMotion{other, prev, curr});
  }
}

bool formsPerfectParallel(int line_prev, int cand, int other_prev, int other_curr) {
  if (line_prev < 0 || other_prev < 0 || other_curr < 0) {
    return false;  // need both voices' two onsets to judge motion.
  }
  const int line_motion = cand - line_prev;
  const int other_motion = other_curr - other_prev;
  if (line_motion == 0 || other_motion == 0) {
    return false;  // oblique motion is always allowed.
  }
  const bool same_dir =
      (line_motion > 0 && other_motion > 0) || (line_motion < 0 && other_motion < 0);
  if (!same_dir) {
    return false;  // contrary motion is always allowed.
  }
  const int curr_ic = ((std::abs(cand - other_curr) % 12) + 12) % 12;
  if (curr_ic != 0 && curr_ic != 7) {
    return false;  // arrival is not a perfect fifth/octave.
  }
  const int prev_ic = ((std::abs(line_prev - other_prev) % 12) + 12) % 12;
  if (prev_ic == curr_ic) {
    return true;  // same perfect interval at both onsets: parallel.
  }
  // Hidden perfect: same-direction arrival on a perfect from another interval,
  // judged forbidden when the upper of the two voices leaps (> 2 semitones).
  const int upper_motion = (cand >= other_curr) ? line_motion : other_motion;
  return std::abs(upper_motion) > 2;
}

int consonantChordTone(const detail::ChordSpec& chord, int voice, int band_lo, int band_hi,
                       int target, const std::vector<int>& theme_pitches, int line_prev,
                       const std::vector<ConcurrentMotion>& motions, detail::Mode mode,
                       bool downbeat) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  auto is_anchor_tone = [&](int midi) {
    return downbeat ? is_triad(midi) : (is_triad(midi) || detail::inScale(midi, mode));
  };
  auto is_parallel = [&](int cand) {
    for (const ConcurrentMotion& motion : motions) {
      if (formsPerfectParallel(line_prev, cand, motion.prev, motion.curr)) {
        return true;
      }
    }
    return false;
  };
  // Voice-ordering window: a lower-indexed voice sounds higher (V0 highest).
  // The anchor must stay at or below every concurrent lower-index voice and at
  // or above every concurrent higher-index voice, so the per-tick order
  // V0 >= V1 >= V2 holds even when a verbatim entry briefly leaves its own band
  // (e.g. a wide stretto follower) -- the band filter alone cannot guarantee it.
  int order_ceiling = band_hi;
  int order_floor = band_lo;
  for (const ConcurrentMotion& motion : motions) {
    if (motion.curr < 0) {
      continue;
    }
    if (motion.voice < voice) {
      order_ceiling = std::min(order_ceiling, motion.curr);  // stay below it.
    } else if (motion.voice > voice) {
      order_floor = std::max(order_floor, motion.curr);  // stay above it.
    }
  }
  // Three nested preferences, each "nearest target wins" within its tier.
  int consonant_free = -1;  // consonant AND parallel-free (best).
  int consonant_free_dist = 1 << 20;
  int consonant_any = -1;  // consonant, parallel allowed (second).
  int consonant_any_dist = 1 << 20;
  int fallback = -1;  // least dissonant (last resort).
  int fallback_score = 1 << 20;
  // Only apply the ordering window when it leaves a non-empty range; a degenerate
  // window (a concurrent voice already outside this band's order) falls back to
  // the band so a candidate always exists.
  const bool order_window_usable = order_floor <= order_ceiling;
  for (int pitch = band_lo; pitch <= band_hi; ++pitch) {
    if (!is_anchor_tone(pitch)) {
      continue;
    }
    if (order_window_usable && (pitch < order_floor || pitch > order_ceiling)) {
      continue;  // would cross a concurrent voice; never admissible.
    }
    int clashes = 0;
    for (int theme : theme_pitches) {
      if (!isConsonantIc(pitch - theme)) {
        ++clashes;
      }
    }
    // Also stay consonant against every earlier voice sounding at this onset
    // (not just the theme): an off-downbeat diatonic anchor would otherwise be
    // free to clash with another figuration voice on the beat grid.
    for (const ConcurrentMotion& motion : motions) {
      if (motion.curr >= 0 && !isConsonantIc(pitch - motion.curr)) {
        ++clashes;
      }
    }
    const int dist = std::abs(pitch - target);
    if (clashes == 0) {
      if (dist < consonant_any_dist) {
        consonant_any_dist = dist;
        consonant_any = pitch;
      }
      if (!is_parallel(pitch) && dist < consonant_free_dist) {
        consonant_free_dist = dist;
        consonant_free = pitch;
      }
    }
    const int score = clashes * 1000 + dist;
    if (score < fallback_score) {
      fallback_score = score;
      fallback = pitch;
    }
  }
  if (consonant_free >= 0) {
    return consonant_free;
  }
  if (consonant_any >= 0) {
    return consonant_any;
  }
  return fallback >= 0 ? fallback : std::clamp(target, band_lo, band_hi);
}

}  // namespace bach::composer
