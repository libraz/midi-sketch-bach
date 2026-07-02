#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/arc.h"
#include "composer/character_profile.h"
#include "composer/figuration.h"
#include "composer/figuration_palette.h"
#include "composer/form_builders.h"
#include "composer/harness_fixture.h"
#include "composer/material.h"
#include "composer/minor_material.h"
#include "composer/rule_helpers.h"
#include "composer/span.h"
#include "composer/texture_helpers.h"
#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Ground-bass variation forms: chaconne (4-bar ground, 3/4) and passacaglia
// (8-bar ground, 3/4).
//
// Both forms share one architecture (modelled on the proven Chaconne chaconne
// and Passacaglia passacaglia fixtures, reworked from 4/4 to 3/4 and generalised
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

// Variation figuration palettes (design tables): the pattern idiom each
// non-climax, non-ground variation cycle takes, rotated by (seed + cycle) so
// consecutive cycles alternate idioms. Cycle 0 (the Ground-role establishing
// statement) and the climax cycle are design values and bypass the rotation:
// the chaconne climax is the densest sawtooth, the passacaglia climax the
// densest scalar wave (each form's established peak texture).
//
// Neither palette carries kArpeggio: broken-chord cycles raise the
// melodic-interval cost (the dominant scorer feature). The chaconne has
// always sat close to the model threshold; the passacaglia's former headroom
// is now spent on the held cadential landing (long closing tones carry fewer
// of the stepwise events the corpus distribution rewards), so its rotation
// likewise keeps to the stepwise idioms.
constexpr PatternKind kChaconnePalette[3] = {PatternKind::kSawtooth, PatternKind::kScalarWave,
                                             PatternKind::kFiguraCorta};
constexpr PatternKind kPassacagliaPalette[4] = {PatternKind::kScalarWave, PatternKind::kSawtooth,
                                                PatternKind::kFiguraCorta, PatternKind::kSawtooth};

// Octave lift applied to the passacaglia V0 sawtooth / figura corta center so
// their center +/- octave anchor band stays above the V1 counter-figuration
// band (the scalar-wave band [60, 79] clears it by construction; the
// center-based patterns start an octave lower without this lift).
constexpr int kPassV0CenterLift = 12;

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

// Multi-wave energy arch. The shared arc (form_director) draws a single climax
// swell at ~80% of the piece with the density tier rising and then falling
// monotonically. BWV582's arch instead reads as TWO waves: an intermediate
// swell earlier in the span, then a terraced (stepped, not smoothly
// crescendoed) buildup into the final arch. These constants and helper add that
// second wave locally to the ground-variation builders without touching the
// shared arc.

// Below this cycle count the piece keeps the single-climax arc unchanged; only
// longer grounds have room for a genuine second wave.
constexpr int kMinCyclesForWave = 8;

// The final buildup is held non-decreasing across this many trailing cycles.
constexpr int kTerracedTailCycles = 3;

/**
 * @brief Cycle index of the intermediate swell (the earlier of the two waves).
 * @param cycle_count Number of ground statements in the piece.
 * @param climax_idx The arc's climax cycle index.
 * @return The swell cycle, or @p cycle_count (an out-of-range sentinel) when the
 *         piece is too short (< kMinCyclesForWave cycles) for a second wave.
 * @note Placed at ~60% of the span, but never closer than two cycles below the
 *       climax so the receding cycle just before the climax (the passacaglia
 *       rests its counter-figuration there) stays a genuine dip.
 */
std::size_t midWaveCycle(std::size_t cycle_count, std::size_t climax_idx) {
  if (static_cast<int>(cycle_count) < kMinCyclesForWave)
    return cycle_count;
  std::size_t idx = (cycle_count * 3) / 5;
  if (climax_idx >= 2 && idx > climax_idx - 2)
    idx = climax_idx - 2;
  return idx;
}

