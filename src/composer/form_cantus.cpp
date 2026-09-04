#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/arc.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/motif_ops.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Fixed-line forms: the chorale prelude (cantus firmus + figuration) and the
// Goldberg-style immutable-bass variation set.
//
// Both honour ResolvedRequest length / mode / character / arc. They reuse the
// proven ChoralePrelude (chorale) and GoldbergVariations (goldberg) note language -- a CF whose
// bar downbeats are an immutable skeleton plus a predominantly-stepwise scalar
// wave above it, and an immutable tiled ground under rising-density scalar-wave
// variations -- but generalise the layout to any snapped bar count, derive the
// CF tune and the per-variation design from (seed, indices) only, and select
// the diatonic scale / ground from the mode.
// ---------------------------------------------------------------------------

namespace {

using detail::Mode;

constexpr Tick kHalf = kTicksPerBeat * 2;       // 960.
constexpr Tick kQuarterDur = kTicksPerBeat;     // 480.
constexpr Tick kEighth = kTicksPerBeat / 2;     // 240.
constexpr Tick kSixteenth = kTicksPerBeat / 4;  // 120.

Tick barTick(int bar) {
  return static_cast<Tick>(bar) * kTicksPerBar;
}

MaterialNote materialNote(Tick start, Tick dur, int pitch) {
  MaterialNote note;
  note.start_tick = start;
  note.duration = dur;
  note.pitch = static_cast<std::uint8_t>(pitch);
  return note;
}

// Map an arc density tier (0..3) plus a character density bias to a
// notes-per-beat subdivision for a figuration / variation bar. Tier 0 =
// quarters, tier 1 = eighths, and tiers 2..3 = sixteenths.
int notesPerBeatFor(const ArcPoint& point, std::int8_t density_bias) {
  int tier = static_cast<int>(point.density_tier) + density_bias;
  if (tier < 0)
    tier = 0;
  if (tier > 3)
    tier = 3;
  if (tier == 0)
    return 1;
  return tier >= 2 ? 4 : 2;
}

// ----- Chorale prelude cantus-firmus tune generator ------------------------
//
// A deterministic, hymn-like chorale tune. The tune is built from 4-bar
// phrases, one whole-note structural tone per bar. Each phrase traces one of a
// small set of stepwise-dominant phrase shapes (scale-degree contours) and ends
// on a cadence degree: phrases alternate authentic (degree 1) and half (degree
// 5) cadences, and the FINAL phrase always closes on the tonic (degree 1). The
// shape selection rotates by seed and phrase index, so the tune is stable per
// seed yet varied across phrases and pieces.
//
// Scale degrees are 1-based diatonic degrees (1 = tonic). A degree is realised
// against the active mode's diatonic scale walking up from a low tonic anchor,
// so the whole tune sits in the C3-region (well below the C4+ figuration above
// it -- no voice crossing).

// Four phrase shapes, each four 1-based scale degrees. Every shape is
// predominantly stepwise (adjacent degrees differ by <= 2) and resolves toward
// its cadence; the cadence degree itself is overwritten per phrase below, so the
// fourth entry is only a lead-in contour.
constexpr std::array<std::array<int, 4>, 4> kPhraseShapes = {{
    {1, 2, 3, 2},  // arch up to the mediant and back.
    {5, 4, 3, 2},  // gentle descent from the dominant.
    {3, 4, 5, 4},  // rise to the dominant.
    {1, 3, 2, 3},  // neighbour-rich oscillation.
}};

// Low tonic anchor for the cantus firmus (C3 = MIDI 48). Phrase degrees walk up
// the diatonic scale from here, keeping the CF in the C3-region.
constexpr int kCfTonicAnchor = 48;

// Resolve a 1-based scale degree to a MIDI pitch in the CF register for the
// given mode. Degree 1 = the tonic anchor; higher degrees walk up the scale.
int cfDegreePitch(int degree, Mode mode) {
  const int steps = degree - 1;
  return detail::scaleUp(kCfTonicAnchor, steps < 0 ? 0 : steps, mode);
}

// Build the immutable CF skeleton: one structural tone per bar over `bars` bars.
// The skeleton tiles bar-per-tone for ANY bar count (sized to `bars` exactly),
// so cantus_firmus_immutable's bar_index lookup is always in range. The cadence
// degree of each 4-bar phrase alternates authentic (1) / half (5); the final
// phrase always ends on the tonic. The leading tone is raised at cadences in
// minor (handled by the harmony mapping; the CF tone itself stays a chord tone).
std::vector<MaterialNote> buildCfSkeleton(int bars, std::uint32_t seed, Mode mode) {
  std::vector<MaterialNote> skeleton;
  skeleton.reserve(static_cast<std::size_t>(bars));
  const int num_phrases = (bars + 3) / 4;  // ceil: the last phrase may be short.
  for (int phrase = 0; phrase < num_phrases; ++phrase) {
    const std::size_t shape_idx =
        (static_cast<std::size_t>(seed) + static_cast<std::size_t>(phrase)) % kPhraseShapes.size();
    const auto& shape = kPhraseShapes[shape_idx];
    const bool is_final = (phrase == num_phrases - 1);
    // Cadence degree: authentic (1) on even phrases, half (5) on odd phrases;
    // the final phrase always resolves to the tonic.
    const int cadence_degree = is_final ? 1 : ((phrase % 2 == 0) ? 1 : 5);
    for (int local = 0; local < 4; ++local) {
      const int bar = phrase * 4 + local;
      if (bar >= bars)
        break;
      // The cadence bar (last bar of the phrase, or the final bar of the piece)
      // takes the cadence degree; other bars take the shape contour.
      const bool is_cadence_bar = (local == 3) || (bar == bars - 1);
      const int degree = is_cadence_bar ? cadence_degree : shape[static_cast<std::size_t>(local)];
      skeleton.push_back(materialNote(barTick(bar), kTicksPerBar, cfDegreePitch(degree, mode)));
    }
  }
  return skeleton;
}

// Map a CF skeleton degree to a per-bar chord whose triad contains the CF tone.
// The chord root is chosen from a small deterministic table so the CF tone is a
// chord tone of the bar's chord (required for the figuration downbeat-anchoring
// rules and for a consonant CF). Returned as (root_pc, is_minor) for the mode.
struct BarChord {
  std::uint8_t root_pc;
  bool minor;
};

// CF pitch class -> harmonizing chord. For each diatonic tone we pick a triad
// (root family) that contains it. Major: I (C E G), IV (F A C), V (G B D),
// vi (A C E). Minor: i (C Eb G), iv (F Ab C), V (G B D, harmonic-minor major
// dominant), VI (Ab C Eb). The choice keeps the CF tone consonant and gives a
// hymn-like I / IV / V / vi (or i / iv / V / VI) harmonization.
BarChord chordForCfTone(int cf_pitch, Mode mode) {
  const int pc = ((cf_pitch % 12) + 12) % 12;
  if (mode == Mode::Major) {
    // Map each C-major scale degree pc to a containing triad root.
    switch (pc) {
      case 0:  // C: I.
        return {0, false};
      case 2:  // D: V (G B D).
        return {7, false};
      case 4:  // E: I (C E G).
        return {0, false};
      case 5:  // F: IV (F A C).
        return {5, false};
      case 7:  // G: V (G B D).
        return {7, false};
      case 9:  // A: vi (A C E).
        return {9, true};
      case 11:  // B: V (G B D).
        return {7, false};
      default:
        return {0, false};
    }
  }
  // Minor (C natural minor degrees: C D Eb F G Ab Bb; V is major dominant).
  switch (pc) {
    case 0:  // C: i.
      return {0, true};
    case 2:  // D: V (G B D); D is the fifth of G.
      return {7, false};
    case 3:  // Eb: i (C Eb G).
      return {0, true};
    case 5:  // F: iv (F Ab C).
      return {5, true};
    case 7:  // G: V (G B D).
      return {7, false};
    case 8:  // Ab: VI (Ab C Eb).
      return {8, false};
    case 10:  // Bb: III-ish; fall back to V's relative -> use VI containing Bb? Bb in
              // Eb major (III). Use III (Eb G Bb).
      return {3, false};
    default:
      return {0, true};
  }
}

// Snap a starting MIDI pitch UP to the nearest chord tone of the bar's triad, so
// a figuration bar's downbeat is harmonically anchored. `third` is 3 (minor) or
// 4 (major) semitones.
int snapUpToChordTone(int start, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int triad_pc[3] = {root_pc % 12, (root_pc + third) % 12, (root_pc + 7) % 12};
  int cur = start;
  while (cur % 12 != triad_pc[0] && cur % 12 != triad_pc[1] && cur % 12 != triad_pc[2])
    ++cur;
  return cur;
}

// Test whether a pitch is a tone of the bar's triad.
bool isChordTone(int pitch, int root_pc, bool minor) {
  const int third = minor ? 3 : 4;
  const int pc = ((pitch % 12) + 12) % 12;
  return pc == ((root_pc % 12) + 12) % 12 || pc == (root_pc + third) % 12 ||
         pc == (root_pc + 7) % 12;
}

// Return the next chord tone strictly above `from`.
int chordToneAbove(int from, int root_pc, bool minor) {
  int cur = from + 1;
  while (!isChordTone(cur, root_pc, minor))
    ++cur;
  return cur;
}

// Build one bar of figuration whose every BEAT onset is a chord tone of the
// bar's triad and whose off-beat sub-positions are stepwise diatonic passing
// tones. Because the per-beat scorer samples only the four beat onsets, and a
// chord tone is consonant against any other chord tone (the CF tone and the
// ground tone are both chord tones of the bar chord), every sampled vertical
// pair stays consonant; the off-beats remain stepwise so no melodic leap is
// introduced. The four beat anchors are contour-indexed picks from the
// chord-tone ladder above `start` (already snapped to a chord tone); contour 0
// is the gentle low-amplitude wave (a0 a1 a2 a1) and the default, so callers
// that pass no `figure` stay byte-identical. The other contours widen or
// redirect the beat windows (a fifth-wide swing, an ascending sweep, a peak
// arch), so the stepwise fill walks runs and descents instead of stamping the
// same third-pendulum bigrams into every bar. `notes_per_beat` is 1
// (quarters: beat anchors only), 2 (eighths), or 4 (sixteenths).
//
// `leap_fill` (sixteenth tier only, default off so existing callers stay
// byte-identical) replaces the OPENING beat's fill with a leap-and-fill cell:
// an ascending-sixth leap from the downbeat anchor to the scale tone five
// degrees up, then stepwise descent -- the classical leap-then-contrary-fill
// shape supplying the ascending-sixth interval bins no stepwise window fill
// reaches. The cell's peak (anchor + a sixth) stays strictly under ladder[3]
// (anchor + an octave), a pitch the contours already reach, so it adds no new
// register ceiling; and every cell tone sits ON or ABOVE the downbeat anchor
// (the bar's lowest), so the register floor the V1 embellishment ceiling
// relies on is preserved. The beat onset itself is untouched (still the
// contour's chord-tone anchor).
//
// Appends to `notes` via the supplied emit callback (start_tick, duration,
// pitch), so the same routine serves both the FigurationSection and the
// PassacagliaVariation builders.
template <typename Emit>
void emitAnchoredBar(int bar, int start, const BarChord& chord, Mode mode, int notes_per_beat,
                     const Emit& emit, int figure = 0, bool leap_fill = false) {
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
    // corpus writes on only ~3% of transitions.) A window with no interior
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

// Append one bar of figuration (a downbeat-anchored scalar wave) to a section.
// The bar opens on a chord tone (so figuration_harmonic_consistency passes),
// then runs scalewise up the mode's scale and back down. `notes_per_beat`
// selects the subdivision (2 = eighths, 4 = sixteenths). `register_base` is the
// pitch the wave starts from before snapping to a chord tone; `offset` shifts
// the start up the scale per seed.
void appendFigurationBar(FigurationSection& section, int bar, const BarChord& chord, Mode mode,
                         int notes_per_beat, int register_base, int offset, int figure = 0,
                         bool leap_fill = false) {
  const int snapped =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  emitAnchoredBar(
      bar, snapped, chord, mode, notes_per_beat,
      [&](Tick start, Tick dur, int pitch) {
        section.notes.push_back(materialNote(start, dur, pitch));
      },
      figure, leap_fill);
}

// ----- Chorale prelude walking-bass (Schubler BWV645 model) ----------------
//
// The Schubler "Wachet auf" texture is a three-layer fabric: a running upper
// figuration (V0), the chorale tune in the middle (V1, the immutable cantus
// firmus), and a quarter-note walking bass below (V2) that connects the chord
// roots stepwise. This is the third layer: a continuous quarter-note line whose
// bar downbeat is the bar chord's root and whose intermediate beats step
// diatonically toward the next bar's root, kept consonant and parallel-free
// against both upper voices via the shared texture machinery.

// Walking-bass register band: C2-B2, strictly below the C3-region cantus firmus
// (whose lowest tone is C3 = 48), so the bass never crosses V1 and voice order
// V0 (C4+) > V1 (C3-region) > V2 (C2) holds at every tick.
constexpr int kBassBandLo = 36;  // C2.
constexpr int kBassBandHi = 47;  // B2.

// Ceiling for every V1 (embellished CF) tone pick: the V0 figuration's
// register floor is C4 (its downbeat anchor never sits below it, and the
// fill never dips under the bar's lowest anchor), so a V1 tone above C4
// would sit over V0's low fill tones -- a voice crossing. Equality is safe
// (the crossing rule is strict).
constexpr int kV1EmbellishCeiling = 60;  // C4.

// Fit a pitch class to the MIDI pitch inside [lo, hi] nearest a center.
int fitPcToBand(int pitch_class, int center, int lo, int hi) {
  const int base = ((pitch_class % 12) + 12) % 12;
  int best = -1;
  int best_dist = 1 << 20;
  for (int oct = lo - 12; oct <= hi + 12; oct += 12) {
    const int cand = base + oct;
    if (cand < lo || cand > hi)
      continue;
    const int dist = std::abs(cand - center);
    if (dist < best_dist) {
      best_dist = dist;
      best = cand;
    }
  }
  if (best < 0)
    best = std::min(std::max(base + 12 * ((center - base) / 12), lo), hi);
  return best;
}

// Build the V2 walking bass over all `bars` bars and append it to `out_notes`.
// Each bar opens on the bar chord's root (a quarter note on the downbeat),
// fitted into the bass band nearest the running cursor. The remaining three
// beats of the bar step diatonically toward the NEXT bar's root, each anchor
// chosen with the shared tier-scored consonantChordTone so it stays consonant
// with the concurrent V0 / V1 tones and forms no parallel/hidden perfect against
// them. The registry already holds V0 (figuration) and V1 (embellished cantus
// firmus). Repeated-pitch runs are bounded by a run-aware nudge: a static
// harmony that would otherwise sustain the root is broken by a single diatonic
// neighbour so no quarter-note run exceeds the corpus ceiling.
void appendWalkingBass(std::vector<MaterialNote>& out_notes, ThemeToneRegistry& registry,
                       const std::vector<BarChord>& bar_chords, Mode mode) {
  const int bars = static_cast<int>(bar_chords.size());
  int cursor = (kBassBandLo + kBassBandHi) / 2;
  int line_prev = -1;
  int prev_pitch = -1;
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;

  for (int bar = 0; bar < bars; ++bar) {
    const BarChord& chord = bar_chords[static_cast<std::size_t>(bar)];
    detail::ChordSpec spec;
    spec.root_pc = chord.root_pc;
    spec.minor = chord.minor;
    // Target the next bar's root (its band-fit pitch) so the intermediate beats
    // walk stepwise toward it; the final bar holds toward its own root.
    const BarChord& next_chord =
        bar_chords[static_cast<std::size_t>(bar + 1 < bars ? bar + 1 : bar)];
    const int next_root = fitPcToBand(next_chord.root_pc, cursor, kBassBandLo, kBassBandHi);
    // Static harmony (next root == this bar's root in the band): the walk has
    // no direction, and "hold the cursor + repeat nudge" collapsed into a
    // two-tone root/third pendulum bar after bar, concentrating the
    // interval-bigram surface on (4|-4). Aim the intermediate beats at a
    // rotating walk contour above the bar root instead (2nd-3rd-5th ascending
    // on even bars, 5th-3rd-2nd descending on odd); every target still passes
    // through consonantChordTone below, so a contour tone that clashes with
    // the sounding upper voices degrades to the nearest consonant chord tone
    // exactly as before.
    const int bar_root_fit = fitPcToBand(chord.root_pc, cursor, kBassBandLo, kBassBandHi);
    const bool static_bar = (next_root == bar_root_fit);
    static constexpr int kStaticWalkDegrees[2][3] = {{1, 2, 4}, {4, 2, 1}};

    for (int beat = 0; beat < 4; ++beat) {
      const Tick beat_tick = barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat;
      const Tick prev_tick = beat_tick - kTicksPerBeat;
      theme_pitches.clear();
      motions.clear();
      registry.concurrentThemePitches(beat_tick, /*voice=*/2, theme_pitches);
      registry.concurrentMotions(prev_tick, beat_tick, /*voice=*/2, /*num_voices=*/3, motions);

      int pitch;
      if (beat == 0) {
        // Downbeat: the bar chord's root in the bass band. A walking bass states
        // the harmony's root on each downbeat (the BWV645 foundation); the root
        // is consonant with the upper chord-tone voices by construction, so it is
        // emitted directly rather than allowing a chord-tone substitution that
        // would weaken the harmonic anchor.
        pitch = fitPcToBand(chord.root_pc, cursor, kBassBandLo, kBassBandHi);
      } else {
        // Intermediate beat: step one diatonic degree from the cursor toward the
        // next bar's root (a passing tone), then pick the nearest consonant,
        // parallel-free diatonic tone to that step target. A static bar aims
        // at the rotating walk contour instead (see above).
        int step_target = cursor;
        if (static_bar) {
          const int degree = kStaticWalkDegrees[bar % 2][beat - 1];
          step_target = detail::scaleUp(bar_root_fit, degree, mode);
        } else if (next_root > cursor)
          step_target = detail::inScale(cursor + 1, mode) ? cursor + 1 : cursor + 2;
        else if (next_root < cursor)
          step_target = detail::inScale(cursor - 1, mode) ? cursor - 1 : cursor - 2;
        step_target = std::min(std::max(step_target, kBassBandLo), kBassBandHi);
        pitch = consonantChordTone(spec, /*voice=*/2, kBassBandLo, kBassBandHi, step_target,
                                   theme_pitches, line_prev, motions, mode, /*downbeat=*/false);
      }

      // Repeat nudge: a static harmony (repeated root) or a held passing tone
      // can repeat the previous pitch. Any OFF-BEAT repeat is displaced (the
      // reference corpus repeats a pitch on only ~3% of transitions, so even a
      // pair reads as a stalled line): first to a diatonic step neighbour that
      // is consonant against every concurrently sounding upper voice (keeping
      // the walking surface stepwise), else to the chord tone above (an
      // arpeggiated walk, consonant by construction). Downbeats are exempt
      // (the root statement is the bar's harmonic anchor and must not be
      // displaced); the off-beat fills carry the variety.
      if (pitch == prev_pitch && beat != 0) {
        // Consonant with every sounding upper voice AND strictly below them
        // all -- the walking bass is the texture's floor, and a displacement
        // that lands ON the cantus firmus tone (a consonant unison) is still
        // a voice crossing.
        auto consonant_with_all = [&](int cand) {
          for (int upper : theme_pitches) {
            if (!isConsonantPair(cand, upper) || cand >= upper)
              return false;
          }
          return true;
        };
        int alt = -1;
        for (int cand : {detail::scaleUp(pitch, 1, mode), detail::scaleDown(pitch, 1, mode)}) {
          if (cand >= kBassBandLo && cand <= kBassBandHi && consonant_with_all(cand)) {
            alt = cand;
            break;
          }
        }
        if (alt < 0) {
          const int chord_tone = chordToneAbove(pitch, chord.root_pc, chord.minor);
          if (chord_tone <= kBassBandHi)
            alt = chord_tone;
        }
        if (alt >= 0)
          pitch = alt;
      }

      // Audible-grain parallel re-check: consonantChordTone (and the repeat
      // nudge above) judge motion at quarter grain, but the upper figuration
      // moves in eighths and sixteenths -- union-onset sampling pairs this
      // beat with the LAST onset before it, a motion the quarter-grain check
      // never sees. Sampling one sixteenth back reproduces that pair for any
      // texture whose smallest value is a sixteenth (a coarser line simply
      // sustains across the sample point). Re-judge the chosen tone at that
      // grain and displace to the nearest diatonic tone that is consonant
      // with the sounding uppers and parallel-free; the tone stands when no
      // such alternative exists within a fifth.
      if (beat != 0 && prev_pitch >= 0) {
        motions.clear();
        registry.concurrentMotions(beat_tick - kSixteenth, beat_tick, /*voice=*/2,
                                   /*num_voices=*/3, motions);
        auto bass_is_parallel = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsPerfectParallel(prev_pitch, cand, motion.prev, motion.curr))
              return true;
          }
          return false;
        };
        if (bass_is_parallel(pitch)) {
          auto admissible = [&](int cand) {
            if (cand < kBassBandLo || cand > kBassBandHi || cand == pitch || cand == prev_pitch ||
                !detail::inScale(cand, mode)) {
              return false;
            }
            for (int upper : theme_pitches) {
              // Consonant and strictly below every sounding upper voice: the
              // bass is the texture's floor, and a consonant unison with the
              // cantus firmus tone is still a voice crossing.
              if (!isConsonantPair(cand, upper) || cand >= upper)
                return false;
            }
            return !bass_is_parallel(cand);
          };
          for (int dist = 1; dist <= 7; ++dist) {
            bool placed = false;
            for (const int sgn : {-1, 1}) {
              const int cand = pitch + sgn * dist;
              if (admissible(cand)) {
                pitch = cand;
                placed = true;
                break;
              }
            }
            if (placed)
              break;
          }
        }
      }

      // Forward parallel guard on the approach beat: the next bar's downbeat
      // states the chord root without substitution (the harmonic anchor), so
      // the only freedom in the (beat 3 -> next root) motion is the beat-3
      // tone itself. The upper voices are final at this point, so a parallel
      // perfect formed against their motion into the bar head is already
      // knowable; re-aim the approach tone when it would lock one in. The
      // root lands relative to the approach tone (band fit follows the
      // cursor), so the candidate's own landing root is recomputed per try.
      if (beat == 3 && bar + 1 < bars && prev_pitch >= 0) {
        const Tick next_bar_tick = barTick(bar + 1);
        std::vector<ConcurrentMotion> fwd_motions;
        auto forward_parallel = [&](int cand, bool strict_only) {
          const int landing_root = fitPcToBand(next_chord.root_pc, cand, kBassBandLo, kBassBandHi);
          fwd_motions.clear();
          registry.concurrentMotions(next_bar_tick - kSixteenth, next_bar_tick,
                                     /*voice=*/2, /*num_voices=*/3, fwd_motions);
          for (const ConcurrentMotion& motion : fwd_motions) {
            const bool hit =
                strict_only
                    ? formsStrictPerfectParallel(cand, landing_root, motion.prev, motion.curr)
                    : formsPerfectParallel(cand, landing_root, motion.prev, motion.curr);
            if (hit)
              return true;
          }
          return false;
        };
        if (forward_parallel(pitch, /*strict_only=*/false)) {
          auto into_beat_parallel = [&](int cand, bool strict_only) {
            for (const ConcurrentMotion& motion : motions) {
              const bool hit =
                  strict_only
                      ? formsStrictPerfectParallel(prev_pitch, cand, motion.prev, motion.curr)
                      : formsPerfectParallel(prev_pitch, cand, motion.prev, motion.curr);
              if (hit)
                return true;
            }
            return false;
          };
          auto admissible = [&](int cand, bool strict_only) {
            if (cand < kBassBandLo || cand > kBassBandHi || cand == pitch ||
                !detail::inScale(cand, mode)) {
              return false;
            }
            for (int upper : theme_pitches) {
              // Consonant and strictly below every sounding upper voice: the
              // bass is the texture's floor, and a consonant unison with the
              // cantus firmus tone is still a voice crossing.
              if (!isConsonantPair(cand, upper) || cand >= upper)
                return false;
            }
            return !into_beat_parallel(cand, strict_only) && !forward_parallel(cand, strict_only);
          };
          // Two displacement tiers. The clean tier first: a tone whose own
          // arrival and forward arrival are neither parallel nor hidden. When
          // the arrival is forced onto a perfect (the immutable skeleton tone
          // over the band-pinned root makes every same-direction approach at
          // least hidden), no clean tone exists -- then both checks relax to
          // TRUE parallels only, accepting an unavoidable hidden rather than
          // keeping a true parallel.
          bool placed = false;
          for (const bool strict_only : {false, true}) {
            if (strict_only && !forward_parallel(pitch, /*strict_only=*/true))
              break;  // current tone is hidden at worst: nothing left to fix.
            for (int dist = 1; dist <= 7 && !placed; ++dist) {
              for (const int sgn : {-1, 1}) {
                const int cand = pitch + sgn * dist;
                if (admissible(cand, strict_only)) {
                  pitch = cand;
                  placed = true;
                  break;
                }
              }
            }
            if (placed)
              break;
          }
        }
      }

      out_notes.push_back(materialNote(beat_tick, kQuarterDur, pitch));
      registry.record(beat_tick, /*voice=*/2, pitch, kQuarterDur);
      line_prev = pitch;
      prev_pitch = pitch;
      cursor = pitch;
    }
  }
}

