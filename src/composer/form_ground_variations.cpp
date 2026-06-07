#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/arc.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/form_builders.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Ground-bass variation forms: chaconne (4-bar ground, 3/4) and passacaglia
// (8-bar ground, 3/4).
//
// Both forms share one architecture (modelled on the proven Phase16 chaconne
// and Phase20 passacaglia fixtures, reworked from 4/4 to 3/4 and generalised
// over length / mode / character / arc):
//
//   - V1 = the immutable ground bass, one dotted-half note per bar, period-
//     tiled across every cycle by the GroundCarrier / PassacagliaGround replay
//     branch (ground_bass_immutable / passacaglia_ground_immutable stay clean).
//   - V0 = one variation block per cycle, a stepwise scalar wave riding well
//     ABOVE the ground (C4-C5 region vs the C2-C3 ground), so V0 >= V1 holds at
//     every shared tick (no voice crossing). Block density, register, and
//     figure orientation are arc-driven and seed-deterministic; the single
//     climax cycle (arc.is_climax) is the densest block and carries the climax
//     flag so ClimaxPlaced fires.
//
// 3/4 bar math: ticks_per_bar = 1440 (3 quarter beats). The scalar wave is
// built per bar with m = 3 * notes_per_beat notes. Every tick the builders
// compute is derived from ticks_per_bar = spec.ticksPerBar(); the form-director
// stamps the matching 3/4 meter onto the returned fixture's HarmonicPlan, so
// the candidate-search replay and the validator both read 1440-tick bars.
//
// Determinism: everything is a pure function of (seed, cycle index). No RNG.
// ---------------------------------------------------------------------------