/**
 * @brief Shape one cycle's density tier into the two-wave energy arch.
 * @param base_tier The arc-resolved, character-biased tier for this cycle.
 * @param cycle The cycle index being resolved.
 * @param cycle_count Number of ground statements in the piece.
 * @param climax_idx The arc's climax cycle index.
 * @param mid_wave_idx The intermediate-swell cycle (midWaveCycle result).
 * @param climax_tier The resolved tier of the real climax cycle (a ceiling for
 *        the intermediate swell -- the second wave stays below the final arch).
 * @param prev_tier The resolved tier of the immediately preceding cycle, or a
 *        negative value at the first cycle.
 * @return The shaped tier, clamped to [0, 3].
 * @note Intermediate swell: at @p mid_wave_idx the tier is raised one step (a
 *       terraced peak, never above the climax cycle's tier). Terraced final
 *       buildup: over the last kTerracedTailCycles cycles the tier is held
 *       monotonically non-decreasing toward the close, so the arc steps up into
 *       the final arch instead of receding. These are design values, not a
 *       search; the climax cycle's forced tier is never lowered (prev_tier can
 *       only raise a later cycle, never a climax whose tier already dominates).
 */
int shapeWaveTier(int base_tier, std::size_t cycle, std::size_t cycle_count, std::size_t climax_idx,
                  std::size_t mid_wave_idx, int climax_tier, int prev_tier) {
  int tier = base_tier;
  if (cycle == mid_wave_idx && mid_wave_idx < cycle_count && cycle != climax_idx) {
    ++tier;
    if (tier > 3)
      tier = 3;
    if (tier > climax_tier)
      tier = climax_tier;
  }
  if (static_cast<int>(cycle_count) >= kMinCyclesForWave &&
      cycle + static_cast<std::size_t>(kTerracedTailCycles) >= cycle_count && prev_tier >= 0) {
    tier = std::max(tier, prev_tier);
  }
  return tier;
}

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
    // A leading-tone bass (B) takes the dominant in first inversion (a G
    // chord over the B bass), matching the historical chaconne plan row
    // "{7, false, ...}, // V6: ground B (chord G)". The literal triad on B is
    // diminished (its fifth is a tritone against the bass), and "major-ising"
    // the root instead (B - D# - F#) injects two chromatic tones the scale
    // does not contain -- the chord-tone anchor paths that do not flatten
    // out-of-scale tones (consonantChordTone) then sound a D# against the
    // held B ground.
    const std::uint8_t root = (pc == 11) ? static_cast<std::uint8_t>(7) : pc;
    plan.push_back({root, detail::diatonicTriadMinor(root, minor_mode), low, pc});
  }
  return plan;
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
  int prev_emitted = -1;  // last pitch actually pushed (anchor OR oscillation tone).
  int cursor = (band_lo + band_hi) / 2;
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;

  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    detail::ChordSpec chord;
    chord.root_pc = plan.root_pc;
    chord.minor = plan.minor;
    // Nearest DIFFERENT triad tone in band (prefer above): shared by the
    // anti-stall escape and the off-beat oscillation below. Out-of-scale triad
    // tones flatten to the scale tone a semitone below (matching the V0
    // anchor policy in barAnchorPitchClasses) so the counter-line never sounds
    // a chromatic tone against the natural-minor V0 figuration (a B against
    // V0's Bb).
    const int chord_third = chord.minor ? 3 : 4;
    int triad_pc[3] = {chord.root_pc % 12, (chord.root_pc + chord_third) % 12,
                       (chord.root_pc + 7) % 12};
    for (int& pc : triad_pc) {
      if (!detail::inScale(pc, mode))
        pc = (pc + 11) % 12;
    }
    auto is_triad = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      return pcl == triad_pc[0] || pcl == triad_pc[1] || pcl == triad_pc[2];
    };
    auto nearest_other_triad_tone = [&](int from) {
      for (int dist = 1; dist <= 12; ++dist) {
        const int above = from + dist;
        const int below = from - dist;
        if (above <= band_hi && is_triad(above))
          return above;
        if (below >= band_lo && is_triad(below))
          return below;
      }
      return from;
    };
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
      // quarter-note tier that surfaces as a stalled repeated-note line. Any
      // repeated quarter anchor is displaced to the nearest DIFFERENT triad
      // tone in band that is ALSO consonant against every concurrently
      // sounding voice (a triad tone is always consonant with the ground, but
      // the V0 wave may sit on a non-chord tone -- a 6th over the ground --
      // that clashes with one triad member and not another): the reference
      // corpus repeats a pitch on only ~3% of transitions, so even a pair
      // reads as a stall. When no admissible different tone exists the repeat
      // stands -- a repeated consonance beats a fresh clash.
      if (notes_per_beat == 1 && anchor == line_prev) {
        for (int dist = 1; dist <= 12; ++dist) {
          bool placed = false;
          for (int cand : {anchor + dist, anchor - dist}) {
            if (cand < band_lo || cand > band_hi || !is_triad(cand))
              continue;
            bool consonant = true;
            for (int upper : theme_pitches) {
              if (!isConsonantIc(cand - upper)) {
                consonant = false;
                break;
              }
            }
            if (consonant) {
              anchor = cand;
              placed = true;
              break;
            }
          }
          if (placed)
            break;
        }
      }
      // Audible-grain parallel re-check. consonantChordTone judged the anchor's
      // parallel motion at QUARTER grain (previous beat -> this beat), but the
      // union-onset sampling the gate (and the ear) uses pairs this anchor with
      // V1's last EMITTED note and the other voices' last sub-beat onset --
      // V0 runs eighths/sixteenths here, so the audible approach interval is a
      // sixteenth window, not a beat. The anti-stall displacement above is also
      // unvetted for parallels. Re-judge the chosen tone from the last emitted
      // pitch at sixteenth grain and displace to a scale tone that is in band,
      // consonant with every concurrent theme tone, and parallel-free; keep the
      // anchor when no such tone exists (a consonant parallel beats a clash).
      if (prev_emitted >= 0) {
        motions.clear();
        registry.concurrentMotions(beat_tick - kTicksPerBeat / 4, beat_tick, /*voice=*/1,
                                   /*num_voices=*/3, motions);
        auto anchor_is_parallel = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsPerfectParallel(prev_emitted, cand, motion.prev, motion.curr)) {
              return true;
            }
          }
          return false;
        };
        if (anchor_is_parallel(anchor)) {
          bool displaced = false;
          for (int dist = 1; dist <= 7 && !displaced; ++dist) {
            for (int dir : {1, -1}) {
              const int cand = anchor + dir * dist;
              if (cand < band_lo || cand > band_hi || cand == prev_emitted ||
                  !detail::inScale(cand, mode)) {
                continue;
              }
              bool consonant = true;
              for (int upper : theme_pitches) {
                if (!isConsonantIc(cand - upper)) {
                  consonant = false;
                  break;
                }
              }
              if (!consonant || anchor_is_parallel(cand)) {
                continue;
              }
              anchor = cand;
              displaced = true;
              break;
            }
          }
        }
      }
      // Off-beat fills oscillate between the anchor and a consonant companion
      // tone: prefer a stepwise diatonic neighbour that is consonant against
      // the held ground (upper first -- the common figure), falling back to
      // the nearest other triad tone (a broken third) when both neighbours
      // clash. A blind diatonic upper-neighbour oscillation proved too harsh
      // here: it hammered a sustained 9th/7th against the bar-long ground note
      // under the running V0 figuration.
      int osc = -1;
      for (int cand : {detail::scaleUp(anchor, 1, mode), detail::scaleDown(anchor, 1, mode)}) {
        if (cand >= band_lo && cand <= band_hi && isConsonantIc(cand - plan.ground_pc)) {
          osc = cand;
          break;
        }
      }
      if (osc < 0)
        osc = nearest_other_triad_tone(anchor);
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        int pitch = (sub % 2 == 1) ? osc : anchor;
        // The oscillation tones move concurrently with the V0 sixteenths, so
        // they need the same audible-grain parallel re-check as the anchor:
        // when the companion tone lands a parallel against a concurrently
        // moving voice, swap to the mirror neighbour (or the nearest other
        // triad tone) that stays consonant with the held ground.
        if (sub % 2 == 1 && prev_emitted >= 0) {
          motions.clear();
          registry.concurrentMotions(mnote.start_tick - kTicksPerBeat / 4, mnote.start_tick,
                                     /*voice=*/1, /*num_voices=*/3, motions);
          auto osc_is_parallel = [&](int cand) {
            for (const ConcurrentMotion& motion : motions) {
              if (formsPerfectParallel(prev_emitted, cand, motion.prev, motion.curr)) {
                return true;
              }
            }
            return false;
          };
          if (osc_is_parallel(pitch)) {
            for (int cand : {detail::scaleUp(anchor, 1, mode), detail::scaleDown(anchor, 1, mode),
                             nearest_other_triad_tone(anchor)}) {
              if (cand == pitch || cand < band_lo || cand > band_hi)
                continue;
              if (!isConsonantIc(cand - plan.ground_pc))
                continue;
              if (osc_is_parallel(cand))
                continue;
              pitch = cand;
              break;
            }
          }
        }
        mnote.pitch = static_cast<std::uint8_t>(pitch);
        notes.push_back(mnote);
        registry.record(mnote.start_tick, /*voice=*/1, pitch, step);
        prev_emitted = pitch;
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

  // Late-cycle rhythmic intensification (design value): from the final third
  // of the cycles on, the ground restates each bar as repeated same-pitch
  // quarters (the BWV582-style martellato) instead of one dotted half. The
  // pitches never change, so the bar-head skeleton stays immutable. At least
  // one unsplit statement always opens the piece (cycles >= 2 guard).
  if (cycles >= 2) {
    const int split_cycle = cycles - (cycles + 2) / 3;
    out.material.passacaglia_ground_split_from = static_cast<Tick>(split_cycle) * period;
  }

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

  // Two-wave energy arch (intermediate swell + terraced final buildup): the
  // swell cycle and the climax cycle's tier ceiling, resolved once for the loop.
  const std::size_t mid_wave_idx = midWaveCycle(static_cast<std::size_t>(cycles), climax_idx);
  const int climax_tier = cycles > 0 ? densityTierFor(req, climax_idx) : 0;
  int prev_wave_tier = -1;

  // Ornament metadata (fixture fields only, never a note): the climax cycle
  // is the form's real energy peak (all three voices sound, densest wave), so
  // the ornament pass intensifies decoration across exactly this ground cycle.
  out.climax_start_tick = static_cast<Tick>(climax_idx) * period;
  out.climax_end_tick = out.climax_start_tick + period;

  // Registration terraces (fixture fields only, never a note): the organ steps
  // up a stop at every cycle boundary where the terraced schedule ADDS a voice
  // (a voice turning on from the prior cycle), plus one at the intermediate-swell
  // (mid-wave) cycle. Organ dynamics move in terraces, not crescendos.
  for (std::size_t cyc = 1; cyc < static_cast<std::size_t>(cycles); ++cyc) {
    const bool v0_added = v0_present[cyc] && !v0_present[cyc - 1];
    const bool v1_added = v1_present[cyc] && !v1_present[cyc - 1];
    if (v0_added || v1_added) {
      out.registration_step_ticks.push_back(static_cast<Tick>(cyc) * period);
    }
  }
  if (mid_wave_idx < static_cast<std::size_t>(cycles)) {
    out.registration_step_ticks.push_back(static_cast<Tick>(mid_wave_idx) * period);
  }

  // V1 counter-figuration accumulates into a single TrioVoiceLine (voice 1),
  // gated per cycle by the schedule; V0 variation blocks accumulate per cycle.
  std::vector<MaterialNote> counter_notes;
  int prev_v0_last = -1;  // previous variation's closing pitch (seam voice-leading).

  for (int cycle = 0; cycle < cycles; ++cycle) {
    const ArcPoint point = req.arc(static_cast<std::size_t>(cycle));
    const Tick block_start = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    const Tick block_end = block_start + period;
    const bool is_climax = point.is_climax;

    // Shape the cycle's density tier into the two-wave energy arch, resolved for
    // every cycle so the terraced-tail carry sees each prior tier. Both the V0
    // principal variation and the V1 counter-figuration swell from this tier.
    const bool establishing = (cycle == 0);
    const int base_tier = establishing ? 0 : densityTierFor(req, static_cast<std::size_t>(cycle));
    const int cycle_tier =
        shapeWaveTier(base_tier, static_cast<std::size_t>(cycle), static_cast<std::size_t>(cycles),
                      climax_idx, mid_wave_idx, climax_tier, prev_wave_tier);
    prev_wave_tier = cycle_tier;

    // V0 principal variation (when present this cycle). Cycle 0 (when it carries
    // V0) is a plain quarter-note establishing statement; later cycles ride the
    // arc density tier.
    std::vector<MaterialNote> v0_notes;
    if (v0_present[static_cast<std::size_t>(cycle)]) {
      const int tier = cycle_tier;
      const int notes_per_beat = establishing ? 1 : notesPerBeatForTier(tier);
      const bool descending_start = ((req.seed + static_cast<std::uint32_t>(cycle)) % 2) == 1;
      // Pattern selection: the establishing cycle and the climax are design
      // values (quarters / densest scalar wave); other cycles rotate the
      // passacaglia palette so consecutive variations alternate idioms.
      const PatternKind pattern =
          (establishing || is_climax)
              ? PatternKind::kScalarWave
              : kPassacagliaPalette[(req.seed + static_cast<std::uint32_t>(cycle)) % 4];
      auto build_variant = [&](int rotation, std::vector<MaterialNote>& dst) {
        switch (pattern) {
          case PatternKind::kSawtooth:
            appendSawtoothCycle(dst, block_start, cycle_bar_plan,
                                point.register_shift + kPassV0CenterLift, rotation, notes_per_beat,
                                mode);
            break;
          case PatternKind::kArpeggio:
            appendArpeggioCycle(
                dst, block_start, cycle_bar_plan, kPassV0BandLo + point.register_shift,
                kPassV0BandHi + point.register_shift, rotation, notes_per_beat, mode);
            break;
          case PatternKind::kFiguraCorta:
            appendFiguraCortaCycle(dst, block_start, cycle_bar_plan,
                                   point.register_shift + kPassV0CenterLift, rotation, mode);
            break;
          case PatternKind::kScalarWave:
          default:
            appendScalarWaveCycle(dst, block_start, cycle_bar_plan,
                                  kPassV0BandLo + point.register_shift,
                                  kPassV0BandHi + point.register_shift, rotation, descending_start,
                                  notes_per_beat, mode);
            break;
        }
      };
      const int rotation_domain = (pattern == PatternKind::kScalarWave) ? 5 : 4;
      const int seed_rotation =
          static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % rotation_domain);
      build_variant(seed_rotation, v0_notes);
      // Voice-lead the variation seam: when the rotation arithmetic opens the
      // new cycle with a leap beyond a fifth from the previous variation's
      // closing pitch, re-pick the rotation that lands nearest that pitch --
      // but never trade the seam for a rougher interior (a variant adding
      // internal leaps is rejected). Cycles whose seam already connects keep
      // their rotated idiom untouched.
      if (prev_v0_last >= 0 && !v0_notes.empty()) {
        auto internal_leaps = [](const std::vector<MaterialNote>& line) {
          int leaps = 0;
          for (std::size_t i = 1; i < line.size(); ++i) {
            if (std::abs(static_cast<int>(line[i].pitch) - static_cast<int>(line[i - 1].pitch)) > 7)
              ++leaps;
          }
          return leaps;
        };
        const int default_seam = std::abs(static_cast<int>(v0_notes.front().pitch) - prev_v0_last);
        if (default_seam > 7) {
          const int default_leaps = internal_leaps(v0_notes);
          int best_seam = default_seam;
          for (int rot = 0; rot < rotation_domain; ++rot) {
            if (rot == seed_rotation)
              continue;
            std::vector<MaterialNote> trial;
            build_variant(rot, trial);
            if (trial.empty())
              continue;
            const int seam = std::abs(static_cast<int>(trial.front().pitch) - prev_v0_last);
            if (seam < best_seam && internal_leaps(trial) <= default_leaps) {
              best_seam = seam;
              v0_notes = std::move(trial);
            }
          }
        }
      }
      // Audible-grain parallel scrub against the immutable ground. The pattern
      // emitters are pure design lines that never read the ground, so a
      // variation whose bar-opening tones track the ground's stepwise motion
      // chains parallel octaves/fifths against it (the dense-character sweeps
      // measured four in a row). The ground moves only at bar heads, so only a
      // bar-opening note can face a moving ground -- every interior onset is
      // oblique against the held ground tone and needs no check. A parallel
      // bar-opening tone is displaced to the nearest other chord tone of the
      // bar that clears the parallel and keeps both surrounding melodic
      // intervals inside a fifth.
      for (std::size_t i = 1; i < v0_notes.size(); ++i) {
        const Tick t = v0_notes[i].start_tick;
        const int bar = static_cast<int>((t - block_start) / kTicksPerBar34);
        if (bar <= 0 || bar >= cycle_bars || t != block_start + bar_tick(bar))
          continue;
        const Tick t_prev = v0_notes[i - 1].start_tick;
        const int prev_bar = static_cast<int>((t_prev - block_start) / kTicksPerBar34);
        if (prev_bar == bar)
          continue;
        const int g_prev = static_cast<int>(ground_pitch[static_cast<std::size_t>(prev_bar)]);
        const int g_now = static_cast<int>(ground_pitch[static_cast<std::size_t>(bar)]);
        const int prev_pitch = static_cast<int>(v0_notes[i - 1].pitch);
        const int pitch = static_cast<int>(v0_notes[i].pitch);
        if (!formsPerfectParallel(prev_pitch, pitch, g_prev, g_now))
          continue;
        const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
        const int chord_third = plan.minor ? 3 : 4;
        const int triad0 = plan.root_pc % 12;
        const int triad1 = (plan.root_pc + chord_third) % 12;
        const int triad2 = (plan.root_pc + 7) % 12;
        const int next_pitch =
            (i + 1 < v0_notes.size()) ? static_cast<int>(v0_notes[i + 1].pitch) : -1;
        bool placed = false;
        for (int dist = 1; dist <= 7 && !placed; ++dist) {
          for (int cand : {pitch + dist, pitch - dist}) {
            const int pc = ((cand % 12) + 12) % 12;
            if (pc != triad0 && pc != triad1 && pc != triad2)
              continue;
            if (cand == prev_pitch || std::abs(cand - prev_pitch) > 7)
              continue;
            if (next_pitch >= 0 && std::abs(next_pitch - cand) > 7)
              continue;
            // The displaced tone must keep both melodic joins scale-legal (no
            // augmented second into or out of the substitute in minor).
            if (rule_helpers::isForbiddenMelodicLeap(static_cast<std::uint8_t>(prev_pitch),
                                                     static_cast<std::uint8_t>(cand),
                                                     out.harmony) ||
                (next_pitch >= 0 && rule_helpers::isForbiddenMelodicLeap(
                                        static_cast<std::uint8_t>(cand),
                                        static_cast<std::uint8_t>(next_pitch), out.harmony)))
              continue;
            if (formsPerfectParallel(prev_pitch, cand, g_prev, g_now))
              continue;
            v0_notes[i].pitch = static_cast<std::uint8_t>(cand);
            placed = true;
            break;
          }
        }
      }
      if (!v0_notes.empty())
        prev_v0_last = static_cast<int>(v0_notes.back().pitch);

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
      const int v1_tier = (cycle_tier > 0) ? cycle_tier - 1 : 0;
      const int notes_per_beat = notesPerBeatForTier(v1_tier);
      appendCounterFiguration(counter_notes, registry, block_start, cycle_bar_plan,
                              point.register_shift, notes_per_beat, mode);
    }
  }

  // --- Cadential landing over the final two bars. The ground is immutable, so
  // the landing lives in the upper voices: V0 stops its figuration on a
  // full-bar leading tone B (consonant over BOTH penultimate-bar ground
  // designs -- a major third over the dominant G, a sixth over the lament's D
  // -- and over the held V1 dominant) carrying the long cadential trill, then
  // resolves up to a full-bar tonic over the final bar's tonic ground. The
  // leading tone is realized in the octave nearest the figuration's closing
  // register, clamped so the trill's upper neighbour stays inside the organ
  // ornament compass and the close stays above the V1 hold. V1 joins the held
  // close instead of running its counter-figuration through it. ---
  int v0_prefinal = 83;  // V0's landing leading tone (read by the V1 hold below).
  int v0_final = 84;     // V0's closing tonic (the V1 third must stay below it).
  if (!out.material.passacaglia_variations.empty()) {
    PassacagliaVariation& last_var = out.material.passacaglia_variations.back();
    const Tick piece_end = static_cast<Tick>(total_bars) * kTicksPerBar34;
    if (last_var.end_tick == piece_end && !last_var.notes.empty()) {
      // The figuration keeps running through the penultimate bar's first half
      // (its stepwise sixteenths are the form's own language), then lands.
      const Tick landing_tick = piece_end - 2 * kTicksPerBar34 + kTicksPerBar34 / 2;
      int near = static_cast<int>(last_var.notes.back().pitch);
      for (const MaterialNote& note : last_var.notes) {
        if (note.start_tick < landing_tick)
          near = static_cast<int>(note.pitch);
      }
      // The trill degree is the leading tone B by preference (rising B -> C
      // resolution); when the figuration's seam into B would be a tritone
      // (the line stops on an F-class tone), the canonical supertonic trill
      // D -> C takes over -- D is equally consonant over both penultimate
      // ground designs (an octave over the lament's D, a fifth over the
      // dominant G) and the seam becomes a third.
      const bool tritone_seam = (near % 12) == 5;  // an F-class tone precedes B only by tritone.
      const int degree_pc = tritone_seam ? 2 : 11;
      const int up = near + ((degree_pc - (near % 12)) % 12 + 12) % 12;
      int prefinal = (up - near <= near - (up - 12)) ? up : up - 12;
      while (prefinal > 83)
        prefinal -= 12;
      while (prefinal < 67)
        prefinal += 12;
      v0_prefinal = prefinal;
      const int final_tone = tritone_seam ? prefinal - 2 : prefinal + 1;
      v0_final = final_tone;
      last_var.notes.erase(
          std::remove_if(last_var.notes.begin(), last_var.notes.end(),
                         [&](const MaterialNote& note) { return note.start_tick >= landing_tick; }),
          last_var.notes.end());
      MaterialNote held;
      held.start_tick = landing_tick;
      held.duration = kTicksPerBar34 - kTicksPerBar34 / 2;
      held.pitch = static_cast<std::uint8_t>(prefinal);
      last_var.notes.push_back(held);
      MaterialNote last;
      last.start_tick = piece_end - kTicksPerBar34;
      last.duration = kTicksPerBar34;
      last.pitch = static_cast<std::uint8_t>(final_tone);
      last_var.notes.push_back(last);
    }
  }
  if (!counter_notes.empty()) {
    const Tick landing_tick =
        static_cast<Tick>(total_bars - 2) * kTicksPerBar34 + kTicksPerBar34 / 2;
    bool sounds_landing = false;
    for (const MaterialNote& note : counter_notes)
      sounds_landing |= note.start_tick >= landing_tick;
    if (sounds_landing) {
      int prev = 60;
      for (const MaterialNote& note : counter_notes) {
        if (note.start_tick < landing_tick)
          prev = static_cast<int>(note.pitch);
      }
      counter_notes.erase(
          std::remove_if(counter_notes.begin(), counter_notes.end(),
                         [&](const MaterialNote& note) { return note.start_tick >= landing_tick; }),
          counter_notes.end());
      // A dominant-triad arpeggio in eighths: every tone is consonant with
      // both penultimate ground designs AND the trill degree above. The base
      // is voice-led to the G nearest the counter-line's closing register;
      // when the rising shape (G B D) would reach the V0 landing tone, the
      // shape inverts to descend (G D B) instead of leaping the whole figure
      // down an octave.
      const int base = (std::abs(prev - 67) < std::abs(prev - 55)) ? 67 : 55;
      const bool descend = base + 7 >= v0_prefinal;
      static constexpr int kRising[3] = {0, 4, 7};     // G B D.
      static constexpr int kFalling[3] = {0, -5, -8};  // G D B.
      const int* offsets = descend ? kFalling : kRising;
      for (int idx = 0; idx < 3; ++idx) {
        MaterialNote step;
        step.start_tick = landing_tick + static_cast<Tick>(idx) * (kTicksPerBar34 / 6);
        step.duration = kTicksPerBar34 / 6;
        step.pitch = static_cast<std::uint8_t>(base + offsets[idx]);
        counter_notes.push_back(step);
      }
      MaterialNote held;
      held.start_tick = static_cast<Tick>(total_bars - 1) * kTicksPerBar34;
      held.duration = kTicksPerBar34;
      // The closing third (E / Eb) arrives by step from the arpeggio's fifth,
      // folded down by octaves so the inner voice stays below V0's tonic close.
      int third = base + ((minor && !picardy) ? 8 : 9);
      while (third >= v0_final)
        third -= 12;
      held.pitch = static_cast<std::uint8_t>(third);
      counter_notes.push_back(held);
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

  // Ornament metadata (fixture fields only, never a note): the climax cycle
  // (~80% of the cycle span, matching arcPoint's design climax) is the form's
  // real energy peak, so the ornament pass intensifies decoration across
  // exactly this ground cycle.
  std::size_t climax_idx = (static_cast<std::size_t>(cycles) * 4) / 5;
  if (cycles > 0 && climax_idx > static_cast<std::size_t>(cycles) - 1)
    climax_idx = static_cast<std::size_t>(cycles) - 1;
  out.climax_start_tick = static_cast<Tick>(climax_idx) * period;
  out.climax_end_tick = out.climax_start_tick + period;

  // Two-wave energy arch (intermediate swell + terraced final buildup): the
  // swell cycle and the climax cycle's tier ceiling, resolved once for the loop.
  const std::size_t mid_wave_idx = midWaveCycle(static_cast<std::size_t>(cycles), climax_idx);
  const int climax_tier = cycles > 0 ? densityTierFor(req, climax_idx) : 0;
  int prev_wave_tier = -1;

  // Registration terraces (fixture fields only, never a note): the chaconne's
  // one structural energy addition the organ terraces is the intermediate-swell
  // (mid-wave) cycle -- present only on long grounds; the climax is already the
  // macro arc's peak. Organ dynamics move in terraces, not crescendos.
  if (mid_wave_idx < static_cast<std::size_t>(cycles)) {
    out.registration_step_ticks.push_back(static_cast<Tick>(mid_wave_idx) * period);
  }

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
    // Shape into the two-wave energy arch (intermediate swell + terraced
    // final buildup); track the prior resolved tier for the terraced carry.
    tier = shapeWaveTier(tier, static_cast<std::size_t>(cycle), static_cast<std::size_t>(cycles),
                         climax_idx, mid_wave_idx, climax_tier, prev_wave_tier);
    prev_wave_tier = tier;
    int notes_per_beat = ground_role ? 1 : notesPerBeatForTier(tier);
    // Anchor rotation: which chord tone opens each bar's anchor group rotates by
    // (seed + cycle), so consecutive cycles trace different anchor contours.
    const int anchor_rotation =
        static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % 4);
    const Tick block_start = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    const Tick block_end = block_start + period;

    // Pattern selection: the Ground-role cycle and the climax are design values
    // (quarters / densest sawtooth); other cycles rotate the chaconne palette so
    // consecutive variations alternate figuration idioms.
    const PatternKind pattern =
        (ground_role || point.is_climax)
            ? PatternKind::kSawtooth
            : kChaconnePalette[(req.seed + static_cast<std::uint32_t>(cycle)) % 3];
    // Register band for the band-confined patterns: the variation tessitura
    // anchored on the first bar's start tone, an octave-and-a-fifth wide
    // (matching the scalar-wave band proportions).
    const int band_lo = cycle_bar_plan.front().low_tone + point.register_shift;
    const int band_hi = band_lo + 19;

    std::vector<MaterialNote> notes;
    switch (pattern) {
      case PatternKind::kScalarWave: {
        const int phase_rotation =
            static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % 5);
        const bool descending_start = ((req.seed + static_cast<std::uint32_t>(cycle)) % 2) == 1;
        appendScalarWaveCycle(notes, block_start, cycle_bar_plan, band_lo, band_hi, phase_rotation,
                              descending_start, notes_per_beat, mode);
        break;
      }
      case PatternKind::kArpeggio:
        appendArpeggioCycle(notes, block_start, cycle_bar_plan, band_lo, band_hi,
                            static_cast<int>((req.seed + static_cast<std::uint32_t>(cycle)) % 4),
                            notes_per_beat, mode);
        break;
      case PatternKind::kFiguraCorta:
        appendFiguraCortaCycle(notes, block_start, cycle_bar_plan, point.register_shift,
                               anchor_rotation, mode);
        break;
      case PatternKind::kSawtooth:
      default:
        appendSawtoothCycle(notes, block_start, cycle_bar_plan, point.register_shift,
                            anchor_rotation, notes_per_beat, mode);
        break;
    }

    // Compact cadential landing on the piece's final bar. Every chaconne /
    // ground cycle ends on the dominant, so the dominant arrives only in the
    // final bar (whose harmony is overridden to the tonic above): the bar
    // splits into a held supertonic D over its first half (the cadential
    // trill site, a fifth over the immutable dominant ground) resolving to a
    // held tonic over the second half.
    if (cycle == cycles - 1 && !notes.empty()) {
      const int near = static_cast<int>(notes.back().pitch);
      const int up = near + ((2 - (near % 12)) % 12 + 12) % 12;  // supertonic at/above.
      int prefinal = (up - near <= near - (up - 12)) ? up : up - 12;
      // Settle the close in the variation's home octave (D5 -> C5): below the
      // organ ornament compass (the trill's upper neighbour must stay
      // playable under the late-cycle register lift) and above the lower
      // voices.
      while (prefinal > 81)
        prefinal -= 12;
      while (prefinal < 67)
        prefinal += 12;
      appendCompactCadentialLanding(notes, static_cast<Tick>(total_bars - 1) * kTicksPerBar34,
                                    kTicksPerBar34, prefinal, prefinal - 2);
    }

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
