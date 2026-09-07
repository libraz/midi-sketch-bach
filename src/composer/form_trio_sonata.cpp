#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/cadenced_progression.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

namespace bach::composer {

using detail::ChordSpec;
using detail::Mode;

// ---------------------------------------------------------------------------
// TrioSonata: three independent voices (V0 = RH/Great high, V1 = LH/Swell mid,
// V2 = Pedal low). V0/V1 are scalar-wave lines (one 16th line, one 8th line),
// V2 is a quarter-note root/fifth pedal. The defining technique is rhythmic
// independence: the three voices keep DISTINCT densities (16 / 8 / 4 notes per
// bar) so voice_independence_threshold passes comfortably.
//
// Progression: one chord per bar from buildCadencedProgression with an internal
// half cadence (V) at every 8-bar boundary's penultimate bar resolving to I on
// the boundary, and the design-valued final V -> I close. Voice-pair rotation:
// which upper voice carries the densest (16th) line rotates by 4-bar cycle (V0
// then V1 then V0 ...); registers stay banded (V0 high, V1 mid, V2 low) so
// swapping the density never crosses voices. The arc lifts the upper voices'
// register toward the climax. Noble character (prefer_dotted) gives V1 a
// dotted-quarter+eighth pattern at low tiers.
// ---------------------------------------------------------------------------
HarnessFixture buildTrioSonataForm(const ResolvedRequest& req) {
  HarnessFixture out;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const Tick kEighth = kTicksPerBeat / 2;     // eighth note.
  const Tick kSixteenth = kTicksPerBeat / 4;  // sixteenth note.

  std::vector<ChordSpec> chords = buildCadencedProgression(bars, req.seed, mode);
  // Internal cadences every 8 bars: penultimate bar of each 8-bar group is V,
  // the boundary bar is I (the tonic landing). This shapes the long form into
  // clear 8-bar periods. The final two bars already hold the design cadence.
  for (int boundary = 8; boundary < bars; boundary += 8) {
    chords[static_cast<std::size_t>(boundary - 1)] = {7, false};  // V before the boundary.
    chords[static_cast<std::size_t>(boundary)] = {0, mode == Mode::Minor};  // I/i on the boundary.
  }
  // Every internal cadence just pinned, and the design-valued close, is a
  // fifth-fall, so the derivation reaches each of them. Nothing in this form is
  // verbatim thematic material -- all three voices are written against the plan
  // bar by bar -- so the spelling has no line it can contradict.
  markDominantSevenths(chords, /*triad_only_bars=*/{}, mode, /*cyclic=*/false);
  writeBarChords(out, chords, mode);

  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  // Append one bar of `notes_per_beat` scalar notes (or a dotted figure) to
  // `dst`, riding above `base_midi`. EVERY BEAT ONSET lands on a chord tone: the
  // beat anchor walks up the triad (root, third, fifth, root) and each beat runs
  // a short scalar neighbour figure from that anchor (the off-beat subdivisions
  // are passing tones). Anchoring every beat to a chord tone keeps the per-beat
  // vertical sample consonant against the pedal and the other manual voice (the
  // beat-sampled vertical_dissonance_ratio stays low); the inner subdivisions
  // are stepwise neighbours, so the line is predominantly stepwise (no leaps).
  // `harmonic` selects the harmonic-minor ascending tetrachord on dominant bars
  // so the leading tone is reached without an augmented second; in major it is
  // ignored.
  // `shape` rotates the intra-beat figure VOCABULARY per bar (the beat anchors
  // themselves never change, so every contract -- per-beat chord-tone
  // consonance, conjunct off-beats, banded registers -- holds for every shape):
  //   0 = rising neighbour arc (anchor, +1, +2, +1),
  //   1 = falling neighbour arc (anchor, -1, -2, -1) when the anchor sits high
  //       enough above the band floor, else the rising arc (a fixed oscillation
  //       cell here proved fatal: bar-long anchor,+1,anchor,+1 trills dominate
  //       the interval-bigram surface with a single repeated pair),
  //   2 = figura corta cell (eighth anchor + two stepping sixteenths; only at
  //       the sixteenth tier, lower tiers fall back to the arc).
  // `zig_dir` sets the zigzag parity of the beat anchors: +1 moves to the
  // chord tone ABOVE on odd beats (and below on even beats), -1 the reverse.
  // Giving the two manual voices OPPOSITE parities makes their strong-beat
  // motion contrary by construction, which keeps simultaneous perfect
  // intervals from chaining into parallel fifths/octaves.
  auto appendScalarBar = [&](std::vector<MaterialNote>& dst, int& prev_anchor, int band_lo,
                             int band_hi, int bar, int notes_per_beat, bool dotted, int shape,
                             int zig_dir, const ThemeToneRegistry* guard_registry,
                             int& prev_emitted) {
    const ChordSpec& bc = chords[static_cast<std::size_t>(bar)];
    const int root_pc = bc.root_pc % 12;
    const int third = bc.minor ? 3 : 4;
    // Triad first, then the seventh when the bar's chord declares one. The two
    // counts below split that set by where a tone may land. The seventh is a
    // chord tone the harmony owns on an accented beat, but on a weak beat the
    // question is a different one -- there a tone outside the triad has to be
    // approached and left by step, and the anchor chain moves by broken-chord
    // leaps, so offering it there would write a dissonance with no stepwise
    // handling. Keeping it to the accented beats also means it is the tone the
    // bar carries into the next one, where the nearest chord tone of the
    // resolution is its own third: the seventh falls by step across the barline.
    const int chord_pc[4] = {root_pc, (root_pc + third) % 12, (root_pc + 7) % 12,
                             detail::chordSeventhPc(bc)};
    const int accent_tones = bc.seventh ? 4 : 3;
    constexpr int kTriadTones = 3;
    const bool harmonic = (mode == Mode::Minor) && (root_pc == 7);  // V wants the leading tone.

    // Audible-grain parallel guard against the already-built other manual
    // voice. The opposite zigzag parities make the BEAT anchors contrary by
    // construction, but a band-edge bounce can re-align them, and the
    // intra-beat cell tones were never covered: with the two voices' anchor
    // centres an octave apart (C5 / C4), two same-direction cells chain
    // parallel octaves at every shared sub-beat onset (the dense-character
    // sweeps surfaced up to 19 per piece). Each candidate tone is re-judged
    // against the guard voice's registered sounding pitches at this voice's
    // last emitted onset and now -- the exact pair the union-onset detector
    // (and the ear) samples. The guard is inactive for the voice built first
    // in the bar, which has nothing to be judged against yet.
    auto guard_sounding = [&](Tick tick) -> int {
      return guard_registry->soundingPitchInVoice(0, tick);
    };
    // The union-onset pair at our onset `tick` is (guard just before tick ->
    // guard at tick): when the guard does not onset at `tick` the two samples
    // are equal, its motion is zero, and oblique motion is always allowed.
    // `line_prev` is explicit rather than always `prev_emitted` so a figure can
    // be judged as a whole before any of it is emitted: the second tone of a
    // two-tone cell follows the first, not the last note already shipped.
    // How badly a candidate collides with the guard voice, not merely whether it
    // does. The faults are ranked because they are not worth the same: a
    // parallel fifth or octave is the cardinal prohibition, while an ottava
    // battuta is a blemish Bach himself commits regularly. A guard that treated
    // them alike would step off a battuta onto a parallel whenever the clean
    // tones ran out, which is the trade backwards. Every substitution below
    // therefore has to LOWER this rank, never merely change it.
    //
    // The anti-parallel level is what makes the escapes safe rather than
    // cosmetic. Every escape below reaches its alternative by reversing the cell
    // tone's direction, and reversing direction turns similar motion into
    // contrary motion without changing where the tone lands -- so a guard that
    // ranked only same-direction faults would hand back the same perfect
    // interval, arrived at the other way round, and count it clean.
    constexpr int kGuardClean = 0;
    constexpr int kGuardBattuta = 1;
    constexpr int kGuardAntiParallel = 2;
    constexpr int kGuardParallel = 3;
    auto guard_rank = [&](int line_prev, int cand, Tick tick) {
      if (guard_registry == nullptr || line_prev < 0)
        return kGuardClean;
      const int other_curr = guard_sounding(tick);
      const int other_prev = guard_sounding(tick - 1);
      if (other_curr < 0 || other_prev < 0)
        return kGuardClean;
      if (formsPerfectParallel(line_prev, cand, other_prev, other_curr))
        return kGuardParallel;
      if (formsAntiParallelPerfect(line_prev, cand, other_prev, other_curr))
        return kGuardAntiParallel;
      if (formsBattuta(line_prev, cand, other_prev, other_curr))
        return kGuardBattuta;
      return kGuardClean;
    };
    // The nearest in-band triad tone that ranks strictly better than the anchor,
    // preferring a fully clean one; the anchor itself when nothing improves on
    // it (a rare double bind -- one consonant parallel beats a non-chord strong
    // beat).
    auto guarded_anchor = [&](int anchor, Tick tick, int tone_count) {
      const int anchor_rank = guard_rank(prev_emitted, anchor, tick);
      for (int accept = kGuardClean; accept < anchor_rank; ++accept) {
        int best = -1;
        int best_dist = 1 << 20;
        for (int tone = 0; tone < tone_count; ++tone) {
          int low = band_lo + (((chord_pc[tone] - band_lo) % 12) + 12) % 12;
          for (int v = low; v <= band_hi; v += 12) {
            if (v == anchor || guard_rank(prev_emitted, v, tick) > accept)
              continue;
            const int dist = std::abs(v - anchor);
            if (dist < best_dist) {
              best_dist = dist;
              best = v;
            }
          }
        }
        if (best >= 0)
          return best;
      }
      return anchor;
    };
    // A candidate the line may swap onto only if it ranks strictly better than
    // what it would otherwise emit. Sharing one shape across the figure cells
    // keeps every substitution monotone in the same rank.
    auto improved_by = [&](int pitch, int alt, Tick tick) {
      if (alt < band_lo || alt > band_hi)
        return pitch;
      return guard_rank(prev_emitted, alt, tick) < guard_rank(prev_emitted, pitch, tick) ? alt
                                                                                         : pitch;
    };

    auto walk = [&](int midi, int steps) {
      if (steps == 0)
        return midi;
      if (steps < 0)
        return detail::scaleDown(midi, -steps, mode);
      return (mode == Mode::Minor) ? detail::minorScaleUp(midi, steps, harmonic)
                                   : detail::scaleUp(midi, steps, Mode::Major);
    };

    // Find the chord tone (of any triad pitch class) NEAREST `near`, realized in
    // the voice's register band [band_lo, band_hi]. Used to voice-lead each beat
    // anchor to the closest chord tone, so consecutive anchors move by at most a
    // third and the line never leaps. The band keeps the voice in its register so
    // the two upper voices never cross and both stay above the pedal.
    auto nearestChordTone = [&](int near, int tone_count) {
      int best = band_lo;
      int best_dist = 1 << 20;
      for (int tone = 0; tone < tone_count; ++tone) {
        int low = band_lo + (((chord_pc[tone] - band_lo) % 12) + 12) % 12;  // chord tone in band.
        for (int v = low; v <= band_hi; v += 12) {
          const int dist = std::abs(v - near);
          if (dist < best_dist) {
            best_dist = dist;
            best = v;
          }
        }
      }
      return best;
    };

    // Find the chord tone STRICTLY beyond `from` in direction `dir` (+1 above,
    // -1 below) inside the band; returns `from` when the band holds none. The
    // nearest-tone helper above cannot serve here: the chord tone nearest
    // (from + 1) is almost always `from` itself (triad tones sit >= 3 semitones
    // apart), so a "nudge" built on it never moves and the anchor chain stalls
    // into a repeated-note line.
    auto chordToneBeyond = [&](int from, int dir, int tone_count) {
      int best = from;
      int best_dist = 1 << 20;
      for (int tone = 0; tone < tone_count; ++tone) {
        int low = band_lo + (((chord_pc[tone] - band_lo) % 12) + 12) % 12;
        for (int v = low; v <= band_hi; v += 12) {
          const int delta = (v - from) * dir;
          if (delta > 0 && delta < best_dist) {
            best_dist = delta;
            best = v;
          }
        }
      }
      return best;
    };

    // Per-beat chord-tone anchors, voice-led ACROSS bars within the voice's band.
    // The bar's first beat lands on the chord tone NEAREST the previous bar's
    // closing anchor (`prev_anchor`); later beats ZIGZAG through the triad
    // (above, below, above), bouncing off the band edges. Every beat onset is a
    // genuine chord tone (so the strong-beat vertical sample is consonant), no
    // anchor repeats its predecessor (a stalled anchor chain reads as a
    // repeated-note line), and the broken-chord motion supplies the third/fourth
    // leaps the reference corpus writes between strong beats.
    int beat_anchor[4];
    const int near = std::max(band_lo, std::min(band_hi, prev_anchor));
    beat_anchor[0] = nearestChordTone(near, accent_tones);
    for (int beat = 1; beat < 4; ++beat) {
      const int prev = beat_anchor[beat - 1];
      const int dir = ((beat % 2 == 1) ? 1 : -1) * zig_dir;
      const int tones = (beat % 2 == 0) ? accent_tones : kTriadTones;
      int anchor = chordToneBeyond(prev, dir, tones);
      if (anchor == prev)  // band edge: bounce the other way.
        anchor = chordToneBeyond(prev, -dir, tones);
      beat_anchor[beat] = anchor;
    }
    prev_anchor = beat_anchor[3];  // carry the closing anchor to the next bar.

    if (dotted) {
      // Noble dotted figure: dotted-quarter + eighth per beat-pair. The long note
      // is the beat anchor (a chord tone on the strong beat); the short note is
      // a scalar neighbour of it. Anchors are the bar's root and fifth.
      const Tick dq = kTicksPerBeat + kEighth;  // dotted quarter.
      const int half_anchor[2] = {beat_anchor[0], beat_anchor[2]};
      // Both tones of the figure carry a candidate anchor: the long note on the
      // strong beat and its ascending neighbour on the dotted seam. Judging
      // only the anchor leaves the tail free to step into a parallel, and once
      // the anchor is placed the tail has nowhere to go -- at the band floor the
      // descending substitute does not exist at all, which is exactly where the
      // bind used to be unavoidable. Choosing the anchor with its own tail in
      // view dissolves the bind instead of trading one fault for another.
      // A figure is only as good as its worse tone, so the whole cell carries one
      // rank and the anchor is chosen to lower it.
      auto figure_rank = [&](int anchor, Tick beat_tick) {
        return std::max(guard_rank(prev_emitted, anchor, beat_tick),
                        guard_rank(anchor, walk(anchor, 1), beat_tick + dq));
      };
      for (int half = 0; half < 2; ++half) {
        const Tick base =
            static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(half) * 2 * kTicksPerBeat;
        int long_pitch = half_anchor[half];
        const int long_rank = figure_rank(long_pitch, base);
        if (long_rank != kGuardClean) {
          // Nearest in-band chord tone whose whole figure ranks better, a clean
          // one first; when nothing improves on the cell, fall back to guarding
          // the strong beat alone, as every other shape in this builder does.
          // Both halves of this figure open on an accented beat, so the seventh
          // is available here on the same terms as the anchors themselves.
          int best = -1;
          for (int accept = kGuardClean; accept < long_rank && best < 0; ++accept) {
            int best_dist = 1 << 20;
            for (int tone = 0; tone < accent_tones; ++tone) {
              const int low = band_lo + (((chord_pc[tone] - band_lo) % 12) + 12) % 12;
              for (int cand = low; cand <= band_hi; cand += 12) {
                if (cand == long_pitch || figure_rank(cand, base) > accept)
                  continue;
                const int dist = std::abs(cand - long_pitch);
                if (dist < best_dist) {
                  best_dist = dist;
                  best = cand;
                }
              }
            }
          }
          long_pitch = (best >= 0) ? best : guarded_anchor(long_pitch, base, accent_tones);
        }
        MaterialNote longn;
        longn.start_tick = base;
        longn.duration = dq;
        longn.pitch = static_cast<std::uint8_t>(long_pitch);
        dst.push_back(longn);
        prev_emitted = long_pitch;
        // The tail is re-judged after the fact too: when no anchor cleared the
        // whole figure, the descending neighbour still carries the same
        // stepwise vocabulary and may escape where the ascending one cannot.
        const Tick short_tick = base + dq;
        const int short_pitch = improved_by(walk(long_pitch, 1), walk(long_pitch, -1), short_tick);
        MaterialNote shortn;
        shortn.start_tick = short_tick;
        shortn.duration = kEighth;
        shortn.pitch = static_cast<std::uint8_t>(short_pitch);
        dst.push_back(shortn);
        prev_emitted = short_pitch;
      }
      return;
    }

    if (shape == 2 && notes_per_beat == 4) {
      // Figura corta cell: eighth on the anchor, then two sixteenths stepping
      // up and back (the same stepwise neighbour vocabulary in the
      // long-short-short rhythm).
      for (int beat = 0; beat < 4; ++beat) {
        const Tick base =
            static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(beat) * kTicksPerBeat;
        const int anchor =
            guarded_anchor(beat_anchor[beat], base, (beat % 2 == 0) ? accent_tones : kTriadTones);
        MaterialNote longn;
        longn.start_tick = base;
        longn.duration = kEighth;
        longn.pitch = static_cast<std::uint8_t>(anchor);
        dst.push_back(longn);
        prev_emitted = anchor;
        for (int sub = 0; sub < 2; ++sub) {
          const Tick tick = base + kEighth + static_cast<Tick>(sub) * kSixteenth;
          const int pitch =
              improved_by(walk(anchor, sub == 0 ? 1 : 2), walk(anchor, sub == 0 ? -1 : -2), tick);
          MaterialNote shortn;
          shortn.start_tick = tick;
          shortn.duration = kSixteenth;
          shortn.pitch = static_cast<std::uint8_t>(pitch);
          dst.push_back(shortn);
          prev_emitted = pitch;
        }
      }
      return;
    }

    const Tick step = (notes_per_beat == 4) ? kSixteenth : kEighth;
    for (int beat = 0; beat < 4; ++beat) {
      const Tick beat_base =
          static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(beat) * kTicksPerBeat;
      const int anchor = guarded_anchor(beat_anchor[beat], beat_base,
                                        (beat % 2 == 0) ? accent_tones : kTriadTones);
      // Within the beat: the onset is the chord-tone anchor; the remaining
      // subdivisions trace an intra-beat cell that resolves back to the
      // anchor's neighbourhood. The sixteenth tier rotates the cell PER BEAT
      // among {rising step arc, broken-chord arc, falling step arc, broken-
      // third climb, leap-and-fill}: the step arcs alone concentrate the
      // interval-bigram surface into the three step|step bins (a level the
      // reference corpus never reaches), the broken-chord arc supplies the
      // third/fifth leaps the corpus writes inside beats, the broken-third
      // climb supplies the third|step alternations between them, and the
      // leap-and-fill cell (an ascending-sixth leap to the scale tone five
      // degrees up, then stepwise descent) supplies the ascending-sixth bins
      // -- the corpus interval mass no step or third cell reaches. A 5-cell
      // modulus over the 4-beat bar keeps the four beats on four DISTINCT
      // cells (consecutive residues) and rotates which cell sits out each
      // bar, so no cell saturates the surface and beats 0 and 3 never alias
      // (a 3-cell modulus made them sample the same cell in every bar).
      // Each cell stays inside the voice's proven sounding envelope
      // [band_lo, band_hi + a 2-degree neighbour]: the falling arc keeps two
      // scale steps of headroom above the band floor, and the broken-chord /
      // broken-third cells point away from whichever band edge would let
      // their widest tone escape the envelope (when neither direction fits,
      // the rising arc substitutes) -- so the strict V2 < V1 < V0 register
      // order the voice-crossing rule samples at every onset is preserved.
      const int sounding_hi = walk(band_hi, 2);
      bool broken_fits = false;
      int broken_dir = 1;
      bool thirds_fits = false;
      int thirds_dir = 1;
      bool leap_fits = false;
      bool falling;
      if (notes_per_beat == 4) {
        const int cell = (bar + beat + shape) % 5;
        if (cell == 1) {
          broken_dir = (walk(anchor, 4) <= sounding_hi) ? 1 : -1;
          broken_fits = (broken_dir > 0) || (walk(anchor, -4) >= band_lo);
        } else if (cell == 3) {
          thirds_dir = (walk(anchor, 3) <= sounding_hi) ? 1 : -1;
          thirds_fits = (thirds_dir > 0) || (walk(anchor, -3) >= band_lo);
        } else if (cell == 4) {
          // Ascending only: the leap's peak must stay inside the proven
          // sounding envelope (a descending mirror would oversupply the
          // descending-sixth bin, already at corpus level). When the peak
          // does not fit, the rising arc substitutes.
          leap_fits = walk(anchor, 5) <= sounding_hi;
        }
        falling = (cell == 2) && (anchor - 4 >= band_lo);
      } else {
        falling = (shape == 1) && (anchor - 4 >= band_lo);
      }
      // Broken-third climb degrees per subdivision: anchor, third up, step
      // back, third up again -- net a fourth, recovered by the next beat's
      // chord-tone anchor.
      static constexpr int kThirdsCell[4] = {0, 2, 1, 3};
      // Leap-and-fill degrees: anchor, sixth up, then two steps back down --
      // the classical leap-then-contrary-fill shape; the cell ends a fourth
      // above the anchor, recovered by the next beat's chord-tone anchor.
      static constexpr int kLeapCell[4] = {0, 5, 4, 3};
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mn;
        mn.start_tick = beat_base + static_cast<Tick>(sub) * step;
        mn.duration = step;
        const int magnitude = (sub <= notes_per_beat / 2) ? sub : (notes_per_beat - sub);
        const int degrees =
            leap_fits ? kLeapCell[sub & 3]
                      : (thirds_fits ? thirds_dir * kThirdsCell[sub & 3]
                                     : (broken_fits ? broken_dir * 2 * magnitude
                                                    : (falling ? -magnitude : magnitude)));
        int pitch = walk(anchor, degrees);
        // Intra-beat cell tone colliding with the guard voice. The mirror comes
        // first: the anchor's other side is the same neighbour vocabulary, so
        // the cell still resolves to the anchor. But the mirror is one tone, and
        // when it falls outside the band there was nothing else to offer -- V1's
        // band is nine semitones wide, so a cell tone stepping away from the
        // floor has its mirror below the floor and the fault stood. The anchor's
        // own pitch is the escape of last resort: an oblique repeat cannot form
        // a perfect motion with anything. It goes last because repeating the
        // anchor flattens the cell into a held tone, a cost the mirror does not
        // pay. This cell reaches a step past the band ceiling, so it carries its
        // own range test rather than the shared one.
        const int cell_rank = guard_rank(prev_emitted, pitch, mn.start_tick);
        if (degrees != 0 && cell_rank != kGuardClean) {
          bool escaped = false;
          for (int accept = kGuardClean; accept < cell_rank && !escaped; ++accept) {
            for (const int alt : {walk(anchor, -degrees), anchor}) {
              if (alt == pitch || alt < band_lo || alt > walk(band_hi, 2))
                continue;
              if (guard_rank(prev_emitted, alt, mn.start_tick) <= accept) {
                pitch = alt;
                escaped = true;
                break;
              }
            }
          }
        }
        mn.pitch = static_cast<std::uint8_t>(pitch);
        dst.push_back(mn);
        prev_emitted = pitch;
      }
    }
  };