// Severity of the worst perfect-interval fault a (prev -> curr) motion forms
// against any concurrently sounding line. 0 clean, 1 battuta, 2 anti-parallel,
// 3 parallel or hidden -- the ranking every displacement in this tree obeys.
int perfectFaultRank(int prev, int curr, const std::vector<ConcurrentMotion>& motions) {
  int worst = 0;
  for (const ConcurrentMotion& motion : motions) {
    if (formsPerfectParallel(prev, curr, motion.prev, motion.curr))
      return 3;
    if (formsAntiParallelPerfect(prev, curr, motion.prev, motion.curr))
      worst = std::max(worst, 2);
    else if (formsBattuta(prev, curr, motion.prev, motion.curr))
      worst = std::max(worst, 1);
  }
  return worst;
}

// A line as the relief pass wants it: every note of one voice in tick order,
// by address, so a voice whose material is stored per block is still one line.
std::vector<MaterialNote*> lineInTickOrder(const std::vector<std::vector<MaterialNote>*>& parts) {
  std::vector<MaterialNote*> line;
  for (std::vector<MaterialNote>* part : parts) {
    for (MaterialNote& note : *part)
      line.push_back(&note);
  }
  std::stable_sort(line.begin(), line.end(), [](const MaterialNote* lhs, const MaterialNote* rhs) {
    return lhs->start_tick < rhs->start_tick;
  });
  return line;
}

