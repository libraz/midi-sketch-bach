#include "composer/figuration_palette.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "composer/texture_helpers.h"

namespace bach::composer {

namespace {

// Diatonic-degree index base: C0 = MIDI 12 is degree 0 (well below the
// figuration registers, so indices stay non-negative).
constexpr int kDegreeBase = 12;

// Subdivision tick lengths on the 4/4 beat grid.
constexpr Tick kQuarter = kTicksPerBeat;
constexpr Tick kEighth = kTicksPerBeat / 2;
constexpr Tick kSixteenth = kTicksPerBeat / 4;

/// @brief Append a single clamped note to a material vector.
void addNote(std::vector<MaterialNote>& dst, Tick tick, Tick dur, int pitch) {
  MaterialNote note;
  note.start_tick = tick;
  note.duration = dur;
  note.pitch = static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
  dst.push_back(note);
}

/// @brief Convert a 4/4 bar index to its starting tick.
Tick barTick(int bar) {
  return static_cast<Tick>(bar) * kTicksPerBar;
}

/// @brief Resolve a ground cycle's per-beat chord-tone anchor-degree chain.
///
/// Each anchor takes the octave of its chord tone NEAREST the previous anchor,
/// so consecutive beat onsets never leap more than a tritone (the minimal
/// octave distance between two pitch classes is <= 6 semitones). At each bar's
/// FIRST beat the fit reference is re-centered toward `center` -- but only when
/// that does not move the anchor by more than a tritone from the running pitch
/// -- so the line cannot drift an octave away over the cycle's descending chord
/// roots, yet a re-center never itself introduces a leap.
std::vector<int> resolveAnchorDegrees(const std::vector<CycleBar>& cycle_bar_plan, int center,
                                      int anchor_rotation, detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  std::vector<int> anchor_deg;
  anchor_deg.reserve(static_cast<std::size_t>(cycle_bars) * 3);
  int running = center;
  for (int bar = 0; bar < cycle_bars; ++bar) {
    const std::vector<int> pcs =
        barAnchorPitchClasses(cycle_bar_plan[static_cast<std::size_t>(bar)], mode);
    for (int beat = 0; beat < 3; ++beat) {
      const int anchor_pc =
          pcs[static_cast<std::size_t>((anchor_rotation + beat) % static_cast<int>(pcs.size()))];
      int fit = fitPitchClass(anchor_pc, running);
      if (beat == 0) {
        // Bar downbeat: prefer the octave nearer `center` when it is within a
        // tritone of the running pitch (gentle re-centering, never a leap).
        const int recentered = fitPitchClass(anchor_pc, center);
        if (std::abs(recentered - running) <= 7)
          fit = recentered;
      }
      // Hard register band: nearest-octave fitting can ratchet monotonically
      // when consecutive chord tones keep resolving upward (or downward), and
      // once the line drifts more than a tritone from `center` the gentle
      // re-centering above can never engage again. Folding the anchor back by
      // whole octaves keeps it a chord tone (consonance preserved) while
      // pinning the tessitura to the variation band -- and keeps the realized
      // pitch inside the MIDI range.
      while (fit > center + 12)
        fit -= 12;
      while (fit < center - 12)
        fit += 12;
      anchor_deg.push_back(midiToDegree(fit, mode));
      running = fit;
    }
  }
  return anchor_deg;
}

/// @brief Realize a triad as three stacked chord tones inside a band.
///
/// The bass is the lowest root-class pitch at or above `band_lo`; the third
/// and fifth stack upward from it. Any tone that escapes `band_hi` folds down
/// by whole octaves (it stays a chord tone, only the voicing inverts). In
/// minor the leading tone B natural (pc 11, the harmonic-minor dominant
/// third) is lowered to the natural-minor third so the broken-chord line
/// stays in natural minor and no Ab->B augmented 2nd can arise.
void stackTriadInBand(std::uint8_t root_pc, bool minor, detail::Mode mode, int band_lo, int band_hi,
                      int out_tones[3]) {
  int third_interval = minor ? 3 : 4;
  if (mode == detail::Mode::Minor && (root_pc + third_interval) % 12 == 11)
    third_interval = 3;  // natural-minor third: keep the line off the leading tone.
  const int pcs[3] = {root_pc % 12, (root_pc + third_interval) % 12, (root_pc + 7) % 12};
  int cursor = band_lo;
  for (int i = 0; i < 3; ++i) {
    int tone = cursor;
    while (((tone % 12) + 12) % 12 != pcs[i])
      ++tone;
    while (tone > band_hi)
      tone -= 12;
    if (tone < band_lo)
      tone += 12;  // a band narrower than an octave cannot hold all three; keep in range.
    out_tones[i] = tone;
    cursor = std::max(cursor, tone);
  }
}

/// @brief Return the next tone of the chord's triad strictly above `from`.
int chordToneAbove(int from, std::uint8_t root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int triad_pc[3] = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12};
  int cur = from + 1;
  while (true) {
    const int pc = ((cur % 12) + 12) % 12;
    if (pc == triad_pc[0] || pc == triad_pc[1] || pc == triad_pc[2])
      return cur;
    ++cur;
  }
}

}  // namespace

int degreeToMidi(int degree, detail::Mode mode) {
  return detail::scaleUp(kDegreeBase, degree, mode);
}

int midiToDegree(int midi, detail::Mode mode) {
  int degree = 0;
  while (degreeToMidi(degree + 1, mode) <= midi)
    ++degree;
  return degree;
}

int fitPitchClass(int pitch_class, int center) {
  const int base = ((pitch_class % 12) + 12) % 12;
  int candidate = center - ((center - base) % 12 + 12) % 12;  // <= center, same pc.
  if (center - candidate > 6)
    candidate += 12;  // round to the nearer octave.
  return candidate;
}

std::vector<int> barAnchorPitchClasses(const CycleBar& bar, detail::Mode mode) {
  const int third = bar.minor ? 3 : 4;
  std::vector<int> anchors;
  anchors.reserve(3);
  for (int interval : {0, third, 7}) {
    int pitch_class = (bar.root_pc + interval) % 12;
    // Flatten out-of-scale triad tones to the scale tone a semitone below
    // (every chromatic pitch class sits one semitone above a scale member in
    // both C major and C natural minor) BEFORE the consonance filter. The
    // anchors feed degree-space chains (midiToDegree / degreeToMidi) that can
    // only realize scale tones, so an unflattened tone would degrade there
    // silently and SKIP the consonance check -- e.g. the A of a D-F-A triad
    // realizing as Ab, a tritone over a D ground. Explicit flattening keeps
    // the useful cases (a major-mode B-root triad's D# anchoring as D) and
    // lets the filter reject the dissonant ones (that triad's F# -> F, a
    // tritone over the B ground).
    if (!detail::inScale(pitch_class, mode))
      pitch_class = (pitch_class + 11) % 12;
    if (!isConsonantIc(pitch_class - bar.ground_pc))
      continue;
    if (std::find(anchors.begin(), anchors.end(), pitch_class) != anchors.end())
      continue;
    anchors.push_back(pitch_class);
  }
  if (anchors.empty())
    anchors.push_back(bar.root_pc % 12);  // defensive: root is always consonant.
  return anchors;
}

