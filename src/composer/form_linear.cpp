#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/cadenced_progression.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/ornament_pass.h"
#include "composer/rule_helpers.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

using detail::ChordSpec;
using detail::Mode;

// ---------------------------------------------------------------------------
// CelloPrelude (BWV1007 Prelude style): a single monophonic running line laid
// out one bar at a time.
//
// The builder is a pure function of (seed, indices): no RNG, deterministic per
// (seed, mode, character, bars). It extends the proven CelloPrelude layout to
// an arbitrary snapped length (8..128 bars), shaping figure density / register
// / cadence from the ResolvedRequest arc.
//
// Progression: one chord per bar from buildCadencedProgression (4-bar blocks
// cycling the diatonic catalog, V -> I close). Figure: each bar is sixteen 16th
// notes (group_size = 4 -> four implicit cells per bar) forming a COMPACT
// scalar wave -- the line opens on a chord tone, runs scalewise up the diatonic
// scale and folds back down, predominantly by step. This is the CelloPrelude/16/17
// note language (a stepwise-dominant running figuration touching chord tones)
// rather than wide bass-fifth-third broken chords: the BWV1007 prelude keeps a
// COMPACT voicing where the line moves mostly by step or small skip between
// neighbouring sixteenths, so the model-scorer's melodic-interval cost stays
// low (large_leap_ratio ~ 0, no remote leaps).
//
// The validator reconstructs two implicit voices from the cell (per-beat) min
// (bass stream) and max (top stream). For a scalar wave both streams move
// stepwise between cells, so implicit_voice_counterpoint stays clean; the cell
// span is never a clean P5/P8 held in parallel motion, so
// arpeggio_no_parallel_perfect never fires. The wave amplitude (how many scale
// degrees it climbs before folding) follows the arc density tier, and the
// register center lifts toward the climax.
// ---------------------------------------------------------------------------
HarnessFixture buildCelloPreludeForm(const ResolvedRequest& req) {
  HarnessFixture out;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const Tick kSix = kTicksPerBeat / 4;  // sixteenth note.
  constexpr int kGroup = 4;             // four sixteenths per implicit cell.
  constexpr int kNotesPerBar = 16;      // sixteen sixteenths per bar.

  const std::vector<ChordSpec> chords = buildCadencedProgression(bars, req.seed, mode,
                                                                 /*cello_implicit_safe=*/true);
  writeBarChords(out, chords, mode);

  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  out.material.arpeggio_template.group_size = kGroup;

  // Walk under the same local dominant policy used by the other form builders:
  // natural minor away from V, with the raised sixth/leading tone admitted only
  // while the active harmony is the major dominant. This removes the former
  // cello-only fixed harmonic-minor collection.
  auto walk = [&](int midi, int steps, bool harmonic) {
    const detail::ChordSpec local_chord =
        harmonic ? detail::ChordSpec{7, false} : detail::ChordSpec{0, true};
    int cur = midi;
    const int direction = steps < 0 ? -1 : 1;
    for (int step = 0; step < std::abs(steps); ++step) {
      cur = detail::melodicScaleStep(cur, direction, mode, &local_chord);
    }
    return cur;
  };

  // Each bar is a low-amplitude scalar OSCILLATION: the line repeatedly climbs
  // `amp` scale degrees and folds back, so over the 16 sixteenths it traces
  // several small there-and-back arcs around the bar anchor. The amplitude is
  // deliberately small (a third / a fourth) so the four per-beat cells' bass/top
  // (min/max) streams stay within ONE scale step of the anchor: the implicit
  // bass and top streams are nearly flat within the bar, so
  // implicit_voice_counterpoint never sees a tritone / major-seventh /
  // augmented-second cell-to-cell leap (which a wide monotonic run's
  // fast-climbing cell minima would otherwise produce at the diatonic B-F
  // tritone boundary). The motion is still wholly stepwise note-to-note (the
  // CelloPrelude/16/17 compact note language), so the model scorer's
  // large_leap_ratio stays ~0.

  // Register window low for a bar (the floor its anchor is realized above),
  // lifted an octave at the climax cycle. Shared by the per-bar walk and the
  // cadence-register simulation below so both agree on the octave.
  auto barWindowLo = [&](int bar_idx) {
    const std::size_t cyc = static_cast<std::size_t>(bar_idx / 4);
    const ArcPoint point = req.arc(std::min(cyc, req.cycle_count - 1));
    return 43 + ((point.register_shift >= 6) ? 12 : 0);
  };
  // Chord tone of `bar_idx` NEAREST `prev`, realized in that bar's window (the
  // same voice-leading rule the per-bar loop applies). Kept as a reusable
  // helper so the cadence landing can extend the anchor chain through the
  // final V and I bars without re-emitting them.
  auto nearestAnchorForBar = [&](int bar_idx, int prev) {
    const int root_pc = chords[static_cast<std::size_t>(bar_idx)].root_pc % 12;
    const int third_semi = chords[static_cast<std::size_t>(bar_idx)].minor ? 3 : 4;
    const int triad_pc[3] = {root_pc, (root_pc + third_semi) % 12, (root_pc + 7) % 12};
    const int wlo = barWindowLo(bar_idx);
    int best = wlo;
    int best_dist = 1 << 20;
    for (int tone = 0; tone < 3; ++tone) {
      int cand = wlo + (((triad_pc[tone] - wlo) % 12) + 12) % 12;
      while (cand + 12 - prev <= prev - cand)  // climb to the nearest octave.
        cand += 12;
      const int dist = std::abs(cand - prev);
      if (dist < best_dist) {
        best_dist = dist;
        best = cand;
      }
    }
    return best;
  };
  // The cadence-landing tonic register: extend the voice-leading anchor chain
  // through the appended (not emitted) final V and I bars from the last
  // figuration bar's closing anchor, then take the tonic C nearest the tonic
  // bar's anchor. This is the register the pre-landing chain would have reached
  // had the two cadence bars run, so the held tonic voice-leads from the line's
  // close rather than snapping to the figuration bar's own octave.
  auto landingTonicFor = [&](int final_bar_anchor) {
    const int anchor_v = nearestAnchorForBar(bars - 2, final_bar_anchor);
    const int anchor_i = nearestAnchorForBar(bars - 1, anchor_v);
    int tonic = 48;  // tonic C nearest the tonic bar's anchor.
    while (tonic + 12 - anchor_i <= anchor_i - tonic)
      tonic += 12;
    return tonic;
  };

  // Voice-led anchor: each bar opens on the chord tone NEAREST the previous
  // bar's closing register, so the implicit bass/top streams never jump an
  // octave at the bar boundary (the failure mode of a fixed per-bar register).
  // Seeded in the cello's tenor range (~C3).
  int prev_anchor = 48;
  // Implicit bass/top extremes of the previous bar's final cell (-1 before
  // the first bar), used to vet each bar figure across the bar seam.
  int prev_cell_lo = -1;
  int prev_cell_hi = -1;
  // Stop two bars short: the final two bars are the cadential landing appended
  // below (a held leading tone then the tonic), not sixteenth figuration. The
  // last iteration (bar == bars - 3) is the final audible figuration bar, so
  // its closing anchor is the register the landing voice-leads from.
  for (int bar = 0; bar < bars - 2; ++bar) {
    const std::size_t cycle = static_cast<std::size_t>(bar / 4);
    const ArcPoint arc = req.arc(std::min(cycle, req.cycle_count - 1));

    // Effective density tier after the character density bias (clamped 0..3). The
    // tier widens the oscillation slightly toward the climax (a third at the calm
    // tiers, a fourth at the peak) and the register center lifts an octave at the
    // climax; both shapes keep every per-cell min/max within one scale step of
    // the anchor, so the implicit-voice streams stay leap-free at any tier.
    int tier = static_cast<int>(arc.density_tier) + profile.density_bias;
    tier = std::max(0, std::min(3, tier));
    // Figure reach in scale degrees: the per-cell oscillation climbs `reach`
    // degrees above the anchor before folding back. A third (2) at the calm
    // tiers, a fourth (3) near the climax, so the line opens up toward the peak.
    // Every cell uses the SAME reach, so the implicit bass/top streams stay
    // constant within the bar regardless of reach.
    const int reach = (tier >= 2) ? 3 : 2;

    const int root_pc = chords[static_cast<std::size_t>(bar)].root_pc % 12;
    const bool chord_minor = chords[static_cast<std::size_t>(bar)].minor;
    const int third_semi = chord_minor ? 3 : 4;
    const int triad_pc[3] = {root_pc, (root_pc + third_semi) % 12, (root_pc + 7) % 12};
    const bool harmonic = (mode == Mode::Minor) && (root_pc == 7);  // V wants the leading tone.

    // Register window: the cello's tenor range, lifted by a whole octave at the
    // climax so the line brightens toward the peak. The window low is the floor
    // the anchor is realized above; quantizing the lift to an octave keeps every
    // pitch class diatonic.
    const int oct_shift = (arc.register_shift >= 6) ? 12 : 0;
    const int window_lo = 43 + oct_shift;  // ~G2 (+oct at climax).

    // Anchor candidates = the three triad tones realized in this bar's register
    // window, each climbed to the octave nearest prev_anchor and ordered
    // nearest-first by distance to prev_anchor. Mid-piece only the nearest is
    // used (bass/top streams move by a small interval at every boundary); the
    // final figuration bar tries them in order so it can voice-lead into the
    // cadential landing without a forbidden seam.
    int cand_anchors[3];
    int cand_dist[3];
    for (int tone = 0; tone < 3; ++tone) {
      int cand = window_lo + (((triad_pc[tone] - window_lo) % 12) + 12) % 12;
      while (cand + 12 - prev_anchor <= prev_anchor - cand)  // climb to the nearest octave.
        cand += 12;
      cand_anchors[tone] = cand;
      cand_dist[tone] = std::abs(cand - prev_anchor);
    }
    int anchor_order[3] = {0, 1, 2};
    for (int aidx = 0; aidx < 3; ++aidx) {
      for (int bidx = aidx + 1; bidx < 3; ++bidx) {
        if (cand_dist[anchor_order[bidx]] < cand_dist[anchor_order[aidx]]) {
          const int tmp = anchor_order[aidx];
          anchor_order[aidx] = anchor_order[bidx];
          anchor_order[bidx] = tmp;
        }
      }
    }
    int anchor = cand_anchors[anchor_order[0]];
    // Forward-seam pitches into the cadential landing, set only for the final
    // figuration bar's anchor candidate under test (< 0 skips the seam check).
    int landing_lo = -1;  // the landing leading tone (implicit bass of the trill).
    int landing_hi = -1;  // the landing tonic (implicit top of the trill).

    // The bar figure rotates per BAR among three textures the real solo-cello
    // prelude mixes: the small oscillation cell, a scale-run triangle, and a
    // pedal-point bariolage. A single oscillation cell looped all piece long
    // concentrated the interval-bigram surface on a handful of pendulum
    // bigrams (x|-x); the corpus bigram mass lives on step chains and varied
    // figures, so the rotation is what restores it. Each candidate's implicit
    // bass/top streams (per-cell min/max, including the boundary to the
    // previous bar's last cell) are vetted against the same forbidden-leap
    // and parallel-perfect predicates the validator applies; an unsafe figure
    // falls through to the next, and the oscillation cell -- whose streams
    // are constant within the bar by construction -- is the final fallback.
    static constexpr int kCellShapes[3][4] = {
        {0, 1, -1, 1},   // rising arc: anchor, +1, reach, +1 (-1 marks reach).
        {0, -1, 1, -1},  // fold-back: anchor, reach, +1, reach.
        {0, 1, 0, -1},   // under-then-over: anchor, +1, anchor, reach.
    };
    auto oscillation_bar = [&](std::array<int, 16>& p) {
      // The shape rotates per CELL, not only per cycle: all three shapes share
      // the same tone set {anchor, +1, reach}, so every cell's implicit
      // min/max extremes are identical regardless of shape and the validator
      // streams are unchanged -- but one shape looped four times per bar
      // stamps the same three-four interval bigrams twelve times, which is
      // exactly the pendulum concentration the corpus bigram surface never
      // reaches.
      for (int cell = 0; cell < 4; ++cell) {
        // The rotation stays in unsigned space: casting the seed to int first
        // makes every seed at or above 2^31 negative, and a negative remainder
        // indexes outside the shape table.
        const int* shape = kCellShapes[(req.seed + static_cast<std::uint32_t>(cycle) +
                                        static_cast<std::uint32_t>(cell)) %
                                       3u];
        for (int idx = 0; idx < 4; ++idx) {
          const int degree = shape[idx] < 0 ? reach : shape[idx];
          p[static_cast<std::size_t>(cell * 4 + idx)] = walk(anchor, degree, harmonic);
        }
      }
    };
    auto run_triangle_bar = [&](std::array<int, 16>& p) {
      // Two half-bar scale-run arches (climb four steps, fold back), the
      // second shifted one scale degree above the first (order swapped on odd
      // bars). The original full-bar arch climbed seven degrees, moving the
      // implicit cell extremes four degrees between cells -- enough to trip
      // the forbidden-leap vet on most anchors, so the figure was silently
      // rejected on two thirds of its turns and the rotation collapsed back
      // to the pendulum figures. Half-bar arches keep the extremes within two
      // degrees (reliably safe) while still supplying the mixed ascending /
      // descending step chains the corpus bigram mass lives on.
      int arch_a[8];
      int arch_b[8];
      for (int idx = 0; idx < 8; ++idx) {
        const int degree = (idx < 5) ? idx : 8 - idx;  // 0 1 2 3 4 3 2 1
        arch_a[idx] = walk(anchor, degree, harmonic);
        arch_b[idx] = walk(anchor, degree + 1, harmonic);
      }
      const bool swap_halves = ((bar + static_cast<int>(cycle)) & 1) != 0;
      for (int idx = 0; idx < 8; ++idx) {
        p[static_cast<std::size_t>(idx)] = swap_halves ? arch_b[idx] : arch_a[idx];
        p[static_cast<std::size_t>(8 + idx)] = swap_halves ? arch_a[idx] : arch_b[idx];
      }
    };
    auto broken_thirds_bar = [&](std::array<int, 16>& p) {
      // Broken-third chains (c-e-d-f): each cell plays degrees
      // (s, s+2, s+1, s+3) with the cell start s drifting 0,1,2,1 across the
      // bar. This is the corpus's third-skip vocabulary -- the (third, -step)
      // / (step, -third) bigram families -- which the three stepwise figures
      // above cannot produce; without it the line's interval mass sits almost
      // entirely on seconds while the reference keeps the majority of its
      // transitions on skips and leaps. Cell extremes are (s, s+3), so the
      // implicit streams move at most one degree per cell seam.
      static constexpr int kStarts[4] = {0, 1, 2, 1};
      for (int cell = 0; cell < 4; ++cell) {
        const int s = kStarts[cell];
        const int degs[4] = {s, s + 2, s + 1, s + 3};
        for (int idx = 0; idx < 4; ++idx) {
          p[static_cast<std::size_t>(cell * 4 + idx)] = walk(anchor, degs[idx], harmonic);
        }
      }
    };
    auto pedal_bariolage_bar = [&](std::array<int, 16>& p) {
      // BWV1007-style bariolage: the anchor as a constant pedal under an
      // upper tone that walks down one scale degree per beat. The implicit
      // bass stream is the pedal (constant); the top stream is stepwise.
      for (int cell = 0; cell < 4; ++cell) {
        const int top = walk(anchor, 6 - cell, harmonic);
        const int under = walk(anchor, 5 - cell, harmonic);
        p[static_cast<std::size_t>(cell * 4 + 0)] = anchor;
        p[static_cast<std::size_t>(cell * 4 + 1)] = top;
        p[static_cast<std::size_t>(cell * 4 + 2)] = under;
        p[static_cast<std::size_t>(cell * 4 + 3)] = top;
      }
    };
    // Vet a candidate bar exactly the way the validator will read it: per-cell
    // min/max streams, adjacent-cell forbidden leaps (including the seam from
    // the previous bar's last cell), implicit parallel perfects, and the
    // cello's practical register ceiling.
    auto cells_safe = [&](const std::array<int, 16>& p) {
      int lo[4];
      int hi[4];
      for (int cell = 0; cell < 4; ++cell) {
        lo[cell] = 127;
        hi[cell] = 0;
        for (int k = 0; k < 4; ++k) {
          const int pitch = p[static_cast<std::size_t>(cell * 4 + k)];
          lo[cell] = std::min(lo[cell], pitch);
          hi[cell] = std::max(hi[cell], pitch);
        }
        if (lo[cell] < 36 || hi[cell] > 76) {
          return false;
        }
      }
      int pl = prev_cell_lo;
      int ph = prev_cell_hi;
      for (int cell = 0; cell < 4; ++cell) {
        if (pl >= 0) {
          const Tick from_tick = cell == 0 ? static_cast<Tick>(bar - 1) * kTicksPerBar
                                           : static_cast<Tick>(bar) * kTicksPerBar +
                                                 static_cast<Tick>(cell - 1) * kTicksPerBeat;
          const Tick to_tick =
              static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(cell) * kTicksPerBeat;
          if (rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(pl),
                                                   static_cast<std::uint8_t>(lo[cell]), out.harmony,
                                                   from_tick, to_tick) ||
              rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(ph),
                                                   static_cast<std::uint8_t>(hi[cell]), out.harmony,
                                                   from_tick, to_tick)) {
            return false;
          }
          if (isParallelPerfectMotion(ph, hi[cell], pl, lo[cell]))
            return false;
        }
        pl = lo[cell];
        ph = hi[cell];
      }
      // Forward seam into the cadential landing (final figuration bar only). The
      // landing writes a full-bar leading tone then the tonic; the ornament pass
      // expands the held leading tone into a cadential trill. The first landing
      // cell the validator reconstructs has its implicit bass at the trill's
      // opening pitch -- the lower turn note (tonic - 3) or the leading tone
      // (tonic - 1), captured here as landing_lo -- and its top at the tonic
      // (landing_hi). The last figuration cell's bass/top extremes must voice-
      // lead into them without a forbidden leap; from a C bass the out-of-scale
      // sixth turn note is an augmented second, exactly the seam this rejects.
      if (landing_lo >= 0) {
        const Tick from_tick = static_cast<Tick>(bar) * kTicksPerBar + 3 * kTicksPerBeat;
        const Tick to_tick = static_cast<Tick>(bar + 1) * kTicksPerBar;
        if (rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(lo[3]),
                                                 static_cast<std::uint8_t>(landing_lo), out.harmony,
                                                 from_tick, to_tick) ||
            rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(hi[3]),
                                                 static_cast<std::uint8_t>(landing_hi), out.harmony,
                                                 from_tick, to_tick)) {
          return false;
        }
      }
      return true;
    };
    std::array<int, 16> pitches{};
    bool placed = false;
    // The figure preference walks with bar AND cycle: a plain bar % 4 stride
    // is 4-periodic and would hand every cycle's first bar (the 4-bar grid)
    // the same figure, freezing the cycle-opening contour the rotation
    // exists to vary.
    // Unsigned remainder for the same reason as the cell rotation above: a
    // negative preference would leave every figure branch unmatched.
    const int pref = static_cast<int>(
        (req.seed + static_cast<std::uint32_t>(bar) + static_cast<std::uint32_t>(cycle)) % 4u);
    auto try_figures = [&]() {
      for (int attempt = 0; attempt < 4; ++attempt) {
        switch ((pref + attempt) % 4) {
          case 0:
            oscillation_bar(pitches);
            break;
          case 1:
            run_triangle_bar(pitches);
            break;
          case 2:
            broken_thirds_bar(pitches);
            break;
          default:
            pedal_bariolage_bar(pitches);
            break;
        }
        if (cells_safe(pitches))
          return true;
      }
      return false;
    };
    if (bar == bars - 3) {
      // Final figuration bar: enumerate the realized triad-tone anchors
      // nearest-first, trying the four figures per anchor, and take the first
      // pair whose closing cell voice-leads into the cadential landing without
      // a forbidden seam. From the harmonic-minor sixth every figure's seam
      // into the landing leading tone is an augmented second, so a nearer
      // anchor alone cannot always clear it -- a farther chord tone can.
      // The trill opens on the lower turn note (tonic - 3) or the leading tone
      // (tonic - 1); the seam must be vetted against the one the ornament pass
      // will pick for this piece.
      // The landing's held leading tone becomes a cadential trill whose opening
      // the ornament pass keys to placementHash(seed, bar, voice). It sits in
      // the penultimate (dominant) bar -- bar index bars - 2 -- on the solo flow
      // line (voice 0), so vet the closing seam against that exact opening.
      const VoiceId solo_flow_voice = 0;
      const bool opens_on_turn = cadenceTrillOpensVonUnten(req.seed, bars - 2, solo_flow_voice);
      for (int oidx = 0; oidx < 3 && !placed; ++oidx) {
        anchor = cand_anchors[anchor_order[oidx]];
        const int final_tonic = landingTonicFor(anchor);
        landing_hi = final_tonic;
        landing_lo = final_tonic - (opens_on_turn ? 3 : 1);
        placed = try_figures();
      }
      if (!placed)
        anchor = cand_anchors[anchor_order[0]];  // restore the nearest for the fallback.
    } else {
      placed = try_figures();
    }
    if (!placed) {
      oscillation_bar(pitches);  // constant-stream fallback (prior behavior).
    }
    for (int slot = 0; slot < kNotesPerBar; ++slot) {
      MaterialNote mn;
      mn.start_tick = static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(slot) * kSix;
      mn.duration = kSix;
      mn.pitch = static_cast<std::uint8_t>(pitches[static_cast<std::size_t>(slot)]);
      out.material.arpeggio_template.notes.push_back(mn);
    }
    prev_cell_lo = std::min({pitches[12], pitches[13], pitches[14], pitches[15]});
    prev_cell_hi = std::max({pitches[12], pitches[13], pitches[14], pitches[15]});
    prev_anchor = anchor;  // voice-lead the next bar from this bar's anchor.
  }

  // Cadential landing: the final two bars stop the sixteenth flow with a
  // full-bar leading tone over the design-valued V bar (the long cadential
  // trill -- the only ornament the solo line takes) resolving to a full-bar
  // tonic. No approach run: the two held notes form only a dropped partial
  // cell for the implicit-voice analysis, so the cell streams end on the last
  // sixteenth bar's extremes without a seam leap. The tonic register extends
  // the anchor chain through the final V and I bars (the same value the final
  // figuration bar vetted its closing seam against), so the landing does not
  // leap and matches the seam the figuration bar was chosen to satisfy.
  {
    const int final_tonic = landingTonicFor(prev_anchor);
    appendCompactCadentialLanding(out.material.arpeggio_template.notes,
                                  static_cast<Tick>(bars - 2) * kTicksPerBar, 2 * kTicksPerBar,
                                  final_tonic - 1, final_tonic);
  }

  // VoicePlan: one ArpeggioFlow span covering the whole piece on voice 0.
  out.voice_plan.num_voices = 1;
  Span span;
  span.id = 0;
  span.start_tick = 0;
  span.end_tick = static_cast<Tick>(bars) * kTicksPerBar;
  span.voice = 0;
  span.intent = VoiceIntent::ArpeggioFlow;
  span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
  out.voice_plan.spans.push_back(span);

  return out;
}

}  // namespace bach::composer