namespace {

// 3/4 bar length in ticks: 3 quarter beats. The form-spec is always 3/4 for
// both ground-variation forms; this mirrors HarmonicPlan::ticksPerBar() for a
// 3/4 plan so the builder-side bar math and the validator-side bar math agree.
constexpr Tick kTicksPerBar34 = 3 * kTicksPerBeat;  // 1440.

// Cycle length (bars) per ground-variation form: chaconne = 4-bar ground,
// passacaglia = 8-bar ground. Matches FormSpec::snap_bars for each form.
constexpr int kChaconneCycleBars = 4;
constexpr int kPassacagliaCycleBars = 8;

/**
 * @brief Map an arc density tier (already character-biased and clamped 0..3)
 *        to a notes-per-beat subdivision.
 * @param tier Density tier in [0, 3].
 * @return Notes per beat: 0 -> quarter (1), 1 -> eighth (2), 2/3 -> sixteenth
 *         (4). Tiers 2 and 3 share the sixteenth subdivision; the climax block
 *         (tier 3) is separated from tier 2 by its is_climax flag, not by an
 *         even denser subdivision (sixteenths are already the running peak).
 */
int notesPerBeatForTier(int tier) {
  if (tier <= 0)
    return 1;
  if (tier == 1)
    return 2;
  return 4;
}

/**
 * @brief Resolve the arc-driven density tier for a cycle, biased by character.
 * @param req The resolved request (supplies the arc curve and character).
 * @param cycle Cycle index in [0, cycle_count).
 * @return The density tier clamped to [0, 3]. The climax cycle is forced to the
 *         peak tier 3 by the arc itself; the character density_bias shifts the
 *         non-climax tiers up or down before clamping.
 */
int densityTierFor(const ResolvedRequest& req, std::size_t cycle) {
  const ArcPoint point = req.arc(cycle);
  int tier = static_cast<int>(point.density_tier) +
             static_cast<int>(detail::characterProfile(req.character).density_bias);
  if (tier < 0)
    tier = 0;
  if (tier > 3)
    tier = 3;
  return tier;
}

// Per-bar harmonic data of one ground cycle: the chord root pitch class, the
// chord quality (minor flag), the lowest variation tone for the V0 figuration,
// and the sounding ground pitch class. The ground pitch class lets the V0
// builder pick beat anchors that stay consonant with the held bass note.
struct CycleBar {
  std::uint8_t root_pc;
  bool minor;
  int low_tone;            // lowest variation tone (C4-C5 region) for this bar.
  std::uint8_t ground_pc;  // pitch class of the sustained ground note this bar.
};

// Build the per-bar cycle plan for a ground table: the chord root tracks the
// ground pitch class bar by bar (quality = the diatonic triad quality on that
// degree), and the variation start tone is the ground pitch lifted by octaves
// into the C4-region band. This is the same root-tracks-bass mapping the
// historical variant-0 plans were written with, generalised to any diatonic
// ground table.
std::vector<CycleBar> planFromGround(const std::uint8_t* ground, std::size_t bars,
                                     bool minor_mode) {
  std::vector<CycleBar> plan;
  plan.reserve(bars);
  for (std::size_t i = 0; i < bars; ++i) {
    const std::uint8_t pc = static_cast<std::uint8_t>(ground[i] % 12u);
    int low = static_cast<int>(ground[i]);
    while (low < 50)
      low += 12;  // lift into the C4-region variation band.
    plan.push_back({pc, detail::diatonicTriadMinor(pc, minor_mode), low, pc});
  }
  return plan;
}

/**
 * @brief Octave-fit a pitch class to the MIDI pitch nearest a target center.
 * @param pitch_class Target pitch class (0..11).
 * @param center Register center the result should sit closest to.
 * @return The MIDI pitch of `pitch_class` whose octave is nearest `center`.
 */
int fitPitchClass(int pitch_class, int center) {
  const int base = ((pitch_class % 12) + 12) % 12;
  int candidate = center - ((center - base) % 12 + 12) % 12;  // <= center, same pc.
  if (center - candidate > 6)
    candidate += 12;  // round to the nearer octave.
  return candidate;
}

// Diatonic-degree index space. degreeToMidi maps a degree index to its MIDI
// pitch; midiToDegree inverts it for any pitch (snapping down to the nearest
// scale member). C0 = MIDI 12 is degree 0 (well below the variation register,
// so indices stay non-negative). Working in degree indices makes fills stepwise
// by construction.
constexpr int kDegreeBase = 12;
int degreeToMidi(int degree, detail::Mode mode) {
  return detail::scaleUp(kDegreeBase, degree, mode);
}
int midiToDegree(int midi, detail::Mode mode) {
  int degree = 0;
  while (degreeToMidi(degree + 1, mode) <= midi)
    ++degree;
  return degree;
}

/**
 * @brief Collect the chord-tone anchor pitch classes for one bar.
 *
 * The anchors are the bar's chord tones (root / third / fifth), each consonant
 * with the held ground (the chord root tracks the ground pitch class). In minor
 * the leading tone B natural (pc 11) is filtered out so V0 stays in natural
 * minor and no Ab->B augmented 2nd can arise.
 *
 * @param bar The bar's harmonic data.
 * @param mode Diatonic mode (selects the third quality filter behaviour).
 * @return Up to three consonant chord-tone pitch classes (always non-empty).
 */
std::vector<int> barAnchorPitchClasses(const CycleBar& bar, detail::Mode mode) {
  const bool minor = mode == detail::Mode::Minor;
  const int third = bar.minor ? 3 : 4;
  std::vector<int> anchors;
  anchors.reserve(3);
  for (int interval : {0, third, 7}) {
    const int pitch_class = (bar.root_pc + interval) % 12;
    if (minor && pitch_class == 11)
      continue;
    if (!isConsonantIc(pitch_class - bar.ground_pc))
      continue;
    anchors.push_back(pitch_class);
  }
  if (anchors.empty())
    anchors.push_back(bar.root_pc % 12);  // defensive: root is always consonant.
  return anchors;
}

/**
 * @brief Build one variation cycle's V0 line: per-beat chord-tone anchoring
 *        with stepwise diatonic fills, threaded continuously to minimise leaps.
 *
 * Pass 1 resolves the full sequence of beat anchors (cycle_bars * 3 of them) as
 * diatonic-degree indices. Each beat onset is a chord tone of its bar's chord,
 * octave-fit to the octave NEAREST the previous anchor and clamped into a FIXED
 * register band so the descending chord roots do not drag the line down an
 * octave -- successive onsets therefore never leap more than a tritone, and the
 * line stays in the C4-C5 region above the ground. Because every anchor is a
 * chord tone consonant with the sustained ground, every beat-onset note the
 * audio scorer samples is consonant with the held bass (vertical-dissonance
 * ratio ~0).
 *
 * Pass 2 emits the notes: each beat opens on its anchor, then fills the
 * sub-beats with stepwise diatonic motion toward the NEXT anchor in the
 * sequence (a true sawtooth), so the surface is conjunct and the jump into the
 * next onset is at most one diatonic step.
 *
 * Variation differentiation (no RNG): `anchor_rotation` rotates which chord tone
 * opens each bar's anchor group, so consecutive cycles trace different anchor
 * contours.
 *
 * @param notes Destination note vector (the variation's realized line).
 * @param block_start Absolute start tick of the variation block.
 * @param cycle_bar_plan Per-bar harmony + register data for the ground cycle.
 * @param register_shift Semitone register lift from the arc (raises the band).
 * @param anchor_rotation Cycle-driven rotation of the chord-tone anchor order.
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendVariationCycle(std::vector<MaterialNote>& notes, Tick block_start,
                          const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                          int anchor_rotation, int notes_per_beat, detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());

  // Register center for the cycle, anchored on the first bar's low tone lifted
  // by the arc shift (the descending chord roots do NOT lower it, so the
  // figuration keeps a stable C4-C5 tessitura).
  const int center = cycle_bar_plan.front().low_tone + register_shift;

  // --- Phase 1: resolve the full anchor-degree sequence -------------------
  // Each anchor takes the octave of its chord tone NEAREST the previous anchor,
  // so consecutive beat onsets never leap more than a tritone (the minimal
  // octave distance between two pitch classes is <= 6 semitones). At each bar's
  // FIRST beat the fit reference is re-centered toward `center` -- but only when
  // that does not move the anchor by more than a tritone from the running pitch
  // -- so the line cannot drift an octave away over the cycle's descending chord
  // roots, yet a re-center never itself introduces a leap.
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
      // sub == 0 is the sampled beat-onset anchor; later subs step one diatonic
      // degree per sub toward the next anchor, bounded so the fill never
      // overshoots it (so the next onset is at most one step away -- no leap).
      int degree = from_deg;
      if (sub > 0) {
        int advance = sub;
        if (advance > span)
          advance = span;  // do not overshoot the next onset.
        degree = from_deg + dir * advance;
      }
      mnote.pitch = static_cast<std::uint8_t>(degreeToMidi(degree, mode));
      notes.push_back(mnote);
    }
  }
}

// ---------------------------------------------------------------------------
// Passacaglia-only 3-voice machinery (BWV582 model). The chaconne path is left
// byte-for-byte unchanged; everything below this banner is reached ONLY from the
// passacaglia branch of buildGroundVariationForm.
// ---------------------------------------------------------------------------

// Register band for the passacaglia principal variation (V0): C4-C5 region, well
// above the middle counter-figuration (V1) and the ground (V2). The wave folds
// inside this band so the line never crosses below V1.
constexpr int kPassV0BandLo = 60;  // C4.
constexpr int kPassV0BandHi = 79;  // G5.

// Register band for the passacaglia counter-figuration (V1): C3-B3 region, kept
// strictly between V0 (>= C4) and the ground (<= C3) so voice_crossing never
// fires (lower voice index sounds higher).
constexpr int kPassV1BandLo = 48;  // C3.
constexpr int kPassV1BandHi = 59;  // B3.

/**
 * @brief Build one principal-variation cycle's V0 line as a continuous diatonic
 *        scalar wave (stepwise-dominant, no repeated pitches, no leaps).
 *
 * Unlike the shared per-beat chord-tone sawtooth (appendVariationCycle, kept for
 * the chaconne), this walks one diatonic scale degree per emitted note and folds
 * its direction at the band edges. Single-step motion is the corpus's dominant
 * melodic interval, so the realized line's melodic-interval distribution matches
 * the reference far better (the dominant scorer feature). The bar downbeat is
 * re-anchored to the nearest chord tone of the bar's chord so beat-onset
 * vertical consonance against the held ground stays high, but the re-anchor is
 * itself reached by a single step (it never introduces a leap), so the surface
 * remains conjunct.
 *
 * Variation differentiation (no RNG): `phase_rotation` shifts the wave's start
 * degree and `descending_start` flips the opening direction, so consecutive
 * cycles trace distinct contours without reintroducing leaps.
 *
 * @param notes Destination note vector (the variation's realized line).
 * @param block_start Absolute start tick of the variation block.
 * @param cycle_bar_plan Per-bar harmony + register data for the ground cycle.
 * @param register_shift Semitone register lift from the arc (raises the band).
 * @param phase_rotation Cycle-driven shift of the wave's start degree.
 * @param descending_start When true, the wave opens descending.
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendScalarWaveCycle(std::vector<MaterialNote>& notes, Tick block_start,
                           const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                           int phase_rotation, bool descending_start, int notes_per_beat,
                           detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  const int band_lo = kPassV0BandLo + register_shift;
  const int band_hi = kPassV0BandHi + register_shift;
  const int lo_deg = midiToDegree(band_lo, mode);
  const int hi_deg = midiToDegree(band_hi, mode);

  // Start degree: the bottom of the band lifted by the cycle's phase rotation,
  // clamped into the band so a large rotation cannot escape it.
  int degree = lo_deg + (phase_rotation % std::max(1, hi_deg - lo_deg));
  if (degree > hi_deg)
    degree = hi_deg;
  int dir = descending_start ? -1 : 1;

  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);
  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    for (int beat = 0; beat < 3; ++beat) {
      const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                             static_cast<Tick>(beat) * kTicksPerBeat;
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        // Bar downbeat (beat 0, sub 0): re-anchor to the nearest chord tone of
        // this bar so the sampled beat onset stays consonant with the held
        // ground -- but only by stepping the wave toward it, so the conjunct
        // surface is preserved and no leap is introduced.
        if (beat == 0 && sub == 0 && bar > 0) {
          const std::vector<int> pcs = barAnchorPitchClasses(plan, mode);
          const int cur_midi = degreeToMidi(degree, mode);
          int best_midi = cur_midi;
          int best_dist = 128;
          for (int pitch_class : pcs) {
            const int fit = fitPitchClass(pitch_class, cur_midi);
            for (int oct : {fit - 12, fit, fit + 12}) {
              if (oct < band_lo || oct > band_hi)
                continue;
              const int dist = std::abs(oct - cur_midi);
              if (dist < best_dist) {
                best_dist = dist;
                best_midi = oct;
              }
            }
          }
          // Step toward the chosen chord tone by a single scale degree (never a
          // leap); if already there, hold the degree.
          const int target_deg = midiToDegree(best_midi, mode);
          if (target_deg > degree)
            ++degree;
          else if (target_deg < degree)
            --degree;
        }
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        mnote.pitch = static_cast<std::uint8_t>(degreeToMidi(degree, mode));
        notes.push_back(mnote);
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

/**
 * @brief Build one cycle's V1 counter-figuration: a per-beat consonant,
 *        parallel-free middle line read back against V0 and the ground (V2).
 *
 * V1 is built AFTER V0 and the ground are recorded in `registry`, so each beat
 * anchor is selected via the shared tier-scored consonantChordTone: a chord tone
 * inside the middle band that is consonant with the concurrent V0 / ground tones
 * and forms no parallel/hidden perfect against them. Off-beats fill stepwise
 * toward the next anchor (conjunct, no leaps), and every pick is registered so
 * the next beat's parallel check sees it.
 *
 * @param notes Destination note vector (the V1 counter-line).
 * @param registry Inter-voice read-back (already holds V0 + ground tones).
 * @param block_start Absolute start tick of the cycle.
 * @param cycle_bar_plan Per-bar harmony for the ground cycle.
 * @param register_shift Semitone register lift from the arc.
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendCounterFiguration(std::vector<MaterialNote>& notes, ThemeToneRegistry& registry,
                             Tick block_start, const std::vector<CycleBar>& cycle_bar_plan,
                             int register_shift, int notes_per_beat, detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  const int band_lo = kPassV1BandLo + register_shift;
  const int band_hi = kPassV1BandHi + register_shift;
  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);

  int line_prev = -1;
  int cursor = (band_lo + band_hi) / 2;
  int anchor_run = 0;  // consecutive beats holding the same anchor pitch.
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;

  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    detail::ChordSpec chord;
    chord.root_pc = plan.root_pc;
    chord.minor = plan.minor;
    for (int beat = 0; beat < 3; ++beat) {
      const Tick beat_tick = block_start + static_cast<Tick>(bar) * kTicksPerBar34 +
                             static_cast<Tick>(beat) * kTicksPerBeat;
      const Tick prev_tick = beat_tick - kTicksPerBeat;
      theme_pitches.clear();
      motions.clear();
      registry.concurrentThemePitches(beat_tick, /*voice=*/1, theme_pitches);
      registry.concurrentMotions(prev_tick, beat_tick, /*voice=*/1, /*num_voices=*/3, motions);
      int anchor = consonantChordTone(chord, /*voice=*/1, band_lo, band_hi, cursor, theme_pitches,
                                      line_prev, motions, mode, /*downbeat=*/beat == 0);
      // Adjacent bars whose chords share a tone near the band centre can pin
      // the nearest-tone anchor chain to ONE pitch for many beats; at the
      // quarter-note tier that surfaces as a stalled repeated-note line. When
      // a fifth identical beat is imminent, force the nearest DIFFERENT triad
      // tone in band instead (a chord tone, so it stays consonant against the
      // ground and the V0 anchors above).
      if (notes_per_beat == 1 && anchor == line_prev && anchor_run >= 4) {
        const int third = chord.minor ? 3 : 4;
        const int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + third) % 12,
                                 (chord.root_pc + 7) % 12};
        auto is_triad = [&](int midi) {
          const int pcl = ((midi % 12) + 12) % 12;
          return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
        };
        for (int dist = 1; dist <= 12; ++dist) {
          const int above = anchor + dist;
          const int below = anchor - dist;
          if (above <= band_hi && is_triad(above)) {
            anchor = above;
            break;
          }
          if (below >= band_lo && is_triad(below)) {
            anchor = below;
            break;
          }
        }
      }
      anchor_run = (anchor == line_prev) ? anchor_run + 1 : 1;
      // Stepwise fill toward the NEXT beat's eventual anchor is unknown here, so
      // fills oscillate around the anchor by single scale steps (conjunct, no
      // leaps); the anchor itself is the consonant beat onset.
      int anchor_deg = midiToDegree(anchor, mode);
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        int deg = anchor_deg;
        if (sub > 0)
          deg = anchor_deg + ((sub % 2 == 1) ? 1 : 0);  // neighbour-tone oscillation.
        const int pitch = degreeToMidi(deg, mode);
        mnote.pitch = static_cast<std::uint8_t>(pitch);
        notes.push_back(mnote);
        registry.record(mnote.start_tick, /*voice=*/1, pitch, step);
      }
      line_prev = anchor;
      cursor = anchor;
    }
  }
}