void appendSawtoothCycle(std::vector<MaterialNote>& notes, Tick block_start,
                         const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                         int anchor_rotation, int notes_per_beat, detail::Mode mode) {
  // Register center for the cycle, anchored on the first bar's low tone lifted
  // by the arc shift (the descending chord roots do NOT lower it, so the
  // figuration keeps a stable C4-C5 tessitura).
  const int center = cycle_bar_plan.front().low_tone + register_shift;

  // --- Phase 1: resolve the full anchor-degree sequence (shared chain). ---
  const std::vector<int> anchor_deg =
      resolveAnchorDegrees(cycle_bar_plan, center, anchor_rotation, mode);

  // --- Phase 2: emit notes, filling each beat toward the next anchor ------
  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);
  const int onset_count = static_cast<int>(anchor_deg.size());
  for (int onset = 0; onset < onset_count; ++onset) {
    const int from_deg = anchor_deg[static_cast<std::size_t>(onset)];
    // Fill toward the next onset's anchor (the last onset reuses its own anchor,
    // i.e. a held final degree, since there is no successor in the block).
    const int to_deg =
        (onset + 1 < onset_count) ? anchor_deg[static_cast<std::size_t>(onset + 1)] : from_deg;
    const int delta = to_deg - from_deg;
    const int dir = (delta >= 0) ? 1 : -1;
    const int span = std::abs(delta);

    const int bar = onset / 3;
    const int beat = onset % 3;
    const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                           static_cast<Tick>(beat) * kTicksPerBeat;
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      MaterialNote mnote;
      mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
      mnote.duration = step;
      // sub == 0 is the sampled beat-onset anchor; later subs walk one diatonic
      // degree per sub toward (and possibly past) the next anchor. The walk is
      // NOT clamped at the anchor: a clamped fill parks on the next onset's
      // pitch and re-attacks it, chaining repeated notes the reference corpus
      // almost never writes (~3% of transitions). A fill that would land the
      // LAST sub exactly on the next anchor bends one step beyond it instead
      // (an echappee resolving onto the anchor by step). When the next anchor
      // repeats this one (span == 0, possible across a chord change that keeps
      // the anchor pitch class), the fill oscillates to the diatonic neighbour
      // toward the register center instead of holding.
      int degree = from_deg;
      if (sub > 0) {
        if (span == 0) {
          const int osc = (degreeToMidi(from_deg, mode) >= center) ? -1 : 1;
          degree = from_deg + ((sub % 2 == 1) ? osc : 0);
        } else {
          degree = from_deg + dir * sub;
          if (sub == notes_per_beat - 1 && degree == to_deg)
            degree = to_deg + dir;
        }
      }
      mnote.pitch = static_cast<std::uint8_t>(degreeToMidi(degree, mode));
      notes.push_back(mnote);
    }
  }
}

void appendScalarWaveCycle(std::vector<MaterialNote>& notes, Tick block_start,
                           const std::vector<CycleBar>& cycle_bar_plan, int band_lo, int band_hi,
                           int phase_rotation, bool descending_start, int notes_per_beat,
                           detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  const int lo_deg = midiToDegree(band_lo, mode);
  const int hi_deg = midiToDegree(band_hi, mode);

  // Start degree: the bottom of the band lifted by the cycle's phase rotation,
  // clamped into the band so a large rotation cannot escape it.
  int degree = lo_deg + (phase_rotation % std::max(1, hi_deg - lo_deg));
  if (degree > hi_deg)
    degree = hi_deg;
  int dir = descending_start ? -1 : 1;
  int last_midi = -1;  // previous emitted pitch (the snap must not re-attack it).

  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);
  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    for (int beat = 0; beat < 3; ++beat) {
      const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                             static_cast<Tick>(beat) * kTicksPerBeat;
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        // EVERY beat onset (sub 0): snap the wave to the bar's nearest chord
        // tone so each sampled beat onset is consonant with the held ground.
        // The triad tones sit at most two scale degrees from any wave position,
        // so the snap is a small turn (a third at worst), never a leap -- and
        // the run between onsets keeps the conjunct scalar surface. A bar-head
        // step-toward variant proved insufficient: at the sixteenth tier the
        // free wave put non-chord tones on the beats of entire cycles.
        // A wave tone whose interval class against the ground is already
        // consonant stays put: snapping it to a triad tone would trade a
        // stepwise wave interval for a skip without improving the vertical
        // surface (thirds and sixths over the ground are as clean as chord
        // tones), and at low density tiers those forced skips dominate the
        // melodic surface.
        if (sub == 0 &&
            !isConsonantIc(degreeToMidi(degree, mode) - static_cast<int>(plan.ground_pc))) {
          const std::vector<int> pcs = barAnchorPitchClasses(plan, mode);
          const int cur_midi = degreeToMidi(degree, mode);
          int best_midi = cur_midi;
          int best_dist = 128;
          int best_fwd_midi = cur_midi;
          int best_fwd_dist = 128;
          for (int pitch_class : pcs) {
            const int fit = fitPitchClass(pitch_class, cur_midi);
            for (int oct : {fit - 12, fit, fit + 12}) {
              if (oct < band_lo || oct > band_hi)
                continue;
              // Never snap back onto the pitch just emitted: a backward snap
              // re-attacks the previous note and reads as a stalled repeat.
              if (oct == last_midi)
                continue;
              const int dist = std::abs(oct - cur_midi);
              if (dist < best_dist) {
                best_dist = dist;
                best_midi = oct;
              }
              if ((oct - cur_midi) * dir >= 0 && dist < best_fwd_dist) {
                best_fwd_dist = dist;
                best_fwd_midi = oct;
              }
            }
          }
          // Prefer the chord tone AHEAD of the running direction when it is
          // within a third, so the snap extends the run instead of turning it
          // back (the nearest tone behind would add a turn + repeat figure and
          // skew the melodic-interval surface away from the corpus's stepwise
          // dominance); fall back to the nearest tone either side.
          degree = midiToDegree((best_fwd_dist <= 4) ? best_fwd_midi : best_midi, mode);
        }
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        mnote.pitch = static_cast<std::uint8_t>(degreeToMidi(degree, mode));
        notes.push_back(mnote);
        last_midi = static_cast<int>(mnote.pitch);
        // Advance one diatonic step, folding direction at the band edges so the
        // line oscillates within the fixed register band.
        degree += dir;
        if (degree >= hi_deg) {
          degree = hi_deg;
          dir = -1;
        } else if (degree <= lo_deg) {
          degree = lo_deg;
          dir = 1;
        }
      }
    }
  }
}

void appendScalarWaveBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                         detail::Mode mode, int notes_per_beat, int base_midi, int ceil_midi,
                         int offset, int& prev_pitch, bool rotate_figures, bool triplet) {
  // Triplet mode subdivides every beat into six sixteenth triplets (80 ticks
  // each) instead of the `notes_per_beat` grid; the anchor + wave logic below is
  // unchanged, only the subdivision count and step duration differ.
  const int npb = triplet ? 6 : notes_per_beat;
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  const int anchor_hi = ceil_midi - 6;  // leave headroom for the ascending wave.
  int anchor;
  if (prev_pitch < 0) {
    // Section's first bar: snap from the band floor shifted by the seed offset.
    anchor = base_midi;
    while (!is_triad(anchor)) {
      ++anchor;
    }
    anchor = detail::scaleUp(anchor, offset, mode);
    while (!is_triad(anchor)) {
      anchor = detail::scaleUp(anchor, 1, mode);
    }
    if (anchor > anchor_hi) {
      anchor = base_midi;
      while (!is_triad(anchor)) {
        ++anchor;
      }
    }
  } else {
    // Chain conjunctly: pick the chord tone nearest the previous bar's last
    // pitch (clamped into the wave's start window) so the bar boundary is a
    // small step rather than a leap to the band floor. In figure-rotation
    // mode the exact previous pitch is excluded: when a bar ends on a chord
    // tone the nearest candidate IS that pitch, and the resulting repeated
    // pair at nearly every bar seam piles up interval-0 mass the corpus does
    // not have (the non-rotating callers keep the legacy choice).
    const int target = std::clamp(prev_pitch, base_midi, anchor_hi);
    int best = -1;
    int best_dist = 1 << 30;
    for (int cand = base_midi; cand <= anchor_hi; ++cand) {
      if (!is_triad(cand)) {
        continue;
      }
      if (rotate_figures && cand == prev_pitch) {
        continue;
      }
      const int dist = std::abs(cand - target);
      if (dist < best_dist) {
        best_dist = dist;
        best = cand;
      }
    }
    anchor = (best >= 0) ? best : base_midi;
    while (!is_triad(anchor)) {
      ++anchor;
    }
  }
  const int notes = 4 * npb;
  std::vector<int> wave;
  wave.reserve(static_cast<std::size_t>(notes) + 2);
  // Walk up by scale steps; when the next step would cross the ceiling, fold the
  // contour back down by scale steps instead of pinning to ceil_midi. Clamping
  // to the ceiling stacked several identical peak pitches into a flat plateau
  // (an interval-0 run the texture gate caps at 4 and the melodic-interval cost
  // penalises); reflecting at the ceiling keeps the line conjunct and in-band.
  int cursor = anchor;
  int dir = 1;
  wave.push_back(cursor);
  for (int idx = 1; idx <= notes / 2; ++idx) {
    int next = (dir > 0) ? detail::scaleUp(cursor, 1, mode) : detail::scaleDown(cursor, 1, mode);
    if (next > ceil_midi) {
      dir = -1;
      next = detail::scaleDown(cursor, 1, mode);
    } else if (next < base_midi) {
      dir = 1;
      next = detail::scaleUp(cursor, 1, mode);
    }
    cursor = std::clamp(next, base_midi, ceil_midi);
    wave.push_back(cursor);
  }
  for (int idx = static_cast<int>(wave.size()) - 2; idx >= 0; --idx) {
    wave.push_back(wave[static_cast<std::size_t>(idx)]);
  }
  if (rotate_figures) {
    // Bar-figure rotation over the precomputed wave (see the header note).
    // Rotation is the point: any single figure applied to every bar
    // over-concentrates the interval-bigram surface instead. The seed offset
    // phases the rotation so different seeds lay the figures over different
    // bars of the same section plan.
    const int figure_mode = (((bar + offset) % 4) + 4) % 4;
    if (figure_mode == 0) {
      // Broken-third chain: displace odd wave indices one scale step along
      // the local direction (c-d-e-f -> c-e-d-f), turning half the steps
      // into thirds while the contour endpoints stay put.
      for (std::size_t idx = 1; idx + 1 < wave.size(); idx += 2) {
        const int local_dir = (wave[idx + 1] >= wave[idx - 1]) ? 1 : -1;
        const int cand = (local_dir > 0) ? detail::scaleUp(wave[idx], 1, mode)
                                         : detail::scaleDown(wave[idx], 1, mode);
        if (cand >= base_midi && cand <= ceil_midi) {
          wave[idx] = cand;
        }
      }
    } else if (figure_mode == 1 && wave.size() > 4) {
      // One dive: from the bar's midpoint the rest of the wave shifts a
      // fourth or fifth (alternating across dive bars) toward the side with
      // register room -- an entry leap here, recovered by the next bar's
      // nearest-chord-tone anchor chain.
      const std::size_t dive_start = wave.size() / 2;
      const int degrees = (((bar + offset) / 4) % 2 == 0) ? 3 : 4;
      const bool down_ok = detail::scaleDown(wave[dive_start], degrees, mode) >= base_midi;
      for (std::size_t idx = dive_start; idx < wave.size(); ++idx) {
        const int cand = down_ok ? detail::scaleDown(wave[idx], degrees, mode)
                                 : detail::scaleUp(wave[idx], degrees, mode);
        if (cand >= base_midi && cand <= ceil_midi) {
          wave[idx] = cand;
        }
      }
    } else if (figure_mode == 2) {
      // Leap-and-cascade: leap from the band floor up to the triad tone a
      // sixth above, then fill back down by scale steps; re-leap when the
      // cascade returns to the floor. The ascending-sixth bins are the
      // corpus intervals no step wave or chord-tone sweep reaches, and the
      // classical leap-then-contrary-fill shape keeps every leap resolved.
      // The leap target is a chord tone, so the landing is consonant with a
      // sustained pedal root by construction.
      int floor_tone = base_midi;
      while (floor_tone <= ceil_midi && !is_triad(floor_tone)) {
        ++floor_tone;
      }
      int leap_to = floor_tone + 8;  // minor sixth; nudged up to the chord tone
      while (leap_to <= ceil_midi && !is_triad(leap_to)) {
        ++leap_to;
      }
      if (floor_tone <= ceil_midi && leap_to <= ceil_midi) {
        std::vector<int> cascade;
        cascade.reserve(wave.size());
        int cur = std::clamp(anchor, floor_tone, leap_to);
        cascade.push_back(cur);
        while (cascade.size() < wave.size()) {
          cur =
              (cur <= floor_tone) ? leap_to : std::max(detail::scaleDown(cur, 1, mode), floor_tone);
          cascade.push_back(cur);
        }
        wave = std::move(cascade);
      }
    } else if (figure_mode == 3) {
      // Arpeggio sweep: rebuild the wave stepping by triad tones instead of
      // scale steps (reflecting at the band edges like the step wave). The
      // chord-tone zigzag supplies the third/fourth interval bins no step
      // figure reaches, and every tone is consonant with the chord by
      // construction, so the sweep sits safely over a sustained pedal root.
      auto triadStep = [&](int from, int direction) {
        int cand = from + direction;
        while (cand >= base_midi - 12 && cand <= ceil_midi + 12 && !is_triad(cand)) {
          cand += direction;
        }
        return cand;
      };
      std::vector<int> arp;
      arp.reserve(wave.size());
      int arp_cursor = anchor;
      int arp_dir = 1;
      arp.push_back(arp_cursor);
      while (arp.size() < wave.size()) {
        int next = triadStep(arp_cursor, arp_dir);
        if (next > ceil_midi || next < base_midi) {
          arp_dir = -arp_dir;
          next = triadStep(arp_cursor, arp_dir);
        }
        if (next > ceil_midi || next < base_midi) {
          next = arp_cursor;  // band narrower than two triad tones (degenerate)
        }
        arp_cursor = next;
        arp.push_back(arp_cursor);
      }
      wave = std::move(arp);
    }
  }
  // Sixteenth triplets are exactly 80 ticks (kTicksPerBeat / 6, no rounding).
  const Tick step =
      triplet ? (kTicksPerBeat / 6)
              : ((notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kEighth : kQuarter));
  int last_pitch = anchor;
  for (int beat = 0; beat < 4; ++beat) {
    for (int sub = 0; sub < npb; ++sub) {
      const int slot = beat * npb + sub;
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      last_pitch = wave[static_cast<std::size_t>(slot) % wave.size()];
      addNote(dst, tick, step, last_pitch);
    }
  }
  prev_pitch = last_pitch;
}

WaveVetoStats& waveVetoStats() {
  static WaveVetoStats stats;
  return stats;
}

