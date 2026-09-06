#ifndef BACH_COMPOSER_BAR_MATERIAL_H
#define BACH_COMPOSER_BAR_MATERIAL_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "composer/arc.h"
#include "composer/figuration.h"
#include "composer/material.h"
#include "core/basic_types.h"

/// @file
/// @brief The bar as a unit of material: its tick arithmetic, the triad it
///        states, and the anchored figure written over that triad.
///
/// A form that lays its material out one bar at a time -- a cantus firmus
/// harmonised bar by bar, an immutable ground tiled in four-bar cycles -- needs
/// the same four answers before it can emit a single note: where the bar
/// starts, which triad it states, which tones of that triad a line may stand
/// on, and how the positions between those tones are filled. Every one of those
/// answers is a property of the bar rather than of the form that asked for it.
/// Stating them once here is what keeps two forms that share a bar shape from
/// deriving it twice and drifting apart: the anchored figure in particular is a
/// contract with the per-beat scorer (every beat onset a chord tone, every
/// off-beat a step), and a second derivation of it is a second place for that
/// contract to be broken.

namespace bach::composer {

constexpr Tick kHalf = kTicksPerBeat * 2;       // 960.
constexpr Tick kQuarterDur = kTicksPerBeat;     // 480.
constexpr Tick kEighth = kTicksPerBeat / 2;     // 240.
constexpr Tick kSixteenth = kTicksPerBeat / 4;  // 120.

/// @brief Start tick of `bar` in 4/4.
Tick barTick(int bar);

/// @brief A MaterialNote from its three fields.
MaterialNote materialNote(Tick start, Tick dur, int pitch);

/// @brief Map an arc density tier (0..3) plus a character density bias to a
///        notes-per-beat subdivision for a figuration / variation bar.
///
/// Tier 0 = quarters, tier 1 = eighths, and tiers 2..3 = sixteenths.
int notesPerBeatFor(const ArcPoint& point, std::int8_t density_bias);

/// @brief The triad one bar states, as (root pitch class, quality).
struct BarChord {
  std::uint8_t root_pc;
  bool minor;
};

/// @brief Snap a starting MIDI pitch UP to the nearest chord tone of the bar's
///        triad, so a figuration bar's downbeat is harmonically anchored.
///
/// `minor` selects a third of 3 (minor) or 4 (major) semitones.
int snapUpToChordTone(int start, int root_pc, bool minor);

/// @brief Test whether a pitch is a tone of the bar's triad.
bool isChordTone(int pitch, int root_pc, bool minor);

/// @brief Return the next chord tone strictly above `from`.
int chordToneAbove(int from, int root_pc, bool minor);

/// @brief Build one bar of figuration whose every BEAT onset is a chord tone of
///        the bar's triad and whose off-beat sub-positions are stepwise
///        diatonic passing tones.
///
/// Because the per-beat scorer samples only the four beat onsets, and a chord
/// tone is consonant against any other chord tone (the CF tone and the ground
/// tone are both chord tones of the bar chord), every sampled vertical pair
/// stays consonant; the off-beats remain stepwise so no melodic leap is
/// introduced. The four beat anchors are contour-indexed picks from the
/// chord-tone ladder above `start` (already snapped to a chord tone); contour 0
/// is the gentle low-amplitude wave (a0 a1 a2 a1) and the default, so callers
/// that pass no `figure` stay byte-identical. The other contours widen or
/// redirect the beat windows (a fifth-wide swing, an ascending sweep, a peak
/// arch), so the stepwise fill walks runs and descents instead of stamping the
/// same third-pendulum bigrams into every bar. `notes_per_beat` is 1
/// (quarters: beat anchors only), 2 (eighths), or 4 (sixteenths).
///
/// `leap_fill` (sixteenth tier only, default off so existing callers stay
/// byte-identical) replaces the OPENING beat's fill with a leap-and-fill cell:
/// an ascending-sixth leap from the downbeat anchor to the scale tone five
/// degrees up, then stepwise descent -- the classical leap-then-contrary-fill
/// shape supplying the ascending-sixth interval bins no stepwise window fill
/// reaches. The cell's peak (anchor + a sixth) stays strictly under ladder[3]
/// (anchor + an octave), a pitch the contours already reach, so it adds no new
/// register ceiling; and every cell tone sits ON or ABOVE the downbeat anchor
/// (the bar's lowest), so the register floor the V1 embellishment ceiling
/// relies on is preserved. The beat onset itself is untouched (still the
/// contour's chord-tone anchor).
///
/// Appends to `notes` via the supplied emit callback (start_tick, duration,
/// pitch), so the same routine serves both the FigurationSection and the
/// PassacagliaVariation builders.
template <typename Emit>
void emitAnchoredBar(int bar, int start, const BarChord& chord, detail::Mode mode,
                     int notes_per_beat, const Emit& emit, int figure = 0, bool leap_fill = false) {
  // Chord-tone ladder above the start, then the contour's four per-beat picks.
  int ladder[4];
  ladder[0] = start;
  for (int idx = 1; idx < 4; ++idx) {
    ladder[idx] = chordToneAbove(ladder[idx - 1], chord.root_pc, chord.minor);
  }
  // Every contour ends on ladder index 0 or 1: a bar that ends high forces a
  // wide descent into the next bar's downbeat anchor, which lines up with the
  // other voices' own downbeat descents (the CF skeleton arrival, the bass
  // root change) into systematic same-direction perfect arrivals.
  // No contour revisits index 0 mid-bar: a degenerate window (from == to)
  // falls back to a neighbour oscillation that dips BELOW the anchor, and a
  // dip under the bar's lowest anchor breaks the register floor the V1
  // embellishment ceiling relies on.
  static constexpr int kContours[4][4] = {
      {0, 1, 2, 1},  // low-amplitude wave (legacy default)
      {0, 2, 2, 1},  // fifth swing with a high oscillation, falling back
      {0, 3, 2, 1},  // run up a wide first window, then fall by chord tones
      {0, 2, 3, 1},  // peak arch: rise to the upper octave region, fall back
  };
  const int contour = ((figure % 4) + 4) % 4;
  std::array<int, 4> anchor;
  for (int beat = 0; beat < 4; ++beat) {
    anchor[static_cast<std::size_t>(beat)] =
        ladder[kContours[contour][static_cast<std::size_t>(beat)]];
  }
  const Tick step =
      notes_per_beat == 1 ? kQuarterDur : (notes_per_beat == 2 ? kEighth : kSixteenth);
  auto step_dir = [&](int p, int d) {
    return (d > 0) ? detail::scaleUp(p, 1, mode) : (detail::inScale(p - 1, mode) ? p - 1 : p - 2);
  };
  for (int beat = 0; beat < 4; ++beat) {
    const int from = anchor[static_cast<std::size_t>(beat)];
    const int to = anchor[static_cast<std::size_t>((beat + 1) % 4)];
    if (leap_fill && notes_per_beat == 4 && beat == 0) {
      // Leap-and-fill opening beat: anchor, sixth up, two steps back down.
      // Every contour opens on ladder[0], so the peak never exceeds the
      // ladder[3] tone the contours already reach, and no cell tone dips
      // below the downbeat anchor.
      int cur = from;
      for (int sub = 0; sub < 4; ++sub) {
        if (sub == 1) {
          cur = detail::scaleUp(from, 5, mode);
        } else if (sub > 1) {
          cur = step_dir(cur, -1);
        }
        emit(barTick(bar) + static_cast<Tick>(sub) * step, step, cur);
      }
      continue;
    }
    // Off-beat sub-positions step diatonically from this beat's anchor toward
    // the next beat's anchor and REFLECT one step short of it, so the next
    // beat's anchor is a fresh onset and the fill never holds a pitch.
    // (Holding -- the previous behaviour -- flattened every beat whose anchors
    // sit a third apart into repeated sixteenths: up to eight identical
    // pitches per bar at the dense tier, an interval-0 surface the reference
    // corpus rarely writes.) A window with no interior
    // scale tone falls back to a neighbour oscillation away from the target,
    // the double-neighbour approach figure.
    const int lo = std::min(from, to);
    const int hi = std::max(from, to);
    auto out_of_window = [&](int p) { return p == to || p > hi || p < lo; };
    // Non-legacy figures alternate the fill of narrow (third-wide) DESCENDING
    // windows: every other such beat opens away from the target -- a
    // neighbour-return (e f e d into c) -- before walking toward it. A
    // third-wide window filled toward the target always walks the same two
    // interior pitches, so without the alternation the dense tier stamps one
    // pendulum bigram pair into every such beat. Ascending windows keep the
    // plain fill: their opening neighbour would dip BELOW the window (and
    // below the bar's lowest anchor at the bar-opening beat), breaking the
    // register floor the V1 embellishment ceiling relies on.
    const bool neighbour_first = figure != 0 && notes_per_beat == 4 && to < from &&
                                 ((bar + beat) & 1) != 0 && (hi - lo) <= 4;
    int walked = from;
    int dir = (to >= from) ? 1 : -1;
    for (int sub = 0; sub < notes_per_beat; ++sub) {
      int pitch = from;
      if (sub > 0) {
        if (neighbour_first && sub <= 2) {
          walked = (sub == 1) ? ((to >= from) ? step_dir(from, -1) : step_dir(from, 1)) : from;
          pitch = walked;
          emit(barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat +
                   static_cast<Tick>(sub) * step,
               step, pitch);
          continue;
        }
        int nxt = step_dir(walked, dir);
        if (out_of_window(nxt)) {
          dir = -dir;
          nxt = step_dir(walked, dir);
        }
        if (out_of_window(nxt)) {
          const int neighbour = (to >= from) ? step_dir(from, -1) : step_dir(from, 1);
          nxt = (walked == from) ? neighbour : from;
        }
        walked = nxt;
        pitch = walked;
      }
      const Tick onset =
          barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat + static_cast<Tick>(sub) * step;
      emit(onset, step, pitch);
    }
  }
}

}  // namespace bach::composer

#endif  // BACH_COMPOSER_BAR_MATERIAL_H