  TrioVoiceLine v0;
  v0.voice = 0;
  v0.manual = 0;  // Great (RH, high register).
  TrioVoiceLine v1;
  v1.voice = 1;
  v1.manual = 1;  // Swell (LH, mid register).

  // The manual registers deliberately overlap: V0's C4--C6 and V1's
  // G3--E4 ranges make a short exchange possible without forcing either hand
  // into an artificial, non-overlapping shelf. The form's explicit
  // AllowTrioUpperMomentary policy (set below) still rejects a held inversion;
  // the pedal remains below both manuals.
  constexpr int kV0BandLo = 60;  // C4.
  constexpr int kV0BandHi = 84;  // C6.
  constexpr int kV1BandLo = 55;  // G3; remains above the pedal ceiling.
  constexpr int kV1BandHi = 64;  // E4.
  int v0_anchor = 72;            // ~C5.
  int v1_anchor = 60;            // ~C4.
  int v0_prev_emitted = -1;
  int v1_prev_emitted = -1;
  ThemeToneRegistry manual_registry;

  for (int bar = 0; bar < bars; ++bar) {
    const std::size_t cycle = static_cast<std::size_t>(bar / 4);
    const ArcPoint arc = req.arc(std::min(cycle, req.cycle_count - 1));

    // Effective density tier after the character density bias (clamped 0..3).
    int tier = static_cast<int>(arc.density_tier) + profile.density_bias;
    tier = std::max(0, std::min(3, tier));

    // The DENSE upper voice is always sixteenths (4/beat). The SECONDARY upper
    // voice's note rate scales with the arc tier (quarter at tier 0, eighths at
    // tier 1+), so total upper density genuinely rises toward the climax while
    // the two upper voices keep DISTINCT rhythms (the trio independence trait).
    // The climax intensity comes from this note-rate rise rather than a register
    // jump, so the lines stay leap-free across bar boundaries.
    const int dense_notes = 4;
    const int sparse_notes = (tier >= 1) ? 2 : 1;

    // Voice-pair rotation: on even cycles V0 carries the dense sixteenth line and
    // V1 the sparser line; on odd cycles they swap density. Registers stay banded
    // (V0 ~C5, V1 ~C4), so the swap never crosses voices.
    const bool v0_dense = (cycle % 2) == 0;
    const int v0_notes = v0_dense ? dense_notes : sparse_notes;
    const int v1_notes = v0_dense ? sparse_notes : dense_notes;

    // Noble dotted preference applies to the V1 line only when V1 is the sparser
    // voice (v0_dense) and the arc tier is low, so the dense sixteenth line and
    // the rhythmic distinction between the voices are preserved.
    const bool v1_dotted = profile.prefer_dotted && v0_dense && tier <= 1;

    // Bar-rotated intra-beat vocabulary, phase-shifted by one slot between
    // the manual voices so they never trace the same figure within a bar
    // (distinct figuration strengthens the voice-independence trait). Rotating
    // per BAR rather than per cycle keeps any single figure from saturating
    // the interval-bigram surface for sixteen beats in a row. The climax
    // cycle is a design value: its dense line keeps the uniform sixteenth
    // arc (the figura corta cell carries fewer notes per beat and would
    // flatten the density peak).
    int v0_shape = static_cast<int>((req.seed + static_cast<std::uint32_t>(bar)) % 3);
    int v1_shape = static_cast<int>((req.seed + static_cast<std::uint32_t>(bar) + 1) % 3);
    if (arc.is_climax)
      (v0_dense ? v0_shape : v1_shape) = 0;

    const std::size_t v0_begin = v0.notes.size();
    appendScalarBar(v0.notes, v0_anchor, kV0BandLo, kV0BandHi, bar, v0_notes, /*dotted=*/false,
                    v0_shape, /*zig_dir=*/1, /*guard_registry=*/nullptr, v0_prev_emitted);
    // Replay V0's placed tones through the shared registry before building V1.
    // This is intentionally span-local: the manual carrier spans cover the
    // whole form, while each bar appends one immutable slice to the same
    // registry. V1 therefore sees the actual sounding V0 tone at every
    // sub-beat, rather than a coarse register-band approximation.
    for (std::size_t i = v0_begin; i < v0.notes.size(); ++i) {
      const MaterialNote& note = v0.notes[i];
      manual_registry.record(note.start_tick, 0, note.pitch, note.duration);
    }
    // V1 is built after V0 within the bar, so it reads the registered V0 line
    // at every onset to avoid audible perfect parallels.
    appendScalarBar(v1.notes, v1_anchor, kV1BandLo, kV1BandHi, bar, v1_notes, v1_dotted, v1_shape,
                    /*zig_dir=*/-1, &manual_registry, v1_prev_emitted);
  }