void appendFigurationWaveBar(ThemeToneRegistry& registry, FigurationSection& section, int bar,
                             int voice, const detail::ChordSpec& chord, detail::Mode mode,
                             int notes_per_beat, int offset, int& prev_anchor, int band_lo,
                             int band_hi, VoiceId num_voices, bool cadential_close) {
  // Register centre for the wave: a few scale degrees above the band floor,
  // shifted by the seed offset, kept clear of the band ceiling so a stepwise
  // fill never runs out of band. The per-beat anchor chain is gently pulled
  // back toward this centre so the conjunct walk cannot drift out of band.
  int center = detail::scaleUp(band_lo, offset + 2, mode);
  if (center > band_hi - 4) {
    center = detail::scaleUp(band_lo, offset, mode);
  }
  if (prev_anchor <= 0) {
    // Section seam: a fresh figuration span has no threaded anchor, but this
    // voice may have sounded right up to this bar in a previous span (a theme
    // entry or earlier figuration). The audible "previous pitch" for the
    // parallel judgment is that note, not the synthetic register centre --
    // with the centre a seam arrival can land an undetectable parallel
    // against another voice moving across the same seam.
    const int sounding =
        registry.soundingPitchInVoice(static_cast<VoiceId>(voice), barTick(bar) - kSixteenth);
    prev_anchor = (sounding >= 0) ? sounding : center;
  }
  const Tick step =
      (notes_per_beat == 4) ? kSixteenth : ((notes_per_beat == 2) ? kTicksPerBeat / 2 : kQuarter);
  std::vector<int> theme_pitches;
  // The line is a triangular scalar wave: it walks one scale step per note in a
  // single direction, reverses when it reaches the top or bottom of a working
  // band centred on `center`, and snaps each beat onset to the nearest consonant
  // chord tone (so the on-beat verticals stay consonant and the bar downbeat is a
  // genuine chord tone). Because the motion is one scale step per note, every
  // consecutive interval is a step except the small (third-sized) snap to a chord
  // tone at the beat onset -- the corpus melodic-interval mass is on steps.
  // Mutable: the window stretches to contain a beat anchor snapped outside it
  // (see the anchor commit below).
  int wave_lo = std::max(band_lo, center - 5);
  int wave_hi = std::min(band_hi, center + 5);
  // Walking cursor and direction, threaded across bars via prev_anchor's sign
  // (positive magnitude is the pitch; an even/odd parity is not stored, so the
  // direction restarts upward each bar -- the wave still reverses within a bar at
  // the band edges, which is what keeps consecutive bars from a sawtooth jump).
  int cursor = std::clamp(prev_anchor, wave_lo, wave_hi);
  int dir = (cursor <= center) ? 1 : -1;
  // Per-bar figure rotation. A purely stepwise wave over-concentrates the
  // melodic-interval surface on seconds (the corpus walks steps on only ~36%
  // of transitions and skips or leaps on ~61%), so the bar figure rotates:
  // a broken-third chain (c-e-d-f-e-g), one fourth/fifth dive per bar, and
  // the plain wave. Rotation matters as much as the figures themselves --
  // applying any single figure to every bar over-concentrates the interval
  // BIGRAM surface instead.
  const int figure_mode = ((bar % 3) + 3) % 3;
  // Prevailing direction of the broken-third arc. Tracked separately from
  // `dir` (which commits to whatever the vetted step actually did) so the
  // chain keeps its net slope until it reflects at the working band edge.
  int arc_dir = dir;
  auto stepScale = [&](int from, int direction) {
    return direction > 0 ? detail::scaleUp(from, 1, mode) : detail::scaleDown(from, 1, mode);
  };
  // Fold a clamped candidate back into the diatonic set, stepping toward the
  // roomier side of [lo, hi]. The voice-band edges themselves are chromatic
  // pitches (V1 spans Eb3..F#4), so a bare clamp to an edge would emit a
  // chromatic figuration tone; when the window has no diatonic member the
  // original candidate is kept (a window that tight pins against another
  // voice, where the unison-class tone is at least order-safe).
  auto foldIntoScale = [&](int candidate, int lo, int hi) {
    int folded = candidate;
    while (!detail::inScale(folded, mode)) {
      folded += (hi - folded >= folded - lo) ? 1 : -1;
      if (folded < lo || folded > hi) {
        return candidate;
      }
    }
    return folded;
  };
  // Walk `degrees` scale steps from `from_pitch` along `direction`, mirroring
  // at the working band edges (like the single-step wave) and clamping to the
  // voice band.
  auto leapScale = [&](int from_pitch, int degrees, int direction) {
    int candidate = direction > 0 ? detail::scaleUp(from_pitch, degrees, mode)
                                  : detail::scaleDown(from_pitch, degrees, mode);
    if (candidate > wave_hi || candidate < wave_lo) {
      candidate = direction > 0 ? detail::scaleDown(from_pitch, degrees, mode)
                                : detail::scaleUp(from_pitch, degrees, mode);
    }
    return foldIntoScale(std::clamp(candidate, band_lo, band_hi), band_lo, band_hi);
  };
  int last_pitch = cursor;
  // Last pitch this call actually emitted (-1 until the first note), used to
  // re-judge the beat anchor at the audible sub-beat grain below.
  int prev_emitted = -1;
  // Rolling window of the last four pitches emitted this bar. The wobble
  // breaker below uses it to detect a line that degenerated into a two-pitch
  // oscillation; a trailing window (rather than the whole bar) keeps the
  // detection blind to a bar-initial tone carried over from the previous bar.
  int recent_pitches[4] = {-1, -1, -1, -1};
  int emitted_count = 0;
  // This line's previous beat anchor, used to judge whether the next anchor
  // moves in parallel with an earlier voice. Seeded from prev_anchor (the prior
  // bar's last anchor) so the bar-boundary beat is also parallel-checked.
  int line_prev_anchor = (prev_anchor > 0) ? prev_anchor : -1;
  std::vector<ConcurrentMotion> motions;
  std::vector<int> window_pitches;
  for (int beat = 0; beat < 4; ++beat) {
    const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    registry.concurrentThemePitches(beat_tick, static_cast<VoiceId>(voice), theme_pitches);
    // Sample the earlier voices one sixteenth (the finest stride any line uses)
    // before the onset: that reproduces the union-onset note pair the validator
    // judges. A beat-wide window reads a sixteenth-note voice four notes back
    // and can miss (or invent) the audible motion into this onset. The line's
    // own "from" is likewise the last emitted sub-beat note when one exists,
    // not the previous beat anchor.
    registry.concurrentMotions(beat_tick - kSixteenth, beat_tick, static_cast<VoiceId>(voice),
                               num_voices, motions);
    const int audible_from = (prev_emitted >= 0) ? prev_emitted : line_prev_anchor;
    // Pitches already-placed voices attack INSIDE this anchor's sustain window
    // (after the onset). A quarter-note anchor under an earlier eighth-note
    // line can be onset-consonant yet sustained against a dissonant mid-beat
    // attack above it; consonantChordTone uses these as a tie-breaker.
    window_pitches.clear();
    for (Tick slot = beat_tick + kSixteenth; slot < beat_tick + step; slot += kSixteenth) {
      for (VoiceId other = 0; other < num_voices; ++other) {
        if (other == static_cast<VoiceId>(voice)) {
          continue;
        }
        const int sounding = registry.soundingPitchInVoice(other, slot);
        if (sounding >= 0) {
          window_pitches.push_back(sounding);
        }
      }
    }
    // Snap the beat onset to the nearest consonant, parallel-free anchor tone
    // (a chord tone on the downbeat, any diatonic tone off the downbeat).
    const int anchor = consonantChordTone(chord, voice, band_lo, band_hi, cursor, theme_pitches,
                                          audible_from, motions, mode, beat == 0, window_pitches,
                                          /*parallel_free_over_consonant=*/true);
    int snapped = std::clamp(anchor, band_lo, band_hi);
    // consonantChordTone relaxes parallel avoidance when every admissible
    // chord tone is parallel-tied (its second/third preference tiers), so the
    // snap can still land a parallel. Displace it to the nearest chord tone
    // that is consonant with the sounding theme tones and parallel-free; the
    // snap stands when no such tone exists within a fifth.
    if (audible_from >= 0) {
      auto anchor_is_parallel = [&](int cand) {
        for (const ConcurrentMotion& motion : motions) {
          if (formsPerfectParallel(audible_from, cand, motion.prev, motion.curr)) {
            return true;
          }
        }
        return false;
      };
      if (anchor_is_parallel(snapped)) {
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {((chord.root_pc % 12) + 12) % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        auto admissible = [&](int cand) {
          if (cand < band_lo || cand > band_hi || cand == snapped) {
            return false;
          }
          const int pc = ((cand % 12) + 12) % 12;
          if (pc != triad_pc[0] && pc != triad_pc[1] && pc != triad_pc[2]) {
            return false;
          }
          for (const int sounding : theme_pitches) {
            if (!isConsonantPair(cand, sounding)) {
              return false;
            }
          }
          return !anchor_is_parallel(cand);
        };
        for (int dist = 1; dist <= 7; ++dist) {
          bool placed = false;
          for (const int sgn : {-1, 1}) {
            const int cand = snapped + sgn * dist;
            if (admissible(cand)) {
              snapped = cand;
              placed = true;
              ++waveVetoStats().anchor_parallel_displaced;
              break;
            }
          }
          if (placed) {
            break;
          }
        }
      }
      // Wobble breaker: the theme-consonance, harshness, and parallel vetoes
      // can shrink the working window's admissible set to two tones (typical
      // against a sounding theme entry, where the scale degree above strikes a
      // seventh and the neighbour below a second), locking the line into an
      // a-b-a-b oscillation. When the last four emitted pitches sit on at
      // most two values and the snapped anchor would stay on them, displace
      // the anchor to a farther chord tone -- order-safe against concurrent
      // voices, consonant with the sounding theme tones, clash-free in the
      // sustain window, parallel-free -- so the bar states the chord in a new
      // register instead of wobbling.
      int bar_pitch_a = -1;
      int bar_pitch_b = -1;
      bool trailing_locked = emitted_count >= 4;
      for (int i = 0; trailing_locked && i < 4; ++i) {
        const int p = recent_pitches[i];
        if (p == bar_pitch_a || p == bar_pitch_b) {
          continue;
        }
        if (bar_pitch_a < 0) {
          bar_pitch_a = p;
        } else if (bar_pitch_b < 0) {
          bar_pitch_b = p;
        } else {
          trailing_locked = false;
        }
      }
      if (beat >= 2 && trailing_locked && (snapped == bar_pitch_a || snapped == bar_pitch_b)) {
        int order_ceiling = band_hi;
        int order_floor = band_lo;
        for (const ConcurrentMotion& motion : motions) {
          if (motion.curr < 0) {
            continue;
          }
          if (motion.voice < voice) {
            order_ceiling = std::min(order_ceiling, motion.curr);
          } else if (motion.voice > voice) {
            order_floor = std::max(order_floor, motion.curr);
          }
        }
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {((chord.root_pc % 12) + 12) % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        auto escape_ok = [&](int cand) {
          if (cand < band_lo || cand > band_hi || cand < order_floor || cand > order_ceiling) {
            return false;
          }
          if (cand == bar_pitch_a || cand == bar_pitch_b) {
            return false;
          }
          const int pc = ((cand % 12) + 12) % 12;
          if (pc != triad_pc[0] && pc != triad_pc[1] && pc != triad_pc[2]) {
            return false;
          }
          for (const int sounding : theme_pitches) {
            if (!isConsonantPair(cand, sounding)) {
              return false;
            }
          }
          for (const int sounding : window_pitches) {
            const int ic = std::abs(cand - sounding) % 12;
            if (ic == 1 || ic == 6 || ic == 11) {
              return false;
            }
          }
          if (audible_from >= 0) {
            for (const ConcurrentMotion& motion : motions) {
              if (formsPerfectParallel(audible_from, cand, motion.prev, motion.curr)) {
                return false;
              }
            }
          }
          return true;
        };
        for (int dist = 3; dist <= 12; ++dist) {
          bool placed = false;
          for (const int sgn : {1, -1}) {
            const int cand = snapped + sgn * dist;
            if (escape_ok(cand)) {
              snapped = cand;
              placed = true;
              break;
            }
          }
          if (placed) {
            ++waveVetoStats().wobble_breaker_fired;
            break;
          }
        }
      }
    }
    cursor = snapped;
    // The consonance / parallel constraints can snap the anchor outside the
    // working wave window. Stretch the window to contain it: with the cursor
    // outside, every wave step would reflect onto the single pitch one step
    // back toward the window -- no alternative candidates -- so the parallel
    // veto would have nothing to displace to.
    if (cursor < wave_lo || cursor > wave_hi) {
      ++waveVetoStats().window_expanded;
    }
    wave_lo = std::min(wave_lo, cursor);
    wave_hi = std::max(wave_hi, cursor);
    line_prev_anchor = cursor;
    if (cadential_close && beat == 2) {
      // Cadential close: the bar's second half sounds as ONE held anchor tone
      // (a half note on the mid-bar strong beat) instead of the running
      // subdivision, so the ornament pass has the strong-beat quarter-or-longer
      // top note the mandatory section-cadence trill needs. The tone is the
      // wave's own vetted beat anchor, additionally checked for consonance
      // against every theme tone sounding ANYWHERE inside the held half-bar
      // window: a sustained anchor can be onset-consonant yet clash with a theme
      // note that attacks mid-window.
      const Tick held_dur = kTicksPerBar / 2;
      const Tick held_end = barTick(bar) + kTicksPerBar;
      // Theme tones sounding anywhere in the held window, sampled on the eighth
      // grid (the finest stride a concurrent theme entry uses here).
      std::vector<int> held_theme;
      std::vector<int> slot_theme;
      for (Tick slot = beat_tick; slot < held_end; slot += kEighth) {
        registry.concurrentThemePitches(slot, static_cast<VoiceId>(voice), slot_theme);
        for (const int tone : slot_theme) {
          held_theme.push_back(tone);
        }
      }
      // Harshness against the held window uses the wave's sustain-vet
      // definition: a minor 2nd / tritone / major 7th (interval class 1 / 6 /
      // 11) is rejected.
      auto held_is_harsh = [&](int cand) {
        for (const int tone : held_theme) {
          const int ic = std::abs(cand - tone) % 12;
          if (ic == 1 || ic == 6 || ic == 11) {
            return true;
          }
        }
        return false;
      };
      int held_pitch = -1;
      if (!held_is_harsh(snapped)) {
        held_pitch = snapped;
      } else {
        // The vetted onset anchor clashes with a mid-window theme attack:
        // displace it to the nearest chord tone that is band-confined,
        // order-safe against the concurrent voices, theme-consonant at the
        // onset, parallel-free, and clean across the whole held window.
        int order_ceiling = band_hi;
        int order_floor = band_lo;
        for (const ConcurrentMotion& motion : motions) {
          if (motion.curr < 0) {
            continue;
          }
          if (motion.voice < voice) {
            order_ceiling = std::min(order_ceiling, motion.curr);
          } else if (motion.voice > voice) {
            order_floor = std::max(order_floor, motion.curr);
          }
        }
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {((chord.root_pc % 12) + 12) % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        auto held_ok = [&](int cand) {
          if (cand < band_lo || cand > band_hi || cand < order_floor || cand > order_ceiling) {
            return false;
          }
          const int pc = ((cand % 12) + 12) % 12;
          if (pc != triad_pc[0] && pc != triad_pc[1] && pc != triad_pc[2]) {
            return false;
          }
          for (const int sounding : theme_pitches) {
            if (!isConsonantPair(cand, sounding)) {
              return false;
            }
          }
          if (audible_from >= 0) {
            for (const ConcurrentMotion& motion : motions) {
              if (formsPerfectParallel(audible_from, cand, motion.prev, motion.curr)) {
                return false;
              }
            }
          }
          return !held_is_harsh(cand);
        };
        for (int dist = 1; dist <= 7 && held_pitch < 0; ++dist) {
          for (const int sgn : {-1, 1}) {
            const int cand = snapped + sgn * dist;
            if (held_ok(cand)) {
              held_pitch = cand;
              break;
            }
          }
        }
      }
      if (held_pitch >= 0) {
        addNote(section.notes, beat_tick, held_dur, held_pitch);
        registry.record(beat_tick, static_cast<VoiceId>(voice), held_pitch, held_dur);
        prev_anchor = held_pitch;
        return;
      }
      // No clean held tone exists: fall through to the normal running wave for
      // the second half (the trill site is simply absent for this seed;
      // validation stays green).
    }
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      const Tick tick =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      const Tick prev_tick = tick - step;
      int pitch;
      if (sub == 0) {
        pitch = cursor;
      } else {
        const int from = cursor;
        // Default the next wave note one scale step in the running direction,
        // reversing at the working band edges so the line stays conjunct.
        auto step_from = [&](int direction) {
          int candidate = stepScale(from, direction);
          if (candidate > wave_hi) {
            candidate = stepScale(from, -1);
          } else if (candidate < wave_lo) {
            candidate = stepScale(from, 1);
          }
          return foldIntoScale(std::clamp(candidate, band_lo, band_hi), band_lo, band_hi);
        };
        int next;
        if (figure_mode == 0) {
          // Broken-third chain: a scale-third along the arc on odd subs, a
          // single step back on even subs -- net one step per pair, so the bar
          // keeps the wave's slope while gaining third vocabulary.
          if (sub % 2 == 1) {
            next = leapScale(from, 2, arc_dir);
            if ((next - from) * arc_dir < 0) {
              arc_dir = -arc_dir;  // the third reflected at the band edge
            }
          } else {
            next = leapScale(from, 1, -arc_dir);
          }
          if (next == from) {
            next = step_from(dir);
          }
        } else if (figure_mode == 1 && beat == 2 && sub == 1) {
          // One dive per bar: a fourth or fifth along the running direction
          // (alternating across successive dive bars), filling the corpus's
          // leap bins; the following steps and the next beat's consonant
          // anchor snap recover the register.
          const int dive_degrees = ((bar / 3) % 2 == 0) ? 3 : 4;
          next = leapScale(from, dive_degrees, dir);
          if (next == from) {
            next = step_from(dir);
          }
        } else {
          next = step_from(dir);
        }
        // Parallel-aware wave: if this step lands a same-direction perfect 5th/8th
        // against an earlier voice's concurrent motion, reverse direction (still a
        // single scale step, so the line stays conjunct). The previous pitch is
        // sampled one sixteenth back -- the finest stride any line uses -- so an
        // eighth/quarter-stride wave hears an already-placed sixteenth line's
        // actual motion into this onset, not a pitch from its own stride back.
        registry.concurrentMotions(tick - kSixteenth, tick, static_cast<VoiceId>(voice), num_voices,
                                   motions);
        auto wave_is_parallel = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsPerfectParallel(from, cand, motion.prev, motion.curr)) {
              return true;
            }
          }
          return false;
        };
        if (wave_is_parallel(next)) {
          ++waveVetoStats().step_parallel_adjusted;
          const int reversed = step_from(-dir);
          if (!wave_is_parallel(reversed)) {
            dir = -dir;
            next = reversed;
          } else {
            // Both single steps land parallels (two diatonic stepwise lines in
            // rhythmic lockstep do this systematically): try a third-skip in
            // either direction before accepting the parallel, mirroring the
            // harsh-clash fallback below.
            for (const int skip_dir : {dir, -dir}) {
              const int skip = (skip_dir > 0) ? detail::scaleUp(from, 2, mode)
                                              : detail::scaleDown(from, 2, mode);
              if (skip < wave_lo || skip > wave_hi) {
                continue;
              }
              if (!wave_is_parallel(skip)) {
                next = skip;
                break;
              }
            }
          }
        }
        // Harshness-aware wave: a passing tone that lands a minor 2nd, tritone,
        // or major 7th against a concurrently sounding earlier voice is the
        // sharpest off-beat clash two independent wave lines can produce. Every
        // sixteenth slot the candidate sounds through is scanned, so a slower
        // line cannot sustain into a clash an already-placed faster line lands
        // mid-duration. Reverse direction (still a single scale step) when the
        // reversed step is both parallel-free and clash-free; milder seconds /
        // sevenths are left alone so ordinary passing motion over a sustained
        // tone survives.
        auto wave_is_harsh = [&](int cand) {
          for (VoiceId other = 0; other < num_voices; ++other) {
            if (other == static_cast<VoiceId>(voice)) {
              continue;
            }
            for (Tick slot = tick; slot < tick + step; slot += kSixteenth) {
              const int sounding = registry.soundingPitchInVoice(other, slot);
              if (sounding < 0) {
                continue;
              }
              const int ic = std::abs(cand - sounding) % 12;
              if (ic == 1 || ic == 6 || ic == 11) {
                return true;
              }
              // Parallel-dissonance chain: a 2nd/7th arrival is acceptable as an
              // isolated passing tone, but not when the previous sub-beat against
              // the same voice was already dissonant (consecutive 2nds/7ths/9ths
              // read as a broken duet rather than passing motion).
              if (ic == 2 || ic == 10) {
                const int prev_sounding = registry.soundingPitchInVoice(other, prev_tick);
                if (prev_sounding >= 0) {
                  const int prev_ic = std::abs(from - prev_sounding) % 12;
                  if (prev_ic == 1 || prev_ic == 2 || prev_ic == 6 || prev_ic == 10 ||
                      prev_ic == 11) {
                    return true;
                  }
                }
              }
            }
          }
          return false;
        };
        if (!wave_is_parallel(next) && wave_is_harsh(next)) {
          ++waveVetoStats().step_harsh_adjusted;
          const int reversed = step_from(-dir);
          if (!wave_is_parallel(reversed) && !wave_is_harsh(reversed)) {
            dir = -dir;
            next = reversed;
          } else {
            // Both single steps clash (or the reversed step lands a parallel):
            // try a third-skip in either direction before accepting the clash.
            // A scale-third skip is the smallest non-step move and reads as an
            // ordinary chord-tone skip inside figuration.
            for (const int skip_dir : {dir, -dir}) {
              const int skip = (skip_dir > 0) ? detail::scaleUp(from, 2, mode)
                                              : detail::scaleDown(from, 2, mode);
              if (skip < wave_lo || skip > wave_hi) {
                continue;
              }
              if (!wave_is_parallel(skip) && !wave_is_harsh(skip)) {
                next = skip;
                break;
              }
            }
          }
        }
        // Keep the per-tick voice order V0 >= V1 >= V2: clamp the wave note below
        // every concurrent lower-index voice and above every concurrent
        // higher-index voice so a wide verbatim entry cannot be crossed.
        int order_ceiling = band_hi;
        int order_floor = band_lo;
        for (const ConcurrentMotion& motion : motions) {
          if (motion.curr < 0) {
            continue;
          }
          if (motion.voice < voice) {
            order_ceiling = std::min(order_ceiling, motion.curr);
          } else if (motion.voice > voice) {
            order_floor = std::max(order_floor, motion.curr);
          }
        }
        if (order_floor <= order_ceiling) {
          // The window edges are other voices' actual pitches (possibly
          // chromatic theme tones), so a pinned note is folded back to a
          // diatonic tone inside the window when one exists.
          const int unclamped = next;
          next = foldIntoScale(std::clamp(next, order_floor, order_ceiling),
                               std::max(order_floor, band_lo), std::min(order_ceiling, band_hi));
          if (next != unclamped) {
            ++waveVetoStats().order_clamp_changed;
          }
          // Clamping can pin the note to a window edge and repeat the previous
          // pitch; if the window still has room, step to the nearest distinct
          // diatonic tone inside it so the line never stalls into a long run.
          if (next == from && order_floor < order_ceiling) {
            int up = stepScale(from, 1);
            int down = detail::scaleDown(from, 1, mode);
            if (up <= order_ceiling && up != from) {
              next = up;
            } else if (down >= order_floor && down != from) {
              next = down;
            }
          }
        }
        // Commit the running direction the chosen step actually moved.
        if (next > from) {
          dir = 1;
        } else if (next < from) {
          dir = -1;
        }
        cursor = next;
        pitch = cursor;
      }
      last_pitch = pitch;
      prev_emitted = pitch;
      recent_pitches[emitted_count % 4] = pitch;
      ++emitted_count;
      addNote(section.notes, tick, step, pitch);
      // Register this figuration note so a voice placed later in the same window
      // can read what this line sounds and avoid a parallel against it.
      registry.record(tick, static_cast<VoiceId>(voice), pitch, step);
    }
  }
  prev_anchor = last_pitch;
}

