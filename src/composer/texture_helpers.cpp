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

bool formsStrictPerfectParallel(int line_prev, int cand, int other_prev, int other_curr) {
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
  return prev_ic == curr_ic;  // same perfect interval at both onsets: parallel.
}

bool formsPerfectParallel(int line_prev, int cand, int other_prev, int other_curr) {
  if (formsStrictPerfectParallel(line_prev, cand, other_prev, other_curr)) {
    return true;
  }
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
  // Hidden perfect: same-direction arrival on a perfect from another interval,
  // judged forbidden when the upper of the two voices leaps (> 2 semitones).
  const int upper_motion = (cand >= other_curr) ? line_motion : other_motion;
  return std::abs(upper_motion) > 2;
}

int consonantChordTone(const detail::ChordSpec& chord, int voice, int band_lo, int band_hi,
                       int target, const std::vector<int>& theme_pitches, int line_prev,
                       const std::vector<ConcurrentMotion>& motions, detail::Mode mode,
                       bool downbeat, const std::vector<int>& window_pitches,
                       bool parallel_free_over_consonant) {
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
  // Four nested preferences. Within each tier, fewer mid-window clashes win
  // first (a sustained anchor should not be struck against by a mid-beat
  // dissonance from an already-placed faster line), then "nearest target".
  // The tier key packs (window clashes, distance) so onset consonance still
  // dominates and an empty window reproduces the previous nearest-wins order.
  // Parallel-freedom outranks consonance: when every consonant tone is
  // parallel-tied (earlier-placed verbatim or ornament lines can pin the
  // vertical that way), a mildly dissonant parallel-free tone -- including an
  // oblique repeat of the previous pitch -- beats a parallel fifth/octave,
  // the cardinal prohibition. The one exception: a parallel-free tone that
  // strikes ic 1/6/11 against a sounding theme tone is excluded from the
  // escape tier, so a consonant-but-parallel tone wins over a wrong-note
  // clash with the foreground line.
  int consonant_free = -1;  // consonant AND parallel-free (best).
  int consonant_free_key = 1 << 20;
  int free_any = -1;  // parallel-free, mildest clash profile (second).
  int free_any_key = 1 << 28;
  int consonant_any = -1;  // consonant, parallel allowed (third).
  int consonant_any_key = 1 << 20;
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
    int weighted_clashes = 0;     // sharp ic 1/6/11 clashes count double.
    bool sharp_vs_theme = false;  // strikes ic 1/6/11 against a theme tone.
    auto add_clash = [&](int sounding, bool is_theme) {
      if (isConsonantIc(pitch - sounding)) {
        return;
      }
      const int ic = std::abs(pitch - sounding) % 12;
      ++clashes;
      const bool sharp = (ic == 1 || ic == 6 || ic == 11);
      weighted_clashes += sharp ? 2 : 1;
      if (sharp && is_theme) {
        sharp_vs_theme = true;
      }
    };
    for (int theme : theme_pitches) {
      add_clash(theme, /*is_theme=*/true);
    }
    // Also stay consonant against every earlier voice sounding at this onset
    // (not just the theme): an off-downbeat diatonic anchor would otherwise be
    // free to clash with another figuration voice on the beat grid.
    for (const ConcurrentMotion& motion : motions) {
      if (motion.curr >= 0) {
        add_clash(motion.curr, /*is_theme=*/false);
      }
    }
    int window_clashes = 0;
    for (int sounding : window_pitches) {
      if (!isConsonantIc(pitch - sounding)) {
        ++window_clashes;
      }
    }
    const int dist = std::abs(pitch - target);
    // Distance stays below 128 (one MIDI band), so a 128 stride keeps the
    // (window clashes, distance) order strictly lexicographic. A candidate
    // farther than a fifth from the running pitch is demoted below every
    // near candidate regardless of its clash count: an anchor lurching a
    // sixth or more is audibly worse than tolerating a mid-window passing
    // clash near the line (the bass otherwise flees to a clash-free tone a
    // tenth away whenever a faster upper line brushes the near candidates).
    // Admissibility -- consonance, parallels, voice order -- is unaffected.
    const int far_stride = (dist > 7) ? (1 << 12) : 0;
    const int key = far_stride + window_clashes * 128 + dist;
    if (clashes == 0) {
      if (key < consonant_any_key) {
        consonant_any_key = key;
        consonant_any = pitch;
      }
      if (!is_parallel(pitch) && key < consonant_free_key) {
        consonant_free_key = key;
        consonant_free = pitch;
      }
    } else if (!is_parallel(pitch) && !sharp_vs_theme) {
      // Weighted clashes dominate the packed (window clashes, distance) key so
      // the mildest clash profile wins, nearest-target breaking ties. A
      // candidate that strikes ic 1/6/11 against a sounding THEME tone never
      // enters this tier: the theme is the foreground line, so a quarter-long
      // m2/M7/tritone against it is more audible than the perfect-interval
      // parallel this tier exists to dodge -- such a candidate may only
      // survive as the last-resort fallback.
      const int free_key = weighted_clashes * (1 << 14) + key;
      if (free_key < free_any_key) {
        free_any_key = free_key;
        free_any = pitch;
      }
    }
    // The clash stride must dominate the packed key even with the far-anchor
    // demotion folded in (key < 1<<13), so the last resort still minimizes
    // dissonance first and distance second.
    const int score = clashes * (1 << 13) + key;
    if (score < fallback_score) {
      fallback_score = score;
      fallback = pitch;
    }
  }
  if (consonant_free >= 0) {
    return consonant_free;
  }
  // Tier order between "parallel-free but clashing" and "consonant but
  // parallel" is a per-form contract: fugue-family figuration prefers the
  // parallel-free escape (a parallel fifth/octave is the cardinal
  // prohibition), while ground-variation forms hold every beat onset
  // mutually consonant over the held ground and keep consonance first.
  if (parallel_free_over_consonant && free_any >= 0) {
    return free_any;
  }
  if (consonant_any >= 0) {
    return consonant_any;
  }
  if (free_any >= 0) {
    return free_any;
  }
  return fallback >= 0 ? fallback : std::clamp(target, band_lo, band_hi);
}