  // Cadential landing on the top line: an eighth-note approach into a held
  // half-note leading tone over the design-valued V bar (the cadential trill
  // site), then a whole-note tonic. V0 owns the landing -- it is the highest
  // voice, so the cadence trill reads as the soprano close.
  constexpr int kV0Tonic = 72;  // C5: the tonic inside the V0 band.
  // Thread the last pre-cadential figuration cell down into the landing's
  // low approach tone. The bare landing begins on E4/Eb4; leaving the scalar
  // wave's C6/B5 peak intact until that instant creates a 19-semitone cliff.
  // Four descending, mode-aware degrees turn that seam into ordinary steps
  // and small skips while retaining the E4/Eb4 meeting point where the two
  // manuals exchange momentarily.
  const Tick landing_start = static_cast<Tick>(bars - 2) * kTicksPerBar;
  std::vector<std::size_t> pre_landing_indices;
  for (std::size_t i = 0; i < v0.notes.size(); ++i) {
    if (v0.notes[i].start_tick < landing_start) {
      pre_landing_indices.push_back(i);
    }
  }
  // Everything from here to the end of the piece is rewritten after the middle
  // voice was built against it, so the closing-region repair at the end of this
  // builder re-judges V1 from this tick on. The landing alone already replaces
  // V0 from the penultimate bar; the thread below reaches four notes further
  // back, so the window opens at whichever of the two starts earlier.
  Tick v1_repair_start = landing_start;
  if (pre_landing_indices.size() >= 4) {
    const int entry = detail::scaleDown(kV0Tonic - 1, 4, mode);
    const int p3 = detail::scaleUp(entry, 4, mode);
    const int p2 = detail::scaleUp(p3, 2, mode);
    const int p1 = detail::scaleUp(p2, 1, mode);
    const int p0 = detail::scaleUp(p1, 1, mode);
    const std::size_t n = pre_landing_indices.size();
    v0.notes[pre_landing_indices[n - 4]].pitch = static_cast<std::uint8_t>(p0);
    v0.notes[pre_landing_indices[n - 3]].pitch = static_cast<std::uint8_t>(p1);
    v0.notes[pre_landing_indices[n - 2]].pitch = static_cast<std::uint8_t>(p2);
    v0.notes[pre_landing_indices[n - 1]].pitch = static_cast<std::uint8_t>(p3);
    v1_repair_start = v0.notes[pre_landing_indices[n - 4]].start_tick;
  }
  appendCadentialLanding(v0.notes, static_cast<Tick>(bars - 2) * kTicksPerBar, kTicksPerBar,
                         kV0Tonic - 1, kV0Tonic, mode, kV0BandLo);