// The four arpeggio figure contours, as indices over the bar's stacked triad
// tones {0 = bass, 1 = mid, 2 = top}. Mid-anchored contours open and close on
// the middle tone so consecutive beats chain smoothly; the outer two contours
// sweep the full triad in one direction.
constexpr int kArpeggioFigures[4][4] = {
    {1, 0, 1, 2},  // mid-bass-mid-top
    {1, 2, 1, 0},  // mid-top-mid-bass
    {0, 1, 2, 1},  // bass-mid-top-mid
    {2, 1, 0, 1},  // top-mid-bass-mid
};

void appendArpeggioCycle(std::vector<MaterialNote>& notes, Tick block_start,
                         const std::vector<CycleBar>& cycle_bar_plan, int band_lo, int band_hi,
                         int figure_index, int notes_per_beat, detail::Mode mode) {
  const int* figure = kArpeggioFigures[((figure_index % 4) + 4) % 4];
  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);

  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan_bar = cycle_bar_plan[static_cast<std::size_t>(bar)];
    int tones[3];
    stackTriadInBand(plan_bar.root_pc, plan_bar.minor, mode, band_lo, band_hi, tones);
    for (int beat = 0; beat < 3; ++beat) {
      const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                             static_cast<Tick>(beat) * kTicksPerBeat;
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        // Subdivided beats take the contour's PREFIX (its opening tones
        // alternate, so consecutive notes never repeat one pitch); the quarter
        // tier walks the contour one element per BEAT for the same reason.
        mnote.pitch = static_cast<std::uint8_t>(tones[figure[notes_per_beat == 1 ? beat : sub]]);
        notes.push_back(mnote);
      }
    }
  }
}

