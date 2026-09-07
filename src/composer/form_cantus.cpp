#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <vector>

#include "composer/arc.h"
#include "composer/arrival_relief.h"
#include "composer/bar_material.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// The chorale prelude: a cantus firmus with figuration above it.
//
// Honours ResolvedRequest length / mode / character / arc. It reuses the proven
// ChoralePrelude (chorale) note language -- a CF whose bar downbeats are an
// immutable skeleton plus a predominantly-stepwise scalar wave above it -- but
// generalises the layout to any snapped bar count, derives the CF tune from
// (seed, indices) only, and selects the diatonic scale from the mode.
// ---------------------------------------------------------------------------

namespace {

using detail::Mode;

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
//
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

// The weak-beat tone that connects two anchors a quarter apart.
//
// Every anchor of the walking bass is a tone of its bar chord, so the line
// moves anchor to anchor by the intervals a triad offers -- thirds, fourths and
// fifths -- and almost never by a step. Beats two and four of 4/4 are
// metrically weak, which frees them to leave the chord: a tone approached AND
// left by step there is a passing or neighbour tone, the ordinary way a
// continuo bass fills the thirds of its own harmony.
//
// Returns the connecting tone, or -1 when the pair admits none: a gap wider
// than a third has no single-step fill, and a candidate outside the bass band
// would take the line out of its register.
int connectingBassTone(int from, int to, Mode mode) {
  const int gap = to - from;
  const int span = std::abs(gap);
  int candidate = -1;
  if (span == 3 || span == 4) {
    // A third: the scale degree between the two anchors passes through.
    candidate = gap > 0 ? detail::scaleUp(from, 1, mode) : detail::scaleDown(from, 1, mode);
  } else if (span <= 2) {
    // A step or a repeat: a neighbour on the far side keeps the beat moving,
    // and it is still left by step. The lower neighbour is preferred so the
    // fill leans away from the voices above rather than toward them.
    for (const int direction : {-1, 1}) {
      const int neighbour =
          direction < 0 ? detail::scaleDown(from, 1, mode) : detail::scaleUp(from, 1, mode);
      if (neighbour != to && std::abs(neighbour - from) <= 2 && std::abs(to - neighbour) <= 2) {
        candidate = neighbour;
        break;
      }
    }
  }
  if (candidate < kBassBandLo || candidate > kBassBandHi)
    return -1;
  if (std::abs(candidate - from) > 2 || std::abs(to - candidate) > 2)
    return -1;
  return candidate;
}

// True when the walking bass may take `bass` under everything sounding with it.
//
// Two conditions, and the second is not the one an upper voice would apply. The
// bass is the texture's floor, so a tone that reaches or passes a sounding line
// is a crossing whatever interval it forms. And a perfect fourth is a consonance
// BETWEEN upper voices but a dissonance against the lowest sounding line, so a
// candidate that would put one under a theme tone has to be read with the
// bass-relative table rather than the pairwise one.
bool bassCarriesUnder(int bass, const std::vector<int>& sounding) {
  for (int upper : sounding) {
    if (bass >= upper || !rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(upper),
                                                             static_cast<std::uint8_t>(bass))) {
      return false;
    }
  }
  return true;
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
      // reference corpus almost never repeats a pitch, so even a pair reads
      // as a stalled line): first to a diatonic step neighbour that
      // is consonant against every concurrently sounding upper voice (keeping
      // the walking surface stepwise), else to the chord tone above (an
      // arpeggiated walk, consonant by construction). Downbeats are exempt
      // (the root statement is the bar's harmonic anchor and must not be
      // displaced); the off-beat fills carry the variety.
      if (pitch == prev_pitch && beat != 0) {
        int alt = -1;
        for (int cand : {detail::scaleUp(pitch, 1, mode), detail::scaleDown(pitch, 1, mode)}) {
          if (cand >= kBassBandLo && cand <= kBassBandHi && bassCarriesUnder(cand, theme_pitches)) {
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
            return bassCarriesUnder(cand, theme_pitches) && !bass_is_parallel(cand);
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

      // Forward guard on the approach beat: the next bar's downbeat states the
      // chord root without substitution (the harmonic anchor) and the bass band
      // spans a single octave, so that root's register is determined too --
      // the only freedom in the (beat 3 -> next root) motion is the beat-3 tone
      // itself. The upper voices are final at this point, so the fault formed
      // against their motion into the bar head is already knowable; re-aim the
      // approach tone when it would lock one in. The root lands relative to the
      // approach tone (band fit follows the cursor), so the candidate's own
      // landing root is recomputed per try.
      if (beat == 3 && bar + 1 < bars && prev_pitch >= 0) {
        const Tick next_bar_tick = barTick(bar + 1);
        std::vector<ConcurrentMotion> fwd_motions;
        registry.concurrentMotions(next_bar_tick - kSixteenth, next_bar_tick, /*voice=*/2,
                                   /*num_voices=*/3, fwd_motions);
        // The set of fault classes a candidate forms across BOTH ends: the
        // motion into the beat itself and the motion from it into the bar head.
        // A set rather than the tree's severity rank, because the classes are
        // counted separately in this form's shipped surface and a displacement
        // that swapped one for another would trade a column, not lower one.
        auto faultClasses = [&](int cand) {
          const int landing = fitPcToBand(next_chord.root_pc, cand, kBassBandLo, kBassBandHi);
          unsigned classes = 0;
          for (const auto& [from, to, lines] :
               {std::tuple{prev_pitch, cand, &motions}, std::tuple{cand, landing, &fwd_motions}}) {
            for (const ConcurrentMotion& motion : *lines) {
              if (formsStrictPerfectParallel(from, to, motion.prev, motion.curr))
                classes |= 8u;
              else if (formsPerfectParallel(from, to, motion.prev, motion.curr))
                classes |= 4u;
              else if (formsAntiParallelPerfect(from, to, motion.prev, motion.curr))
                classes |= 2u;
              else if (formsBattuta(from, to, motion.prev, motion.curr))
                classes |= 1u;
            }
          }
          return classes;
        };
        // The reach is a sixth. The bass band is a single octave, so a tone
        // near either edge has clean answers only at the far edge, out of reach
        // of a fifth; a sixth is the widest leap a walking bass takes
        // idiomatically, and the ascending distance order means it is only ever
        // used once every smaller displacement has been refused.
        const unsigned standing = faultClasses(pitch);
        if (standing != 0) {
          for (int dist = 1; dist <= 7; ++dist) {
            bool placed = false;
            for (const int sgn : {-1, 1}) {
              const int cand = pitch + sgn * dist;
              if (cand < kBassBandLo || cand > kBassBandHi || !detail::inScale(cand, mode) ||
                  !bassCarriesUnder(cand, theme_pitches)) {
                continue;
              }
              // Strictly fewer faults, and never a class the standing tone did
              // not already form: the displacement can only empty columns, so
              // no fault this guard removes reappears as another.
              const unsigned candidate = faultClasses(cand);
              if (candidate != standing && (candidate & ~standing) == 0u) {
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

      out_notes.push_back(materialNote(beat_tick, kQuarterDur, pitch));
      registry.record(beat_tick, /*voice=*/2, pitch, kQuarterDur);
      line_prev = pitch;
      prev_pitch = pitch;
      cursor = pitch;
    }
  }

  // Fill the weak beats. The anchors above are chord tones by construction, so
  // the line as it stands walks by thirds and fifths; beats two and four are
  // metrically weak and may leave the chord, which is what turns those thirds
  // into steps. Run as a second pass because the fill needs the anchor on BOTH
  // sides of the beat -- a passing tone that is not left by step is an
  // unprepared dissonance, not a fill -- and the anchor after it is not chosen
  // until the beat itself has been placed.
  //
  // A substitution that would lock in a perfect parallel against either
  // neighbouring arrival is refused and the chord tone stands: the fill is a
  // melodic preference and the parallel is the cardinal prohibition.
  const Tick weak_beats[2] = {kTicksPerBeat, 3 * kTicksPerBeat};
  std::vector<ConcurrentMotion> into_beat;
  std::vector<ConcurrentMotion> out_of_beat;
  for (std::size_t idx = 1; idx + 1 < out_notes.size(); ++idx) {
    const Tick position = out_notes[idx].start_tick % kTicksPerBar;
    if (position != weak_beats[0] && position != weak_beats[1])
      continue;
    const int before = static_cast<int>(out_notes[idx - 1].pitch);
    const int standing = static_cast<int>(out_notes[idx].pitch);
    const int after = static_cast<int>(out_notes[idx + 1].pitch);
    const int fill = connectingBassTone(before, after, mode);
    if (fill < 0 || fill == standing)
      continue;
    const Tick beat_tick = out_notes[idx].start_tick;
    const Tick next_tick = out_notes[idx + 1].start_tick;
    registry.concurrentMotions(beat_tick - kSixteenth, beat_tick, /*voice=*/2, /*num_voices=*/3,
                               into_beat);
    registry.concurrentMotions(next_tick - kSixteenth, next_tick, /*voice=*/2, /*num_voices=*/3,
                               out_of_beat);
    if (perfectFaultRank(before, fill, into_beat) > perfectFaultRank(before, standing, into_beat))
      continue;
    if (perfectFaultRank(fill, after, out_of_beat) > perfectFaultRank(standing, after, out_of_beat))
      continue;
    out_notes[idx].pitch = static_cast<std::uint8_t>(fill);
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

}  // namespace bach::composer