  // One brief manual exchange near the cadence makes the overlapping manual
  // tessitura audible without turning it into a sustained role inversion. The
  // candidate is selected from already-written carrier notes: V1 meets V0 on
  // one sub-beat, rises by a scale-neighbour semitone, and the following union
  // onset restores V0 above it. If a particular length/seed has no such safe
  // meeting, the form simply keeps its ordinary contrary-motion cadence.
  const auto sounding_pitch = [](const std::vector<MaterialNote>& line, Tick tick) -> int {
    for (auto it = line.rbegin(); it != line.rend(); ++it) {
      if (it->start_tick <= tick && tick < it->start_tick + it->duration)
        return static_cast<int>(it->pitch);
    }
    return -1;
  };
  // The manual lines rest between onsets at the sparser tiers, and a perfect
  // parallel is heard note-to-note ACROSS such a rest: the union-onset reading
  // pairs the last tone that sounded with the next one that starts. Sampling one
  // tick back returns nothing from inside a rest and silently clears the pair,
  // so guards take the latest onset strictly before the tick as the "from" tone.
  const auto onset_before = [](const std::vector<MaterialNote>& line, Tick tick) -> int {
    int pitch = -1;
    Tick best = 0;
    for (const MaterialNote& note : line) {
      if (note.start_tick < tick && (pitch < 0 || note.start_tick >= best)) {
        best = note.start_tick;
        pitch = static_cast<int>(note.pitch);
      }
    }
    return pitch;
  };
  const Tick coda_start = static_cast<Tick>(bars - 2) * kTicksPerBar;
  const Tick final_bar_tick = static_cast<Tick>(bars - 1) * kTicksPerBar;
  for (MaterialNote& note : v1.notes) {
    if (note.start_tick < coda_start || note.start_tick + note.duration >= final_bar_tick)
      continue;
    const int v0_now = sounding_pitch(v0.notes, note.start_tick);
    const int v0_next = sounding_pitch(v0.notes, note.start_tick + note.duration);
    if (v0_now == static_cast<int>(note.pitch) && v0_next >= v0_now + 1 && note.pitch < 127) {
      ++note.pitch;
      break;
    }
  }

  // V1 joins the held final chord instead of running figuration through the
  // final bar: one whole-note third of the closing tonic triad (E, or Eb in
  // minor without the Picardy lift), filling the triad between the V0 tonic
  // and the pedal root.
  {
    const bool picardy = (mode == Mode::Minor) && detail::usePicardy(req.seed);
    const int third = (mode == Mode::Minor && !picardy) ? 63 : 64;  // Eb4 / E4.
    v1.notes.erase(
        std::remove_if(v1.notes.begin(), v1.notes.end(),
                       [&](const MaterialNote& note) { return note.start_tick >= final_bar_tick; }),
        v1.notes.end());
    MaterialNote held;
    held.start_tick = final_bar_tick;
    held.duration = kTicksPerBar;
    held.pitch = static_cast<std::uint8_t>(third);
    v1.notes.push_back(held);
  }

  out.material.trio_voices.push_back(std::move(v0));
  out.material.trio_voices.push_back(std::move(v1));

  // V2 (Pedal): the continuo, and a continuo is a LINE. Every beat carries a
  // chord tone -- the downbeat the bar's root, the closing beat the tone that
  // leads into the next bar's root -- and the gaps a single diatonic tone can
  // bridge are filled with a connecting eighth. So the harmony is stated where
  // it is sampled (the beat onsets stay chord tones, which is what keeps the
  // strong-beat vertical reading consonant) while the motion between those
  // points is stepwise, which is the difference between a bass that walks and
  // one that marks the chord with repeated and leaping quarters.
  //
  // Every tone is realized inside one narrow band, so the whole voice stays low
  // (<= F3 = 53 < the mid voice's floor) and the V2 < V1 < V0 register banding
  // holds; the root register is voice-led near the previous bar's root so
  // successive bars' roots move by a small interval rather than an octave jump.
  TrioVoiceLine v2;
  v2.voice = 2;
  v2.manual = 3;  // Pedal (low register).
  // The walk lives in [C2, F3]. The floor is the organ pedalboard's own low
  // register, not a convenience: a triad inside a thirteenth puts three tones in
  // reach and no more, and the guard below has to choose the tone that arrives
  // at the next bar head without moving in a perfect class with either manual.
  // With the roots sitting near the bottom of a narrow band, almost every tone
  // it can offer approaches them from ABOVE, so the arrival is similar motion
  // whenever the manuals descend -- which is where this voice used to spend its
  // hidden perfects. An octave of room below the roots is what lets it arrive
  // from underneath instead.
  constexpr int kPedalFloor = 36;  // C2.
  constexpr int kPedalCeil = 53;   // F3: still clear of the mid voice's floor.

  // The manual voices are final when the pedal is laid down, so every pedal
  // tone can be judged against their audible-grain motion (sampling one
  // sixteenth back reproduces the union-onset pair for the sixteenth-note
  // upper lines; coarser bars simply sustain across the sample point).
  // Full scans (no sorted-order early exit): the cadential landing is appended
  // after the bar loop, so the manual lines are not strictly onset-ordered.
  const auto upper_sounding = [&](const std::vector<MaterialNote>& line, Tick t) -> int {
    for (auto it = line.rbegin(); it != line.rend(); ++it) {
      if (it->start_tick <= t && t < it->start_tick + it->duration)
        return static_cast<int>(it->pitch);
    }
    return -1;
  };
  // The sparse manual line rests between onsets, and the parallel is heard
  // note-to-note across the rest, so the "from" tone is the latest onset
  // strictly before t rather than a tone required to sound at a fixed grain.
  const auto upper_before = [&](const std::vector<MaterialNote>& line, Tick t) -> int {
    int pitch = -1;
    Tick best = 0;
    for (const MaterialNote& note : line) {
      if (note.start_tick < t && (pitch < 0 || note.start_tick >= best)) {
        best = note.start_tick;
        pitch = static_cast<int>(note.pitch);
      }
    }
    return pitch;
  };
  // The manual lines were std::move'd into out.material.trio_voices above, so
  // the guard reads them from their final home (v0.notes / v1.notes are empty
  // husks at this point).
  // Ranked rather than pooled, for the reason the manual guard is: the pedal
  // walks a chord-tone cell under two lines that mostly move the other way, so
  // most of what an audit hears here arrives by contrary motion. Judging only
  // same-direction arrivals left the pedal free to leap down onto an octave --
  // and it is the last voice written, so nothing downstream corrects it.
  //
  // The true parallel and the hidden perfect sit on SEPARATE rungs, unlike the
  // manual guard one voice above. The pedal has no room to treat them alike:
  // its band spans a thirteenth and holds three chord tones, so under two lines
  // that both move at every beat almost every tone the bar offers is at least a
  // hidden perfect against one of them. Pooling the two left the guard with no
  // candidate ranking better than the design tone, so it kept whatever it had --
  // including a true parallel it could have traded for a hidden one.
  //
  // The battuta sits ABOVE the hidden perfect here, which is the opposite of
  // the order every other guard in the product uses. The pedal's band spans a
  // thirteenth and a triad puts three tones in it, so the escape below is
  // choosing between faults far more often than it is finding a clean tone --
  // which makes the order it chooses by the thing that decides this form's
  // counterpoint profile. Both strict classes stay worst; only the two payable
  // ones swap.
  //
  // The reference corpus does not settle which way they should sit. Swapping
  // them back was measured across every character, mode and seed against three
  // defensible strata of the same repertoire, and the sign of the result
  // changes with the stratum: the encoded three-voice group prefers this order,
  // the organ group prefers the general one by a margin small enough that the
  // worst single cell moves the other way. An order that only wins under one
  // reading of the corpus is not evidence for changing the order, so this one
  // stands on the ranking argument alone.
  //
  // The contrary arrival is a different matter and the corpus is not divided
  // about it: every stratum of the reference works writes that class far more
  // sparingly than either payable one, so scaled by the spread each occupies it
  // is several times the dearer. It therefore sits at the bottom of what this
  // guard is willing to pay rather than the top, where it used to be the first
  // thing reached for.
  constexpr int kPedalClean = 0;
  // A restated tone is not a fault -- an oblique voice forms no perfect motion
  // with anything -- so it sits between clean and the cheapest real one. It has
  // a price because a bass that restates its way out of every bind stops being
  // a line, and it is below the faults because the corpus writes a continuo
  // holding its tone constantly and a hidden perfect sparingly.
  constexpr int kPedalRepeat = 1;
  constexpr int kPedalHidden = 2;
  constexpr int kPedalBattuta = 3;
  constexpr int kPedalAntiParallel = 4;
  constexpr int kPedalParallel = 5;
  const auto pedal_fault_rank = [&](int from, int cand, Tick t) {
    int worst = kPedalClean;
    for (const TrioVoiceLine& manual : out.material.trio_voices) {
      const int prev = upper_before(manual.notes, t);
      const int curr = upper_sounding(manual.notes, t);
      if (prev < 0 || curr < 0)
        continue;
      if (formsStrictPerfectParallel(from, cand, prev, curr))
        return kPedalParallel;
      if (formsPerfectParallel(from, cand, prev, curr))
        worst = std::max(worst, kPedalHidden);
      else if (formsAntiParallelPerfect(from, cand, prev, curr))
        worst = std::max(worst, kPedalAntiParallel);
      else if (formsBattuta(from, cand, prev, curr))
        worst = std::max(worst, kPedalBattuta);
    }
    return worst;
  };