void appendCadentialLanding(std::vector<MaterialNote>& line, Tick penult_bar_start,
                            Tick ticks_per_bar, int prefinal, int final_pitch, detail::Mode mode,
                            int band_lo, const detail::ChordSpec* downbeat_chord,
                            bool prefer_descending, bool lift_to_context) {
  const Tick eighth = duration::kEighthNote;
  const Tick half_bar = ticks_per_bar / 2;
  const int run_len = static_cast<int>(half_bar / eighth);  // 4 in 4/4, 3 in 3/4.

  if (lift_to_context) {
    // The formula's register is the caller's design value, but the line it
    // interrupts may be running an octave higher (a climax block compressing
    // against the keyboard ceiling). When the last figuration pitch before
    // the landing sits more than a major sixth above the approach run's entry
    // tone, re-octave the whole formula upward: pitch classes are preserved,
    // so consonance against the other voices is unchanged, and the close
    // connects from the line instead of falling off a registral cliff. The
    // lift never carries the trill site past d''' (MIDI 86, the keyboard
    // ceiling; +2 covers the trill's upper neighbour).
    int last_pitch = -1;
    Tick last_tick = 0;
    for (const MaterialNote& note : line) {
      if (note.start_tick < penult_bar_start && (last_pitch < 0 || note.start_tick >= last_tick)) {
        last_tick = note.start_tick;
        last_pitch = note.pitch;
      }
    }
    if (last_pitch >= 0) {
      const int run_entry = detail::scaleDown(prefinal, run_len, mode);
      const int gap_plain = std::abs(last_pitch - run_entry);
      const int gap_lifted = std::abs(last_pitch - (run_entry + 12));
      if (gap_plain > 9 && gap_lifted < gap_plain && prefinal + 14 <= 86) {
        prefinal += 12;
        final_pitch += 12;
      }
    }
  }

  // Drop everything from the penultimate bar on; the landing replaces it.
  line.erase(
      std::remove_if(line.begin(), line.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= penult_bar_start; }),
      line.end());

  // Approach run INTO the pre-final tone. Ascending by preference: in minor an
  // ascent into the raised leading tone takes the melodic-minor sixth degree
  // (A natural), avoiding the augmented second the natural-minor walk would
  // make. When the ascent would dip below the band floor (a narrow band whose
  // tonic hugs the floor), the run flips to a descent from above instead.
  std::vector<int> run(static_cast<std::size_t>(run_len));
  if (mode == detail::Mode::Minor && ((prefinal % 12) == 11)) {
    // Melodic-minor ascent into the leading tone: ... Eb F G A(natural) -> B.
    static constexpr int kMelodicOffsets[4] = {-8, -6, -4, -2};
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = prefinal + kMelodicOffsets[4 - run_len + idx];
  } else {
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = detail::scaleDown(prefinal, run_len - idx, mode);
  }
  if (prefer_descending || run.front() < band_lo) {
    // Descending run from above: e.g. F E D C -> B.
    for (int idx = 0; idx < run_len; ++idx)
      run[static_cast<std::size_t>(idx)] = detail::scaleUp(prefinal, run_len - idx, mode);
  }
  if (downbeat_chord != nullptr) {
    // Snap the downbeat eighth to the nearest chord tone (the figuration
    // downbeat rule reads the bar-head onset).
    const int third = downbeat_chord->minor ? 3 : 4;
    const int triad_pc[3] = {downbeat_chord->root_pc % 12, (downbeat_chord->root_pc + third) % 12,
                             (downbeat_chord->root_pc + 7) % 12};
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    int snapped = run.front();
    for (int delta = 0; delta <= 6; ++delta) {
      if (is_triad(run.front() + delta)) {
        snapped = run.front() + delta;
        break;
      }
      if (is_triad(run.front() - delta)) {
        snapped = run.front() - delta;
        break;
      }
    }
    run.front() = snapped;
  }

  for (int idx = 0; idx < run_len; ++idx) {
    MaterialNote note;
    note.start_tick = penult_bar_start + static_cast<Tick>(idx) * eighth;
    note.duration = eighth;
    note.pitch = static_cast<std::uint8_t>(std::clamp(run[static_cast<std::size_t>(idx)], 0, 127));
    line.push_back(note);
  }

  // Held pre-final tone over the bar's second half (the trill site).
  MaterialNote held;
  held.start_tick = penult_bar_start + half_bar;
  held.duration = ticks_per_bar - half_bar;
  held.pitch = static_cast<std::uint8_t>(std::clamp(prefinal, 0, 127));
  line.push_back(held);

  // Full-bar final tone (the held resolution).
  MaterialNote last;
  last.start_tick = penult_bar_start + ticks_per_bar;
  last.duration = ticks_per_bar;
  last.pitch = static_cast<std::uint8_t>(std::clamp(final_pitch, 0, 127));
  line.push_back(last);
}

void appendCompactCadentialLanding(std::vector<MaterialNote>& line, Tick final_bar_start,
                                   Tick ticks_per_bar, int prefinal, int final_pitch) {
  line.erase(
      std::remove_if(line.begin(), line.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= final_bar_start; }),
      line.end());
  const Tick half_bar = ticks_per_bar / 2;
  MaterialNote held;
  held.start_tick = final_bar_start;
  held.duration = half_bar;
  held.pitch = static_cast<std::uint8_t>(std::clamp(prefinal, 0, 127));
  line.push_back(held);
  MaterialNote last;
  last.start_tick = final_bar_start + half_bar;
  last.duration = ticks_per_bar - half_bar;
  last.pitch = static_cast<std::uint8_t>(std::clamp(final_pitch, 0, 127));
  line.push_back(last);
}

}  // namespace bach::composer