void appendArpeggioBar(std::vector<MaterialNote>& notes, int bar, const detail::ChordSpec& chord,
                       detail::Mode mode, int band_lo, int band_hi, int figure_index,
                       int notes_per_beat) {
  const int* figure = kArpeggioFigures[((figure_index % 4) + 4) % 4];
  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);
  int tones[3];
  stackTriadInBand(chord.root_pc, chord.minor, mode, band_lo, band_hi, tones);
  for (int beat = 0; beat < 4; ++beat) {
    const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      // Subdivided beats take the contour's PREFIX (its opening tones
      // alternate, so consecutive notes never repeat one pitch); the quarter
      // tier walks the contour one element per BEAT for the same reason.
      addNote(notes, beat_tick + static_cast<Tick>(sub) * step, step,
              tones[figure[notes_per_beat == 1 ? beat : sub]]);
    }
  }
}

void appendFiguraCortaBar(std::vector<MaterialNote>& notes, int bar, int start,
                          const detail::ChordSpec& chord, detail::Mode mode, int figure,
                          bool dotted) {
  // Chord-tone ladder above the start; the four per-beat anchors are
  // contour-indexed picks from it (see the header note). Contour 0 is the
  // legacy low-amplitude wave (rise two chord tones, fall back one), so the
  // bar boundary voice-leads smoothly.
  int ladder[4];
  ladder[0] = start;
  for (int idx = 1; idx < 4; ++idx) {
    ladder[idx] = chordToneAbove(ladder[idx - 1], chord.root_pc, chord.minor);
  }
  // Every contour ends on ladder index 0 or 1: a bar ending high forces a
  // wide descent into the next bar's downbeat, which lines up with the other
  // voices' downbeat descents into same-direction perfect arrivals. And no
  // contour revisits index 0 mid-bar: a held anchor turns into a neighbour
  // figure that dips BELOW it, breaking the register floor the chorale's V1
  // embellishment ceiling relies on.
  static constexpr int kCortaContours[4][4] = {
      {0, 1, 2, 1},  // low-amplitude wave (legacy default)
      {0, 2, 2, 1},  // fifth swing with a held high turn, falling back
      {0, 3, 2, 1},  // run up a wide first window, then fall by chord tones
      {0, 2, 3, 1},  // peak arch
  };
  const int contour = ((figure % 4) + 4) % 4;
  int anchor[4];
  for (int beat = 0; beat < 4; ++beat) {
    anchor[beat] = ladder[kCortaContours[contour][beat]];
  }

  // Long-short-short cell per beat: eighth + two sixteenths (one full beat).
  constexpr Tick kCellDur[3] = {kEighth, kSixteenth, kSixteenth};
  constexpr Tick kCellOffset[3] = {0, kEighth, kEighth + kSixteenth};

  for (int beat = 0; beat < 4; ++beat) {
    const int from = anchor[beat];
    const int to = anchor[(beat + 1) % 4];
    const int dir = (to >= from) ? 1 : -1;
    // The two shorts walk diatonically toward the next beat's anchor. A walk
    // that would stall (repeat a pitch) or land ON the next anchor (an off-beat
    // double of the following attack) is bent instead of held: the reference
    // corpus repeats a pitch on only ~3% of transitions, so a held short reads
    // as a stalled line, not an idiom.
    int w1 = (dir > 0) ? detail::scaleUp(from, 1, mode) : detail::scaleDown(from, 1, mode);
    int w2 = (dir > 0) ? detail::scaleUp(w1, 1, mode) : detail::scaleDown(w1, 1, mode);
    if (to == from) {
      // Held anchor: a turn figure (upper then lower neighbour) keeps all four
      // boundary transitions stepwise and repeat-free.
      w1 = detail::scaleUp(from, 1, mode);
      w2 = detail::scaleDown(from, 1, mode);
    } else if ((dir > 0 && w1 >= to) || (dir < 0 && w1 <= to)) {
      // Adjacent anchors: a returning neighbour on the far side, so the cell
      // closes on `from` and the next attack is a fresh step.
      w1 = (dir > 0) ? detail::scaleDown(from, 1, mode) : detail::scaleUp(from, 1, mode);
      w2 = from;
    } else if ((dir > 0 && w2 >= to) || (dir < 0 && w2 <= to)) {
      // Anchors a third apart: passing tone, then an echappee one step beyond
      // the next anchor, resolving onto it by step at the next attack.
      w2 = (dir > 0) ? detail::scaleUp(to, 1, mode) : detail::scaleDown(to, 1, mode);
    }
    if (dotted) {
      // A written dotted cell, not merely a "dotted feel": the held anchor
      // occupies three sixteenths and the approach tone completes the beat.
      addNote(notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, 3 * kSixteenth, from);
      addNote(notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + 3 * kSixteenth,
              kSixteenth, w2);
      continue;
    }
    const int cell[3] = {from, w1, w2};
    for (int sub = 0; sub < 3; ++sub) {
      addNote(notes, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + kCellOffset[sub],
              kCellDur[sub], cell[sub]);
    }
  }
}