// Displace an arrival that the way in cannot answer for.
//
// Reached only when the tone before it is a bar head, which is structural and
// may not move, and the arrival itself is not -- so the arrival is a tone of the
// figure and the one free end left.
//
// How far it may travel is read off the figure rather than fixed: no further
// than the wider of the two intervals it already spans, and at least a step. A
// tone embedded in a run may then only step, which is what keeps the run's
// conjunct surface, while one the design already leaps to and from may be
// re-aimed as freely as the design itself moves. A repair that trades a
// contrapuntal blemish for a hole in the figure is not a repair.
//
// Non-regressive at both ends, like the way-in re-aim: the replacement lowers
// the fault formed arriving here and may not raise the one formed leaving.
void relieveRunningArrival(const std::vector<MaterialNote*>& line, std::size_t arrival_idx,
                           const ThemeToneRegistry& registry, VoiceId voice, VoiceId num_voices,
                           Mode mode, const std::vector<ConcurrentMotion>& into_arrival,
                           int approach, int original_rank) {
  MaterialNote& note = *line[arrival_idx];
  const int original = static_cast<int>(note.pitch);

  std::vector<ConcurrentMotion> at_arrival;
  registry.concurrentMotions(note.start_tick - 1, note.start_tick, voice, num_voices, at_arrival);
  bool original_consonant = true;
  for (const ConcurrentMotion& motion : at_arrival) {
    if (!isConsonantPair(original, motion.curr)) {
      original_consonant = false;
      break;
    }
  }

  std::vector<ConcurrentMotion> out_of_arrival;
  int next_pitch = -1;
  if (arrival_idx + 1 < line.size()) {
    const MaterialNote& next = *line[arrival_idx + 1];
    next_pitch = static_cast<int>(next.pitch);
    registry.concurrentMotions(next.start_tick - kSixteenth, next.start_tick, voice, num_voices,
                               out_of_arrival);
  }
  const int exit_ceiling =
      next_pitch < 0 ? 0 : perfectFaultRank(original, next_pitch, out_of_arrival);
  int displacement = std::max(2, std::abs(original - approach));
  if (next_pitch >= 0)
    displacement = std::max(displacement, std::abs(original - next_pitch));

  // A replacement that takes a neighbour's pitch the displaced tone did not
  // already share lengthens a static run, so it is tried only after the window
  // has been swept without one.
  const auto flattens = [&](int cand) {
    return (cand == approach && original != approach) ||
           (next_pitch >= 0 && cand == next_pitch && original != next_pitch);
  };
  for (int accept = 0; accept < original_rank; ++accept) {
    for (const bool allow_flatten : {false, true}) {
      for (int dist = 1; dist <= displacement; ++dist) {
        for (const int sgn : {-1, 1}) {
          const int cand = original + sgn * dist;
          if (!detail::inScale(cand, mode) || (!allow_flatten && flattens(cand)))
            continue;
          bool admissible = true;
          for (const ConcurrentMotion& motion : at_arrival) {
            // A lower voice index sounds higher.
            if (motion.voice < voice ? cand >= motion.curr : cand <= motion.curr) {
              admissible = false;
              break;
            }
            if (original_consonant && !isConsonantPair(cand, motion.curr)) {
              admissible = false;
              break;
            }
          }
          if (!admissible || perfectFaultRank(approach, cand, into_arrival) > accept)
            continue;
          if (next_pitch >= 0 && perfectFaultRank(cand, next_pitch, out_of_arrival) > exit_ceiling)
            continue;
          note.pitch = static_cast<std::uint8_t>(cand);
          return;
        }
      }
    }
  }
}

// Relieve one line's arrivals over the voices already placed.
//
// Every beat is an arrival, not only the bar head. The voice under this one
// moves within the bar as well as at its head -- the bass states the bar chord
// and then arpeggiates it -- so the two lines trace the same triad and reach a
// perfect interval together off the downbeat as readily as on it.
//
// What moves is the onset immediately before the arrival, never the arrival
// itself. At a bar head both ends are fixed: the bass band spans a single
// octave so the root's register is determined, the cantus firmus lands on its
// immutable skeleton tone, and the figuration's own head must be a chord tone.
// Inside the bar the arrival is instead a running tone whose neighbours spell
// the figure, and moving it there breaks the shape the block exists to state.
// Either way the answer is on the way in. It is applied here rather than inside
// the line's own loop because the lower voices are built last: this is the
// earliest point at which the fault is visible at all.
//
// The re-aim is non-regressive at both ends. The replacement must lower the
// fault it forms arriving at the beat, and may not raise the one it forms
// arriving at its own onset; it keeps the register order, and may be dissonant
// only where the tone it displaces already was (the eighth-note fills are
// passing tones and dissonant by design).
void relieveArrivals(const std::vector<MaterialNote*>& line, const ThemeToneRegistry& registry,
                     VoiceId voice, VoiceId num_voices, int bars, Mode mode) {
  constexpr int kBeatsPerBar = kTicksPerBar / kTicksPerBeat;
  std::vector<ConcurrentMotion> into_head;
  std::vector<ConcurrentMotion> into_onset;
  std::vector<ConcurrentMotion> at_onset;
  for (int beat = 1; beat < bars * kBeatsPerBar; ++beat) {
    const Tick head = static_cast<Tick>(beat) * kTicksPerBeat;
    std::size_t approach_idx = line.size();
    std::size_t arrival_idx = line.size();
    for (std::size_t idx = 0; idx < line.size(); ++idx) {
      const Tick start = line[idx]->start_tick;
      if (start < head)
        approach_idx = idx;
      else if (start == head)
        arrival_idx = idx;
      else
        break;
    }
    if (arrival_idx == line.size() || approach_idx == line.size())
      continue;
    MaterialNote& approach = *line[approach_idx];
    const int arrival = static_cast<int>(line[arrival_idx]->pitch);
    const int original = static_cast<int>(approach.pitch);
    const int own_prev = approach_idx > 0 ? static_cast<int>(line[approach_idx - 1]->pitch) : -1;

    // Sampled one sixteenth back, the grain at which a union-onset reading pairs
    // an arrival with each other voice's last preceding onset.
    into_head.clear();
    registry.concurrentMotions(head - kSixteenth, head, voice, num_voices, into_head);
    const int original_rank = perfectFaultRank(original, arrival, into_head);
    if (original_rank == 0)
      continue;

    if (approach.start_tick % kTicksPerBar == 0) {
      // Nothing on the way in is free: the tone before this arrival is a bar
      // head, pinned at both ends. Where the arrival is a bar head too there is
      // no free tone at all and the fault stands. Off the downbeat the arrival
      // is instead a running tone, so it is the one that moves -- by a step, so
      // the figure keeps its conjunct surface, and only where it neither
      // dissolves the register order nor worsens the motion out of the beat.
      if (head % kTicksPerBar == 0)
        continue;
      relieveRunningArrival(line, arrival_idx, registry, voice, num_voices, mode, into_head,
                            original, original_rank);
      continue;
    }

    into_onset.clear();
    registry.concurrentMotions(approach.start_tick - kSixteenth, approach.start_tick, voice,
                               num_voices, into_onset);
    const int onset_ceiling = perfectFaultRank(own_prev, original, into_onset);
    at_onset.clear();
    registry.concurrentMotions(approach.start_tick - 1, approach.start_tick, voice, num_voices,
                               at_onset);
    bool original_consonant = true;
    for (const ConcurrentMotion& motion : at_onset) {
      if (!isConsonantPair(original, motion.curr)) {
        original_consonant = false;
        break;
      }
    }
    // Whether the fault being repaired is a true parallel rather than one of the
    // weaker perfect approaches. Only that one is worth a vertical price.
    bool original_strict = false;
    for (const ConcurrentMotion& motion : into_head) {
      if (formsStrictPerfectParallel(original, arrival, motion.prev, motion.curr)) {
        original_strict = true;
        break;
      }
    }
    // The replacement sits between two fixed tones, and both intervals need a
    // ceiling: bounding only the leap into the head leaves the approach free to
    // be reached by a leap of its own, which is a registral break whether or not
    // it resolves the perfect interval. The two ceilings are not the same size.
    // Leaving the head is the constrained end, so it holds to what the designed
    // tone already spanned; entering is the free end, and is asked only not to
    // exceed an octave, because tightening it there costs more perfect intervals
    // elsewhere than the wider choice ever buys.
    const int leap_ceiling = std::max(7, std::abs(arrival - original));
    const int entry_ceiling = own_prev < 0 ? 0 : std::max(12, std::abs(original - own_prev));
    // Whether the replacement takes a neighbour's pitch that the displaced tone
    // did not already share. Such a tone lengthens a static run, so it is tried
    // only once the window has been swept without one -- ranked, not vetoed,
    // because a rule this pass has closed leaves no room to decline a repair.
    auto flattens = [&](int cand) {
      return (cand == own_prev && original != own_prev) || (cand == arrival && original != arrival);
    };
    auto admissible = [&](int cand, bool allow_dissonance) {
      if (!detail::inScale(cand, mode) || std::abs(arrival - cand) > leap_ceiling)
        return false;
      if (own_prev >= 0 && std::abs(cand - own_prev) > entry_ceiling)
        return false;
      for (const ConcurrentMotion& motion : at_onset) {
        // A lower voice index sounds higher.
        if (motion.voice < voice ? cand >= motion.curr : cand <= motion.curr)
          return false;
        if (original_consonant && !allow_dissonance && !isConsonantPair(cand, motion.curr))
          return false;
      }
      return perfectFaultRank(own_prev, cand, into_onset) <= onset_ceiling;
    };

    // Clean first, then progressively less clean, but never at or below the
    // rank the displaced tone already carried.
    //
    // The sweep covers the whole admissible window and is only ORDERED by
    // distance from the tone it displaces. What constrains a replacement is the
    // two intervals it spans; staying near the designed tone is a preference.
    // Bounding the sweep at the designed tone instead conflated the two, and put
    // the stepwise neighbourhood of the arrival out of reach exactly when the
    // design leaps into the head -- and a step into a perfect interval is the one
    // approach that is legal however the other voice moves.
    //
    // The consonance the replacement inherits is a preference, not a bound. A
    // tone displacing a consonant one is asked to stay consonant, so the pass
    // cannot trade a perfect interval for a vertical clash. But where the fault
    // is a TRUE parallel and the consonant window comes back empty, the tone
    // stands and the parallel ships -- and a passing dissonance is the smaller
    // fault of the two. So the consonance clause is dropped on a second sweep,
    // reached only after the first has been walked in full: the same trade the
    // bass's own forward guard makes one voice below.
    const int reach = 2 * leap_ceiling;
    bool placed = false;
    for (const bool allow_dissonance : {false, true}) {
      if (allow_dissonance && (placed || !original_strict || !original_consonant))
        break;
      for (int accept = 0; accept < original_rank && !placed; ++accept) {
        for (const bool allow_flatten : {false, true}) {
          for (int dist = 1; dist <= reach && !placed; ++dist) {
            for (const int sgn : {-1, 1}) {
              const int cand = original + sgn * dist;
              if ((allow_flatten || !flattens(cand)) && admissible(cand, allow_dissonance) &&
                  perfectFaultRank(cand, arrival, into_head) <= accept) {
                approach.pitch = static_cast<std::uint8_t>(cand);
                placed = true;
                break;
              }
            }
          }
          if (placed)
            break;
        }
      }
    }
  }
}

}  // namespace