/**
 * @brief Resolve the per-cycle voice-presence schedule for a passacaglia.
 *
 * BWV582-style terraced growth derived purely from the period (cycle) count:
 *   - periods >= 4: cycle 0 is a ground-solo intro (V0 and V1 rest); the middle
 *     cycles add V0 over the ground with ONE receding cycle where V1 rests; the
 *     climax cycle sounds all three voices.
 *   - periods == 3 (the default 24-bar / 3-period piece): NO intro -- cycle 0 is
 *     V0 + ground, cycle 1 adds V1, the climax cycle is the full texture.
 *   - periods <= 2: degenerate -- the first cycle is V0 + ground and every later
 *     cycle is the full texture (no room for an intro terrace).
 *
 * @param cycle_count Number of ground statements in the piece (>= 1).
 * @param climax_idx The arc climax cycle index.
 * @param out_v0 Receives, per cycle, whether V0 (principal variation) sounds.
 * @param out_v1 Receives, per cycle, whether V1 (counter-figuration) sounds.
 */
void resolveVoiceSchedule(std::size_t cycle_count, std::size_t climax_idx,
                          std::vector<bool>& out_v0, std::vector<bool>& out_v1) {
  out_v0.assign(cycle_count, true);
  out_v1.assign(cycle_count, false);
  if (cycle_count == 0)
    return;

  if (cycle_count >= 4) {
    // Cycle 0: ground-solo intro (V0 and V1 silent).
    out_v0[0] = false;
    out_v1[0] = false;
    // The single receding cycle: the cycle just before the climax keeps V0 but
    // rests V1 (a momentary thinning before the climax restores all three).
    std::size_t receding = (climax_idx > 1) ? climax_idx - 1 : 1;
    for (std::size_t cyc = 1; cyc < cycle_count; ++cyc) {
      out_v0[cyc] = true;
      out_v1[cyc] = (cyc != receding);
    }
    out_v1[climax_idx] = true;  // climax always sounds all three.
    out_v0[climax_idx] = true;
  } else if (cycle_count == 3) {
    // No intro: cycle 0 = V0 + ground, cycle 1 adds V1, climax = full.
    out_v1[0] = false;
    out_v1[1] = true;
    out_v1[2] = true;
  } else {
    // periods <= 2: first cycle V0 + ground, later cycles full.
    for (std::size_t cyc = 1; cyc < cycle_count; ++cyc)
      out_v1[cyc] = true;
  }
}

