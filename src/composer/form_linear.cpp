#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

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

// C harmonic-minor scale membership (the scale the validator's melodic-leap
// check uses for minor keys): {0, 2, 3, 5, 7, 8, 11}. The subtonic Bb (pc 10)
// and the natural sixth A (pc 9) are NOT members, so a chord ROOT at those
// pitch classes participating in an ic-3 melodic leap of the implicit bass
// stream would read as an augmented second to the validator.
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
// seeds), the perfect-authentic close every length lands on. When `hm_safe` is
// set the minor pattern set is restricted to harmonic-minor-safe roots.
//
// @param bars Total bar count (>= 8).
// @param seed Piece seed (selects which catalog pattern each block uses).
// @param mode Major selects kHarmonyPatterns, Minor selects kHarmonyPatternsMinor.
// @param hm_safe When true (minor only), skip catalog patterns containing an
//        out-of-harmonic-minor root (the subtonic VII = Bb), so the cello's
//        implicit bass stream never makes a forbidden ic-3 leap to/from Bb.
// @return Per-bar chord list of length `bars`.
std::vector<BarChord> buildProgression(int bars, std::uint32_t seed, Mode mode,
                                       bool hm_safe = false) {
  const auto& catalog =
      (mode == Mode::Minor) ? detail::kHarmonyPatternsMinor : detail::kHarmonyPatterns;
  // Patterns admissible for this request: all of them, unless hm_safe filters
  // out any pattern containing an out-of-harmonic-minor root.
  std::vector<std::size_t> admissible;
  for (std::size_t pat = 0; pat < catalog.size(); ++pat) {
    bool ok = true;
    if (hm_safe && mode == Mode::Minor) {
      for (const ChordSpec& spec : catalog[pat]) {
        if (!inHarmonicMinor(spec.root_pc)) {
          ok = false;
          break;
        }
      }
    }
    if (ok)
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

  // The cello's implicit bass stream is the wave's per-cell minimum; restrict
  // the minor progression to harmonic-minor-safe roots so adjacent diatonic-root
  // anchors never make a forbidden ic-3 leap to/from the out-of-scale subtonic.
  const std::vector<BarChord> chords = buildProgression(bars, req.seed, mode, /*hm_safe=*/true);
  writeHarmony(out, chords, mode);

  const detail::CharacterProfile& profile = detail::characterProfile(req.character);

  out.material.arpeggio_template.group_size = kGroup;

  // Scale walker for the wave. MAJOR uses the shared C-major diatonic walk; MINOR
  // uses the validator's EXACT C harmonic-minor scale {C D Eb F G Ab B}, so every
  // wave tone is in-scale (the implicit-voice validator only flags an augmented
  // second when a cell extreme lands on an out-of-scale tone or makes an adjacent
  // ic-3 step, so keeping every tone in the validator's own scale removes the
  // out-of-scale case entirely). The one adjacent augmented second the harmonic
  // minor contains (Ab -> B, degrees 6-7) only matters BETWEEN consecutive cell
  // extremes; the small per-bar arc plus the voice-led chord-tone anchors below
  // keep the bass/top streams from ever spanning that Ab<->B step, so the
  // implicit streams stay clean.
  auto minorHarmonicWalkUp = [](int midi, int steps) {
    static constexpr int kPcs[7] = {0, 2, 3, 5, 7, 8, 11};  // C D Eb F G Ab B.
    int cur = midi;
    for (int s = 0; s < steps; ++s) {
      for (int add = 1; add <= 12; ++add) {
        const int pc = ((cur + add) % 12 + 12) % 12;
        bool member = false;
        for (int idx = 0; idx < 7; ++idx)
          member |= (pc == kPcs[idx]);
        if (member) {
          cur += add;
          break;
        }
      }
    }
    return cur;
  };
  auto walk = [&](int midi, int steps, bool /*harmonic*/) {
    if (steps < 0)
      return midi;
    return (mode == Mode::Minor) ? minorHarmonicWalkUp(midi, steps)
                                 : detail::scaleUp(midi, steps, Mode::Major);
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

  // Voice-led anchor: each bar opens on the chord tone NEAREST the previous
  // bar's closing register, so the implicit bass/top streams never jump an
  // octave at the bar boundary (the failure mode of a fixed per-bar register).
  // Seeded in the cello's tenor range (~C3).
  int prev_anchor = 48;
  // Implicit bass/top extremes of the previous bar's final cell (-1 before
  // the first bar), used to vet each bar figure across the bar seam.
  int prev_cell_lo = -1;
  int prev_cell_hi = -1;
  for (int bar = 0; bar < bars; ++bar) {
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

    // Anchor = the chord tone nearest the previous bar's close, realized in this
    // bar's register window. Try each triad tone in the octave nearest
    // prev_anchor; pick the closest. This voice-leads the bar openings so the
    // bass/top streams move by a small interval at every boundary.
    int anchor = window_lo;
    int best_dist = 1 << 20;
    for (int tone = 0; tone < 3; ++tone) {
      int cand = window_lo + (((triad_pc[tone] - window_lo) % 12) + 12) % 12;
      while (cand + 12 - prev_anchor <= prev_anchor - cand)  // climb to the nearest octave.
        cand += 12;
      const int dist = std::abs(cand - prev_anchor);
      if (dist < best_dist) {
        best_dist = dist;
        anchor = cand;
      }
    }

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
      const int* shape = kCellShapes[(req.seed + cycle) % 3];
      int cell_fig[4];
      for (int idx = 0; idx < 4; ++idx) {
        const int degree = shape[idx] < 0 ? reach : shape[idx];
        cell_fig[idx] = walk(anchor, degree, harmonic);
      }
      for (int slot = 0; slot < kNotesPerBar; ++slot) {
        p[static_cast<std::size_t>(slot)] = cell_fig[slot % 4];
      }
    };
    auto run_triangle_bar = [&](std::array<int, 16>& p) {
      // Eight sixteenths climbing one scale step each, then folding back down
      // the same tones: a full-bar scale-run arch (step-chain bigrams).
      int w[8];
      for (int idx = 0; idx < 8; ++idx) {
        w[idx] = walk(anchor, idx, harmonic);
      }
      for (int idx = 0; idx < 8; ++idx) {
        p[static_cast<std::size_t>(idx)] = w[idx];
      }
      for (int idx = 1; idx <= 6; ++idx) {
        p[static_cast<std::size_t>(7 + idx)] = w[7 - idx];
      }
      p[14] = w[0];
      p[15] = w[0];
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
          if (rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(pl),
                                                   static_cast<std::uint8_t>(lo[cell]),
                                                   out.harmony) ||
              rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(ph),
                                                   static_cast<std::uint8_t>(hi[cell]),
                                                   out.harmony)) {
            return false;
          }
          const int prev_ic = std::abs(ph - pl) % 12;
          const int cur_ic = std::abs(hi[cell] - lo[cell]) % 12;
          if (prev_ic == cur_ic && (prev_ic == 0 || prev_ic == 7)) {
            const int bass_motion = lo[cell] - pl;
            const int top_motion = hi[cell] - ph;
            if (bass_motion != 0 && top_motion != 0 && (bass_motion > 0) == (top_motion > 0)) {
              return false;
            }
          }
        }
        pl = lo[cell];
        ph = hi[cell];
      }
      return true;
    };
    std::array<int, 16> pitches{};
    bool placed = false;
    const int pref = (req.seed + bar) % 3;
    for (int attempt = 0; attempt < 3 && !placed; ++attempt) {
      switch ((pref + attempt) % 3) {
        case 0:
          oscillation_bar(pitches);
          break;
        case 1:
          run_triangle_bar(pitches);
          break;
        default:
          pedal_bariolage_bar(pitches);
          break;
      }
      placed = cells_safe(pitches);
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
  // sixteenth bar's extremes without a seam leap. The tonic register
  // voice-leads from the closing anchor so the landing does not leap.
  {
    int final_tonic = 48;  // tonic C nearest the line's closing register.
    while (final_tonic + 12 - prev_anchor <= prev_anchor - final_tonic)
      final_tonic += 12;
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
                             int& prev_emitted) {
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
      // among {rising step arc, broken-chord arc, falling step arc}: the step
      // arcs alone concentrate the interval-bigram surface into the three
      // step|step bins (a level the reference corpus never reaches), and the
      // broken-chord arc supplies the third/fifth leaps the corpus writes
      // inside beats. Each cell stays inside the voice's proven sounding
      // envelope [band_lo, band_hi + a 2-degree neighbour]: the falling arc
      // keeps two scale steps of headroom above the band floor, and the
      // broken-chord arc points away from whichever band edge would let its
      // fifth escape the envelope (when neither direction fits, the rising
      // arc substitutes) -- so the strict V2 < V1 < V0 register order the
      // voice-crossing rule samples at every onset is preserved.
      const int sounding_hi = walk(band_hi, 2);
      bool broken_fits = false;
      int broken_dir = 1;
      bool falling;
      if (notes_per_beat == 4) {
        const int cell = (bar + beat + shape) % 3;
        if (cell == 1) {
          broken_dir = (walk(anchor, 4) <= sounding_hi) ? 1 : -1;
          broken_fits = (broken_dir > 0) || (walk(anchor, -4) >= band_lo);
        }
        falling = (cell == 2) && (anchor - 4 >= band_lo);
      } else {
        falling = (shape == 1) && (anchor - 4 >= band_lo);
      }
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mn;
        mn.start_tick = beat_base + static_cast<Tick>(sub) * step;
        mn.duration = step;
        const int magnitude = (sub <= notes_per_beat / 2) ? sub : (notes_per_beat - sub);
        const int degrees =
            broken_fits ? broken_dir * 2 * magnitude : (falling ? -magnitude : magnitude);
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

  // Per-voice running anchors and register bands. Each voice's bar-opening beat
  // is voice-led from its own previous closing anchor, so the line evolves
  // smoothly across bar boundaries (no remote register-reset leap); the band
  // clamps the line to its octave so the two upper voices never cross and both
  // stay above the pedal. V0 anchors in [A4, A5], V1 anchors in [G3, F#4]: V1's
  // highest sounding note (anchor 66 + a 2-step neighbour = 68) stays below V0's
  // lowest anchor (69), and V1's lowest (55) stays above the pedal's highest
  // (53), so V2 < V1 < V0 holds everywhere.
  constexpr int kV0BandLo = 69;  // A4.
  constexpr int kV0BandHi = 79;  // G5.
  constexpr int kV1BandLo = 55;  // G3.
  constexpr int kV1BandHi = 64;  // E4 (anchor ceiling; a 2-degree neighbour stays < V0 floor).
  int v0_anchor = 72;            // ~C5.
  int v1_anchor = 60;            // ~C4.
  int v0_prev_emitted = -1;
  int v1_prev_emitted = -1;

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

    appendScalarBar(v0.notes, v0_anchor, kV0BandLo, kV0BandHi, bar, v0_notes, /*dotted=*/false,
                    v0_shape, /*zig_dir=*/1, /*guard_line=*/nullptr, v0_prev_emitted);
    // V1 is built after V0 within the bar, so it guards each tone against the
    // already-emitted V0 line (the audible-grain parallel pair).
    appendScalarBar(v1.notes, v1_anchor, kV1BandLo, kV1BandHi, bar, v1_notes, v1_dotted, v1_shape,
                    /*zig_dir=*/-1, /*guard_line=*/&v0.notes, v1_prev_emitted);
  }

  // Cadential landing on the top line: an eighth-note approach into a held
  // half-note leading tone over the design-valued V bar (the cadential trill
  // site), then a whole-note tonic. V0 owns the landing -- it is the highest
  // voice, so the cadence trill reads as the soprano close.
  constexpr int kV0Tonic = 72;  // C5: the tonic inside the V0 band.
  appendCadentialLanding(v0.notes, static_cast<Tick>(bars - 2) * kTicksPerBar, kTicksPerBar,
                         kV0Tonic - 1, kV0Tonic, mode, kV0BandLo);

  // V1 joins the held final chord instead of running figuration through the
  // final bar: one whole-note third of the closing tonic triad (E, or Eb in
  // minor without the Picardy lift), filling the triad between the V0 tonic
  // and the pedal root.
  {
    const bool picardy = (mode == Mode::Minor) && detail::usePicardy(req.seed);
    const int third = (mode == Mode::Minor && !picardy) ? 63 : 64;  // Eb4 / E4.
    const Tick final_bar_tick = static_cast<Tick>(bars - 1) * kTicksPerBar;
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
  int prev_root = 48;              // seed near C3.
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
    const int beat_pitch[4] = {root_midi, inner_a, inner_b, root_midi};
    for (int beat = 0; beat < 4; ++beat) {
      MaterialNote mn;
      mn.start_tick =
          static_cast<Tick>(bar) * kTicksPerBar + static_cast<Tick>(beat) * kTicksPerBeat;
      mn.duration = kTicksPerBeat;
      // Root on the outer beats (1, 4): the bar closes on the root for a small
      // boundary step into the next bar's root.
      mn.pitch = static_cast<std::uint8_t>(beat_pitch[beat]);
      v2.notes.push_back(mn);
    }
    prev_root = root_midi;
  }
  out.material.trio_voices.push_back(std::move(v2));

  // VoicePlan: one TrioVoiceCarrier span per voice over the whole piece.
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

  return out;
}

}  // namespace bach::composer