HarnessFixture buildChoralePreludeForm(const ResolvedRequest& req) {
  HarnessFixture out;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int offset = static_cast<int>(req.seed % 4u);
  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Cantus firmus skeleton (immutable, one tone per bar, sized to `bars`).
  const std::vector<MaterialNote> skeleton = buildCfSkeleton(bars, req.seed, mode);

  // Per-bar harmony harmonizing the CF tone (CF tone is a chord tone of its bar
  // chord). Drives both the figuration downbeat anchor and the CF consonance.
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  std::vector<BarChord> bar_chords;
  bar_chords.reserve(static_cast<std::size_t>(bars));
  const bool picardy = mode == Mode::Minor && detail::usePicardy(req.seed);
  for (int bar = 0; bar < bars; ++bar) {
    BarChord chord = chordForCfTone(skeleton[static_cast<std::size_t>(bar)].pitch, mode);
    if (bar == bars - 1) {
      // The structural cantus and bass both resolve to C.  Make the cadence
      // an actual tonic triad by declaring its quality explicitly; minor
      // pieces use the deterministic Picardy policy shared by other forms.
      chord = {0, mode == Mode::Minor && !picardy};
    }
    bar_chords.push_back(chord);
    ChordEvent ce;
    ce.start_tick = barTick(bar);
    ce.root_pc = chord.root_pc;
    ce.quality = chord.minor ? ChordQuality::Minor : ChordQuality::Major;
    ce.is_picardy = bar == bars - 1 && picardy;
    out.harmony.chords.push_back(ce);
  }

  // Immutable skeleton material (one whole note per bar). cantus_firmus_immutable
  // reads this vector for the per-bar downbeat lookup.
  out.material.cantus_firmus = skeleton;

  // Embellished CF: each bar opens on the skeleton tone (downbeat == skeleton,
  // per the rule), held as a half note over beats 0-1. Beats 2 and 3 land on
  // CHORD TONES of the bar's triad -- so the two sampled second-half beats stay
  // consonant against the figuration above (which is itself a chord-tone wave) --
  // and any stepwise decoration is confined to the off-beat eighths, which the
  // per-beat scorer never samples. Embellishment density rises with the bar's arc
  // density tier plus the character ornament density: a low-activity bar walks two
  // plain quarter chord tones; a high-activity bar fills the off-beats with
  // stepwise passing eighths between those chord-tone beats.
  //
  // Pick the chord tone nearest a target pitch (a neighbour of the skeleton tone),
  // keeping the embellishment melodically close to the structural tone.
  auto nearestChordTone = [&](int target, const BarChord& chord) -> int {
    int up = target;
    while (!isChordTone(up, chord.root_pc, chord.minor))
      ++up;
    int down = target;
    while (!isChordTone(down, chord.root_pc, chord.minor))
      --down;
    return (up - target) <= (target - down) ? up : down;
  };
  // One diatonic step from `from` toward `to` (stepwise off-beat passing tone).
  auto stepToward = [&](int from, int to) -> int {
    if (to > from)
      return detail::inScale(from + 1, mode) ? from + 1 : from + 2;
    if (to < from)
      return detail::inScale(from - 1, mode) ? from - 1 : from - 2;
    return from;
  };

  // V0 figuration: one FigurationSection covering all `bars` bars, a
  // predominantly-stepwise scalar wave riding ABOVE the CF. Density follows the
  // arc (eighths in calm cycles, sixteenths into the climax) and the register
  // lifts with the arc register shift; Noble figuration prefers a denser dotted
  // feel realized as the sixteenth subdivision. Each bar's downbeat snaps to a
  // chord tone, so figuration_harmonic_consistency stays clean. Built BEFORE the
  // CF embellishment so a run-breaking CF substitution can be checked consonant
  // and parallel-free against the concurrently sounding figuration.
  const int cycle_count = static_cast<int>(req.cycle_count);
  FigurationSection fig;
  fig.voice = 0;
  fig.start_tick = 0;
  fig.end_tick = barTick(bars);
  for (int bar = 0; bar < bars; ++bar) {
    const int cycle = cycle_count > 0 ? (bar * cycle_count) / bars : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    int notes_per_beat = notesPerBeatFor(point, profile.density_bias);
    if (profile.prefer_dotted)
      notes_per_beat = 4;  // Noble: a busier, dotted-feel running figuration.
    // Register base stays at C4 (60) plus the arc register lift, comfortably
    // above the C3-region cantus firmus.
    const int register_base = 60 + static_cast<int>(point.register_shift);
    const BarChord& chord = bar_chords[static_cast<std::size_t>(bar)];
    // Pattern rotation per 4-bar cycle: the anchored scalar wave alternates
    // with the figura corta cell (eighth + two sixteenths per beat -- the
    // dotted-feel chorale figuration idiom), so consecutive cycles trace
    // distinct accompaniment figures over the unchanged cantus firmus. The
    // climax cycle is a design value and keeps the densest wave; the cycle
    // holding the piece's mid-boundary bar always takes the corta figure so
    // the mid sub-cadence carries ornament-eligible long notes regardless of
    // the seed's rotation parity.
    const int mid_boundary_bar = bars >= 8 ? ((bars / 2) / 4) * 4 - 1 : -1;
    const bool mid_cycle = mid_boundary_bar >= 0 && (bar / 4) == (mid_boundary_bar / 4);
    const bool corta = !point.is_climax &&
                       ((((req.seed + static_cast<std::uint32_t>(bar / 4)) % 2) == 1) || mid_cycle);
    if (corta) {
      const int corta_figure =
          static_cast<int>((req.seed + req.seed / 4u + static_cast<std::uint32_t>(bar)) % 4u);
      appendFiguraCortaBar(fig.notes, bar,
                           snapUpToChordTone(detail::scaleUp(register_base, offset, mode),
                                             chord.root_pc, chord.minor),
                           detail::ChordSpec{chord.root_pc, chord.minor}, mode, corta_figure,
                           profile.prefer_dotted);
    } else {
      // Anchor-contour rotation, phased by the seed so the same section plan
      // lays the contours over different bars across seeds. The phase folds in
      // seed/4 as well: every other seed input here is mod-4 (register offset,
      // corta parity), so without it seeds congruent mod 4 produced identical
      // chorale preludes.
      const int figure =
          static_cast<int>((req.seed + req.seed / 4u + static_cast<std::uint32_t>(bar)) % 4u);
      // Sixteenth-tier bars open with the leap-and-fill cell (ascending-sixth
      // leap, stepwise descent): the contour windows' stepwise fills alone
      // leave the ascending-sixth interval bins -- a fixture of the chorale
      // figuration idiom -- almost empty.
      appendFigurationBar(fig, bar, chord, mode, notes_per_beat, register_base, offset, figure,
                          /*leap_fill=*/notes_per_beat == 4);
    }
  }

  // Cadential landing on the figuration: the running line stops with an
  // eighth-note approach into a held pre-final tone (the cadential trill
  // site), then a whole-note third over the CF's closing tonic. The CF's
  // penultimate-bar chord harmonizes whatever degree the tune walks there, so
  // the pre-final tone is chosen for consonance: the step above the tonic (the
  // 2nd-degree trill) when the penultimate chord supports it, else the leading
  // tone, else the tonic itself (an anticipation). The approach downbeat snaps
  // to a chord tone (the figuration downbeat rule reads the bar head).
  {
    constexpr int kFigTonic = 72;  // C5: the figuration's closing register.
    const int closing_third = kFigTonic + ((mode == Mode::Minor && !picardy) ? 3 : 4);
    const BarChord& penult = bar_chords[static_cast<std::size_t>(bars - 2)];
    const detail::ChordSpec penult_spec{penult.root_pc, penult.minor};
    const int third = penult.minor ? 3 : 4;
    const int triad_pc[3] = {penult.root_pc % 12, (penult.root_pc + third) % 12,
                             (penult.root_pc + 7) % 12};
    auto consonant_with_penult = [&](int pitch) {
      for (int tone : triad_pc) {
        if (!isConsonantIc(pitch - tone))
          return false;
      }
      return true;
    };
    int prefinal = kFigTonic;  // anticipation fallback.
    const int step_above = detail::scaleUp(kFigTonic, 1, mode);
    if (consonant_with_penult(step_above)) {
      prefinal = step_above;
    } else if (consonant_with_penult(kFigTonic - 1)) {
      prefinal = kFigTonic - 1;  // raised leading tone (B natural in minor too).
    }
    appendCadentialLanding(fig.notes, barTick(bars - 2), kTicksPerBar, prefinal, closing_third,
                           mode,
                           /*band_lo=*/62, &penult_spec, /*prefer_descending=*/false,
                           /*lift_to_context=*/true);
  }

  // V0 figuration registry for the embellishment loop's run-break consonance
  // check. (V2 is selected AFTER this loop against a registry pre-loaded with
  // V0+V1, so the bass adapts to whatever the CF substitutes settle on.)
  ThemeToneRegistry fig_registry;
  for (const MaterialNote& note : fig.notes)
    fig_registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);

  // The maximum allowed run of identical V1 pitches. The texture gate caps
  // repeated runs at four, so a fourth consecutive identical pitch is displaced
  // to a consonant diatonic neighbour. Tracked across the whole V1 line (the
  // half-note skeleton tone plus the beat-2/beat-3 chord tones), so same-degree
  // adjacent bars no longer chain a plain `tone,tone,tone` figure into a long run.
  constexpr int kMaxV1Run = 4;
  int v1_prev = -1;  // previous sounding V1 pitch.
  int v1_run = 0;    // length of the current identical-pitch run (1-based).

  // Track a V1 pitch as it is emitted, returning a (possibly substituted) pitch
  // whose addition does not extend an identical-pitch run past kMaxV1Run. When
  // the candidate would be the kMaxV1Run-th identical pitch, it is displaced to
  // the nearest consonant, parallel-free diatonic NEIGHBOUR (upper preferred,
  // then lower) of the candidate. The substitute is a step away from the
  // structural tone and resolves back on the following beat, the standard chorale
  // embellishment vocabulary. The downbeat skeleton tone is never passed here, so
  // the immutable bar-head pitch is preserved.
  auto breakRun = [&](int cand, const BarChord& chord, Tick onset) -> int {
    const bool repeats = (cand == v1_prev);
    int chosen = cand;
    if (repeats && v1_run + 1 >= kMaxV1Run) {
      // Concurrent V0 figuration pitch at this onset (for consonance / parallels).
      const int fig_now = fig_registry.soundingPitchInVoice(/*voice=*/0, onset);
      const int fig_prev = fig_registry.soundingPitchInVoice(/*voice=*/0, onset - kQuarterDur);
      // Candidate neighbours: a whole/half step up, then down, staying diatonic.
      const int up = detail::inScale(cand + 1, mode) ? cand + 1 : cand + 2;
      const int down = detail::inScale(cand - 1, mode) ? cand - 1 : cand - 2;
      for (int neighbour : {up, down}) {
        // Stay inside the V1 register window (see the departure picks).
        if (neighbour == cand || neighbour <= kBassBandHi || neighbour > kV1EmbellishCeiling)
          continue;
        if (fig_now >= 0 && !isConsonantPair(neighbour, fig_now))
          continue;
        if (fig_now >= 0 && formsPerfectParallel(v1_prev, neighbour, fig_prev, fig_now))
          continue;
        chosen = neighbour;
        break;
      }
      // Keep the substitute a real chord-bracketed neighbour: if neither neighbour
      // is admissible, fall back to a chord tone above so the beat stays consonant
      // (unless that would breach the embellishment ceiling -- then the repeat
      // stands rather than crossing into the V0 register).
      if (chosen == cand) {
        const int chord_tone = chordToneAbove(cand, chord.root_pc, chord.minor);
        if (chord_tone <= kV1EmbellishCeiling)
          chosen = chord_tone;
      }
    }
    v1_run = (chosen == v1_prev) ? v1_run + 1 : 1;
    v1_prev = chosen;
    return chosen;
  };

  // Parallel guard for the embellishment's chord-tone beats. The beat-2 /
  // beat-3 tones are chosen from the bar chord alone, but the V0 figuration
  // is already final, so a chord tone that moves with V0 into a perfect
  // class is knowable at emission time. Re-judge the candidate against V0 at
  // both grains the union-onset sampler can pair this beat with (the eighth
  // before it when the figuration is dense, the quarter otherwise) and
  // displace to the nearest tone of the bar chord that clears the parallel
  // and stays consonant; the candidate stands when no alternative exists.
  auto guardV1Parallel = [&](int cand, Tick onset, const BarChord& chord) -> int {
    if (v1_prev < 0)
      return cand;
    const int fig_now = fig_registry.soundingPitchInVoice(/*voice=*/0, onset);
    if (fig_now < 0)
      return cand;
    auto parallel_with_fig = [&](int p) {
      for (const Tick grain : {kSixteenth, kEighth, kQuarterDur}) {
        const int fig_prev = fig_registry.soundingPitchInVoice(/*voice=*/0, onset - grain);
        if (fig_prev >= 0 && formsPerfectParallel(v1_prev, p, fig_prev, fig_now))
          return true;
      }
      return false;
    };
    if (!parallel_with_fig(cand))
      return cand;
    const int third_pc = (chord.root_pc + (chord.minor ? 3 : 4)) % 12;
    const int fifth_pc = (chord.root_pc + 7) % 12;
    auto is_chord_pc = [&](int p) {
      const int pc = ((p % 12) + 12) % 12;
      return pc == chord.root_pc || pc == third_pc || pc == fifth_pc;
    };
    for (int dist = 1; dist <= 7; ++dist) {
      for (const int sgn : {1, -1}) {
        const int alt = cand + sgn * dist;
        // Stay clear of the walking-bass band below (the bass needs room to
        // sit strictly under the CF line) and of the V0 figuration floor
        // above (an alternative past the embellishment ceiling sits over
        // V0's low fill tones).
        if (alt <= kBassBandHi || alt > kV1EmbellishCeiling || alt == v1_prev || !is_chord_pc(alt))
          continue;
        if (!isConsonantPair(alt, fig_now) || parallel_with_fig(alt))
          continue;
        return alt;
      }
    }
    return cand;
  };

  // The passing eighths between the chord-tone beats carry no guard of their
  // own: guardV1Parallel substitutes chord tones only, which a passing tone is
  // by definition not obliged to be, so it has nothing admissible to offer here.
  // Left unguarded these eighths step in parallel with the figuration above
  // whenever the two lines happen to move the same way, which is often. The
  // escape ranks the same way every displacement in this tree does, over a
  // diatonic candidate set, and keeps the oblique repeat in reserve as the last
  // resort -- a repeated tone cannot form a parallel with anything, which is
  // exactly why it is worth holding back for the case where nothing else is
  // clean.
  auto guardV1Passing = [&](int cand, Tick onset, int from) -> int {
    if (from < 0)
      return cand;
    const int fig_now = fig_registry.soundingPitchInVoice(/*voice=*/0, onset);
    if (fig_now < 0)
      return cand;
    auto rank = [&](int pitch) {
      int worst = 0;
      for (const Tick grain : {kSixteenth, kEighth, kQuarterDur}) {
        const int fig_prev = fig_registry.soundingPitchInVoice(/*voice=*/0, onset - grain);
        if (fig_prev < 0)
          continue;
        if (formsPerfectParallel(from, pitch, fig_prev, fig_now))
          return 3;
        if (formsAntiParallelPerfect(from, pitch, fig_prev, fig_now))
          worst = std::max(worst, 2);
        else if (formsBattuta(from, pitch, fig_prev, fig_now))
          worst = std::max(worst, 1);
      }
      return worst;
    };
    const int cand_rank = rank(cand);
    if (cand_rank == 0)
      return cand;
    const int cand_consonant = isConsonantPair(cand, fig_now);
    const int alternatives[] = {detail::scaleUp(from, 1, mode), detail::scaleDown(from, 1, mode),
                                detail::scaleUp(from, 2, mode), detail::scaleDown(from, 2, mode),
                                from};
    for (int accept = 0; accept < cand_rank; ++accept) {
      for (const int alt : alternatives) {
        if (alt == cand || alt <= kBassBandHi || alt > kV1EmbellishCeiling)
          continue;
        if (cand_consonant && !isConsonantPair(alt, fig_now))
          continue;
        if (rank(alt) <= accept)
          return alt;
      }
    }
    return cand;
  };

  for (int bar = 0; bar < bars; ++bar) {
    const int tone = skeleton[static_cast<std::size_t>(bar)].pitch;
    const BarChord& chord = bar_chords[static_cast<std::size_t>(bar)];
    const Tick base = barTick(bar);
    // Arc density for the cycle this bar belongs to, biased by the character's
    // ornament density. A denser bar fills the off-beats with passing eighths.
    const int cycle = cycle_count > 0 ? (bar * cycle_count) / bars : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    const int activity =
        static_cast<int>(point.density_tier) + static_cast<int>(profile.ornament_density);

    // Beat 2 / beat 3 chord-tone targets, rotating the departure figure per
    // bar: the third-up-and-return (the chord tone strictly above the skeleton
    // tone, resolving back), a fifth departure falling to the third, or a
    // fifth departure returning home. All picks are chord tones, so the
    // sampled beats stay consonant, and every variant ends the bar low (a bar
    // ending high forces a wide descent into the next skeleton tone that
    // lines up with the other voices' downbeat descents into same-direction
    // perfect arrivals); a single figure repeated every bar stamped the
    // third-pendulum interval bigram into a tenth of the line. (A
    // nearest-tone pick for beat 2 ties back to the skeleton tone itself,
    // which flattens the bar into a tone,tone,tone repeated-note figure.)
    // Every pick is lifted clear of the walking-bass band: the embellishment
    // orbits the CF in the C3 region, and a tone at or below B2 leaves the
    // bass (whose own band tops out there) no room to sit strictly below.
    auto liftAboveBassBand = [&](int t) {
      while (t <= kBassBandHi)
        t = chordToneAbove(t, chord.root_pc, chord.minor);
      return t;
    };
    const int above1 = liftAboveBassBand(chordToneAbove(tone, chord.root_pc, chord.minor));
    const int above2 = chordToneAbove(above1, chord.root_pc, chord.minor);
    // The fifth departure must also respect the V1 embellishment ceiling
    // (the V0 figuration's register floor): a climb past it would sit above
    // V0's low fill tones -- a voice crossing. When the fifth has no room,
    // the bar falls back to the third-up-and-return.
    const bool fifth_fits = above2 <= kV1EmbellishCeiling;
    const int departure =
        fifth_fits
            ? static_cast<int>((req.seed + req.seed / 4u + static_cast<std::uint32_t>(bar)) % 3u)
            : 0;
    const int beat2 = (departure == 0) ? above1 : above2;
    const int beat3 = (departure == 1) ? above1 : liftAboveBassBand(nearestChordTone(tone, chord));

    // The final bar holds the closing tonic as one whole note: the CF joins
    // the held final chord instead of walking chord-tone quarters through the
    // close (the bar-head skeleton tone is unchanged).
    if (bar == bars - 1) {
      out.material.cf_embellished.push_back(materialNote(base, kTicksPerBar, tone));
      v1_run = (tone == v1_prev) ? v1_run + 1 : 1;
      v1_prev = tone;
      continue;
    }

    // Downbeat skeleton tone, held as a half note (beats 0-1). The skeleton tone
    // is immutable, so it is never substituted; it still advances the run tracker
    // so a chain that continues through the bar head is counted.
    out.material.cf_embellished.push_back(materialNote(base, kHalf, tone));
    v1_run = (tone == v1_prev) ? v1_run + 1 : 1;
    v1_prev = tone;
    if (activity >= 3) {
      // Dense: chord-tone beats with a stepwise passing eighth on each off-beat.
      // The stepwise off-beats already break repetition; the run-break guard on
      // the two chord-tone beats keeps a long static figure from chaining.
      const int b2 = breakRun(guardV1Parallel(beat2, base + kHalf, chord), chord, base + kHalf);
      const int off2 = guardV1Passing(stepToward(b2, beat3), base + kHalf + kEighth, b2);
      out.material.cf_embellished.push_back(materialNote(base + kHalf, kEighth, b2));
      out.material.cf_embellished.push_back(materialNote(base + kHalf + kEighth, kEighth, off2));
      // The passing eighth advances the run tracker so the next beat sees it.
      v1_run = (off2 == v1_prev) ? v1_run + 1 : 1;
      v1_prev = off2;
      const int b3 = breakRun(guardV1Parallel(beat3, base + kHalf + 2 * kEighth, chord), chord,
                              base + kHalf + 2 * kEighth);
      // The final passing eighth leads into the NEXT bar's skeleton tone; when
      // the next bar repeats this bar's degree it becomes an upper-neighbour
      // return instead (stepping "toward" the tone we already sit on would
      // just repeat the pitch).
      const int next_tone =
          (bar + 1 < bars) ? static_cast<int>(skeleton[static_cast<std::size_t>(bar + 1)].pitch)
                           : tone;
      int off3 = stepToward(b3, next_tone);
      if (off3 == b3)
        off3 = detail::scaleUp(b3, 1, mode);
      off3 = guardV1Passing(off3, base + kHalf + 3 * kEighth, b3);
      out.material.cf_embellished.push_back(materialNote(base + kHalf + 2 * kEighth, kEighth, b3));
      out.material.cf_embellished.push_back(
          materialNote(base + kHalf + 3 * kEighth, kEighth, off3));
      v1_run = (off3 == v1_prev) ? v1_run + 1 : 1;
      v1_prev = off3;
    } else {
      // Plain: two quarter chord tones on beats 2 and 3, each run-break guarded so
      // a same-degree run never exceeds four identical pitches.
      const int b2 = breakRun(guardV1Parallel(beat2, base + kHalf, chord), chord, base + kHalf);
      out.material.cf_embellished.push_back(materialNote(base + kHalf, kQuarterDur, b2));
      const int b3 = breakRun(guardV1Parallel(beat3, base + kHalf + kQuarterDur, chord), chord,
                              base + kHalf + kQuarterDur);
      out.material.cf_embellished.push_back(
          materialNote(base + kHalf + kQuarterDur, kQuarterDur, b3));
    }
  }
  out.material.cf_is_embellished = true;
  out.material.cf_placement = 1;  // Tenor (documentary).

  // Publish the figuration section built above (before the embellishment loop).
  out.material.figuration_sections.push_back(fig);

  // V2 walking bass (Schubler BWV645 third layer): a quarter-note bass line
  // connecting the chord roots stepwise, sitting in the C2 band well below the
  // cantus firmus. Built AFTER V0 (figuration) and V1 (embellished cantus
  // firmus) are recorded in the registry, so each anchor is selected consonant
  // and parallel-free against both upper voices. The bass carries the
  // TrioVoiceCarrier intent (verbatim replay, stamping TrioVoiceIndependent),
  // matching the Goldberg / passacaglia middle-voice precedent in this tree:
  // because it is the ONLY voice carrying that bit, voice_independence_threshold
  // (which needs >= 2 such voices) stays inert -- no soft-fail is introduced.
  ThemeToneRegistry bass_registry;
  for (const MaterialNote& note : fig.notes)
    bass_registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
  for (const MaterialNote& note : out.material.cf_embellished)
    bass_registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch), note.duration);
  std::vector<MaterialNote> bass_notes;
  bass_notes.reserve(static_cast<std::size_t>(bars) * 4);
  appendWalkingBass(bass_notes, bass_registry, bar_chords, mode);
  const Tick final_bar_tick = barTick(bars - 1);
  const Tick final_approach_tick = final_bar_tick - kQuarterDur;
  int final_root = -1;
  for (const MaterialNote& note : bass_notes) {
    if (note.start_tick == final_bar_tick)
      final_root = static_cast<int>(note.pitch);
  }
  // Dominant approach into the tonic coda. This pitch is not a preference and
  // is not negotiable against the counterpoint: the imperfect authentic cadence
  // registered below requires the bass to sound the dominant pitch class on the
  // approach beat, and the bass band admits exactly one octave of it. Any
  // perfect-interval fault this beat forms has to be resolved by moving the
  // voice above it, never by re-aiming the bass.
  for (MaterialNote& note : bass_notes) {
    if (note.start_tick == final_approach_tick) {
      note.pitch = 43;  // G2.
      break;
    }
  }
  // The bass joins the held final chord: its final bar collapses to one
  // whole-note tonic root instead of walking quarters through the close.
  if (final_root >= 0) {
    bass_notes.erase(
        std::remove_if(bass_notes.begin(), bass_notes.end(),
                       [&](const MaterialNote& note) { return note.start_tick >= final_bar_tick; }),
        bass_notes.end());
    bass_notes.push_back(materialNote(final_bar_tick, kTicksPerBar, final_root));
  }

  // Re-recorded from the finished lines: the two rewrites above (the pinned
  // dominant approach and the coda collapse) landed after the walking bass was
  // emitted, so the registry it built no longer describes the bass that ships.
  // Relieved in build order, each line against the other two as they now stand,
  // so the second pass sees the first pass's result rather than the tones it
  // replaced.
  std::vector<MaterialNote>& fig_notes = out.material.figuration_sections.back().notes;
  for (VoiceId voice : {VoiceId{1}, VoiceId{0}}) {
    ThemeToneRegistry relief_registry;
    if (voice != 0) {
      for (const MaterialNote& note : fig_notes)
        relief_registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch),
                               note.duration);
    }
    if (voice != 1) {
      for (const MaterialNote& note : out.material.cf_embellished)
        relief_registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch),
                               note.duration);
    }
    for (const MaterialNote& note : bass_notes)
      relief_registry.record(note.start_tick, /*voice=*/2, static_cast<int>(note.pitch),
                             note.duration);
    relieveArrivals(lineInTickOrder({voice == 0 ? &fig_notes : &out.material.cf_embellished}),
                    relief_registry, voice, /*num_voices=*/3, bars, mode);
  }

  ChordEvent approach;
  approach.start_tick = final_approach_tick;
  approach.root_pc = 7;
  approach.quality = ChordQuality::Dominant7;
  approach.degree = RomanNumeral::V;
  approach.function = HarmonicFunction::D;
  approach.has_degree = true;
  out.harmony.chords.insert(out.harmony.chords.end() - 1, approach);
  out.harmony.cadences.push_back({final_bar_tick, CadenceType::ImperfectAuthentic});
  TrioVoiceLine bass_line;
  bass_line.voice = 2;
  bass_line.manual = 3;  // documentary (Pedal): V2 = lowest line.
  bass_line.notes = std::move(bass_notes);
  out.material.trio_voices.push_back(std::move(bass_line));

  // VoicePlan: V0 one FigurationCarrier span over all bars; V1 one
  // CantusFirmusCarrier span over all bars; V2 one TrioVoiceCarrier span (the
  // walking bass) over all bars. Register order V0 (figuration, C4+) >
  // V1 (cantus firmus, C3-region) > V2 (walking bass, C2) holds at every tick,
  // so no voice crossing occurs.
  out.voice_plan.num_voices = 3;
  Span fig_span;
  fig_span.id = 0;
  fig_span.start_tick = 0;
  fig_span.end_tick = barTick(bars);
  fig_span.voice = 0;
  fig_span.intent = VoiceIntent::FigurationCarrier;
  fig_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(fig_span);

  Span cf_span;
  cf_span.id = 1;
  cf_span.start_tick = 0;
  cf_span.end_tick = barTick(bars);
  cf_span.voice = 1;
  cf_span.intent = VoiceIntent::CantusFirmusCarrier;
  cf_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(cf_span);

  Span bass_span;
  bass_span.id = 2;
  bass_span.start_tick = 0;
  bass_span.end_tick = barTick(bars);
  bass_span.voice = 2;
  bass_span.intent = VoiceIntent::TrioVoiceCarrier;
  bass_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(bass_span);

  // Cadential suspension in the ornamenting top voice. Search the closing
  // cadence region from the penultimate bar backward; the preceding final beat
  // prepares the held pitch, which resolves on beat two before a one-beat rest.
  // The cantus remains immutable while its actual pitches constrain the
  // inserted top line above V1 and keep every non-bass vertical pair consonant.
  {
    auto& bass = out.material.trio_voices.front().notes;
    const auto& cantus = out.material.cf_embellished;
    const auto& top = out.material.figuration_sections.front().notes;
    bool installed = false;
    for (int bar_offset : {2, 3, 4, 5}) {
      if (installed || bars <= bar_offset)
        break;
      const Tick suspension_tick = barTick(bars - bar_offset);
      const Tick preparation_tick = suspension_tick - kTicksPerBeat;
      const Tick resolution_tick = suspension_tick + kTicksPerBeat;
      const int original_bass_prep = soundingMaterialPitch(bass, preparation_tick);
      const int held_bass = soundingMaterialPitch(bass, suspension_tick);
      const int cantus_prep = soundingMaterialPitch(cantus, preparation_tick);
      const int cantus_sus = soundingMaterialPitch(cantus, suspension_tick);
      const int cantus_res = soundingMaterialPitch(cantus, resolution_tick);
      const int top_prep = soundingMaterialPitch(top, preparation_tick);
      const int top_sus = soundingMaterialPitch(top, suspension_tick);
      const int top_res = soundingMaterialPitch(top, resolution_tick);
      const int band_hi = std::min(86, std::max({top_prep, top_sus, top_res}) + 5);
      // The tone the three lines were last read against, one sixteenth before
      // each arrival -- the grain a union-onset reading pairs the voices at.
      const int bass_before = soundingMaterialPitch(bass, preparation_tick - kSixteenth);
      const int cantus_before = soundingMaterialPitch(cantus, preparation_tick - kSixteenth);
      const int top_before = soundingMaterialPitch(top, preparation_tick - kSixteenth);
      // The figure also pins the bass under its resolution, which changes what
      // the bass LEAVES that beat with. The top line rests immediately after, so
      // the pair that carries the motion is the cantus over the bass -- and it
      // does not depend on which preparation tone is picked below, so a bar
      // whose exit cannot be answered is abandoned for the next candidate bar
      // rather than searched.
      const Tick departure_tick = resolution_tick + kTicksPerBeat;
      if (formsStrictPerfectParallel(held_bass, soundingMaterialPitch(bass, departure_tick),
                                     soundingMaterialPitch(cantus, departure_tick - kSixteenth),
                                     soundingMaterialPitch(cantus, departure_tick)))
        continue;
      for (int distance = 0; distance <= 7 && !installed; ++distance) {
        for (int direction : {1, -1}) {
          if (distance == 0 && direction < 0)
            continue;
          const int bass_prep = original_bass_prep + direction * distance;
          if (bass_prep < 30 || bass_prep >= cantus_prep || !detail::inScale(bass_prep, mode) ||
              !isConsonantPair(cantus_prep, bass_prep))
            continue;
          for (SuspensionType type :
               {SuspensionType::Sus7_6, SuspensionType::Sus4_3, SuspensionType::Sus9_8}) {
            SuspensionPattern suspension;
            if (!designUpperSuspension(
                    type, preparation_tick, suspension_tick, resolution_tick,
                    /*voice=*/0, static_cast<std::uint8_t>(bass_prep),
                    static_cast<std::uint8_t>(held_bass), static_cast<std::uint8_t>(held_bass),
                    static_cast<std::uint8_t>(cantus_prep), static_cast<std::uint8_t>(cantus_sus),
                    static_cast<std::uint8_t>(cantus_res),
                    /*band_lo=*/
                    std::max({bass_prep, held_bass, cantus_prep, cantus_sus, cantus_res}) + 1,
                    band_hi, mode, &suspension))
              continue;
            // Displacing the bass and installing the carrier are one choice: the
            // carrier overwrites the top line across the whole figure, so what
            // ships here is the designed suspension over the displaced bass, not
            // the figuration either of them was read against. This is the last
            // point at which the form touches any voice, so the three-line
            // surface the pair produces is read once, whole, before either end
            // is committed -- every arrival the figure creates, against every
            // other line sounding into it.
            const int prep_pitch = static_cast<int>(suspension.preparation_pitch);
            const int sus_pitch = static_cast<int>(suspension.suspension_pitch);
            const int res_pitch = static_cast<int>(suspension.resolution_pitch);
            if (formsStrictPerfectParallel(top_before, prep_pitch, cantus_before, cantus_prep) ||
                formsStrictPerfectParallel(top_before, prep_pitch, bass_before, bass_prep) ||
                formsStrictPerfectParallel(cantus_before, cantus_prep, bass_before, bass_prep) ||
                formsStrictPerfectParallel(cantus_prep, cantus_sus, bass_prep, held_bass) ||
                formsStrictPerfectParallel(sus_pitch, res_pitch, cantus_sus, cantus_res))
              continue;
            for (MaterialNote& note : bass) {
              if (note.start_tick <= preparation_tick &&
                  preparation_tick < note.start_tick + note.duration)
                note.pitch = static_cast<std::uint8_t>(bass_prep);
              if (note.start_tick == resolution_tick)
                note.pitch = static_cast<std::uint8_t>(held_bass);
            }
            installed = installSuspensionCarrier(out.material, out.voice_plan, suspension);
            break;
          }
          if (installed)
            break;
        }
      }
    }
  }

  return out;
}