/**
 * @brief Build the 3-voice passacaglia (Material, HarmonicPlan, VoicePlan).
 *
 * V0 = principal scalar-wave variation (PassacagliaVariation, C4-C5), V1 = a
 * consonant parallel-free counter-figuration (TrioVoiceCarrier, C3-B3, exactly
 * the Goldberg builder's middle-voice pattern), V2 = the immutable ground
 * (PassacagliaGround, C2-C3 -- the SAME pitches as the 2-voice form, only the
 * voice id moves to 2 so register order V0 > V1 > V2 holds). Terraced growth is
 * derived from the period count (resolveVoiceSchedule).
 *
 * @param req The resolved request.
 * @param cycle_bars Bars per ground cycle (8 for passacaglia).
 * @param ground_pitch The cycle-relative ground bass pitches, one per bar.
 * @param cycle_bar_plan Per-bar harmony + variation start tone for one cycle.
 * @return The assembled fixture (meter is stamped later by the form-director).
 */
HarnessFixture buildPassacagliaThreeVoice(const ResolvedRequest& req, int cycle_bars,
                                          const std::vector<std::uint8_t>& ground_pitch,
                                          const std::vector<CycleBar>& cycle_bar_plan) {
  HarnessFixture out;

  const int total_bars = static_cast<int>(req.bars);
  const int cycles = total_bars / cycle_bars;
  const Tick period = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  const detail::Mode mode = req.mode;
  const bool minor = mode == detail::Mode::Minor;
  const bool picardy = minor && detail::usePicardy(req.seed);

  auto bar_tick = [](int bar) { return static_cast<Tick>(bar) * kTicksPerBar34; };

  // --- V2 ground bass: identical pitches / rhythm to the 2-voice form; only the
  // voice assignment (set on the span below) moves to V2. ---
  std::vector<MaterialNote>& ground = out.material.passacaglia_ground;
  for (int bar = 0; bar < cycle_bars; ++bar) {
    MaterialNote gnote;
    gnote.start_tick = bar_tick(bar);
    gnote.duration = kTicksPerBar34;
    gnote.pitch = ground_pitch[static_cast<std::size_t>(bar)];
    ground.push_back(gnote);
  }
  out.material.passacaglia_ground_period = period;

  // --- HarmonicPlan: one chord per bar; the final bar resolves to the tonic. ---
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = minor;
  for (int bar = 0; bar < total_bars; ++bar) {
    const int cyc_bar = bar % cycle_bars;
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(cyc_bar)];
    ChordEvent chord;
    chord.start_tick = bar_tick(bar);
    if (bar == total_bars - 1) {
      chord.root_pc = 0;
      chord.quality = (picardy || !minor) ? ChordQuality::Major : ChordQuality::Minor;
    } else {
      chord.root_pc = plan.root_pc;
      chord.quality = plan.minor ? ChordQuality::Minor : ChordQuality::Major;
    }
    out.harmony.chords.push_back(chord);
  }

  // --- Terraced growth schedule (period-count derived). ---
  std::size_t climax_idx = (static_cast<std::size_t>(cycles) * 4) / 5;
  if (cycles > 0 && climax_idx > static_cast<std::size_t>(cycles) - 1)
    climax_idx = static_cast<std::size_t>(cycles) - 1;
  std::vector<bool> v0_present;
  std::vector<bool> v1_present;
  resolveVoiceSchedule(static_cast<std::size_t>(cycles), climax_idx, v0_present, v1_present);

  // V1 counter-figuration accumulates into a single TrioVoiceLine (voice 1),
  // gated per cycle by the schedule; V0 variation blocks accumulate per cycle.
  std::vector<MaterialNote> counter_notes;

  for (int cycle = 0; cycle < cycles; ++cycle) {
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    const Tick block_start = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    const Tick block_end = block_start + period;
    const bool is_climax = point.is_climax;

    // V0 principal variation (when present this cycle). Cycle 0 (when it carries
    // V0) is a plain quarter-note establishing statement; later cycles ride the
    // arc density tier.
    std::vector<MaterialNote> v0_notes;
    if (v0_present[static_cast<std::size_t>(cycle)]) {
      const bool establishing = (cycle == 0);
      const int tier = establishing ? 0 : densityTierFor(req, static_cast<std::size_t>(cycle));
      const int notes_per_beat = establishing ? 1 : notesPerBeatForTier(tier);
      const int phase_rotation =
          static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % 5);
      const bool descending_start = ((req.seed + static_cast<std::uint32_t>(cycle)) % 2) == 1;
      appendScalarWaveCycle(v0_notes, block_start, cycle_bar_plan, point.register_shift,
                            phase_rotation, descending_start, notes_per_beat, mode);

      PassacagliaVariation var;
      var.voice = 0;
      var.start_tick = block_start;
      var.end_tick = block_end;
      var.density_level = tier;
      var.is_climax = is_climax;
      var.notes = v0_notes;
      out.material.passacaglia_variations.push_back(std::move(var));
    }

    // V1 counter-figuration (when present this cycle). Read back V0 (this cycle)
    // and the ground (period-tiled) so the counter-line stays consonant and
    // parallel-free; the counter-line is one density tier below V0.
    if (v1_present[static_cast<std::size_t>(cycle)]) {
      ThemeToneRegistry registry;
      for (const MaterialNote& note : v0_notes)
        registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
      for (int bar = 0; bar < cycle_bars; ++bar) {
        const std::size_t gi = static_cast<std::size_t>(bar);
        registry.record(block_start + bar_tick(bar), /*voice=*/2,
                        static_cast<int>(ground_pitch[gi]), kTicksPerBar34);
      }
      const int tier = densityTierFor(req, static_cast<std::size_t>(cycle));
      const int v1_tier = (tier > 0) ? tier - 1 : 0;
      const int notes_per_beat = notesPerBeatForTier(v1_tier);
      appendCounterFiguration(counter_notes, registry, block_start, cycle_bar_plan,
                              point.register_shift, notes_per_beat, mode);
    }
  }

  if (!counter_notes.empty()) {
    TrioVoiceLine counter_line;
    counter_line.voice = 1;
    counter_line.manual = 1;  // documentary (Swell): V1 = middle counter-line.
    counter_line.notes = std::move(counter_notes);
    out.material.trio_voices.push_back(std::move(counter_line));
  }

  // --- VoicePlan: 3 voices, register order V0 > V1 > V2. ---
  out.voice_plan.num_voices = 3;
  SpanId next_span_id = 0;

  Span ground_span;
  ground_span.id = next_span_id++;
  ground_span.start_tick = 0;
  ground_span.end_tick = static_cast<Tick>(total_bars) * kTicksPerBar34;
  ground_span.voice = 2;
  ground_span.intent = VoiceIntent::PassacagliaGround;
  ground_span.subdivision = Subdivision::Quarter;
  out.voice_plan.spans.push_back(ground_span);

  for (int cycle = 0; cycle < cycles; ++cycle) {
    if (!v0_present[static_cast<std::size_t>(cycle)])
      continue;
    Span var_span;
    var_span.id = next_span_id++;
    var_span.start_tick = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    var_span.end_tick = var_span.start_tick + period;
    var_span.voice = 0;
    var_span.intent = VoiceIntent::PassacagliaVariation;
    var_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(var_span);
  }

  for (int cycle = 0; cycle < cycles; ++cycle) {
    if (!v1_present[static_cast<std::size_t>(cycle)])
      continue;
    Span counter_span;
    counter_span.id = next_span_id++;
    counter_span.start_tick = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    counter_span.end_tick = counter_span.start_tick + period;
    counter_span.voice = 1;
    counter_span.intent = VoiceIntent::TrioVoiceCarrier;
    counter_span.subdivision = Subdivision::Quarter;
    out.voice_plan.spans.push_back(counter_span);
  }

  return out;
}