void appendFiguraCortaCycle(std::vector<MaterialNote>& notes, Tick block_start,
                            const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                            int anchor_rotation, detail::Mode mode) {
  const int center = cycle_bar_plan.front().low_tone + register_shift;
  const std::vector<int> anchor_deg =
      resolveAnchorDegrees(cycle_bar_plan, center, anchor_rotation, mode);

  // Long-short-short cell per beat: eighth + two sixteenths (one full beat).
  constexpr Tick kCellDur[3] = {kEighth, kSixteenth, kSixteenth};
  constexpr Tick kCellOffset[3] = {0, kEighth, kEighth + kSixteenth};

  const int onset_count = static_cast<int>(anchor_deg.size());
  for (int onset = 0; onset < onset_count; ++onset) {
    const int from_deg = anchor_deg[static_cast<std::size_t>(onset)];
    // The two shorts step toward the next onset's anchor. A short that would
    // repeat a pitch (stalled walk) or land ON the next anchor (an off-beat
    // double of the following attack) is bent into a neighbour or echappee
    // instead: the reference corpus repeats a pitch on only ~3% of transitions.
    const int to_deg =
        (onset + 1 < onset_count) ? anchor_deg[static_cast<std::size_t>(onset + 1)] : from_deg;
    const int delta = to_deg - from_deg;
    const int dir = (delta >= 0) ? 1 : -1;
    const int span = std::abs(delta);

    int w1_deg;
    int w2_deg;
    if (span == 0) {
      // Held anchor: a turn figure toward the register center keeps every
      // boundary transition stepwise and repeat-free.
      const int osc = (degreeToMidi(from_deg, mode) >= center) ? -1 : 1;
      w1_deg = from_deg + osc;
      w2_deg = from_deg - osc;
    } else if (span == 1) {
      // Adjacent anchors: a returning neighbour on the far side, closing on
      // `from` so the next attack is a fresh step.
      w1_deg = from_deg - dir;
      w2_deg = from_deg;
    } else if (span == 2) {
      // Anchors a third apart: passing tone, then an echappee one step beyond
      // the next anchor, resolving onto it by step at the next attack.
      w1_deg = from_deg + dir;
      w2_deg = to_deg + dir;
    } else {
      // Wider gaps: plain stepwise walk; it never reaches the next anchor.
      w1_deg = from_deg + dir;
      w2_deg = from_deg + 2 * dir;
    }

    const int bar = onset / 3;
    const int beat = onset % 3;
    const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                           static_cast<Tick>(beat) * kTicksPerBeat;
    const int cell_deg[3] = {from_deg, w1_deg, w2_deg};
    for (int sub = 0; sub < 3; ++sub) {
      MaterialNote mnote;
      mnote.start_tick = beat_tick + kCellOffset[sub];
      mnote.duration = kCellDur[sub];
      mnote.pitch = static_cast<std::uint8_t>(degreeToMidi(cell_deg[sub], mode));
      notes.push_back(mnote);
    }
  }
}

void appendGestureBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                      detail::Mode mode, int band_lo, int band_hi, int octave_drop) {
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  // Main tone: the highest triad tone whose upper diatonic neighbour still
  // fits the band (the mordent must not poke above the ceiling).
  int main_tone = band_hi;
  while (main_tone > band_lo &&
         (!is_triad(main_tone) || detail::scaleUp(main_tone, 1, mode) > band_hi))
    --main_tone;
  const int upper = detail::scaleUp(main_tone, 1, mode);

  // Resolve the octave drop: each octave lowers the whole gesture by 12, but the
  // drop is pulled back toward 0 in whole octaves until the main-tone anchor
  // still sits at or above band_lo (the descending run tail may dip below).
  int shift = std::max(0, octave_drop) * 12;
  while (shift > 0 && main_tone - shift < band_lo)
    shift -= 12;

  // Mordent onset: upper neighbour, then the main tone (two sixteenths).
  const Tick start = barTick(bar);
  addNote(dst, start, kSixteenth, upper - shift);
  addNote(dst, start + kSixteenth, kSixteenth, main_tone - shift);

  // Descending diatonic run from the main tone, stopping at the band floor
  // instead of repeating it; the rest of the bar stays silent. The floor test
  // uses the un-shifted band so a dropped gesture keeps the same run length
  // (its pitches are the base gesture transposed down by `shift`).
  int cursor = main_tone;
  for (int idx = 2; idx < 8; ++idx) {
    const int next = detail::scaleDown(cursor, 1, mode);
    if (next < band_lo)
      break;
    cursor = next;
    addNote(dst, start + static_cast<Tick>(idx) * kSixteenth, kSixteenth, cursor - shift);
  }
}

void appendChordBlockBar(std::vector<std::vector<MaterialNote>>& voice_notes, int bar,
                         const detail::ChordSpec& chord, detail::Mode mode, int top_hi,
                         Tick block_dur) {
  (void)mode;  // triad-only voicing; no scale walk is needed.
  const int third = chord.minor ? 3 : 4;
  const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                           (chord.root_pc + 7) % 12};
  auto is_triad = [&](int midi) {
    const int pcl = ((midi % 12) + 12) % 12;
    return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
  };
  // Stack downward from the ceiling: V0 = highest triad tone <= top_hi, each
  // later voice the next triad tone below, so V0 >= V1 >= V2 by construction.
  const int num_voices = static_cast<int>(voice_notes.size());
  int cursor = top_hi;
  std::vector<int> tones;
  tones.reserve(static_cast<std::size_t>(num_voices));
  for (int voice = 0; voice < num_voices; ++voice) {
    while (cursor > 0 && !is_triad(cursor))
      --cursor;
    tones.push_back(cursor);
    --cursor;  // resume the scan strictly below the tone just taken.
  }

  const int blocks = static_cast<int>(kTicksPerBar / block_dur);
  for (int voice = 0; voice < num_voices; ++voice) {
    for (int blk = 0; blk < blocks; ++blk) {
      addNote(voice_notes[static_cast<std::size_t>(voice)],
              barTick(bar) + static_cast<Tick>(blk) * block_dur, block_dur,
              tones[static_cast<std::size_t>(voice)]);
    }
  }
}

void appendPedalWalkBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                        detail::Mode mode, int band_lo, int band_hi, int& prev_pitch) {
  (void)mode;  // root / fifth only; no scale walk is needed.
  const int root_pc = chord.root_pc % 12;
  const int fifth_pc = (chord.root_pc + 7) % 12;
  if (prev_pitch < 0) {
    // First bar: open on the root nearest the band centre.
    prev_pitch = fitPitchClass(root_pc, (band_lo + band_hi) / 2);
  }
  for (int beat = 0; beat < 4; ++beat) {
    // Beats alternate root and fifth, each octave-fit nearest the previous
    // pitch and folded back into the band (a fold keeps the pitch class).
    const int pc = (beat % 2 == 0) ? root_pc : fifth_pc;
    int pitch = fitPitchClass(pc, prev_pitch);
    while (pitch > band_hi)
      pitch -= 12;
    while (pitch < band_lo)
      pitch += 12;
    addNote(dst, barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kQuarter, pitch);
    prev_pitch = pitch;
  }
}

}  // namespace bach::composer