// --- Goldberg variation framework ------------------------------------------

GoldbergVariationKind goldbergVariationKind(std::size_t variation_index) {
  // BWV988 scheme: every third variation is a canon (variations 3, 6, ..., 27),
  // at a rising imitation interval (unison, 2nd, 3rd, ... 9th). `variation_index`
  // is zero-based, so the 1-based variation number is variation_index + 1; a
  // canon is variation number v where v % 3 == 0. The final variation of the
  // full 30-variation set (v == 30) is NOT a canon: it is the densest figuration
  // peak (the BWV988 "Quodlibet" slot), so the canon rule is capped at v < 30.
  const std::size_t variation_number = variation_index + 1;
  if (variation_number == 30)
    return GoldbergVariationKind::Quodlibet;
  if (variation_number % 3 == 0 && variation_number < 30)
    return GoldbergVariationKind::Canon;
  return GoldbergVariationKind::Figuration;
}

namespace {

// The Goldberg ground tables live in kGoldbergGroundsMajor /
// kGoldbergGroundsMinor (seed-selected design variants, period 4 bars, all
// tones C2-region chord roots so the variation downbeat anchoring stays
// consonant and the ground tiles exactly).

// Goldberg figuration palette (design table): the pattern idiom each
// non-climax figuration variation block takes, rotated by (seed +
// variation_index) so consecutive variations alternate idioms. The climax
// block and variation 30 are design values (the densest anchored scalar wave)
// and bypass the rotation; aria and canon blocks have their own layouts.
// No kArpeggio here: broken-chord blocks raise the melodic-interval cost (the
// dominant scorer feature) and the goldberg sits close to the model
// threshold, so its rotation keeps to the stepwise idioms.
constexpr PatternKind kGoldbergPalette[2] = {PatternKind::kScalarWave, PatternKind::kFiguraCorta};

// Per-bar chord for the ground cycle: the chord root IS the ground tone's
// pitch class, with the diatonic triad quality on that degree (in minor the V
// is the harmonic-minor major dominant), so the harmony stays consonant with
// the bass for every ground variant.
BarChord goldbergBarChord(std::uint8_t ground_pitch, Mode mode) {
  const std::uint8_t pc = static_cast<std::uint8_t>(ground_pitch % 12u);
  return {pc, detail::diatonicTriadMinor(pc, mode == Mode::Minor)};
}

// The eight eighth-note positions of one aria-bass bar. Each bar articulates
// its root, third and fifth and returns to the root on both structural accents,
// so the bass does not merely hold the bar's root: it arpeggiates the bar chord
// within the bar. Any voice above that figures the same triad therefore meets
// it on a perfect interval off the downbeat as readily as on it, which is why
// this shape is stated once and read by everything that has to answer for it.
constexpr Tick kAriaBassUnit = kTicksPerBeat / 2;
std::array<int, 8> goldbergAriaBassBar(int root, Mode mode) {
  const BarChord chord = goldbergBarChord(static_cast<std::uint8_t>(root), mode);
  const int third = root + (chord.minor ? 3 : 4);
  const int fifth = root + 7;
  return {root, third, fifth, third, root, fifth, third, root};
}

// Diatonic transpose a pitch UP by `degrees` scale steps (degrees may be 0 =
// unison). Octave membership is preserved because scaleUp walks the scale.
int transposeUp(int pitch, int degrees, Mode mode) {
  return degrees <= 0 ? pitch : detail::scaleUp(pitch, degrees, mode);
}

// Append an aria bar (the m=2 two-half-notes SPECIAL layout from GoldbergVariations): a
// half note on the wave start, then a half note on the NEXT CHORD TONE up. Both
// half notes are chord tones of the bar's triad, so each sampled beat (the bar's
// two half-note onsets) is consonant against the ground tone below (itself a
// chord root). Used for the opening aria and (when N >= 24) the da-capo
// restatement.
void appendAriaBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                   int register_base, int offset) {
  const int start =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  const Tick base = barTick(bar);
  var.notes.push_back(materialNote(base, kHalf, start));
  var.notes.push_back(
      materialNote(base + kHalf, kHalf, chordToneAbove(start, chord.root_pc, chord.minor)));
}