/**
 * @brief Build the (Material, HarmonicPlan, VoicePlan) triple for a ground-
 *        variation form (chaconne or passacaglia).
 *
 * Shared between buildChaconneForm and buildPassacagliaForm; the two differ
 * only in cycle length (4 vs 8 bars) and the per-bar ground / harmony tables.
 *
 * @param req The resolved request (bars, seed, mode, character, arc).
 * @param cycle_bars Bars per ground cycle (4 chaconne, 8 passacaglia).
 * @param ground_pitch The cycle-relative ground bass pitches, one per bar.
 * @param cycle_bar_plan The per-bar harmony + variation start tone for one
 *        ground cycle (cycle_bars entries).
 * @param passacaglia When true, route through the PassacagliaGround /
 *        PassacagliaVariation carriers (8-bar form); when false, the chaconne
 *        GroundCarrier / VariationCarrier carriers (4-bar form).
 * @return The assembled fixture (meter is stamped later by the form-director).
 */
HarnessFixture buildGroundVariationForm(const ResolvedRequest& req, int cycle_bars,
                                        const std::vector<std::uint8_t>& ground_pitch,
                                        const std::vector<CycleBar>& cycle_bar_plan,
                                        bool passacaglia) {
  HarnessFixture out;

  const int total_bars = static_cast<int>(req.bars);
  const int cycles = total_bars / cycle_bars;
  const Tick period = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  const detail::Mode mode = req.mode;
  const bool minor = mode == detail::Mode::Minor;
  const bool picardy = minor && detail::usePicardy(req.seed);

  auto bar_tick = [](int bar) { return static_cast<Tick>(bar) * kTicksPerBar34; };

  // --- V1 ground bass: one dotted-half (full-bar) note per bar, cycle-relative
  // ticks. The replay branch period-tiles it across every cycle. ---
  std::vector<MaterialNote>& ground =
      passacaglia ? out.material.passacaglia_ground : out.material.ground_bass;
  for (int bar = 0; bar < cycle_bars; ++bar) {
    MaterialNote gnote;
    gnote.start_tick = bar_tick(bar);
    gnote.duration = kTicksPerBar34;  // dotted half in 3/4.
    gnote.pitch = ground_pitch[static_cast<std::size_t>(bar)];
    ground.push_back(gnote);
  }
  if (passacaglia)
    out.material.passacaglia_ground_period = period;
  else
    out.material.ground_bass_period = period;

  // --- HarmonicPlan: one chord per bar over every cycle. The final bar of the
  // whole piece resolves to the tonic (i / I) for a proper closing cadence,
  // regardless of where the per-cycle progression would otherwise land (a
  // chaconne cycle ends on V; the last statement substitutes the tonic). When
  // the piece is minor and the seed is even, that final tonic takes a Picardy
  // (major) third. ---
  out.harmony.tonic_pc = 0;
  out.harmony.is_minor = minor;
  for (int bar = 0; bar < total_bars; ++bar) {
    const int cyc_bar = bar % cycle_bars;
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(cyc_bar)];
    ChordEvent chord;
    chord.start_tick = bar_tick(bar);
    if (bar == total_bars - 1) {
      // Closing cadence on the tonic; Picardy raises the third to major.
      chord.root_pc = 0;
      chord.quality = (picardy || !minor) ? ChordQuality::Major : ChordQuality::Minor;
    } else {
      chord.root_pc = plan.root_pc;
      chord.quality = plan.minor ? ChordQuality::Minor : ChordQuality::Major;
    }
    out.harmony.chords.push_back(chord);
  }

  // --- V0 variation blocks: one per cycle, arc-driven density / register /
  // figure orientation. The first cycle is a plain Ground-role statement
  // (quarter notes only, no sub-quarter ornaments) so the chaconne form's
  // variation_role_ornament_constraint stays satisfied. ---
  for (int cycle = 0; cycle < cycles; ++cycle) {
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    // Cycle 0 is the sparse Ground-role establishing statement (quarters).
    const bool ground_role = (cycle == 0);
    int tier = ground_role ? 0 : densityTierFor(req, static_cast<std::size_t>(cycle));
    int notes_per_beat = ground_role ? 1 : notesPerBeatForTier(tier);
    // Anchor rotation: which chord tone opens each bar's anchor group rotates by
    // (seed + cycle), so consecutive cycles trace different anchor contours.
    const int anchor_rotation =
        static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % 4);
    const Tick block_start = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    const Tick block_end = block_start + period;

    std::vector<MaterialNote> notes;
    appendVariationCycle(notes, block_start, cycle_bar_plan, point.register_shift, anchor_rotation,
                         notes_per_beat, mode);

    if (passacaglia) {
      PassacagliaVariation var;
      var.voice = 0;
      var.start_tick = block_start;
      var.end_tick = block_end;
      var.density_level = tier;
      var.is_climax = point.is_climax;
      var.notes = std::move(notes);
      out.material.passacaglia_variations.push_back(std::move(var));
    } else {
      VariationDecl var;
      // Role assignment. Cycle 0 is the plain Ground-role statement (quarters
      // only, so variation_role_ornament_constraint stays satisfied -- no
      // sub-quarter note may carry the Ground role). Every later (subdivided)
      // cycle rotates through Respond -> Propel -> Assert, never re-using
      // Ground, since those blocks contain eighths / sixteenths. The climax
      // cycle is always Assert (the peak role).
      static constexpr VariationRole kActiveRoles[3] = {
          VariationRole::Respond, VariationRole::Propel, VariationRole::Assert};
      VariationRole role =
          ground_role ? VariationRole::Ground
                      : (point.is_climax ? VariationRole::Assert : kActiveRoles[(cycle - 1) % 3]);
      var.role = role;
      var.voice = 0;
      var.start_tick = block_start;
      var.end_tick = block_end;
      var.density_level = tier;
      var.notes = std::move(notes);
      out.material.variations.push_back(std::move(var));
    }
  }

  // --- VoicePlan: V1 ground carrier over the whole piece; V0 variation carrier
  // per cycle, windows matching each block exactly. V0 (C4-C5) stays above V1
  // (C2-C3) so no voice crossing occurs. ---
  out.voice_plan.num_voices = 2;

  Span ground_span;
  ground_span.id = 0;
  ground_span.start_tick = 0;
  ground_span.end_tick = static_cast<Tick>(total_bars) * kTicksPerBar34;
  ground_span.voice = 1;
  ground_span.intent = passacaglia ? VoiceIntent::PassacagliaGround : VoiceIntent::GroundCarrier;
  ground_span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
  out.voice_plan.spans.push_back(ground_span);

  for (int cycle = 0; cycle < cycles; ++cycle) {
    Span var_span;
    var_span.id = static_cast<SpanId>(1 + cycle);
    var_span.start_tick = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    var_span.end_tick = var_span.start_tick + period;
    var_span.voice = 0;
    var_span.intent =
        passacaglia ? VoiceIntent::PassacagliaVariation : VoiceIntent::VariationCarrier;
    var_span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
    out.voice_plan.spans.push_back(var_span);
  }

  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Chaconne: 3/4, 4-bar ground period (BWV1004 arch model).