  // How a gap between two adjacent bass tones reads as a line. A third is the
  // cheapest because one diatonic tone bridges it into two steps; a fourth or a
  // fifth is an ordinary walking leap; anything wider breaks the line, and a
  // repeated tone is what stops it being a line at all.
  const auto gap_cost = [](int from, int to) {
    const int gap = std::abs(to - from);
    if (gap == 0)
      return 8;
    if (gap <= 2)
      return 2;
    if (gap <= 4)
      return 0;
    if (gap <= 7)
      return 3;
    if (gap <= 12)
      return 6;
    return 40;  // wider than an octave: not a step this line takes at all.
  };

  // Onsets where the pedal ran out of room: its band spans a thirteenth and a
  // triad puts exactly three tones in it, so a bar can arrive where all three
  // read as a true parallel against one manual or the other and the design tone
  // has to stand. The middle manual is the voice that can still travel there,
  // and the closing repair at the end of this builder already knows how to move
  // it, so the ticks are handed to it rather than answered here.
  std::vector<Tick> pedal_boxed_ticks;
  int prev_root = 48;  // seed near C3.
  for (int bar = 0; bar < bars; ++bar) {
    const ChordSpec& bar_chord = chords[static_cast<std::size_t>(bar)];
    const int root_pc = bar_chord.root_pc % 12;
    // Root in the pedal register nearest the previous bar's root, so successive
    // bars' roots move by a small interval (no octave-plus boundary leap).
    int root_midi = kPedalFloor + (((root_pc - kPedalFloor) % 12) + 12) % 12;
    while (root_midi + 12 <= kPedalCeil &&
           std::abs((root_midi + 12) - prev_root) < std::abs(root_midi - prev_root))
      root_midi += 12;
    if (bar == bars - 1) {
      // Final bar: the pedal joins the held closing chord with a whole-note
      // tonic root instead of walking.
      MaterialNote held;
      held.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
      held.duration = kTicksPerBar;
      held.pitch = static_cast<std::uint8_t>(root_midi);
      v2.notes.push_back(held);
      prev_root = root_midi;
      continue;
    }

    // The bar's triad tones realized inside the pedal band, ascending: the
    // stations the walk steps between, and the tones a displaced beat may be
    // moved to. The band spans a thirteenth, so a triad normally puts three of
    // them within reach, a third and a fourth apart.
    std::array<int, 8> station{};
    int stations = 0;
    {
      const int third_pc = (root_pc + (bar_chord.minor ? 3 : 4)) % 12;
      const int fifth_pc = (root_pc + 7) % 12;
      for (int cand = kPedalFloor; cand <= kPedalCeil && stations < 8; ++cand) {
        const int pc = cand % 12;
        if (pc == root_pc || pc == third_pc || pc == fifth_pc) {
          station[static_cast<std::size_t>(stations)] = cand;
          ++stations;
        }
      }
    }

    // The next bar's root, which the closing beats walk into. The downbeat is
    // the harmonic anchor and is never displaced, so it is fixed before this
    // bar's own tones are chosen; it depends on this bar's root only, which no
    // displacement below changes.
    int next_root = -1;
    if (bar + 1 < bars) {
      const int next_pc = chords[static_cast<std::size_t>(bar + 1)].root_pc % 12;
      next_root = kPedalFloor + (((next_pc - kPedalFloor) % 12) + 12) % 12;
      while (next_root + 12 <= kPedalCeil &&
             std::abs((next_root + 12) - root_midi) < std::abs(next_root - root_midi))
        next_root += 12;
    }

    // Closing beat: the chord tone that leads best into the next bar's root --
    // a third away wherever the band holds one, so the connecting eighth steps
    // through the gap into the downbeat. The next root itself is the dearest
    // choice of all: a bar that ends on the pitch it is about to restate marks
    // the harmony instead of arriving at it, which is where the old
    // root-on-both-outer-beats cell spent most of its repeated notes.
    int approach = root_midi;
    if (next_root >= 0) {
      int best = 1 << 20;
      for (int idx = 0; idx < stations; ++idx) {
        const int cand = station[static_cast<std::size_t>(idx)];
        const int cost = gap_cost(cand, next_root) * 16 + std::abs(cand - root_midi);
        if (cost < best) {
          best = cost;
          approach = cand;
        }
      }
    }

    // The two inner beats walk from the root to the closing tone through the
    // bar's own stations. A route is scored by how its four gaps read as a
    // line, plus a penalty for every reversal of direction, so the bar travels
    // rather than rocking between two tones. Ties rotate per bar: successive
    // bars over the same chord would otherwise trace an identical contour, and
    // a bass whose every bar is the same cell is what a listener hears as a
    // figured-bass accompaniment rather than a part.
    const auto route_cost = [&](int first, int second) {
      const int walk[5] = {root_midi, first, second, approach,
                           next_root >= 0 ? next_root : approach};
      int cost = 0;
      int prev_dir = 0;
      for (int leg = 0; leg + 1 < 5; ++leg) {
        cost += gap_cost(walk[leg], walk[leg + 1]);
        const int dir = (walk[leg + 1] > walk[leg]) ? 1 : (walk[leg + 1] < walk[leg] ? -1 : 0);
        if (dir != 0) {
          if (prev_dir != 0 && dir != prev_dir)
            ++cost;
          prev_dir = dir;
        }
      }
      return cost;
    };
    int inner_a = root_midi;
    int inner_b = approach;
    {
      int best = 1 << 20;
      int matches = 0;
      for (int first = 0; first < stations; ++first) {
        for (int second = 0; second < stations; ++second) {
          const int cost = route_cost(station[static_cast<std::size_t>(first)],
                                      station[static_cast<std::size_t>(second)]);
          if (cost < best) {
            best = cost;
            matches = 1;
          } else if (cost == best) {
            ++matches;
          }
        }
      }
      const int pick = static_cast<int>((req.seed + static_cast<std::uint32_t>(bar)) %
                                        static_cast<std::uint32_t>(std::max(matches, 1)));
      int seen = 0;
      for (int first = 0; first < stations; ++first) {
        for (int second = 0; second < stations; ++second) {
          if (route_cost(station[static_cast<std::size_t>(first)],
                         station[static_cast<std::size_t>(second)]) != best)
            continue;
          if (seen == pick) {
            inner_a = station[static_cast<std::size_t>(first)];
            inner_b = station[static_cast<std::size_t>(second)];
          }
          ++seen;
        }
      }
    }

    // How much of the bar the continuo fills with eighths: a budget rather than
    // fixed slots, because only a gap of a third holds a tone that steps into
    // both of its ends, and which beats offer one depends on the chord. The
    // calm tiers keep two, the livelier ones three, so the movement's own
    // density curve reaches the bass; the pedal still articulates well under
    // either manual line, which is what keeps the trio's three rhythms apart.
    const std::size_t pedal_cycle = static_cast<std::size_t>(bar / 4);
    const ArcPoint pedal_arc = req.arc(std::min(pedal_cycle, req.cycle_count - 1));
    int pedal_tier = static_cast<int>(pedal_arc.density_tier) + profile.density_bias;
    pedal_tier = std::max(0, std::min(3, pedal_tier));
    const int link_budget = (pedal_tier >= 2) ? 3 : 2;
    int links_placed = 0;
    // The penultimate bar writes no connecting tone: the cadence contract
    // restores the dominant root on its closing beat after this loop has run,
    // so any eighth derived from the tone that beat carried here would be left
    // standing against a tone that is no longer there.
    const bool cadence_bar = (bar == bars - 2);

    // The diatonic tone that steps into BOTH ends of a gap, i.e. the passing
    // tone of a third. It is tried from either end because a scale step taken
    // from the upper side of a minor-key dominant lands on the natural seventh,
    // a semitone under that chord's own leading tone. No such tone means the
    // beat keeps its plain quarter: a connecting tone left by leap is not a
    // passing tone, it is an unprepared dissonance.
    const auto connecting_tones = [&](int from, int to, int* out) {
      int count = 0;
      if (from == to)
        return count;
      const int dir = (to > from) ? 1 : -1;
      const int cands[2] = {detail::melodicScaleStep(from, dir, mode, &bar_chord),
                            detail::melodicScaleStep(to, -dir, mode, &bar_chord)};
      for (const int cand : cands) {
        if (cand < kPedalFloor || cand > kPedalCeil)
          continue;
        if (std::abs(cand - from) < 1 || std::abs(cand - from) > 2)
          continue;
        if (std::abs(to - cand) < 1 || std::abs(to - cand) > 2)
          continue;
        if (count == 1 && out[0] == cand)
          continue;
        out[count] = cand;
        ++count;
      }
      return count;
    };

    const int beat_pitch[4] = {root_midi, inner_a, inner_b, approach};
    // Audible-grain parallel guard. The downbeat root is the harmonic anchor
    // and is never displaced; a parallel INTO it is owned by the previous
    // bar's closing beat (guarded on its own turn below, including the
    // forward motion into this root). Beats 2-4 swap to another chord tone
    // of the bar when their design tone would move in a perfect class with
    // either manual voice; the design tone stands when no alternative clears.
    int pedal_prev = v2.notes.empty() ? -1 : static_cast<int>(v2.notes.back().pitch);
    for (int beat = 0; beat < 4; ++beat) {
      const Tick t =
          static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(beat) * kTicksPerBeat;
      const bool boundary = (beat == 3);
      // The tone this beat walks into: the next beat's anchor, or the next
      // bar's root at the barline, which is the only one already fixed.
      const int follows = boundary ? next_root : beat_pitch[beat + 1];
      // The barline arrival is the walking gesture itself, so the closing beat
      // always keeps a slot; the inner beats share what is left of the budget.
      const bool wants_link =
          follows >= 0 && !cadence_bar &&
          (boundary ? links_placed < link_budget : links_placed < link_budget - 1);
      int pitch = beat_pitch[beat];
      // Both ends of the beat, worst first: the motion into it and, on the
      // closing beat, the boundary motion into the next bar's fixed root. The
      // second is the reason a repair here cannot look only backwards -- the
      // bar head it lands on cannot move, so a tone that clears its own arrival
      // and ruins that one has traded a fault for a fault. A filled closing
      // beat is judged as a whole figure, anchor and connecting eighth
      // together: a figure is only as good as its worse tone, and an anchor
      // that clears its own arrival while forcing the eighth into a parallel
      // has made the same bad trade one subdivision later.
      const auto beat_rank = [&](int cand) {
        int worst = kPedalClean;
        if (beat != 0 && pedal_prev >= 0)
          worst = std::max(worst, pedal_fault_rank(pedal_prev, cand, t));
        if (!boundary || next_root < 0)
          return worst;
        const int plain = pedal_fault_rank(cand, next_root, t + kTicksPerBeat);
        int link[2];
        const int link_count = wants_link ? connecting_tones(cand, next_root, link) : 0;
        int best_figure = plain;  // the fill is optional, so the plain quarter is the floor.
        for (int idx = 0; idx < link_count; ++idx) {
          best_figure = std::min(
              best_figure, std::max(pedal_fault_rank(cand, link[idx], t + kEighth),
                                    pedal_fault_rank(link[idx], next_root, t + kTicksPerBeat)));
        }
        return std::max(worst, best_figure);
      };
      // Restating the tone the pedal has just sounded is not free, and the
      // guard used to treat it as though it were: an oblique repeat can form no
      // perfect motion with anything, so it cleared every rank trivially and
      // the escape took it several beats running -- which is how a bass line
      // turns back into a bar-long marker of one pitch. Pricing it above clean
      // means the walk moves wherever a chord tone is available to move to.
      // Pricing it BELOW the faults means it stalls rather than paying one: the
      // reference corpus writes a continuo holding its tone constantly and a
      // hidden perfect at a tenth of the rate this voice would reach if a
      // repeat outranked one. The downbeat is exempt: it carries the bar's root
      // whatever the previous bar closed on.
      const auto escape_rank = [&](int cand) {
        const int rank = beat_rank(cand);
        if (beat != 0 && cand == pedal_prev)
          return std::max(rank, kPedalRepeat);
        return rank;
      };
      const int design_rank = escape_rank(pitch);
      if (design_rank != kPedalClean) {
        // Chord tones nearest the design tone first, so a displaced beat moves
        // as little as the fault allows and the bar's contour survives the
        // repair rather than being redrawn by whichever tone happens to sit
        // lowest in the band.
        std::array<int, 8> order{};
        for (int idx = 0; idx < stations; ++idx)
          order[static_cast<std::size_t>(idx)] = station[static_cast<std::size_t>(idx)];
        for (int idx = 1; idx < stations; ++idx) {
          const int cand = order[static_cast<std::size_t>(idx)];
          int slot = idx;
          while (slot > 0 && std::abs(order[static_cast<std::size_t>(slot - 1)] - pitch) >
                                 std::abs(cand - pitch)) {
            order[static_cast<std::size_t>(slot)] = order[static_cast<std::size_t>(slot - 1)];
            --slot;
          }
          order[static_cast<std::size_t>(slot)] = cand;
        }
        // A bass does not leap more than an octave, and the band is now wide
        // enough to offer one that would: a repair reaching past an octave has
        // stopped repairing the line and started replacing it. The bar head it
        // walks into is fixed, so the reach is measured at both ends.
        constexpr int kPedalLeapCeiling = 12;
        const auto within_reach = [&](int cand) {
          if (pedal_prev >= 0 && std::abs(cand - pedal_prev) > kPedalLeapCeiling)
            return false;
          return !boundary || next_root < 0 || std::abs(cand - next_root) <= kPedalLeapCeiling;
        };
        bool placed = false;
        for (int accept = kPedalClean; accept < design_rank && !placed; ++accept) {
          for (int idx = 0; idx < stations; ++idx) {
            const int cand = order[static_cast<std::size_t>(idx)];
            if (cand == pitch || !within_reach(cand))
              continue;
            if (escape_rank(cand) <= accept) {
              pitch = cand;
              placed = true;
              break;
            }
          }
          // Chord tones first, then the diatonic tones between them. The three
          // degrees already appear at every octave the band can hold, so when
          // none of them clears, the escape is not choosing badly -- it is out
          // of chord to choose from, and measured over the sweep that happens at
          // about one onset in six where a diatonic tone WOULD be clean. A
          // passing tone in a walking bass is idiomatic here in a way a
          // displaced root is not, so the widening is worth its harmonic cost.
          //
          // The borrowed tone is not held to consonance against the manuals.
          // Requiring it kept nearly the whole fault: the onsets where the chord
          // is spent are the same onsets where a manual is sounding across the
          // beat, so the tone that would have been clean is usually the one that
          // brushes it. What that requirement bought was measured on the axis it
          // trades into -- the share of beat onsets carrying a sounding second,
          // seventh or tritone -- and without the requirement this voice still
          // runs at about half the rate of the sparest three-voice organ work in
          // the reference corpus, and well inside the ceiling the form's own
          // vertical test holds it to.
          // The perfect approach is the fault the corpus is strict about; the
          // passing second is one it writes constantly in the same texture.
          // Nearest to the design tone first, so the walk moves as little as the
          // fault allows.
          //
          // The borrowed tone answers the two STRICT classes only. Every beat
          // onset of this voice is where the harmony is sampled, so leaving the
          // chord there is paid for in vertical dissonance, and a hidden
          // perfect or a battuta is not worth that price -- those the walk pays
          // on its own chord tones. A true or anti-parallel is: it is the fault
          // the corpus is strict about, this voice is written last, and a fault
          // it cannot leave is one the piece ships.
          //
          // Neither end of a borrowed tone may be a leap. The design tones are
          // the ones entitled to travel -- they are the chord -- so a tone
          // borrowed from between them has to be walked to and walked away
          // from, or it reads as the line breaking off rather than passing
          // through. A fifth is the widest either end may be.
          constexpr int kPedalBorrowedReach = 7;
          if (design_rank < kPedalAntiParallel)
            continue;
          const auto walkable = [&](int cand) {
            if (pedal_prev >= 0 && std::abs(cand - pedal_prev) > kPedalBorrowedReach)
              return false;
            return follows < 0 || std::abs(follows - cand) <= kPedalBorrowedReach;
          };
          for (int away = 1; away <= kPedalBorrowedReach && !placed; ++away) {
            for (const int cand : {pitch - away, pitch + away}) {
              if (cand < kPedalFloor || cand > kPedalCeil)
                continue;
              if (!detail::inScale(cand, mode) || !walkable(cand))
                continue;
              if (escape_rank(cand) <= accept) {
                pitch = cand;
                placed = true;
                break;
              }
            }
          }
        }
      }
      // The connecting eighth, derived from the tone actually placed rather than
      // from the design tone the guard may have moved off. The fill is optional,
      // so it is held to the beat it replaces: a connecting tone that walks into
      // a perfect class the plain quarter would have avoided has bought its step
      // with a fault, and the step is not worth that. Judged over both of its
      // motions -- into the tone and out of it -- because inserting an onset
      // changes the arrival at the next one as well.
      int link_pitch = -1;
      if (wants_link && follows >= 0) {
        int link[2];
        const int link_count = connecting_tones(pitch, follows, link);
        const int plain_rank = pedal_fault_rank(pitch, follows, t + kTicksPerBeat);
        int best_rank = 1 << 20;
        for (int idx = 0; idx < link_count; ++idx) {
          const int rank = std::max(pedal_fault_rank(pitch, link[idx], t + kEighth),
                                    pedal_fault_rank(link[idx], follows, t + kTicksPerBeat));
          if (rank < best_rank) {
            best_rank = rank;
            link_pitch = link[idx];
          }
        }
        if (best_rank > plain_rank || best_rank >= kPedalParallel)
          link_pitch = -1;
      }
      // Onsets where this voice ran out of room, handed to the middle manual by
      // the closing repair. A hidden perfect is running out of room just as a
      // true parallel is: the pedal is written last against two settled lines,
      // its band holds a handful of chord tones, and whatever it leaves standing
      // is what ships. The middle voice is the one still free to move at those
      // onsets, and it judges the same two classes there.
      //
      // What the hand-over does and does not reach: it widens the window the
      // middle voice repairs in, and that voice is still moved only off faults of
      // its own. A perfect approach the pedal forms against the UPPER voice is
      // not one of those, and no later pass answers it -- by then both of its
      // operands are settled. Roughly half of what this voice leaves standing is
      // that kind, and it is left standing on purpose: the alternative is moving
      // a line that is already finished. The bar head is skipped for the same
      // reason it is exempt from the pedal's own escape, and including it here
      // was measured to change nothing at all, since the onsets it adds are ones
      // where the middle voice reads clean and the pass returns immediately.
      if (beat != 0 && pedal_prev >= 0 && pedal_fault_rank(pedal_prev, pitch, t) >= kPedalHidden)
        pedal_boxed_ticks.push_back(t);
      if (link_pitch >= 0 && pedal_fault_rank(pitch, link_pitch, t + kEighth) >= kPedalHidden) {
        pedal_boxed_ticks.push_back(t + kEighth);
      }
      if (next_root >= 0) {
        const int leaves = (link_pitch >= 0) ? link_pitch : pitch;
        if (pedal_fault_rank(leaves, next_root, t + kTicksPerBeat) >= kPedalHidden)
          pedal_boxed_ticks.push_back(t + kTicksPerBeat);
      }
      MaterialNote mn;
      mn.start_tick = t;
      mn.duration = (link_pitch >= 0) ? kEighth : kTicksPerBeat;
      mn.pitch = static_cast<std::uint8_t>(pitch);
      v2.notes.push_back(mn);
      pedal_prev = pitch;
      if (link_pitch >= 0) {
        MaterialNote link_note;
        link_note.start_tick = t + kEighth;
        link_note.duration = kEighth;
        link_note.pitch = static_cast<std::uint8_t>(link_pitch);
        v2.notes.push_back(link_note);
        pedal_prev = link_pitch;
        ++links_placed;
      }
    }
    prev_root = root_midi;
  }
  // The final cadence contract is structural V -> I.  The generic
  // anti-parallel substitution above may replace the penultimate bar's last
  // root with another chord tone; restore the dominant root on the exact
  // approach beat. The restoration answers the cadence contract only -- it is
  // not a licence for perfect motion here, which is heard at a cadence exactly
  // as it is anywhere else. Any parallel this restoration re-opens is repaired
  // in the middle voice by the closing-region pass at the end of this builder,
  // the one voice of the three that is free to move at the cadence.
  const Tick final_approach_tick = static_cast<Tick>(bars - 1) * kTicksPerBar - kTicksPerBeat;
  for (MaterialNote& note : v2.notes) {
    if (note.start_tick != final_approach_tick)
      continue;
    int dominant = static_cast<int>(note.pitch);
    while (dominant % 12 != 7)
      --dominant;
    if (dominant < kPedalFloor)
      dominant += 12;
    note.pitch = static_cast<std::uint8_t>(std::clamp(dominant, kPedalFloor, kPedalCeil));
    break;
  }
  out.material.trio_voices.push_back(std::move(v2));