// Snapped goldberg figuration start with the keyboard ceiling applied. The
// figure built on the start (the contour ladder, the figura corta cell) rises
// up to a ninth above it, so a start above MIDI 77 would carry the line past
// d''' (MIDI 86), the top of the Bach keyboard compass. Walk such starts down
// to the highest chord tone at or under the cap -- the arc's climax register
// lift compresses against the instrument ceiling, exactly as the real
// keyboard writing does.
int goldbergFigurationStart(int register_base, int offset, const BarChord& chord, Mode mode) {
  int snapped =
      snapUpToChordTone(detail::scaleUp(register_base, offset, mode), chord.root_pc, chord.minor);
  constexpr int kFigStartCeiling = 77;
  if (snapped > kFigStartCeiling) {
    const int third = chord.minor ? 3 : 4;
    const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                             (chord.root_pc + 7) % 12};
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    int capped = kFigStartCeiling;
    while (capped > 0 && !is_triad(capped))
      --capped;
    snapped = capped;
  }
  return snapped;
}

// Append a figuration variation bar: a downbeat-anchored scalar wave with
// `notes_per_beat` subdivision (1 = quarters, 2 = eighths, 4 = sixteenths).
void appendVariationBar(PassacagliaVariation& var, int bar, const BarChord& chord, Mode mode,
                        int notes_per_beat, int register_base, int offset) {
  const int snapped = goldbergFigurationStart(register_base, offset, chord, mode);
  emitAnchoredBar(bar, snapped, chord, mode, notes_per_beat, [&](Tick start, Tick dur, int pitch) {
    var.notes.push_back(materialNote(start, dur, pitch));
  });
}

// Canon register anchor. Narrow canons place the dux in this upper band and
// imitate below; canons at the fifth and wider place the dux one octave lower
// and imitate above, matching the rising-interval layout without crossing the
// physical V0/V1 register order.
constexpr int kCanonLeaderBase = 72;  // C5: aligned with the figuration band.

// The leader tones a canon bar may be built on: chord tones of that bar's
// ground chord, taken downwards from the ceiling of the leader band.
//
// The band is one octave deep, which is the shallowest depth that always holds
// all three tones of a triad -- a narrower one leaves bars with a single
// candidate, and a bar with no choice cannot answer for what it collides with.
// Depth is what the band is for; the register order it used to guarantee by
// being narrow is now read off the assembled block instead, where it is a
// measured fact rather than a bound taken on trust.
//
// Always three per bar. Where the band holds fewer the last is repeated, so a
// choice over this set is always defined and never has to test for emptiness.
std::array<std::array<int, 3>, 4> canonLeaderCandidates(int pitch_ceiling, Mode mode,
                                                        const std::array<std::uint8_t, 4>& ground) {
  const int pitch_floor = pitch_ceiling - 11;
  std::array<std::array<int, 3>, 4> tones{};
  for (int bar = 0; bar < 4; ++bar) {
    const BarChord chord = goldbergBarChord(ground[static_cast<std::size_t>(bar)], mode);
    const int third = chord.minor ? 3 : 4;
    const std::array<int, 3> chord_pcs = {static_cast<int>(chord.root_pc),
                                          (static_cast<int>(chord.root_pc) + third) % 12,
                                          (static_cast<int>(chord.root_pc) + 7) % 12};
    int count = 0;
    for (int pitch = pitch_ceiling; pitch >= pitch_floor && count < 3; --pitch) {
      const int pc = pitch % 12;
      if (pc == chord_pcs[0] || pc == chord_pcs[1] || pc == chord_pcs[2])
        tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count++)] = pitch;
    }
    // An octave-deep band cannot miss a chord tone, but fail closed to the
    // snapped one rather than to silence if a future ground table ever does.
    if (count == 0) {
      int fallback = snapUpToChordTone(pitch_floor, chord.root_pc, chord.minor);
      while (fallback > pitch_ceiling)
        fallback -= 12;
      tones[static_cast<std::size_t>(bar)][0] = fallback;
      count = 1;
    }
    while (count < 3) {
      tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count)] =
          tones[static_cast<std::size_t>(bar)][static_cast<std::size_t>(count - 1)];
      ++count;
    }
  }
  return tones;
}

// The dux cell: a six-note soggetto with an eighth-eighth-quarter rhythm in each
// half-bar, its chord-tone beat onsets connected by contrary neighbours.
std::vector<MaterialNote> canonSoggetto(int tone, bool rising, Mode mode) {
  const int first_neighbour =
      rising ? detail::scaleUp(tone, 1, mode) : detail::scaleDown(tone, 1, mode);
  const int second_neighbour =
      rising ? detail::scaleDown(tone, 1, mode) : detail::scaleUp(tone, 1, mode);
  std::vector<MaterialNote> soggetto;
  soggetto.reserve(6);
  soggetto.push_back(materialNote(0, kEighth, tone));
  soggetto.push_back(materialNote(kEighth, kEighth, first_neighbour));
  soggetto.push_back(materialNote(kTicksPerBeat, kTicksPerBeat, tone));
  soggetto.push_back(materialNote(kHalf, kEighth, tone));
  soggetto.push_back(materialNote(kHalf + kEighth, kEighth, second_neighbour));
  soggetto.push_back(materialNote(kHalf + kTicksPerBeat, kTicksPerBeat, tone));
  return soggetto;
}

// The two lines of one canon block, before either is assigned to a voice.
struct CanonLines {
  std::vector<MaterialNote> dux;
  std::vector<MaterialNote> comes;
};