//
// Ground (one dotted-half per bar): one of the seed-selected design variants
// in kChaconneGroundsMinor / kChaconneGroundsMajor. Variant 0 is the historical
// descending tetrachord (minor C3 Bb2 Ab2 G2 / major C3 B2 A2 G2); the other
// variants are a root-leap line and an ascending tetrachord. The variant is
// fixed for the whole piece, so the ground stays immutable within a piece.
//
// Harmony per cycle (one chord per bar): the chord root tracks the ground
// pitch class bar by bar. The variation start tones trace the bar's bass an
// octave up (C4-region), so each bar's wave opens on a chord-consonant tone.
// ---------------------------------------------------------------------------
HarnessFixture buildChaconneForm(const ResolvedRequest& req) {
  const bool minor = req.mode == detail::Mode::Minor;

  // Ground pitches (cycle-relative, one per bar): a seed-selected design
  // variant. Variant 0 is the historical descending-fourth C->G table.
  const std::size_t variant = detail::groundVariantIndex(req.seed);
  const auto& table =
      minor ? detail::kChaconneGroundsMinor[variant] : detail::kChaconneGroundsMajor[variant];
  const std::vector<std::uint8_t> ground_pitch(table.begin(), table.end());

  // Per-bar harmony + variation start tone for one 4-bar cycle.
  // CycleBar fields: {chord root pc, minor?, low tone (C4 region), ground pc}.
  // The ground pc is the ground note's pitch class for that bar; the chord root
  // tracks it, so every chord-tone anchor is consonant with the held ground.
  // Variant 0 keeps its historical literal plan (the major plan reads the bass
  // B as a V6 chord, which the generic root-tracking mapping does not produce);
  // the other variants derive the plan from the ground table.
  std::vector<CycleBar> plan;
  if (variant == 0 && minor) {
    // Minor: i (C) - VII (Bb) - VI (Ab) - V (G).
    plan = {
        {0, true, 60, 0},     // i  : ground C, start C4.
        {10, false, 58, 10},  // VII: ground Bb, start Bb3.
        {8, false, 56, 8},    // VI : ground Ab, start Ab3.
        {7, false, 55, 7},    // V  : ground G,  start G3.
    };
  } else if (variant == 0) {
    // Major: I (C) - V (G) - vi (A) - V (G).
    plan = {
        {0, false, 60, 0},   // I : ground C, start C4.
        {7, false, 55, 11},  // V6: ground B (chord G), start G3.
        {9, true, 57, 9},    // vi: ground A, start A3.
        {7, false, 55, 7},   // V : ground G, start G3.
    };
  } else {
    plan = planFromGround(table.data(), table.size(), minor);
  }

  return buildGroundVariationForm(req, kChaconneCycleBars, ground_pitch, plan,
                                  /*passacaglia=*/false);
}