  // VoicePlan: one TrioVoiceCarrier span per voice over the whole piece.
  out.harmony.voice_crossing_policy = VoiceCrossingPolicy::AllowTrioUpperMomentary;
  out.voice_plan.num_voices = 3;
  for (VoiceId voice = 0; voice < 3; ++voice) {
    Span span;
    span.id = static_cast<SpanId>(voice);
    span.start_tick = 0;
    span.end_tick = static_cast<Tick>(bars) * kTicksPerBar;
    span.voice = voice;
    span.intent = VoiceIntent::TrioVoiceCarrier;
    span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
    out.voice_plan.spans.push_back(span);
  }

  // Cadential suspension in the middle manual, prepared on the final beat
  // before a closing-region downbeat and resolved on beat two before a
  // one-beat rest. Search backward from the antepenultimate bar and derive the
  // carrier from the actual outer voices so every seed/mode keeps the trio's
  // register order.
  {
    const auto& upper = out.material.trio_voices[0].notes;
    const auto& middle = out.material.trio_voices[1].notes;
    auto& bass = out.material.trio_voices[2].notes;
    // The figure's three tones are designed from register and dissonance alone,
    // and the closing repair below deliberately steps over the window they
    // occupy, so nothing downstream ever re-reads them against the outer voices.
    const auto strict_against = [&](const std::vector<MaterialNote>& outer, int line_prev, int cand,
                                    Tick tick) {
      const int other_prev = onset_before(outer, tick);
      const int other_curr = soundingMaterialPitch(outer, tick);
      return other_prev >= 0 && other_curr >= 0 &&
             formsStrictPerfectParallel(line_prev, cand, other_prev, other_curr);
    };
    bool installed = false;
    for (int bar_offset : {3, 4, 5, 6, 7}) {
      if (installed || bars <= bar_offset)
        break;
      const Tick suspension_tick = static_cast<Tick>(bars - bar_offset) * kTicksPerBar;
      const Tick preparation_tick = suspension_tick - kTicksPerBeat;
      const Tick resolution_tick = suspension_tick + kTicksPerBeat;
      const Tick carrier_resume_tick = resolution_tick + 2 * kTicksPerBeat;
      const int held_bass = soundingMaterialPitch(bass, suspension_tick);
      const int bass_prep = soundingMaterialPitch(bass, preparation_tick);
      const int upper_prep = soundingMaterialPitch(upper, preparation_tick);
      const int upper_sus = soundingMaterialPitch(upper, suspension_tick);
      const int upper_res = soundingMaterialPitch(upper, resolution_tick);
      const int middle_before =
          soundingMaterialPitch(middle, preparation_tick > 0 ? preparation_tick - 1 : 0);
      const int middle_after = soundingMaterialPitch(middle, carrier_resume_tick);
      int upper_window_min = 127;
      for (const MaterialNote& note : upper) {
        if (note.start_tick < resolution_tick + kTicksPerBeat &&
            note.start_tick + note.duration > preparation_tick)
          upper_window_min = std::min(upper_window_min, static_cast<int>(note.pitch));
      }
      const int ceiling = std::min({upper_prep, upper_sus, upper_res, upper_window_min}) - 1;
      if (middle_before < 0 || middle_after < 0)
        continue;
      // The suspension replaces a short window inside an otherwise continuous
      // manual line, so it must be designed in the register the middle voice
      // actually occupies at the two splice points. Without this the design
      // search -- which walks the band downward and takes the highest formula
      // that fits -- returns a valid 7-6/4-3/9-8 an octave above the resuming
      // carrier, which is still an audible remote leap and is discarded, losing
      // the cadential suspension entirely.
      const int splice_lo = std::max(middle_before, middle_after) - 12;
      const int splice_hi = std::min(middle_before, middle_after) + 12;
      const int design_lo = std::max(std::max(bass_prep, held_bass) + 1, splice_lo);
      const int design_hi = std::min(ceiling, splice_hi);
      for (SuspensionType type :
           {SuspensionType::Sus7_6, SuspensionType::Sus4_3, SuspensionType::Sus9_8}) {
        SuspensionPattern suspension;
        if (!designUpperSuspension(
                type, preparation_tick, suspension_tick, resolution_tick,
                /*voice=*/1, static_cast<std::uint8_t>(bass_prep),
                static_cast<std::uint8_t>(held_bass), static_cast<std::uint8_t>(held_bass),
                static_cast<std::uint8_t>(upper_prep), static_cast<std::uint8_t>(upper_sus),
                static_cast<std::uint8_t>(upper_res),
                /*band_lo=*/design_lo, design_hi, mode, &suspension))
          continue;
        // Safety net for the register clamp above: the resolution steps one or
        // two semitones below the suspended tone, so it can still fall just
        // under the clamped floor.
        if (std::abs(static_cast<int>(suspension.preparation_pitch) - middle_before) > 12 ||
            std::abs(static_cast<int>(suspension.resolution_pitch) - middle_after) > 12) {
          continue;
        }
        const int prep_pitch = static_cast<int>(suspension.preparation_pitch);
        const int sus_pitch = static_cast<int>(suspension.suspension_pitch);
        const int res_pitch = static_cast<int>(suspension.resolution_pitch);
        // Reject a formula whose own onsets would ship a true parallel; the next
        // suspension type (or the next bar offset) is tried instead. The bass is
        // exempt at the resolution: the rewrite immediately below pins it to the
        // suspended bar's tone AND flattens the beat that leads into it, so it
        // moves obliquely there by construction.
        if (strict_against(upper, middle_before, prep_pitch, preparation_tick) ||
            strict_against(bass, middle_before, prep_pitch, preparation_tick) ||
            strict_against(upper, prep_pitch, sus_pitch, suspension_tick) ||
            strict_against(bass, prep_pitch, sus_pitch, suspension_tick) ||
            strict_against(upper, sus_pitch, res_pitch, resolution_tick)) {
          continue;
        }
        // The rewrite also changes what the bass LEAVES the resolution beat
        // with: the pedal chose its next tone against the tone the rewrite
        // replaces, and its own guard has long since run. Only the top voice
        // needs re-reading there -- the middle is inside the figure's window,
        // where it either rests or holds its resolution, and an oblique voice is
        // never in a parallel.
        const Tick departure_tick = resolution_tick + kTicksPerBeat;
        const int bass_departure = soundingMaterialPitch(bass, departure_tick);
        if (bass_departure >= 0 &&
            strict_against(upper, held_bass, bass_departure, departure_tick)) {
          continue;
        }
        // The bass is oblique into the resolution only if it also LEAVES the
        // suspended beat on the tone it entered on. A walking pedal splits that
        // beat into a quarter and a connecting eighth, and the eighth steps
        // away, so the pitch immediately before the resolution is the
        // connector's rather than the held one and the exemption above would be
        // resting on a motion that is not there. Collapse the pair back into the
        // plain quarter the walk writes wherever it cannot fill a gap; because
        // the resolution is one beat after the suspension, that quarter is
        // held_bass by construction.
        for (std::size_t idx = 0; idx + 1 < bass.size(); ++idx) {
          if (bass[idx].start_tick != suspension_tick || bass[idx].duration >= kTicksPerBeat)
            continue;
          if (bass[idx + 1].start_tick >= resolution_tick)
            break;
          bass[idx].duration = kTicksPerBeat;
          bass.erase(bass.begin() + static_cast<std::ptrdiff_t>(idx) + 1);
          break;
        }
        for (std::size_t idx = 0; idx < bass.size(); ++idx) {
          if (bass[idx].start_tick != resolution_tick)
            continue;
          bass[idx].pitch = static_cast<std::uint8_t>(held_bass);
          // The rewritten beat no longer carries the tone its connecting eighth
          // was derived from, so that eighth would be stepping out of a pitch
          // that is not there any more. Collapse the pair back into the plain
          // quarter the pedal writes wherever the walk cannot fill the gap.
          if (bass[idx].duration < kTicksPerBeat && idx + 1 < bass.size() &&
              bass[idx + 1].start_tick < resolution_tick + kTicksPerBeat) {
            bass[idx].duration = kTicksPerBeat;
            bass.erase(bass.begin() + static_cast<std::ptrdiff_t>(idx) + 1);
          }
          break;
        }
        installed = installSuspensionCarrier(out.material, out.voice_plan, suspension);
        if (installed) {
          // The figure may sit up to seven bars from the end, well before the
          // landing the closing repair below already covers, and it pins the
          // bass under its own resolution. The tone the middle voice resumes its
          // carrier on is therefore judged against a bass that no longer reads
          // as it did when that tone was written, so open the repair window at
          // the disturbance rather than at the landing.
          v1_repair_start = std::min(v1_repair_start, resolution_tick);
        }
        break;
      }
    }
  }

