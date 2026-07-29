#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/minor_material.h"
#include "composer/ornament_pass.h"
#include "composer/rule_helpers.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Per-bar linear forms: cello prelude (monophonic arpeggio flow) and trio
// sonata (three independent voices).
//
// Both builders are pure functions of (seed, indices): no RNG, deterministic
// per (seed, mode, character, bars). They extend the proven CelloPrelude (cello)
// and TrioSonata (trio) layouts to an arbitrary snapped length (8..128 bars),
// shaping figure density / register / cadence from the ResolvedRequest arc.
// ---------------------------------------------------------------------------

namespace {

using detail::ChordSpec;
using detail::Mode;

// One whole-bar harmonic step: root pitch class + minor-quality flag. The
// per-bar progressions below are drawn from the shared diatonic catalogs
// (kHarmonyPatterns / kHarmonyPatternsMinor) so both forms speak the same
// harmonic language as the rest of the composer.
struct BarChord {
  std::uint8_t root_pc;
  bool minor;
};

// The cello's implicit-voice shapes need a dedicated migration before they can
// safely admit every natural-minor root. Keep that form-local admissibility
// guard while the shared validator now evaluates the real directional context.
constexpr bool inHarmonicMinor(int pc) {
  const int p = ((pc % 12) + 12) % 12;
  return p == 0 || p == 2 || p == 3 || p == 5 || p == 7 || p == 8 || p == 11;
}

// @brief Build an N-bar per-bar progression from the shared 4-chord harmony
//        catalogs, ending on a design-valued V -> I(i) cadence.
//
// Each 4-bar block cycles a catalog pattern selected by (seed, block) so
// successive blocks differ; the final two bars are overwritten with a half
// cadence (V) then the tonic (I / i, or a Picardy I in minor on the elected
// seeds), the perfect-authentic close every length lands on.
//
// @param bars Total bar count (>= 8).
// @param seed Piece seed (selects which catalog pattern each block uses).
// @param mode Major selects kHarmonyPatterns, Minor selects kHarmonyPatternsMinor.
// @return Per-bar chord list of length `bars`.
std::vector<BarChord> buildProgression(int bars, std::uint32_t seed, Mode mode,
                                       bool cello_implicit_safe = false) {
  const auto& catalog =
      (mode == Mode::Minor) ? detail::kHarmonyPatternsMinor : detail::kHarmonyPatterns;
  std::vector<std::size_t> admissible;
  for (std::size_t pat = 0; pat < catalog.size(); ++pat) {
    bool valid = true;
    if (cello_implicit_safe && mode == Mode::Minor) {
      for (const ChordSpec& spec : catalog[pat])
        valid = valid && inHarmonicMinor(spec.root_pc);
    }
    if (valid)
      admissible.push_back(pat);
  }
  if (admissible.empty())
    admissible.push_back(0);
  std::vector<BarChord> chords;
  chords.reserve(static_cast<std::size_t>(bars));
  for (int bar = 0; bar < bars; ++bar) {
    const int block = bar / 4;
    const std::size_t pat =
        admissible[(static_cast<std::size_t>(seed) + static_cast<std::size_t>(block)) %
                   admissible.size()];
    const ChordSpec& spec = catalog[pat][static_cast<std::size_t>(bar % 4)];
    chords.push_back({spec.root_pc, spec.minor});
  }
  // Design-valued final cadence: V (dominant, always major) then tonic. In
  // minor the tonic stays minor unless the seed elects a Picardy third.
  const bool tonic_minor = (mode == Mode::Minor) && !detail::usePicardy(seed);
  chords[static_cast<std::size_t>(bars - 2)] = {7, false};
  chords[static_cast<std::size_t>(bars - 1)] = {0, tonic_minor};
  return chords;
}

// @brief Emit one ChordEvent per bar into a HarmonicPlan from a progression.
// @param out Fixture whose harmony plan receives the chords.
// @param chords Per-bar progression.
// @param mode Selects the tonic minor flag for the plan.
void writeHarmony(HarnessFixture& out, const std::vector<BarChord>& chords, Mode mode) {
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = (mode == Mode::Minor);
  for (std::size_t bar = 0; bar < chords.size(); ++bar) {
    ChordEvent chord;
    chord.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
    chord.root_pc = chords[bar].root_pc;
    chord.quality = chords[bar].minor ? ChordQuality::Minor : ChordQuality::Major;
    out.harmony.chords.push_back(chord);
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// CelloPrelude (BWV1007 Prelude style): a single monophonic running line.
//
// Progression: one chord per bar from buildProgression (4-bar blocks cycling
// the diatonic catalog, V -> I close). Figure: each bar is sixteen sixteenth
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

  const std::vector<BarChord> chords = buildProgression(bars, req.seed, mode,
                                                        /*cello_implicit_safe=*/true);
  writeHarmony(out, chords, mode);

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
      // exactly the pendulum concentration the corpus bigram surface (top
      // entry only ~2.6%) never reaches.
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

// ---------------------------------------------------------------------------
// TrioSonata: three independent voices (V0 = RH/Great high, V1 = LH/Swell mid,
// V2 = Pedal low). V0/V1 are scalar-wave lines (one 16th line, one 8th line),
// V2 is a quarter-note root/fifth pedal. The defining technique is rhythmic
// independence: the three voices keep DISTINCT densities (16 / 8 / 4 notes per
// bar) so voice_independence_threshold passes comfortably.
//
// Progression: one chord per bar from buildProgression with an internal half
// cadence (V) at every 8-bar boundary's penultimate bar resolving to I on the
// boundary, and the design-valued final V -> I close. Voice-pair rotation: which
// upper voice carries the densest (16th) line rotates by 4-bar cycle (V0 then V1
// then V0 ...); registers stay banded (V0 high, V1 mid, V2 low) so swapping the
// density never crosses voices. The arc lifts the upper voices' register toward
// the climax. Noble character (prefer_dotted) gives V1 a dotted-quarter+eighth
// pattern at low tiers.
// ---------------------------------------------------------------------------
HarnessFixture buildTrioSonataForm(const ResolvedRequest& req) {
  HarnessFixture out;
  const int bars = static_cast<int>(req.bars);
  const Mode mode = req.mode;
  const Tick kEighth = kTicksPerBeat / 2;     // eighth note.
  const Tick kSixteenth = kTicksPerBeat / 4;  // sixteenth note.

  std::vector<BarChord> chords = buildProgression(bars, req.seed, mode);
  // Internal cadences every 8 bars: penultimate bar of each 8-bar group is V,
  // the boundary bar is I (the tonic landing). This shapes the long form into
  // clear 8-bar periods. The final two bars already hold the design cadence.
  for (int boundary = 8; boundary < bars; boundary += 8) {
    chords[static_cast<std::size_t>(boundary - 1)] = {7, false};  // V before the boundary.
    chords[static_cast<std::size_t>(boundary)] = {0, mode == Mode::Minor};  // I/i on the boundary.
  }
  writeHarmony(out, chords, mode);

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
                             int zig_dir, const std::vector<MaterialNote>* guard_line,
                             const ThemeToneRegistry* guard_registry, int& prev_emitted) {
    const BarChord& bc = chords[static_cast<std::size_t>(bar)];
    const int root_pc = bc.root_pc % 12;
    const int third = bc.minor ? 3 : 4;
    const int triad_pc[3] = {root_pc, (root_pc + third) % 12, (root_pc + 7) % 12};
    const bool harmonic = (mode == Mode::Minor) && (root_pc == 7);  // V wants the leading tone.

    // Audible-grain parallel guard against the already-built other manual
    // voice. The opposite zigzag parities make the BEAT anchors contrary by
    // construction, but a band-edge bounce can re-align them, and the
    // intra-beat cell tones were never covered: with the two voices' anchor
    // centres an octave apart (C5 / C4), two same-direction cells chain
    // parallel octaves at every shared sub-beat onset (the dense-character
    // sweeps surfaced up to 19 per piece). Each candidate tone is re-judged
    // against the guard line's sounding pitches at this voice's last emitted
    // onset and now -- the exact pair the union-onset detector (and the ear)
    // samples.
    auto guard_sounding = [&](Tick t) -> int {
      if (guard_registry != nullptr)
        return guard_registry->soundingPitchInVoice(0, t);
      if (guard_line == nullptr)
        return -1;
      for (auto it = guard_line->rbegin(); it != guard_line->rend(); ++it) {
        if (it->start_tick <= t && t < it->start_tick + it->duration)
          return static_cast<int>(it->pitch);
      }
      return -1;
    };
    // The union-onset pair at our onset `tick` is (guard just before tick ->
    // guard at tick): when the guard does not onset at `tick` the two samples
    // are equal, its motion is zero, and oblique motion is always allowed.
    auto forms_guard_parallel = [&](int cand, Tick tick) {
      if (guard_line == nullptr || prev_emitted < 0)
        return false;
      const int other_curr = guard_sounding(tick);
      const int other_prev = guard_sounding(tick - 1);
      if (other_curr < 0 || other_prev < 0)
        return false;
      return formsPerfectParallel(prev_emitted, cand, other_prev, other_curr);
    };
    // Nearest in-band triad tone that does not land the parallel; the anchor
    // itself when every alternative is also parallel (a rare double bind --
    // one consonant parallel beats a non-chord strong beat).
    auto guarded_anchor = [&](int anchor, Tick tick) {
      if (!forms_guard_parallel(anchor, tick))
        return anchor;
      int best = anchor;
      int best_dist = 1 << 20;
      for (int tone = 0; tone < 3; ++tone) {
        int low = band_lo + (((triad_pc[tone] - band_lo) % 12) + 12) % 12;
        for (int v = low; v <= band_hi; v += 12) {
          if (v == anchor || forms_guard_parallel(v, tick))
            continue;
          const int dist = std::abs(v - anchor);
          if (dist < best_dist) {
            best_dist = dist;
            best = v;
          }
        }
      }
      return best;
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
    auto nearestChordTone = [&](int near) {
      int best = band_lo;
      int best_dist = 1 << 20;
      for (int tone = 0; tone < 3; ++tone) {
        int low = band_lo + (((triad_pc[tone] - band_lo) % 12) + 12) % 12;  // chord tone in band.
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
    auto chordToneBeyond = [&](int from, int dir) {
      int best = from;
      int best_dist = 1 << 20;
      for (int tone = 0; tone < 3; ++tone) {
        int low = band_lo + (((triad_pc[tone] - band_lo) % 12) + 12) % 12;
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
    beat_anchor[0] = nearestChordTone(near);
    for (int beat = 1; beat < 4; ++beat) {
      const int prev = beat_anchor[beat - 1];
      const int dir = ((beat % 2 == 1) ? 1 : -1) * zig_dir;
      int anchor = chordToneBeyond(prev, dir);
      if (anchor == prev)  // band edge: bounce the other way.
        anchor = chordToneBeyond(prev, -dir);
      beat_anchor[beat] = anchor;
    }
    prev_anchor = beat_anchor[3];  // carry the closing anchor to the next bar.

    if (dotted) {
      // Noble dotted figure: dotted-quarter + eighth per beat-pair. The long note
      // is the beat anchor (a chord tone on the strong beat); the short note is
      // its upper scalar neighbour. Anchors are the bar's root and fifth.
      const Tick dq = kTicksPerBeat + kEighth;  // dotted quarter.
      const int half_anchor[2] = {beat_anchor[0], beat_anchor[2]};
      for (int half = 0; half < 2; ++half) {
        const Tick base =
            static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(half) * 2 * kTicksPerBeat;
        const int long_pitch = guarded_anchor(half_anchor[half], base);
        MaterialNote longn;
        longn.start_tick = base;
        longn.duration = dq;
        longn.pitch = static_cast<std::uint8_t>(long_pitch);
        dst.push_back(longn);
        prev_emitted = long_pitch;
        const int short_pitch = walk(long_pitch, 1);
        MaterialNote shortn;
        shortn.start_tick = base + dq;
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
        const int anchor = guarded_anchor(beat_anchor[beat], base);
        MaterialNote longn;
        longn.start_tick = base;
        longn.duration = kEighth;
        longn.pitch = static_cast<std::uint8_t>(anchor);
        dst.push_back(longn);
        prev_emitted = anchor;
        for (int sub = 0; sub < 2; ++sub) {
          const Tick tick = base + kEighth + static_cast<Tick>(sub) * kSixteenth;
          int pitch = walk(anchor, sub == 0 ? 1 : 2);
          if (forms_guard_parallel(pitch, tick)) {
            const int alt = walk(anchor, sub == 0 ? -1 : -2);
            if (alt >= band_lo && !forms_guard_parallel(alt, tick))
              pitch = alt;
          }
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
      const int anchor = guarded_anchor(beat_anchor[beat], beat_base);
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
        // Intra-beat cell tone landing a parallel against the guard voice:
        // mirror the cell tone to the anchor's other side when that stays in
        // the band and clears the parallel (the mirrored tone is the same
        // neighbour vocabulary, so the cell still resolves to the anchor).
        if (degrees != 0 && forms_guard_parallel(pitch, mn.start_tick)) {
          const int alt = walk(anchor, -degrees);
          if (alt >= band_lo && alt <= walk(band_hi, 2) &&
              !forms_guard_parallel(alt, mn.start_tick))
            pitch = alt;
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
                    v0_shape, /*zig_dir=*/1, /*guard_line=*/nullptr, /*guard_registry=*/nullptr,
                    v0_prev_emitted);
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
                    /*zig_dir=*/-1, /*guard_line=*/nullptr, &manual_registry, v1_prev_emitted);
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

  // V2 (Pedal): one quarter-note per beat outlining the bar's chord root and
  // fifth, the slow harmonic foundation. The bar OPENS and CLOSES on the root
  // (beats 1 and 4) with the fifth on the two inner beats (2, 3), so the bar's
  // last pedal note is the root, a small step from the next bar's root -- no
  // octave-plus boundary leap (the cadence-boundary root jump that previously
  // produced > octave drops). The fifth is realized a perfect fourth BELOW the
  // root so the whole voice stays compact and low (<= F3 = 53 < the mid voice's
  // floor), preserving the V2 < V1 < V0 register banding. The root register is
  // voice-led near the previous bar's root so successive roots move by a small
  // interval.
  TrioVoiceLine v2;
  v2.voice = 2;
  v2.manual = 3;                   // Pedal (low register).
  constexpr int kPedalFloor = 41;  // F2: roots live in [F2, F3), fifths a 4th below.
  constexpr int kPedalCeil = 53;   // F3.

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
  const auto pedal_parallel = [&](int from, int cand, Tick t) {
    for (const TrioVoiceLine& manual : out.material.trio_voices) {
      const int prev = upper_before(manual.notes, t);
      const int curr = upper_sounding(manual.notes, t);
      if (prev >= 0 && curr >= 0 && formsPerfectParallel(from, cand, prev, curr))
        return true;
    }
    return false;
  };

  int prev_root = 48;  // seed near C3.
  for (int bar = 0; bar < bars; ++bar) {
    const int root_pc = chords[static_cast<std::size_t>(bar)].root_pc % 12;
    // Root in the pedal register nearest the previous bar's root, so successive
    // bars' roots move by a small interval (no octave-plus boundary leap).
    int root_midi = kPedalFloor + (((root_pc - kPedalFloor) % 12) + 12) % 12;
    while (root_midi + 12 <= kPedalCeil &&
           std::abs((root_midi + 12) - prev_root) < std::abs(root_midi - prev_root))
      root_midi += 12;
    if (bar == bars - 1) {
      // Final bar: the pedal joins the held closing chord with a whole-note
      // tonic root instead of the root/fifth quarters.
      MaterialNote held;
      held.start_tick = static_cast<Tick>(bar) * kTicksPerBar;
      held.duration = kTicksPerBar;
      held.pitch = static_cast<std::uint8_t>(root_midi);
      v2.notes.push_back(held);
      prev_root = root_midi;
      continue;
    }
    const int fifth_midi = root_midi - 5;  // a perfect fourth below the root (a fifth).
    // Chord third in the pedal register, voiced nearest the root. Walking
    // root - fifth - third - root removes the repeated inner fifth (a repeated
    // bass pair every bar saturates the interval surface with unisons the
    // reference corpus almost never writes) and widens the pedal's pitch-class
    // palette; the third is a chord tone, so every sampled beat stays
    // consonant against the chord-tone manual voices above.
    const int third_pc = (root_pc + (chords[static_cast<std::size_t>(bar)].minor ? 3 : 4)) % 12;
    int third_midi = kPedalFloor + (((third_pc - kPedalFloor) % 12) + 12) % 12;
    while (third_midi + 12 <= kPedalCeil &&
           std::abs((third_midi + 12) - root_midi) < std::abs(third_midi - root_midi))
      third_midi += 12;
    // The two inner beats rotate their chord-tone order per bar (fifth-third /
    // third-fifth / third-upper-fifth): a single fixed walking cell repeated
    // every bar stamps the same two interval bigrams across the whole pedal
    // line, a concentration the reference corpus never writes. The outer
    // beats stay on the root, so the bar still closes a small step from the
    // next bar's root and every beat remains a chord tone.
    const int fifth_up = (root_midi + 7 <= kPedalCeil) ? root_midi + 7 : fifth_midi;
    int inner_a = fifth_midi;
    int inner_b = third_midi;
    switch (bar % 3) {
      case 1:
        inner_a = third_midi;
        inner_b = fifth_midi;
        break;
      case 2:
        inner_a = third_midi;
        inner_b = fifth_up;
        break;
      default:
        break;
    }
    int beat_pitch[4] = {root_midi, inner_a, inner_b, root_midi};
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
      int pitch = beat_pitch[beat];
      // Next bar's root (the landing of the closing beat's boundary step),
      // recomputed exactly: it depends on this bar's root only, which a
      // displacement never changes.
      int next_root = -1;
      if (beat == 3 && bar + 1 < bars) {
        const int next_pc = chords[static_cast<std::size_t>(bar + 1)].root_pc % 12;
        next_root = kPedalFloor + (((next_pc - kPedalFloor) % 12) + 12) % 12;
        while (next_root + 12 <= kPedalCeil &&
               std::abs((next_root + 12) - root_midi) < std::abs(next_root - root_midi))
          next_root += 12;
      }
      const bool into_par = beat != 0 && pedal_prev >= 0 && pedal_parallel(pedal_prev, pitch, t);
      const bool fwd_par = next_root >= 0 && pedal_parallel(pitch, next_root, t + kTicksPerBeat);
      if (into_par || fwd_par) {
        auto admissible = [&](int cand) {
          if (cand < kPedalFloor || cand > kPedalCeil || cand == pitch)
            return false;
          if (pedal_prev >= 0 && pedal_parallel(pedal_prev, cand, t))
            return false;
          // The closing beat also owns the boundary motion into the next
          // bar's fixed root: do not trade an audible parallel here for one
          // at the bar head.
          if (next_root >= 0 && pedal_parallel(cand, next_root, t + kTicksPerBeat))
            return false;
          return true;
        };
        for (const int cand : {fifth_midi, third_midi, fifth_up, root_midi, root_midi - 12,
                               fifth_midi + 12, third_midi + 12}) {
          if (admissible(cand)) {
            pitch = cand;
            break;
          }
        }
      }
      MaterialNote mn;
      mn.start_tick = t;
      mn.duration = kTicksPerBeat;
      // Root on the outer beats (1, 4): the bar closes on the root for a small
      // boundary step into the next bar's root.
      mn.pitch = static_cast<std::uint8_t>(pitch);
      v2.notes.push_back(mn);
      pedal_prev = pitch;
    }
    prev_root = root_midi;
  }
  // The final cadence contract is structural V -> I.  The generic
  // anti-parallel substitution above may replace the penultimate bar's last
  // root with another chord tone; restore the dominant root on the exact
  // approach beat (parallel motion into a declared cadence is evaluated under
  // the cadence exception).
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
      for (SuspensionType type :
           {SuspensionType::Sus7_6, SuspensionType::Sus4_3, SuspensionType::Sus9_8}) {
        SuspensionPattern suspension;
        if (!designUpperSuspension(
                type, preparation_tick, suspension_tick, resolution_tick,
                /*voice=*/1, static_cast<std::uint8_t>(bass_prep),
                static_cast<std::uint8_t>(held_bass), static_cast<std::uint8_t>(held_bass),
                static_cast<std::uint8_t>(upper_prep), static_cast<std::uint8_t>(upper_sus),
                static_cast<std::uint8_t>(upper_res),
                /*band_lo=*/std::max(bass_prep, held_bass) + 1, ceiling, mode, &suspension))
          continue;
        // The suspension replaces a short window inside an otherwise
        // continuous manual line.  Keep both splice points within an octave;
        // a valid 7-6/4-3/9-8 formula in the wrong register is still an
        // audible remote leap when the original carrier resumes.
        if (middle_before < 0 || middle_after < 0 ||
            std::abs(static_cast<int>(suspension.preparation_pitch) - middle_before) > 12 ||
            std::abs(static_cast<int>(suspension.resolution_pitch) - middle_after) > 12) {
          continue;
        }
        for (MaterialNote& note : bass) {
          if (note.start_tick == resolution_tick)
            note.pitch = static_cast<std::uint8_t>(held_bass);
        }
        installed = installSuspensionCarrier(out.material, out.voice_plan, suspension);
        break;
      }
    }
  }

  return out;
}

}  // namespace bach::composer