// Lay out a canon block from a leader-tone assignment. The dux states the
// soggetto once per bar, re-anchored to the same source cell; the comes is an
// exact constant-semitone copy of it, delayed one bar and truncated at the
// block end.
CanonLines layOutCanon(const std::array<int, 4>& designed, int block_start_bar,
                       int source_register_shift, int comes_shift, Mode mode) {
  CanonLines lines;
  lines.dux.reserve(24);
  for (int local = 0; local < 4; ++local) {
    const int bar = block_start_bar + local;
    const int tone = designed[static_cast<std::size_t>(bar % 4)] + source_register_shift;
    const auto source = canonSoggetto(tone, /*rising=*/(local % 2) == 0, mode);
    auto anchored = motif_ops::reanchorMelody(source, barTick(bar));
    lines.dux.insert(lines.dux.end(), anchored.begin(), anchored.end());
  }
  lines.comes.reserve(18);
  const Tick block_end = barTick(block_start_bar + 4);
  for (const auto& note : lines.dux) {
    const Tick delayed = note.start_tick + kTicksPerBar;
    if (delayed >= block_end)
      continue;
    MaterialNote copy = note;
    copy.start_tick = delayed;
    copy.pitch = static_cast<std::uint8_t>(static_cast<int>(copy.pitch) + comes_shift);
    lines.comes.push_back(copy);
  }
  return lines;
}

// Read a laid-out four-bar block against the aria bass it will sound over.
//
// A block whose upper voices are settled here is a closed system. The aria bass
// repeats on exactly the four-bar period the block spans, it is immutable by
// contract, and the relief pass that answers for the free figuration elsewhere
// deliberately skips the imitative blocks -- the canon pair cannot be re-aimed
// one end at a time without dissolving the imitation. So the whole three-voice
// surface follows from the choices made here and can be read before a note is
// committed, at the grain an external reading pairs the voices at: every onset
// of any voice, against whatever the others are sounding then.
//
// Reported worst first, so the array compares as a preference order: a crossing
// or a unison breaks the register order the form is built on, a true parallel is the fault
// the ear names, then the weaker perfect approaches, then the dissonant
// simultaneities. A caller with its own terms to weigh interleaves them.
std::array<int, 4> goldbergBlockFaults(const std::vector<MaterialNote>& upper,
                                       const std::vector<MaterialNote>& inner, int block_start_bar,
                                       const std::array<std::uint8_t, 4>& ground, Mode mode) {
  std::vector<MaterialNote> bass;
  bass.reserve(32);
  for (int local = 0; local < 4; ++local) {
    const int bar = block_start_bar + local;
    const std::array<int, 8> phrase =
        goldbergAriaBassBar(ground[static_cast<std::size_t>(bar % 4)], mode);
    for (std::size_t pos = 0; pos < phrase.size(); ++pos) {
      bass.push_back(materialNote(barTick(bar) + static_cast<Tick>(pos) * kAriaBassUnit,
                                  kAriaBassUnit, phrase[pos]));
    }
  }
  // Register order, highest first, matching the physical voice indices.
  const std::vector<MaterialNote>* voices[3] = {&upper, &inner, &bass};

  std::vector<Tick> onsets;
  for (const std::vector<MaterialNote>* voice : voices) {
    for (const MaterialNote& note : *voice)
      onsets.push_back(note.start_tick);
  }
  std::sort(onsets.begin(), onsets.end());
  onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());

  std::array<int, 4> score{};
  std::array<int, 3> prev = {-1, -1, -1};
  for (const Tick tick : onsets) {
    std::array<int, 3> curr = {-1, -1, -1};
    for (std::size_t idx = 0; idx < 3; ++idx) {
      for (const MaterialNote& note : *voices[idx]) {
        if (note.start_tick <= tick && tick < note.start_tick + note.duration) {
          curr[idx] = static_cast<int>(note.pitch);
          break;
        }
      }
    }
    for (std::size_t above = 0; above < 3; ++above) {
      for (std::size_t below = above + 1; below < 3; ++below) {
        if (curr[above] < 0 || curr[below] < 0)
          continue;
        // Strict: the form's register order admits no unison either, so a
        // meeting counts the same as a crossing.
        if (curr[above] <= curr[below])
          ++score[0];
        if (!isConsonantPair(curr[above], curr[below]))
          ++score[3];
        if (prev[above] < 0 || prev[below] < 0)
          continue;
        if (formsStrictPerfectParallel(prev[above], curr[above], prev[below], curr[below]))
          ++score[1];
        else if (formsPerfectParallel(prev[above], curr[above], prev[below], curr[below]) ||
                 formsAntiParallelPerfect(prev[above], curr[above], prev[below], curr[below]) ||
                 formsBattuta(prev[above], curr[above], prev[below], curr[below]))
          ++score[2];
      }
    }
    prev = curr;
  }
  return score;
}

// Choose the leader tones for a canon block.
//
// Exhaustive over the candidate set rather than a left-to-right walk. A bar's
// tone answers for two bars at once -- it is the dux in its own bar and the
// comes in the next -- so a per-bar cost cannot be settled before the neighbour
// it will be echoed against is known. Three tones in each of four bars is a
// small enough set to read every assembly of it exactly.
std::array<int, 4> designCanonLeader(int pitch_ceiling, Mode mode,
                                     const std::array<std::uint8_t, 4>& ground,
                                     int source_register_shift, int comes_shift,
                                     bool imitate_above) {
  const std::array<std::array<int, 3>, 4> candidates =
      canonLeaderCandidates(pitch_ceiling, mode, ground);
  std::array<int, 4> chosen{};
  std::array<int, 6> best{};
  bool have_best = false;
  std::array<std::size_t, 4> pick{};
  for (pick[0] = 0; pick[0] < 3; ++pick[0]) {
    for (pick[1] = 0; pick[1] < 3; ++pick[1]) {
      for (pick[2] = 0; pick[2] < 3; ++pick[2]) {
        for (pick[3] = 0; pick[3] < 3; ++pick[3]) {
          std::array<int, 4> assignment{};
          for (std::size_t bar = 0; bar < 4; ++bar)
            assignment[bar] = candidates[bar][pick[bar]];
          const CanonLines lines = layOutCanon(assignment, /*block_start_bar=*/0,
                                               source_register_shift, comes_shift, mode);
          const std::array<int, 4> faults = goldbergBlockFaults(
              imitate_above ? lines.comes : lines.dux, imitate_above ? lines.dux : lines.comes,
              /*block_start_bar=*/0, ground, mode);
          // The leader's own bar-to-bar steps, which the comes inherits exactly:
          // a tritone or a seventh between adjacent bars is unsingable however
          // well it behaves against the other voices, so it is weighed above the
          // faults that only the combination produces, and total travel breaks
          // ties last so the contour walks rather than leaps.
          int unsingable = 0;
          int travel = 0;
          for (std::size_t bar = 1; bar < 4; ++bar) {
            const int step = std::abs(assignment[bar] - assignment[bar - 1]);
            if (step == interval::kTritone || step >= interval::kMinor7th)
              ++unsingable;
            travel += step;
          }
          const std::array<int, 6> score = {faults[0], faults[1], unsingable,
                                            faults[2], faults[3], travel};
          if (!have_best || score < best) {
            best = score;
            chosen = assignment;
            have_best = true;
          }
        }
      }
    }
  }
  return chosen;
}

// Build one canonic variation block (a 4-bar window).
//
// Unison through fourth canons imitate below: physical V0 is the dux and V1 the
// comes. Fifth and wider canons imitate above: physical V1 is the lower dux and
// V0 the comes. This preserves the validator's V0 > V1 register convention
// while making the wide canon's musical direction genuinely upward.
void buildCanonBlock(PassacagliaVariation& principal, std::vector<MaterialNote>& inner_notes,
                     int block_start_bar, int imitation_degrees, Mode mode,
                     const std::array<std::uint8_t, 4>& ground) {
  const int imitation_semitones =
      transposeUp(kCanonLeaderBase, imitation_degrees, mode) - kCanonLeaderBase;
  const bool imitate_above = imitation_degrees >= 4;
  const int source_register_shift = imitate_above ? -12 : 12;
  const int comes_shift = imitate_above ? imitation_semitones + 12 : imitation_semitones - 24;
  const int design_ceiling = imitation_degrees >= 8 ? 70 : 72;
  const std::array<int, 4> designed = designCanonLeader(
      design_ceiling, mode, ground, source_register_shift, comes_shift, imitate_above);
  const CanonLines lines =
      layOutCanon(designed, block_start_bar, source_register_shift, comes_shift, mode);

  if (imitate_above) {
    principal.notes.insert(principal.notes.end(), lines.comes.begin(), lines.comes.end());
    inner_notes.insert(inner_notes.end(), lines.dux.begin(), lines.dux.end());
  } else {
    principal.notes.insert(principal.notes.end(), lines.dux.begin(), lines.dux.end());
    inner_notes.insert(inner_notes.end(), lines.comes.begin(), lines.comes.end());
  }
}

}  // namespace