  // Closing-region parallel repair on the middle manual. Inside the bar loop V1
  // is judged against the V0 line as it stood then, but two later passes rewrite
  // exactly what it was judged against: the pre-landing thread plus
  // appendCadentialLanding replace V0 from `v1_repair_start` to the end of the
  // piece, and the cadence contract restores the pedal's dominant root on the
  // approach beat after that voice's own guard had chosen another chord tone.
  // Nothing re-reads the result, so the earlier verdicts no longer describe the
  // shipped notes. Re-judge V1 against the FINAL content of both outer voices
  // over that window and move it off any surviving perfect parallel: the
  // soprano's cadential close and the bass's structural V -> I are fixed, so the
  // middle voice is the one that can travel. The same pass answers the onsets
  // anywhere in the piece where the pedal's own guard was boxed in, for the same
  // reason: at those the middle voice is again the only one still free.
  {
    const std::vector<MaterialNote>& upper = out.material.trio_voices[0].notes;
    const std::vector<MaterialNote>& pedal = out.material.trio_voices[2].notes;
    std::vector<MaterialNote>& middle = out.material.trio_voices[1].notes;
    // Same sampling as the in-loop guard: the outer voice's last onset before
    // this one and whatever it sounds at it, which is the pair the union-onset
    // reading (and the ear) takes. An outer voice that does not move between the
    // two samples is oblique and can never be in a parallel.
    //
    // Ranked for the reason the pedal guard is: inside the closing region the
    // soprano's cadence and the bass's V -> I are both fixed, so the triad tones
    // this voice may take here are frequently all at least a hidden perfect
    // against one of them. A boolean reject then finds nothing better than the
    // design tone and ships the true parallel it was called to remove.
    constexpr int kMiddleClean = 0;
    constexpr int kMiddleHidden = 1;
    constexpr int kMiddleParallel = 2;
    const auto outer_fault_rank = [&](int line_prev, int cand, Tick tick) {
      if (line_prev < 0)
        return kMiddleClean;
      int worst = kMiddleClean;
      const std::vector<MaterialNote>* outers[2] = {&upper, &pedal};
      for (const std::vector<MaterialNote>* outer : outers) {
        const int other_prev = onset_before(*outer, tick);
        const int other_curr = sounding_pitch(*outer, tick);
        if (other_prev < 0 || other_curr < 0)
          continue;
        if (formsStrictPerfectParallel(line_prev, cand, other_prev, other_curr))
          return kMiddleParallel;
        if (formsPerfectParallel(line_prev, cand, other_prev, other_curr))
          worst = std::max(worst, kMiddleHidden);
      }
      return worst;
    };
    // The cadential suspension installed above hands its three tones to a
    // SuspensionCarrier span, which replays them from the material pattern; the
    // authored middle-voice notes inside that window are not what ships, and
    // the tone the resumed carrier is heard to follow is the pattern's
    // resolution. Step over the window and continue the chain from that
    // resolution, so the first repaired note after the splice is judged against
    // what actually sounds.
    const auto suspension_at = [&](Tick tick) -> const SuspensionPattern* {
      for (const SuspensionPattern& pattern : out.material.suspension_patterns) {
        if (pattern.voice != 1)
          continue;
        if (tick >= pattern.preparation_tick && tick < pattern.resolution_tick + 2 * kTicksPerBeat)
          return &pattern;
      }
      return nullptr;
    };
    int prev_pitch = -1;  // the middle voice's previous SHIPPED pitch.
    for (std::size_t idx = 0; idx < middle.size(); ++idx) {
      MaterialNote& note = middle[idx];
      const SuspensionPattern* suspended = suspension_at(note.start_tick);
      if (suspended != nullptr) {
        prev_pitch = static_cast<int>(suspended->resolution_pitch);
        continue;
      }
      const int design = static_cast<int>(note.pitch);
      const bool pedal_boxed = std::find(pedal_boxed_ticks.begin(), pedal_boxed_ticks.end(),
                                         note.start_tick) != pedal_boxed_ticks.end();
      if ((note.start_tick < v1_repair_start && !pedal_boxed) ||
          outer_fault_rank(prev_pitch, design, note.start_tick) == kMiddleClean) {
        prev_pitch = design;
        continue;
      }
      const std::size_t bar_index =
          std::min(static_cast<std::size_t>(note.start_tick / kTicksPerBar), chords.size() - 1);
      const ChordSpec& bar_chord = chords[bar_index];
      const int root_pc = bar_chord.root_pc % 12;
      const int third_semi = bar_chord.minor ? 3 : 4;
      const int triad_pc[3] = {root_pc, (root_pc + third_semi) % 12, (root_pc + 7) % 12};
      const int next_pitch =
          (idx + 1 < middle.size()) ? static_cast<int>(middle[idx + 1].pitch) : -1;
      const Tick next_tick = (idx + 1 < middle.size()) ? middle[idx + 1].start_tick : 0;
      const int upper_now = sounding_pitch(upper, note.start_tick);
      const int pedal_now = sounding_pitch(pedal, note.start_tick);
      // How badly a tone reads at BOTH ends of the onset: the motion into it and
      // the motion out of it (a repair that hands the parallel to the following
      // onset has repaired nothing).
      const auto both_ends = [&](int cand) {
        int rank = outer_fault_rank(prev_pitch, cand, note.start_tick);
        if (next_pitch >= 0)
          rank = std::max(rank, outer_fault_rank(cand, next_pitch, next_tick));
        return rank;
      };
      // Nearest in-band triad tone that reads strictly better than the design
      // one, preferring a fully clean rung before settling for a hidden perfect,
      // and keeping the pedal < middle < upper order the trio is read in at this
      // onset. When nothing improves on it, the design tone stands: one
      // consonant parallel beats a non-chord tone in the middle of a cadence.
      const int design_rank = both_ends(design);
      int best = design;
      for (int accept = kMiddleClean; accept < design_rank && best == design; ++accept) {
        int best_dist = 1 << 20;
        for (int tone = 0; tone < 3; ++tone) {
          const int low = kV1BandLo + (((triad_pc[tone] - kV1BandLo) % 12) + 12) % 12;
          for (int cand = low; cand <= kV1BandHi; cand += 12) {
            if (cand == design || (upper_now >= 0 && cand >= upper_now) ||
                (pedal_now >= 0 && cand <= pedal_now))
              continue;
            if (both_ends(cand) > accept)
              continue;
            const int dist = std::abs(cand - design);
            if (dist < best_dist) {
              best_dist = dist;
              best = cand;
            }
          }
        }
      }
      note.pitch = static_cast<std::uint8_t>(best);
      prev_pitch = best;
    }
  }

  return out;
}

}  // namespace bach::composer