// ---------------------------------------------------------------------------
// Passacaglia: 3/4, 8-bar ground period (BWV582 model).
//
// Ground (one dotted-half per bar): one of the seed-selected design variants
// in kPassacagliaGroundsMinor / kPassacagliaGroundsMajor. Variant 0 is the
// historical one-octave descent (minor = kGroundMinorDescent, the BWV582-style
// lament line; major = the diatonic C-major equivalent); the other variants are
// a leaping root-progression line and a lament with an upper-neighbour turn.
// The variant is fixed for the whole piece, so the ground stays immutable
// within a piece.
//
// Harmony per cycle (one chord per bar): the chord root tracks the ground pitch
// class per bar (the simplest valid mapping consistent with the bass), with the
// quality the diatonic triad quality on that scale degree.
// ---------------------------------------------------------------------------
HarnessFixture buildPassacagliaForm(const ResolvedRequest& req) {
  const bool minor = req.mode == detail::Mode::Minor;

  // Ground pitches (cycle-relative, one per bar): a seed-selected design
  // variant. Variant 0 is the historical descending lament line (minor variant
  // 0 mirrors kGroundMinorDescent).
  const std::size_t variant = detail::groundVariantIndex(req.seed);
  const auto& table =
      minor ? detail::kPassacagliaGroundsMinor[variant] : detail::kPassacagliaGroundsMajor[variant];
  const std::vector<std::uint8_t> ground_pitch(table.begin(), table.end());

  // Per-bar harmony + variation start tone for one 8-bar cycle, derived from
  // the ground table. The chord root equals the ground pitch class every bar
  // (quality = the diatonic triad quality on that degree: in minor the V is the
  // harmonic-minor major dominant and a bass B in major is treated as a major
  // root to stay consonant), so every chord-tone anchor is consonant with the
  // held ground; the variation start tone is the ground pitch lifted by octaves
  // into the C4-C5 region. For variant 0 this reproduces the historical plan
  // bar for bar.
  const std::vector<CycleBar> plan = planFromGround(table.data(), table.size(), minor);

  return buildPassacagliaThreeVoice(req, kPassacagliaCycleBars, ground_pitch, plan);
}

}  // namespace bach::composer