HarnessFixture buildGoldbergVariationsForm(const ResolvedRequest& req) {
  HarnessFixture out;
  constexpr int kCycleBars = 4;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const int offset = static_cast<int>(req.seed % 4u);
  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Block layout: block 0 (bars 0-3) is the aria; blocks 1..K are 4-bar
  // figuration variations; when N >= 24 the final block restates the aria
  // (da capo). K is derived from the bar count so the variation COUNT is
  // length-driven.
  const int num_blocks = bars / kCycleBars;  // bars is snapped to a multiple of 4.
  const bool da_capo = bars >= 24;
  const int da_capo_block = da_capo ? num_blocks - 1 : -1;

  // Dedicated compressed aria-bass phrase: 32 structural tones across four
  // bars (eight eighth-note positions per bar). Each bar articulates its root,
  // third, and fifth while returning to the root on both structural accents.
  // The complete phrase is repeated unchanged through every variation and da
  // capo; it is not represented as Passacaglia material.
  const std::size_t ground_variant = detail::groundVariantIndex(req.seed);
  const auto& ground = (mode == Mode::Major) ? detail::kGoldbergGroundsMajor[ground_variant]
                                             : detail::kGoldbergGroundsMinor[ground_variant];
  for (int bar = 0; bar < kCycleBars; ++bar) {
    const std::array<int, 8> phrase =
        goldbergAriaBassBar(ground[static_cast<std::size_t>(bar)], mode);
    for (std::size_t pos = 0; pos < phrase.size(); ++pos) {
      out.material.goldberg_aria_bass.push_back(materialNote(
          barTick(bar) + static_cast<Tick>(pos) * kAriaBassUnit, kAriaBassUnit, phrase[pos]));
    }
  }
  out.material.goldberg_aria_bass_period = static_cast<Tick>(kCycleBars) * kTicksPerBar;

  // Canon follower line (V1). Populated only for canonic variation blocks; the
  // follower notes for every canon block are appended here in time order (one
  // block at a time), then handed to a single TrioVoiceLine on V1. V1 is silent
  // outside canon blocks (no notes), keeping the texture clean and the validator
  // quiet for the figuration / aria blocks. The follower carries the
  // TrioVoiceIndependent bit, but because it is the ONLY voice carrying that bit
  // the voice_independence_threshold rule stays inert (it needs >= 2 such
  // voices), so no soft-fail is introduced.
  std::vector<MaterialNote> inner_voice;
  std::vector<int> inner_blocks;

  // Per-bar harmony (ground cycle, tiled). Drives the variation downbeat anchor.
  // The final bar is an explicit tonic arrival rather than one more aria-bass
  // repetition: the latter may end on V or vi depending on the selected ground
  // variant. Minor-mode cadences retain the deterministic Picardy policy.
  const BarChord final_cadence_chord = {0, mode == Mode::Minor && !detail::usePicardy(req.seed)};
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (int bar = 0; bar < bars; ++bar) {
    const BarChord chord =
        bar == bars - 1
            ? final_cadence_chord
            : goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
    ChordEvent ce;
    ce.start_tick = barTick(bar);
    ce.root_pc = chord.root_pc;
    ce.quality = chord.minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(ce);
  }

  // The climax block is the arc climax cycle (~80% of the span), NOT necessarily
  // the last block. The aria (block 0) and any da-capo block are never the
  // climax.
  const int cycle_count = static_cast<int>(req.cycle_count);
  int climax_block = -1;
  for (int blk = 0; blk < num_blocks; ++blk) {
    if (blk == 0 || blk == da_capo_block)
      continue;
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    if (req.arc(static_cast<std::size_t>(cycle)).is_climax) {
      climax_block = blk;
      break;
    }
  }
  // If the arc placed its climax on a block we excluded (aria / da-capo), fall
  // back to the last figuration variation block so a climax is always present.
  if (climax_block < 0) {
    for (int blk = num_blocks - 1; blk >= 1; --blk) {
      if (blk != da_capo_block) {
        climax_block = blk;
        break;
      }
    }
  }

  // Variation register anchor: ~C5 region (72), well above the C2 ground.
  constexpr int kVarRegisterBase = 72;

  for (int blk = 0; blk < num_blocks; ++blk) {
    PassacagliaVariation var;
    var.voice = 0;
    var.start_tick = barTick(blk * kCycleBars);
    var.end_tick = var.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var.is_climax = (blk == climax_block);

    const bool is_aria = (blk == 0) || (blk == da_capo_block);
    if (is_aria) {
      var.density_level = 0;
      for (int local = 0; local < kCycleBars; ++local) {
        const int bar = blk * kCycleBars + local;
        appendAriaBar(var, bar,
                      goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
                      mode, kVarRegisterBase, offset);
      }
      if (blk == num_blocks - 1) {
        // Keep the aria's m=2 rhythm at the da-capo, but turn its final bar
        // into a dominant-to-tonic upper-voice arrival over the coda bass.
        // These two notes are both consonant with the final tonic harmony.
        var.notes[var.notes.size() - 2].pitch = kVarRegisterBase + 7;
        var.notes.back().pitch = kVarRegisterBase;
      }
      out.material.goldberg_variations.push_back(var);
      continue;
    }

    // Variation block. The variation index is the post-aria ordinal (the aria is
    // block 0; the first variation is block 1 => variation_index 0). The kind
    // dispatch routes Figuration vs Canon (BWV988: every third variation is a
    // canon at a rising imitation interval).
    const std::size_t variation_index = static_cast<std::size_t>(blk - 1);
    const std::size_t variation_number = variation_index + 1;  // 1-based (1..K).
    const int cycle = cycle_count > 0 ? (blk * cycle_count) / num_blocks : 0;
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));

    switch (goldbergVariationKind(variation_index)) {
      case GoldbergVariationKind::Canon: {
        // Canon number c = variation_number / 3 (1..9 across the full set);
        // imitation interval = (c - 1) diatonic degrees above unison (unison,
        // 2nd, 3rd, ... 9th), following the BWV988 scheme. The leader runs in
        // eighths (canons cap at eighths so the two clear lines stay legible),
        // so density_level is the eighth tier regardless of the arc.
        const int canon_number = static_cast<int>(variation_number / 3);
        const int imitation_degrees = canon_number - 1;  // 0 = unison canon.
        var.density_level = 1;
        buildCanonBlock(var, inner_voice, blk * kCycleBars, imitation_degrees, mode, ground);
        inner_blocks.push_back(blk);
        break;
      }
      case GoldbergVariationKind::Quodlibet: {
        // Variation 30 combines the dense principal figuration with a second,
        // independently recurring chord-tone tune in the middle register.
        var.density_level = 2;
        for (int local = 0; local < kCycleBars; ++local) {
          const int bar = blk * kCycleBars + local;
          appendVariationBar(
              var, bar, goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode),
              mode, 4, kVarRegisterBase, offset);
        }
        // The tune states each bar's triad in one rotation held across the whole
        // block, so what recurs is its shape. Which rotation that is has to be
        // read off the texture rather than fixed: the bass arpeggiates the same
        // triad underneath at its own pace, so a rotation that happens to reach
        // the same chord tones in step with it doubles the bass instead of
        // answering it. Every rotation is laid out against the figuration
        // already settled above and the bass below, and the cleanest is kept.
        std::vector<MaterialNote> tune;
        std::array<int, 4> best_score{};
        for (int rotation = 0; rotation < 4; ++rotation) {
          std::vector<MaterialNote> candidate;
          candidate.reserve(16);
          for (int local = 0; local < kCycleBars; ++local) {
            const int bar = blk * kCycleBars + local;
            const BarChord chord =
                goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
            const int root_pc = chord.root_pc;
            const int third_pc = (root_pc + (chord.minor ? 3 : 4)) % 12;
            const int fifth_pc = (root_pc + 7) % 12;
            const std::array<int, 4> theme_pcs = {third_pc, fifth_pc, root_pc, fifth_pc};
            for (std::size_t beat = 0; beat < theme_pcs.size(); ++beat) {
              const int pc =
                  theme_pcs[(beat + static_cast<std::size_t>(rotation)) % theme_pcs.size()];
              int pitch = 60 + ((pc - 60) % 12 + 12) % 12;
              while (pitch > 67)
                pitch -= 12;
              candidate.push_back(materialNote(
                  barTick(bar) + static_cast<Tick>(beat) * kTicksPerBeat, kTicksPerBeat, pitch));
            }
          }
          const std::array<int, 4> score =
              goldbergBlockFaults(var.notes, candidate, blk * kCycleBars, ground, mode);
          if (rotation == 0 || score < best_score) {
            best_score = score;
            tune = std::move(candidate);
          }
        }
        inner_voice.insert(inner_voice.end(), tune.begin(), tune.end());
        inner_blocks.push_back(blk);
        break;
      }
      case GoldbergVariationKind::Figuration:
      default: {
        int notes_per_beat = notesPerBeatFor(point, profile.density_bias);
        const bool design_peak = var.is_climax || variation_number == 30;
        if (var.is_climax)
          notes_per_beat = 4;  // the arc climax block is the densest by design.
        // The final variation of the full set (variation 30) is the design
        // secondary peak (the BWV988 Quodlibet slot): the densest figuration
        // tier directly, no search.
        if (variation_number == 30)
          notes_per_beat = 4;
        if (profile.prefer_dotted && notes_per_beat < 4)
          notes_per_beat = 2;  // Noble keeps a moderate, dignified subdivision.
        var.density_level = notes_per_beat == 1 ? 0 : (notes_per_beat == 4 ? 2 : 1);
        const int register_base = kVarRegisterBase + static_cast<int>(point.register_shift);
        // Pattern selection: the climax block and variation 30 are design
        // values (the densest anchored scalar wave); other figuration blocks
        // rotate the goldberg palette so consecutive variations alternate
        // idioms (the BWV988 figuration-type rotation).
        const PatternKind pattern = design_peak
                                        ? PatternKind::kScalarWave
                                        : kGoldbergPalette[(req.seed + variation_index) % 2];
        for (int local = 0; local < kCycleBars; ++local) {
          const int bar = blk * kCycleBars + local;
          const BarChord chord =
              goldbergBarChord(ground[static_cast<std::size_t>(bar % kCycleBars)], mode);
          switch (pattern) {
            case PatternKind::kFiguraCorta:
              appendFiguraCortaBar(var.notes, bar,
                                   goldbergFigurationStart(register_base, offset, chord, mode),
                                   detail::ChordSpec{chord.root_pc, chord.minor}, mode);
              break;
            case PatternKind::kScalarWave:
            default:
              appendVariationBar(var, bar, chord, mode, notes_per_beat, register_base, offset);
              break;
          }
        }
        break;
      }
    }
    // Cadential landing on the piece's final variation block (the da-capo aria
    // already closes in plain half notes, so it keeps its own layout). The
    // tiled ground does not cadence -- its final bar may sit on V or vi -- so
    // both landing tones are chosen for consonance: the final tone is the
    // tonic when the final bar's chord contains it, else the chord root; the
    // pre-final tone is its diatonic upper step when the penultimate chord
    // supports it, else the step below, else an anticipation.
    if (blk == num_blocks - 1 && !is_aria && !var.notes.empty()) {
      const BarChord final_chord = final_cadence_chord;
      const BarChord penult_chord =
          goldbergBarChord(ground[static_cast<std::size_t>((bars - 2) % kCycleBars)], mode);
      auto consonant_with = [&](int pitch, const BarChord& chord) {
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        for (int tone : triad_pc) {
          if (!isConsonantIc(pitch - tone))
            return false;
        }
        return true;
      };
      int final_tone = kVarRegisterBase;  // C5 tonic by preference.
      if (!consonant_with(final_tone, final_chord)) {
        // Fall back to the final chord's root in the variation register.
        final_tone = kVarRegisterBase + ((final_chord.root_pc - kVarRegisterBase) % 12 + 12) % 12;
        if (final_tone > kVarRegisterBase + 6)
          final_tone -= 12;
      }
      int prefinal = final_tone;  // anticipation fallback.
      const int step_above = detail::scaleUp(final_tone, 1, mode);
      if (consonant_with(step_above, penult_chord)) {
        prefinal = step_above;
      } else if (consonant_with(final_tone - 1, penult_chord)) {
        prefinal = final_tone - 1;
      }
      appendCadentialLanding(var.notes, barTick(bars - 2), kTicksPerBar, prefinal, final_tone, mode,
                             /*band_lo=*/62, /*downbeat_chord=*/nullptr,
                             /*prefer_descending=*/false, /*lift_to_context=*/true);
    }

    out.material.goldberg_variations.push_back(var);
  }

  out.material.goldberg_inner_voice = std::move(inner_voice);

  // VoicePlan (3 voices, strictly ordered by register so voice_crossing never
  // fires): V0 = principal variation, V1 = canon/quodlibet inner line, and V2
  // = the immutable aria-bass phrase. All use dedicated Goldberg carriers.
  out.voice_plan.num_voices = 3;
  SpanId next_span_id = 0;

  Span ground_span;
  ground_span.id = next_span_id++;
  ground_span.start_tick = 0;
  const Tick final_bar_tick = barTick(bars - 1);
  const Tick final_approach_tick = final_bar_tick - kTicksPerBeat;
  ground_span.end_tick = final_approach_tick;
  ground_span.voice = 2;
  ground_span.intent = VoiceIntent::GoldbergBassCarrier;
  ground_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(ground_span);

  // The immutable four-bar aria bass deliberately stops before the final bar.
  // Its explicit tonic coda resolves the otherwise open V/vi terminal ground.
  CodaDecl coda;
  coda.voice = 2;
  coda.notes.push_back(materialNote(final_approach_tick, kTicksPerBeat, 43));
  coda.notes.push_back(materialNote(final_bar_tick, kTicksPerBar, ground.front()));
  out.material.coda_extensions.push_back(std::move(coda));

  // The variation line is the only voice here that may move at all. The aria
  // bass is immutable by contract -- goldberg_aria_bass_immutable demands every
  // one of its onsets verbatim -- and it is also the voice the variation runs
  // into: the bar chord's root IS the ground tone's pitch class, the variation
  // is anchored three octaves above the same class, and both derive their bar
  // tone from that one root, so they arrive congruently by construction rather
  // than by accident. Nothing in the variation loop can see this, because the
  // Goldberg builder keeps no registry of the voices it has already placed.
  // The congruence is not confined to the bar head either: the aria bass
  // arpeggiates that same root through the bar while the variation figures the
  // same triad above it, so the two lines meet on a perfect interval off the
  // downbeat as well. Relieved here, where the whole texture is finally known,
  // on the same terms as the chorale prelude: the tone that moves is the onset
  // before each arrival.
  {
    ThemeToneRegistry relief_registry;
    const Tick ground_period = out.material.goldberg_aria_bass_period;
    for (Tick offset_tick = 0; offset_tick < final_approach_tick; offset_tick += ground_period) {
      for (const MaterialNote& note : out.material.goldberg_aria_bass) {
        const Tick start = offset_tick + note.start_tick;
        if (start < final_approach_tick)
          relief_registry.record(start, /*voice=*/2, static_cast<int>(note.pitch), note.duration);
      }
    }
    for (const MaterialNote& note : out.material.coda_extensions.back().notes)
      relief_registry.record(note.start_tick, /*voice=*/2, static_cast<int>(note.pitch),
                             note.duration);
    for (const MaterialNote& note : out.material.goldberg_inner_voice)
      relief_registry.record(note.start_tick, /*voice=*/1, static_cast<int>(note.pitch),
                             note.duration);
    // Canon and quodlibet blocks are excluded. Their variation line is the
    // canon LEADER, and the follower on V1 is that leader plus one fixed
    // imitation interval; moving a leader tone without moving its comes breaks
    // the imitation the block exists to state. The blocks that remain carry
    // free figuration, where a tone answers to nothing but its own line.
    std::vector<bool> imitative(out.material.goldberg_variations.size(), false);
    for (int blk : inner_blocks) {
      if (blk >= 0 && static_cast<std::size_t>(blk) < imitative.size())
        imitative[static_cast<std::size_t>(blk)] = true;
    }
    std::vector<std::vector<MaterialNote>*> blocks;
    blocks.reserve(out.material.goldberg_variations.size());
    for (std::size_t idx = 0; idx < out.material.goldberg_variations.size(); ++idx) {
      if (!imitative[idx])
        blocks.push_back(&out.material.goldberg_variations[idx].notes);
    }
    relieveArrivals(lineInTickOrder(blocks), relief_registry, /*voice=*/0,
                    /*num_voices=*/3, bars, mode);
  }

  Span coda_span;
  coda_span.id = next_span_id++;
  coda_span.start_tick = final_approach_tick;
  coda_span.end_tick = barTick(bars);
  coda_span.voice = 2;
  coda_span.intent = VoiceIntent::CodaCarrier;
  coda_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(coda_span);

  ChordEvent approach;
  approach.start_tick = final_approach_tick;
  approach.root_pc = 7;
  approach.quality = ChordQuality::Dominant7;
  approach.degree = RomanNumeral::V;
  approach.function = HarmonicFunction::D;
  approach.has_degree = true;
  out.harmony.chords.insert(out.harmony.chords.end() - 1, approach);
  out.harmony.cadences.push_back({final_bar_tick, CadenceType::ImperfectAuthentic});

  // V0 principal line: one dedicated variation span per block.
  for (int blk = 0; blk < num_blocks; ++blk) {
    Span var_span;
    var_span.id = next_span_id++;
    var_span.start_tick = barTick(blk * kCycleBars);
    var_span.end_tick = var_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    var_span.voice = 0;
    var_span.intent = VoiceIntent::GoldbergVariationCarrier;
    var_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(var_span);
  }

  // V1 inner line: canon followers plus the variation-30 Quodlibet tune.
  for (int blk : inner_blocks) {
    Span follower_span;
    follower_span.id = next_span_id++;
    follower_span.start_tick = barTick(blk * kCycleBars);
    follower_span.end_tick =
        follower_span.start_tick + static_cast<Tick>(kCycleBars) * kTicksPerBar;
    follower_span.voice = 1;
    follower_span.intent = VoiceIntent::GoldbergInnerVoiceCarrier;
    follower_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(follower_span);
  }

  return out;
}

}  // namespace bach::composer
