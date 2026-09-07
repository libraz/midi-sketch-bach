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

// A ground bass is recognised by its pitch sequence and by the metric positions
// those pitches fall on, so a returning statement can be re-articulated without
// ceasing to be the same ground. Every treatment below keeps each bar's
// structural tone on the bar head at its own pitch and subdivides only the span
// it fills: no tone moves, none is added, none is dropped. Attack counts rise
// down the list and the statements take them in that order, so the ground is
// announced whole and is progressively broken up as the variations accumulate
// over it -- the diminution the form is named for, heard in the bass.
enum class GroundArticulation {
  kSustained,       // one dotted half: the ground as first stated.
  kUpperNeighbour,  // the tone leaves the bar through the scale step above.
  kLowerNeighbour,  // the tone leaves the bar through the scale step below.
  kNeighbourTurn,   // the tone turns above and back before the ground steps on.
};

// The treatments the statements after the opening one rotate through, in rising
// order of subdivision.
constexpr GroundArticulation kGroundGrowth[3] = {GroundArticulation::kUpperNeighbour,
                                                 GroundArticulation::kLowerNeighbour,
                                                 GroundArticulation::kNeighbourTurn};

/**
 * @brief The articulation a ground statement takes.
 * @param cycle Statement index in [0, cycle_count).
 * @return kSustained for the opening statement -- the ground is heard whole
 *         before it is broken up -- then the growth rotation.
 */
GroundArticulation groundArticulationFor(int cycle) {
  if (cycle <= 0)
    return GroundArticulation::kSustained;
  return kGroundGrowth[static_cast<std::size_t>((cycle - 1) % 3)];
}

/**
 * @brief Emit one bar of the ground under one articulation.
 * @param out Receives the bar's notes, appended in time order.
 * @param bar_start Tick of the bar head.
 * @param pitch The bar's structural tone.
 * @param articulation The treatment this statement takes.
 * @param mode Diatonic mode the decorating tone is drawn from.
 * @note The decorating tone is one diatonic scale degree from the structural
 *       tone and the bar returns to that tone before the ground steps on, so
 *       every interval the bar adds is a step and the skeleton still sounds on
 *       the bar head and on the beat the ground leaves from.
 */
void appendGroundBar(std::vector<MaterialNote>& out, Tick bar_start, std::uint8_t pitch,
                     GroundArticulation articulation, detail::Mode mode) {
  const auto emit = [&](Tick offset, Tick duration, int tone) {
    MaterialNote gnote;
    gnote.start_tick = bar_start + offset;
    gnote.duration = duration;
    gnote.pitch = static_cast<std::uint8_t>(tone);
    out.push_back(gnote);
  };
  const int tone = static_cast<int>(pitch);
  const int above = detail::scaleUp(tone, 1, mode);
  const int below = detail::scaleDown(tone, 1, mode);
  // Every decorating tone falls inside the last beat. The structural tone holds
  // every beat onset of the bar, which is where this form judges its verticals:
  // the decoration is the ornamental approach to the ground's next step, not a
  // tone the harmony has to account for.
  constexpr Tick kDecorationStart = 2 * kTicksPerBeat + kTicksPerBeat / 2;
  constexpr Tick kEighth = kTicksPerBeat / 2;
  switch (articulation) {
    case GroundArticulation::kUpperNeighbour:
      emit(0, kDecorationStart, tone);
      emit(kDecorationStart, kEighth, above);
      break;
    case GroundArticulation::kLowerNeighbour:
      emit(0, kDecorationStart, tone);
      emit(kDecorationStart, kEighth, below);
      break;
    case GroundArticulation::kNeighbourTurn:
      emit(0, kDecorationStart, tone);
      emit(kDecorationStart, kEighth / 2, above);
      emit(kDecorationStart + kEighth / 2, kEighth / 2, tone);
      break;
    case GroundArticulation::kSustained:
    default:
      emit(0, kTicksPerBar34, tone);
      break;
  }
}

/**
 * @brief Emit the ground bass for every statement of the piece.
 *
 * The material carries the whole line rather than one cycle for the replay
 * branch to tile, because consecutive statements no longer share an
 * articulation. The caller declares the emitted length as the replay period, so
 * the period-tiled replay lays the line down exactly once.
 *
 * @param out Receives the ground, appended in time order.
 * @param total_bars Bars in the piece.
 * @param cycle_bars Bars per ground statement.
 * @param ground_pitch The cycle-relative structural tones, one per bar.
 * @param sustained_from_cycle First statement left sustained in the material.
 *        The passacaglia hands its late statements to the replay branch's own
 *        restatement device and stops decorating here; a form without that
 *        device passes the statement count so every statement is treated here.
 * @param sounding_bars Bars the ground carrier actually sounds for. Its last bar
 *        stays sustained: the ground hands over to whatever follows -- for the
 *        chaconne an authored cadential coda -- on a plain structural tone.
 * @param mode Diatonic mode the decorating tones are drawn from.
 */
void appendGroundStatements(std::vector<MaterialNote>& out, int total_bars, int cycle_bars,
                            const std::vector<std::uint8_t>& ground_pitch, int sustained_from_cycle,
                            int sounding_bars, detail::Mode mode) {
  const auto pitchAtBar = [&](int bar) {
    return ground_pitch[static_cast<std::size_t>(bar % cycle_bars)];
  };
  for (int bar = 0; bar < total_bars; ++bar) {
    const int cycle = bar / cycle_bars;
    GroundArticulation articulation = cycle >= sustained_from_cycle ? GroundArticulation::kSustained
                                                                    : groundArticulationFor(cycle);
    // A bar whose successor restates the same tone stays held: the decoration
    // closes by returning to the structural tone, so decorating this bar would
    // put that tone against its own restatement across the bar line as a
    // unison. The tone is held instead -- the phrase-end hold before the
    // relaunch.
    if (bar + 1 < total_bars && pitchAtBar(bar + 1) == pitchAtBar(bar))
      articulation = GroundArticulation::kSustained;
    if (bar + 1 >= sounding_bars)
      articulation = GroundArticulation::kSustained;
    appendGroundBar(out, static_cast<Tick>(bar) * kTicksPerBar34, pitchAtBar(bar), articulation,
                    mode);
  }
}

/**
 * @brief The ground tone sounding on every beat of a window.
 * @param ground The emitted ground line for the whole piece, in time order.
 * @param start_tick First tick of the window.
 * @param bars Bars in the window.
 * @return Three tones per bar. A beat inside a held tone reports that tone, so
 *         the voices written over the ground read it at the grain a decorated
 *         statement moves at rather than at bar grain.
 */
std::vector<std::uint8_t> groundBeatTones(const std::vector<MaterialNote>& ground, Tick start_tick,
                                          int bars) {
  std::vector<std::uint8_t> tones(static_cast<std::size_t>(bars) * 3, 0);
  std::uint8_t last = ground.empty() ? 0 : ground.front().pitch;
  for (std::size_t idx = 0; idx < tones.size(); ++idx) {
    const Tick tick = start_tick + static_cast<Tick>(idx) * kTicksPerBeat;
    for (const MaterialNote& note : ground) {
      if (note.start_tick <= tick && tick < note.start_tick + note.duration)
        last = note.pitch;
    }
    tones[idx] = last;
  }
  return tones;
}

/// @brief Whether the ground and one line above it reach a perfect interval by
///        a forbidden motion anywhere in [from_tick, to_tick].
///
/// Sampled at the union of the two lines' onsets, which is the succession a
/// listener hears and the pairing the counterpoint audit reads.
bool formsPerfectMotionAgainst(const std::vector<MaterialNote>& ground,
                               const std::vector<MaterialNote>& upper, Tick from_tick,
                               Tick to_tick) {
  std::vector<Tick> onsets;
  for (const std::vector<MaterialNote>* line : {&ground, &upper}) {
    for (const MaterialNote& note : *line) {
      if (note.start_tick >= from_tick && note.start_tick <= to_tick)
        onsets.push_back(note.start_tick);
    }
  }
  std::sort(onsets.begin(), onsets.end());
  onsets.erase(std::unique(onsets.begin(), onsets.end()), onsets.end());
  int prev_low = -1;
  int prev_high = -1;
  for (const Tick tick : onsets) {
    const int low = soundingMaterialPitch(ground, tick);
    const int high = soundingMaterialPitch(upper, tick);
    if (low >= 0 && high >= 0 && prev_low >= 0 && prev_high >= 0 &&
        (formsPerfectParallel(prev_high, high, prev_low, low) ||
         formsAntiParallelPerfect(prev_high, high, prev_low, low) ||
         formsBattuta(prev_high, high, prev_low, low))) {
      return true;
    }
    prev_low = low;
    prev_high = high;
  }
  return false;
}

/**
 * @brief Withdraw a bar's diminution wherever it walks into a voice above it.
 *
 * The decoration is written before the voices over the ground, so those voices
 * are vetted against a bass that already moves inside the bar. Withdrawing a
 * decoration afterwards only leaves the bass more static under them, and a held
 * tone moves obliquely against everything, so no placement this pass reverts can
 * turn a clean succession into a fault WITHIN the bar. A withdrawn bar returns
 * to the plain sustained statement.
 *
 * That guarantee stops at the bar line. The decoration is the last tone of its
 * bar, so it is also what the next bar's first onset is heard against; taking it
 * back changes that reference and can turn a clean seam into a fault. A voice
 * written over the ground must therefore be vetted against the bar-head
 * skeleton -- what the bar keeps whether or not its decoration survives -- and
 * not against the decorated surface.
 *
 * @param ground The emitted ground line, edited in place.
 * @param upper The lines written above it.
 * @param total_bars Bars in the piece.
 */
void withdrawClashingDiminution(std::vector<MaterialNote>& ground,
                                const std::vector<const std::vector<MaterialNote>*>& upper,
                                int total_bars) {
  for (int bar = 0; bar < total_bars; ++bar) {
    const Tick bar_start = static_cast<Tick>(bar) * kTicksPerBar34;
    const Tick bar_end = bar_start + kTicksPerBar34;
    std::size_t first = ground.size();
    std::size_t last = 0;
    for (std::size_t idx = 0; idx < ground.size(); ++idx) {
      if (ground[idx].start_tick >= bar_start && ground[idx].start_tick < bar_end) {
        first = std::min(first, idx);
        last = std::max(last, idx);
      }
    }
    if (first >= ground.size() || last == first)
      continue;  // an undecorated bar has nothing to withdraw.
    // One beat either side: the motions INTO the bar's first decorating tone and
    // OUT of its last are as much successions as the ones inside it.
    const Tick from_tick = bar_start >= kTicksPerBeat ? bar_start - kTicksPerBeat : 0;
    const Tick to_tick = bar_end + kTicksPerBeat;
    const auto clashes = [&](const std::vector<MaterialNote>& line) {
      for (const std::vector<MaterialNote>* voice : upper) {
        if (formsPerfectMotionAgainst(line, *voice, from_tick, to_tick))
          return true;
      }
      return false;
    };
    if (!clashes(ground))
      continue;
    MaterialNote held = ground[first];
    held.duration = kTicksPerBar34;
    ground.erase(ground.begin() + static_cast<std::ptrdiff_t>(first),
                 ground.begin() + static_cast<std::ptrdiff_t>(last) + 1);
    ground.insert(ground.begin() + static_cast<std::ptrdiff_t>(first), held);
  }
}

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

/// @brief Spell the cycle's dominant with its seventh.
///
/// The ground repeats, so the plan is read as a cycle: the closing bar's
/// successor is the bar that opens the next statement. That wrap is exactly
/// where a ground bass puts its dominant -- every table here ends on the fifth
/// degree and restarts on the tonic -- so reading the plan linearly, as a
/// through-composed section is read, would find no dominant at all.
///
/// `triad_only_bars` are cycle-relative. A bar that already carries a declared
/// suspension is one of them: the suspension is this form's accented dissonance,
/// authored with its own preparation and resolution and verified as a pattern,
/// and the search that installs it picks the tone under it from the bar's chord.
/// Spelling that same bar with a seventh moves the figuration the search reads
/// and can leave the cadence with no admissible suspension at all -- trading a
/// prepared dissonance the form is built on for an unprepared one it is not.
void markCycleDominantSevenths(std::vector<CycleBar>& plan, detail::Mode mode,
                               const std::vector<int>& triad_only_bars) {
  std::vector<detail::ChordSpec> spelling;
  spelling.reserve(plan.size());
  for (const CycleBar& bar : plan)
    spelling.push_back({bar.root_pc, bar.minor});
  markDominantSevenths(spelling, triad_only_bars, mode, /*cyclic=*/true);
  for (std::size_t bar = 0; bar < plan.size(); ++bar)
    plan[bar].seventh = spelling[bar].seventh;
}

// Compass the ground-parallel repair may relocate a V0 onset into: the C4
// region, where the variation sits above every lower voice, up to the top of
// the register the ornament pass can decorate. A cycle whose figuration is
// lifted by the arc raises the floor with it (see the repair's band_lo).
constexpr int kV0RepairFloor = 60;
constexpr int kV0RepairCeiling = 91;

// The chaconne coda's dominant bass (G2): the tone the bass steps to on the
// penultimate bar's closing beat before stating the tonic under the final bar.
constexpr int kCodaBassDominant = 43;

/// @brief Displace V0 beat onsets that form a moving-ground parallel perfect.
///
/// The variation palette emitters deliberately know nothing about the immutable
/// ground. Apply this once after either form has emitted a cycle so passacaglia
/// and chaconne share identical repair semantics.
///
/// EVERY beat onset is inspected, not only the bar head: the anchor rotation
/// decides which beat of the bar carries the chord root, so a root chain that
/// tracks the ground can sit on any beat of the metre.  Each onset is judged
/// against two references, and a replacement must clear both:
///   - the immediately preceding onset of this voice, which is the succession a
///     listener hears and the one the counterpoint audit reads; and
///   - the same beat of the previous bar, the chain the ground's bar-rate motion
///     turns into consecutive perfects at that metrical position.
///
/// @param band_lo Lowest pitch a relocation may take. The repair changes
///        register freely, so this floor is what keeps the variation above the
///        voices under it: a cycle the arc lifts must raise it by the same
///        shift, or a repaired onset drops into the middle voice's band.
/// @param every_voice_placed Whether the two contrary-motion faults may also be
///        repaired here. Every candidate is judged against the ground and
///        nothing else, so in a texture that still has a voice to come the
///        relocation is unverified against it -- and the wider the remit, the
///        more onsets move and the more often that unverified choice is the
///        one that ships. In the two-voice chaconne there is nothing left to
///        verify against and the wider remit removes the form's characteristic
///        fault outright; in the three-voice passacaglia, whose counter
///        figuration is written after this scan, it measurably traded parallel
///        fifths for parallel octaves, which is the wrong direction.
/// @param ground_pitch The ground tone sounding on every beat of this block,
///        three entries per bar (groundBeatTones). A statement the diminution
///        decorates moves inside the bar, so the reference is per beat.
void scrubGroundParallels(std::vector<MaterialNote>& notes, Tick block_start,
                          const std::vector<std::uint8_t>& ground_pitch,
                          const std::vector<CycleBar>& cycle_bar_plan, const HarmonicPlan& harmony,
                          int preceding_v0_bar_head = -1, int preceding_ground_pitch = -1,
                          int preceding_v0_last = -1, int band_lo = kV0RepairFloor,
                          bool every_voice_placed = true) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  // Realized V0 pitch per beat: the previous bar's (the chain reference, seeded
  // at the cycle seam with the caller's closing bar head) and this bar's.
  int prior_beat_pitch[3] = {preceding_v0_bar_head, -1, -1};
  int prior_bar = -1;
  int beat_pitch[3] = {-1, -1, -1};
  int scanned_bar = -1;
  // The onset immediately before the one under test, with the ground tone held
  // under it -- the pair the perfect-motion audit samples. Seeded at the block
  // seam with the caller's closing onset, for the same reason the bar chain is:
  // the audit reads one continuous line, so the first onset of a block is not
  // the first onset of anything and leaving it unreferenced makes every seam a
  // hole. The ground under that closing onset is the previous block's last bar
  // tone, which is what preceding_ground_pitch carries.
  int previous_onset_pitch = preceding_v0_last;
  int previous_onset_ground = preceding_ground_pitch;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    const Tick tick = notes[i].start_tick;
    const Tick offset = tick - block_start;
    const int bar = static_cast<int>(offset / kTicksPerBar34);
    if (bar < 0 || bar >= cycle_bars)
      continue;
    if (bar != scanned_bar) {
      if (scanned_bar >= 0) {
        for (int beat = 0; beat < 3; ++beat)
          prior_beat_pitch[beat] = beat_pitch[beat];
        prior_bar = scanned_bar;
      }
      for (int& pitch_at_beat : beat_pitch)
        pitch_at_beat = -1;
      scanned_bar = bar;
    }
    const Tick tick_in_bar = offset - static_cast<Tick>(bar) * kTicksPerBar34;
    // The ground is read at the grain it moves at. A statement the diminution
    // decorates changes tone inside the bar, so a bar-grain reference would vet
    // the figuration against a tone the bass has already left -- the succession
    // the audit hears is the one against the tone actually sounding here.
    const int ground_now = static_cast<int>(
        ground_pitch[static_cast<std::size_t>(bar) * 3 + tick_in_bar / kTicksPerBeat]);
    const int pitch = static_cast<int>(notes[i].pitch);
    if (tick_in_bar % kTicksPerBeat != 0) {
      previous_onset_pitch = pitch;  // sub-beat fill: never an anchor position.
      previous_onset_ground = ground_now;
      continue;
    }
    const int beat = static_cast<int>(tick_in_bar / kTicksPerBeat);
    // Same beat of the previous bar, at the same grain.
    const int prior_bar_ground =
        prior_bar >= 0 ? static_cast<int>(ground_pitch[static_cast<std::size_t>(prior_bar) * 3 +
                                                       static_cast<std::size_t>(beat)])
                       : preceding_ground_pitch;
    // Either reference alone leaves a real chain in place: the bar-to-bar chain
    // is invisible to the preceding onset when the two are inside one held
    // ground tone, and the audited succession is invisible to the chain when the
    // figuration turns between them.
    //
    // Ranked, not pooled. Over a ground that rises while the figuration falls,
    // the same-direction test alone is nearly blind: the two lines here almost
    // never move together, so what the audit hears as a perfect arrival reaches
    // it by contrary motion -- as a battuta when the figuration leaps down onto
    // it, as an anti-parallel when the two exchange one perfect interval for
    // another. Folding those into the boolean would be worse than leaving them
    // out, because a candidate search that cannot find a fully clean tone keeps
    // the ORIGINAL, and some originals are true parallels. The rank instead
    // lets a replacement be accepted for being less bad.
    const auto groundFaultRank = [&](int candidate) {
      const int reference[2][2] = {{previous_onset_pitch, previous_onset_ground},
                                   {prior_beat_pitch[beat], prior_bar_ground}};
      int worst = 0;
      for (const auto& pair : reference) {
        if (formsPerfectParallel(pair[0], candidate, pair[1], ground_now))
          return 3;
        if (!every_voice_placed)
          continue;
        if (formsAntiParallelPerfect(pair[0], candidate, pair[1], ground_now))
          worst = std::max(worst, 2);
        else if (formsBattuta(pair[0], candidate, pair[1], ground_now))
          worst = std::max(worst, 1);
      }
      return worst;
    };
    const int original_rank = groundFaultRank(pitch);
    const int prev_pitch = previous_onset_pitch;
    if (original_rank == 0) {
      beat_pitch[beat] = pitch;
      previous_onset_pitch = pitch;
      previous_onset_ground = ground_now;
      continue;
    }

    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    const int chord_third = plan.minor ? 3 : 4;
    // The relocation targets are the bar's chord tones, the declared seventh
    // among them. Reading a bare triad here leaves the repair a strictly
    // narrower set than the emitters it is repairing were given, and on a
    // dominant bar the tone it is missing is the one that clears a fault the
    // triad cannot: every triad tone of a dominant sits a perfect interval or a
    // third from the ground it is the bass of, while the seventh sits a seventh
    // away and so can never itself arrive as a perfect.
    const int chord_pc[4] = {
        plan.root_pc % 12, (plan.root_pc + chord_third) % 12, (plan.root_pc + 7) % 12,
        detail::chordSeventhPc(detail::ChordSpec{plan.root_pc, plan.minor, true})};
    const int chord_tones = plan.seventh ? 4 : 3;
    const auto isRelocationTarget = [&](int candidate) {
      const int pitch_class = ((candidate % 12) + 12) % 12;
      for (int tone = 0; tone < chord_tones; ++tone) {
        if (pitch_class == chord_pc[tone])
          return true;
      }
      return false;
    };
    const int melodic_prev = i > 0 ? static_cast<int>(notes[i - 1].pitch) : -1;
    const int next_pitch = (i + 1 < notes.size()) ? static_cast<int>(notes[i + 1].pitch) : -1;
    bool placed = false;
    // How far the onset may travel depends on where it sits in the bar. A bar
    // head is a structural arrival, and taking the chord tone in another octave
    // there is idiomatic -- restricting a downbeat to its neighbours' stepwise
    // contour leaves a large class of structural parallels untouched. An onset
    // inside the bar is a note of a running figure, so it may only be exchanged
    // for a tone a step away: the line keeps its conjunct surface, and if no
    // neighbour works the parallel stands. A repair that trades a contrapuntal
    // blemish for a hole in the melody is not a repair.
    const int max_displacement = (beat == 0) ? kV0RepairCeiling - band_lo : 2;
    const auto createsMinorAugmentedSecond = [](int from, int to) {
      const int from_pc = ((from % 12) + 12) % 12;
      const int to_pc = ((to % 12) + 12) % 12;
      return (from_pc == 8 && to_pc == 11) || (from_pc == 11 && to_pc == 8);
    };
    // Cleanest first, and never worse than what is already there: the triad
    // tier and the contextual-scale tier below are both re-offered at each
    // accept level, so a clean scale tone is preferred over a triad tone that
    // only downgrades the fault.
    for (int accept = 0; accept < original_rank && !placed; ++accept) {
      for (int dist = 1; dist <= max_displacement && !placed; ++dist) {
        for (int cand : {pitch + dist, pitch - dist}) {
          if (!isRelocationTarget(cand)) {
            continue;
          }
          // The declared seventh is the one chord tone whose dissonance against
          // the ground is the harmony rather than a fault, so it is the one
          // relocation target the consonance filter must not reject.
          const bool is_declared_seventh = plan.seventh && ((cand % 12) + 12) % 12 == chord_pc[3];
          if (cand < band_lo || cand > kV0RepairCeiling || cand == prev_pitch ||
              (!is_declared_seventh && !rule_helpers::isConsonantInterval(cand - ground_now))) {
            continue;
          }
          if ((melodic_prev >= 0 && createsMinorAugmentedSecond(melodic_prev, cand)) ||
              (next_pitch >= 0 && createsMinorAugmentedSecond(cand, next_pitch))) {
            continue;
          }
          if (groundFaultRank(cand) > accept) {
            continue;
          }
          notes[i].pitch = static_cast<std::uint8_t>(cand);
          placed = true;
          break;
        }
      }
      // At a leading-tone bass the structural triad is deliberately a dominant
      // in first inversion.  If all of those tones would retain the fault, use
      // another contextual scale tone that is still consonant above the bass
      // rather than leave the perfect motion in place.
      for (int dist = 1; dist <= max_displacement && !placed; ++dist) {
        for (int cand : {pitch + dist, pitch - dist}) {
          if (cand < band_lo || cand > kV0RepairCeiling || cand == prev_pitch ||
              !rule_helpers::isConsonantInterval(cand - ground_now) ||
              !rule_helpers::isContextualScalePitch(static_cast<std::uint8_t>(cand), harmony, tick,
                                                    cand - pitch) ||
              (melodic_prev >= 0 && createsMinorAugmentedSecond(melodic_prev, cand)) ||
              (next_pitch >= 0 && createsMinorAugmentedSecond(cand, next_pitch)) ||
              groundFaultRank(cand) > accept) {
            continue;
          }
          notes[i].pitch = static_cast<std::uint8_t>(cand);
          placed = true;
          break;
        }
      }
    }
    // Whether the onset was relocated or had to keep its pitch, it becomes the
    // reference both chains read from here on.
    beat_pitch[beat] = static_cast<int>(notes[i].pitch);
    previous_onset_pitch = static_cast<int>(notes[i].pitch);
    previous_onset_ground = ground_now;
  }
}

int pitchAtBarHead(const std::vector<MaterialNote>& notes, Tick tick) {
  for (const MaterialNote& note : notes) {
    if (note.start_tick == tick)
      return static_cast<int>(note.pitch);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Middle-voice machinery. Both ground forms carry a line between the variation
// and the ground that realises the harmony the ground implies;
// appendCounterFiguration writes it, and each form supplies its own register
// band. The passacaglia's three-voice assembly (resolveVoiceSchedule and
// buildPassacagliaThreeVoice) is reached only from buildPassacagliaForm.
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
 * @param band_lo Lowest pitch the line may take (above every voice under it).
 * @param band_hi Highest pitch the line may take (below every voice over it).
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendCounterFiguration(std::vector<MaterialNote>& notes, ThemeToneRegistry& registry,
                             Tick block_start, const std::vector<CycleBar>& cycle_bar_plan,
                             int band_lo, int band_hi, int notes_per_beat, detail::Mode mode) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  const Tick step = kTicksPerBeat / static_cast<Tick>(notes_per_beat);

  int line_prev = -1;
  int prev_emitted = -1;  // last pitch actually pushed (anchor OR oscillation tone).
  // The counter-line is ONE continuous voice across ground cycles, and this
  // vector already holds the previous cycle's tail whenever the schedule kept V1
  // sounding. Opening each cycle with no history disabled both parallel tests on
  // its first beat -- consonantChordTone reads a negative previous pitch as "no
  // motion to judge", and the audible-grain re-check below is skipped outright
  // -- so the cycle seam shipped this form's largest single fault group.
  if (!notes.empty() && notes.back().start_tick + notes.back().duration == block_start) {
    prev_emitted = static_cast<int>(notes.back().pitch);
    line_prev = prev_emitted;
  }
  int cursor = (band_lo + band_hi) / 2;
  std::vector<int> theme_pitches;
  std::vector<ConcurrentMotion> motions;

  for (int bar = 0; bar < cycle_bars; ++bar) {
    const CycleBar& plan = cycle_bar_plan[static_cast<std::size_t>(bar)];
    detail::ChordSpec chord;
    chord.root_pc = plan.root_pc;
    chord.minor = plan.minor;
    chord.seventh = plan.seventh;
    // Nearest DIFFERENT chord tone in band (prefer above): shared by the
    // anti-stall escape and the off-beat oscillation below. Out-of-scale triad
    // tones flatten to the scale tone a semitone below (matching the V0
    // anchor policy in barAnchorPitchClasses) so the counter-line never sounds
    // a chromatic tone against the natural-minor V0 figuration (a B against
    // V0's Bb). The declared seventh joins the set: reading a bare triad here
    // while the beat anchor reads four tones would let the escape push the line
    // off a seventh the selector deliberately placed, or judge the chord spent
    // while one of its tones was still free.
    const int chord_third = chord.minor ? 3 : 4;
    int chord_pc[4] = {chord.root_pc % 12, (chord.root_pc + chord_third) % 12,
                       (chord.root_pc + 7) % 12, detail::chordSeventhPc(chord)};
    for (int& pc : chord_pc) {
      if (!detail::inScale(pc, mode))
        pc = (pc + 11) % 12;
    }
    const int chord_tones = chord.seventh ? 4 : 3;
    auto is_chord_tone = [&](int midi) {
      const int pcl = ((midi % 12) + 12) % 12;
      for (int tone = 0; tone < chord_tones; ++tone) {
        if (pcl == chord_pc[tone])
          return true;
      }
      return false;
    };
    auto nearest_other_chord_tone = [&](int from) {
      for (int dist = 1; dist <= 12; ++dist) {
        const int above = from + dist;
        const int below = from - dist;
        if (above <= band_hi && is_chord_tone(above))
          return above;
        if (below >= band_lo && is_chord_tone(below))
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
      // corpus almost never repeats a pitch, so even a pair reads as a stall.
      // When no admissible different tone exists the repeat
      // stands -- a repeated consonance beats a fresh clash.
      if (notes_per_beat == 1 && anchor == line_prev) {
        for (int dist = 1; dist <= 12; ++dist) {
          bool placed = false;
          for (int cand : {anchor + dist, anchor - dist}) {
            if (cand < band_lo || cand > band_hi || !is_chord_tone(cand))
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
        auto anchor_is_true_parallel = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsStrictPerfectParallel(prev_emitted, cand, motion.prev, motion.curr)) {
              return true;
            }
          }
          return false;
        };
        auto anchor_has_battuta = [&](int cand) {
          for (const ConcurrentMotion& motion : motions) {
            if (formsBattuta(prev_emitted, cand, motion.prev, motion.curr)) {
              return true;
            }
          }
          return false;
        };
        if (anchor_is_parallel(anchor) || anchor_has_battuta(anchor)) {
          // Two passes over the same candidates. The first demands full
          // parallel-freedom. The second runs only when the anchor is a TRUE
          // parallel and nothing was fully free, and then accepts a hidden
          // perfect: the chord root tracks the ground's pitch class every bar
          // and this band is one octave wide, so the ground's octave companion
          // is often the only chord tone in reach -- a band-pinned tone meeting
          // an immutable one, where every approach is at least hidden. Holding
          // out for a free tone there keeps the true parallel; taking the
          // hidden one steps off the fault the ear actually tracks.
          //
          // The battuta joins the first pass but not the second. This anchor
          // chain is load-bearing -- every off-beat tone of the beat derives
          // from it -- so it is displaced for a contrary arrival only when a
          // tone free of every approach fault is available; an anchor whose
          // only fault is the battuta never reaches the relaxing pass, which
          // stays reserved for stepping off a true parallel. Both strict
          // columns hold empty across the sweep with it in, and the hidden and
          // contrary columns fall rather than pay.
          bool displaced = false;
          for (int pass = 0; pass < 2 && !displaced; ++pass) {
            if (pass == 1 && !anchor_is_true_parallel(anchor))
              break;
            // The sweep reaches the whole band, not a fixed fifth-and-a-bit. Two
            // of the three tests below are pitch-class shaped (scale membership,
            // consonance against the concurrent tones), so the admissible set is
            // sparse and its nearest member is regularly further than a fifth
            // away; a short radius left the anchor sitting on a true parallel
            // while a free tone waited an octave down.
            for (int dist = 1; dist <= band_hi - band_lo && !displaced; ++dist) {
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
                if (!consonant)
                  continue;
                if (pass == 0 ? (anchor_is_parallel(cand) || anchor_has_battuta(cand))
                              : anchor_is_true_parallel(cand))
                  continue;
                anchor = cand;
                displaced = true;
                break;
              }
            }
          }
          // Last resort: hold the tone that just sounded. Oblique motion forms
          // no parallel at all, and where the band offers nothing else the
          // repeated tone is the smaller blemish -- the anti-stall displacement
          // above exists to keep the line from going dull, not to buy that at
          // the price of a parallel octave.
          if (!displaced && anchor_is_true_parallel(anchor) && prev_emitted >= band_lo &&
              prev_emitted <= band_hi) {
            bool consonant = true;
            for (int upper : theme_pitches) {
              if (!isConsonantIc(prev_emitted - upper)) {
                consonant = false;
                break;
              }
            }
            if (consonant)
              anchor = prev_emitted;
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
        osc = nearest_other_chord_tone(anchor);
      for (int sub = 0; sub < notes_per_beat; ++sub) {
        MaterialNote mnote;
        mnote.start_tick = beat_tick + static_cast<Tick>(sub) * step;
        mnote.duration = step;
        int pitch = (sub % 2 == 1) ? osc : anchor;
        // Every off-beat tone moves concurrently with the V0 sixteenths, so all
        // of them need the same audible-grain parallel re-check as the beat
        // anchor: when the tone lands a parallel against a concurrently moving
        // voice, swap to the mirror neighbour (or the nearest other triad tone)
        // that stays consonant with the held ground. The anchor's RETURN at the
        // second half of the beat counts here too -- it is approached from the
        // companion tone, which is a different motion from the one the beat
        // head was judged on, and leaving it out shipped the largest single
        // group of parallel octaves this form produced. Only the beat head
        // itself is excluded, having just been judged above.
        if (sub > 0 && prev_emitted >= 0) {
          motions.clear();
          registry.concurrentMotions(mnote.start_tick - kTicksPerBeat / 4, mnote.start_tick,
                                     /*voice=*/1, /*num_voices=*/3, motions);
          // The true parallel and the hidden perfect sit on separate rungs. The
          // companion vocabulary here is four tones wide and each must still be
          // consonant with the held ground, so demanding full freedom regularly
          // rejects all four and leaves the design tone in place -- including
          // when that tone is the true parallel and a merely hidden companion
          // was available. Take the mildest fault the vocabulary can reach.
          constexpr int kOscClean = 0;
          constexpr int kOscHidden = 1;
          constexpr int kOscParallel = 2;
          auto osc_fault_rank = [&](int cand) {
            int worst = kOscClean;
            for (const ConcurrentMotion& motion : motions) {
              if (formsStrictPerfectParallel(prev_emitted, cand, motion.prev, motion.curr))
                return kOscParallel;
              if (formsPerfectParallel(prev_emitted, cand, motion.prev, motion.curr))
                worst = kOscHidden;
            }
            return worst;
          };
          // The anchor is the last candidate, not one of the first: repeating
          // it flattens the oscillation into a held tone, which is why the
          // neighbours and the broken third are tried ahead of it. But an
          // oblique repeat cannot form a parallel with anything, so where the
          // whole companion vocabulary is tied it is the one escape left.
          const int design_rank = osc_fault_rank(pitch);
          for (int accept = kOscClean; accept < design_rank; ++accept) {
            bool placed = false;
            for (int cand : {detail::scaleUp(anchor, 1, mode), detail::scaleDown(anchor, 1, mode),
                             nearest_other_chord_tone(anchor), anchor}) {
              if (cand == pitch || cand < band_lo || cand > band_hi)
                continue;
              if (!isConsonantIc(cand - plan.ground_pc))
                continue;
              if (osc_fault_rank(cand) > accept)
                continue;
              pitch = cand;
              placed = true;
              break;
            }
            if (placed)
              break;
          }
          // Last resort: sustain the tone that just sounded. Oblique motion can
          // form no parallel at all, and the repeat introduces no interval the
          // ear has not already accepted one sixteenth earlier -- which is why
          // it is exempt from the ground-consonance test the fresh candidates
          // take, and how it escapes bars whose chord puts a tritone between the
          // companion tone and the ground. It ranks below the anchor's own
          // return because a repeat flattens the oscillation; only a true
          // parallel is worth that.
          if (osc_fault_rank(pitch) == kOscParallel)
            pitch = prev_emitted;
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

// The chaconne middle voice hangs from its ceiling rather than standing on its
// floor: the form realises its chords close under the melody, the way a violin
// stops them, not spread thinly over the bass. The line therefore takes the
// octave immediately below the variation and only reaches further down when the
// ground leaves it no room.
constexpr int kChaconneMiddleSpan = 12;

// A statement leaving less room than this between the variation and the ground
// stays two-voice. A middle voice confined to three scale tones can only
// circle them, which reads as a drone rather than as a voice.
constexpr int kChaconneMiddleMinSpan = 5;

/**
 * @brief Build the chaconne's middle voice: the line that realises the chords
 *        the ground implies, one statement at a time.
 *
 * The opening statement stays two-voice -- the ground is announced whole under
 * a plain variation before anything fills the harmony in. It is written after
 * the variation and the ground, so each beat anchor is chosen against both, and
 * it subdivides at half the variation's rate: the variation stays the line the
 * ear follows.
 *
 * The band is resolved per statement from the two lines the middle voice has to
 * fit between, so it tracks the arc's register shifts without a constant of its
 * own and no onset can cross either neighbour.
 *
 * A statement the line cannot take cleanly is left in two voices. The band a
 * statement offers can be narrow enough that every admissible tone reaches a
 * perfect interval against the variation, and there the line has no escape: it
 * holds the fault. A variation set breathes anyway -- a statement stated by the
 * outer pair alone is one of the form's own textures -- so the line is offered
 * each statement and withdraws from the ones it cannot sit in.
 *
 * @param notes Receives the middle line, appended in time order.
 * @param variations The variation statements, one per cycle, already final.
 * @param ground The emitted ground line for the whole piece.
 * @param cycle_bar_plan Per-bar harmony for one ground statement.
 * @param variation_notes_per_beat The variation's subdivision, one per cycle.
 * @param mode Diatonic mode the line is drawn from.
 * @param out_present Receives, per cycle, whether the middle voice sounds.
 */
void appendChaconneMiddleVoice(std::vector<MaterialNote>& notes,
                               const std::vector<VariationDecl>& variations,
                               const std::vector<MaterialNote>& ground,
                               const std::vector<CycleBar>& cycle_bar_plan,
                               const std::vector<int>& variation_notes_per_beat, detail::Mode mode,
                               std::vector<bool>& out_present) {
  const int cycle_bars = static_cast<int>(cycle_bar_plan.size());
  const Tick period = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  const std::size_t cycles = variations.size();
  out_present.assign(cycles, false);
  // The variation as one continuous line, so a statement is read against the
  // tone the variation actually left off on rather than starting blind at its
  // own first onset.
  std::vector<MaterialNote> variation_line;
  for (const VariationDecl& variation : variations)
    variation_line.insert(variation_line.end(), variation.notes.begin(), variation.notes.end());
  // The ground's bar-head skeleton: the tone each bar keeps whether or not its
  // diminution survives the withdrawal pass that runs after this one.
  std::vector<MaterialNote> structural_ground;
  for (const MaterialNote& note : ground) {
    if (note.start_tick % kTicksPerBar34 != 0)
      continue;
    MaterialNote bar_tone = note;
    bar_tone.duration = kTicksPerBar34;
    structural_ground.push_back(bar_tone);
  }
  for (std::size_t cycle = 1; cycle < cycles; ++cycle) {
    const Tick block_start = static_cast<Tick>(cycle) * period;
    const Tick block_end = block_start + period;
    int variation_low = 128;
    for (const MaterialNote& note : variations[cycle].notes) {
      if (note.start_tick >= block_start && note.start_tick < block_end)
        variation_low = std::min(variation_low, static_cast<int>(note.pitch));
    }
    // The ground is read with its diminution still in place. The withdrawal
    // pass runs after this one and only ever replaces a decorating tone with
    // the structural tone below it, so a ceiling clear of the decorated line is
    // clear of the plain one too.
    int ground_high = -1;
    for (const MaterialNote& note : ground) {
      if (note.start_tick >= block_start && note.start_tick < block_end)
        ground_high = std::max(ground_high, static_cast<int>(note.pitch));
    }
    if (variation_low > 127 || ground_high < 0)
      continue;
    const int band_hi = variation_low - 1;
    const int band_lo = std::max(ground_high + 1, band_hi - kChaconneMiddleSpan + 1);
    if (band_hi - band_lo < kChaconneMiddleMinSpan)
      continue;

    ThemeToneRegistry registry;
    for (const MaterialNote& note : variations[cycle].notes)
      registry.record(note.start_tick, /*voice=*/0, static_cast<int>(note.pitch), note.duration);
    // The variation's closing tone and the ground's preceding bar: without them
    // the statement's first beat has no motion to judge and both parallel tests
    // there pass on an undefined pair.
    if (!variations[cycle - 1].notes.empty()) {
      const MaterialNote& tail = variations[cycle - 1].notes.back();
      registry.record(tail.start_tick, /*voice=*/0, static_cast<int>(tail.pitch), tail.duration);
    }
    // The ground is recorded at bar grain -- each bar's structural tone held
    // through the bar -- rather than at the surface a decorated statement
    // actually sounds. The withdrawal pass runs after this one and can take a
    // decoration back; that changes the tone sounding at the end of its bar,
    // which is the reference the next bar's first onset is heard against. A
    // line vetted against a decoration that is later withdrawn is left walking
    // into the structural tone in parallel across the statement seam, so the
    // reference here is the tone the bar keeps either way.
    for (const MaterialNote& note : structural_ground) {
      if (note.start_tick >= block_start - kTicksPerBar34 && note.start_tick < block_end)
        registry.record(note.start_tick, /*voice=*/2, static_cast<int>(note.pitch), note.duration);
    }
    const int variation_rate = variation_notes_per_beat[cycle];
    const std::size_t before = notes.size();
    appendCounterFiguration(notes, registry, block_start, cycle_bar_plan, band_lo, band_hi,
                            variation_rate > 1 ? variation_rate / 2 : 1, mode);
    // Bar by bar, the line is offered the statement and withdraws from what it
    // cannot hold. A bar is dropped when the line reaches a perfect interval
    // against either neighbour by a forbidden motion, or when its bar head --
    // this form's structural arrival, where the chord changes and both outer
    // lines state it -- meets the variation as a dissonance. Onsets inside a bar
    // are left to the line's own selector: a passing dissonance between two
    // running figurations is the style, a dissonant arrival is not.
    //
    // The bars are judged in order and a dropped one is removed before the next
    // is judged, so a fault a bar carried into its successor goes with it. Each
    // window opens a beat early and closes on the next bar's head: the motions
    // into a bar's first tone and out of its last are as much successions as the
    // ones inside it.
    bool sounds_anywhere = false;
    for (int bar = 0; bar < cycle_bars; ++bar) {
      const Tick bar_start = block_start + static_cast<Tick>(bar) * kTicksPerBar34;
      const Tick bar_end = bar_start + kTicksPerBar34;
      const Tick from_tick = bar_start >= kTicksPerBeat ? bar_start - kTicksPerBeat : 0;
      bool drop = formsPerfectMotionAgainst(notes, variation_line, from_tick, bar_end) ||
                  formsPerfectMotionAgainst(structural_ground, notes, from_tick, bar_end);
      if (!drop) {
        const int above = soundingMaterialPitch(variation_line, bar_start);
        const int head = soundingMaterialPitch(notes, bar_start);
        drop = above >= 0 && head >= 0 && !isConsonantIc(above - head);
      }
      if (!drop) {
        sounds_anywhere = true;
        continue;
      }
      notes.erase(std::remove_if(notes.begin() + static_cast<std::ptrdiff_t>(before), notes.end(),
                                 [&](const MaterialNote& note) {
                                   return note.start_tick >= bar_start && note.start_tick < bar_end;
                                 }),
                  notes.end());
    }
    out_present[cycle] = sounds_anywhere;
  }
}

/**
 * @brief Land the chaconne's middle voice on the closing cadence.
 *
 * The coda replaces the ground's last return: the bass holds its support tone
 * through the penultimate bar, steps to its dominant on that bar's closing beat
 * and states the tonic under the final bar. The counter-figuration is written
 * from the statement's own bar chords, which no longer describe either of
 * those, so the line stops at the approach beat and states the cadence itself
 * -- a dominant tone under the variation's own dominant, then the tone that
 * completes the closing triad. Both are taken under the variation and over the
 * bass, so the close keeps the register order the rest of the piece holds.
 *
 * @param notes The middle line, edited in place.
 * @param final_variation The closing variation statement.
 * @param total_bars Bars in the piece.
 * @param coda_support The bass tone held through the penultimate bar.
 * @param coda_dominant The bass tone on the penultimate bar's closing beat.
 * @param coda_tonic The bass tone under the final bar.
 * @param major_close Whether the closing triad takes a major third.
 */
void closeChaconneMiddleVoice(std::vector<MaterialNote>& notes,
                              const std::vector<MaterialNote>& final_variation, int total_bars,
                              int coda_support, int coda_dominant, int coda_tonic,
                              bool major_close) {
  const Tick final_bar_tick = static_cast<Tick>(total_bars - 1) * kTicksPerBar34;
  const Tick approach_tick = final_bar_tick - kTicksPerBeat;
  int previous = -1;
  for (const MaterialNote& note : notes) {
    if (note.start_tick < approach_tick)
      previous = static_cast<int>(note.pitch);
  }
  if (previous < 0)
    return;  // the closing statement carries no middle voice to land.
  notes.erase(
      std::remove_if(notes.begin(), notes.end(),
                     [&](const MaterialNote& note) { return note.start_tick >= approach_tick; }),
      notes.end());

  int variation_prev = -1;
  for (const MaterialNote& note : final_variation) {
    if (note.start_tick < approach_tick)
      variation_prev = static_cast<int>(note.pitch);
  }
  const int variation_approach = soundingMaterialPitch(final_variation, approach_tick);
  const int variation_head = soundingMaterialPitch(final_variation, final_bar_tick);
  int variation_final = 128;
  for (const MaterialNote& note : final_variation) {
    if (note.start_tick >= final_bar_tick)
      variation_final = std::min(variation_final, static_cast<int>(note.pitch));
  }

  // Ranked, not filtered: the cadence is stated whatever the register offers,
  // so a tone is chosen for being the least compromised rather than rejected
  // for being compromised at all. A true parallel outranks a clash with the
  // variation, which outranks taking a less characteristic chord tone, which
  // outranks distance from the tone just sounded.
  const auto pick = [](const int(&chord_pcs)[3], int floor_excl, int ceiling_incl, int line_prev,
                       const std::vector<int>& against, int other_prev, int other_curr,
                       int bass_prev, int bass_curr) {
    int best = -1;
    int best_key = 1 << 24;
    for (int idx = 0; idx < 3; ++idx) {
      for (int cand = line_prev - 12; cand <= line_prev + 12; ++cand) {
        if (cand <= floor_excl || cand > ceiling_incl || cand % 12 != chord_pcs[idx])
          continue;
        int key = std::abs(cand - line_prev) + idx * (1 << 6);
        for (const int other : against) {
          if (other >= 0 && !isConsonantIc(cand - other))
            key += 1 << 12;
        }
        if (formsStrictPerfectParallel(line_prev, cand, other_prev, other_curr) ||
            formsStrictPerfectParallel(line_prev, cand, bass_prev, bass_curr))
          key += 1 << 18;
        if (key < best_key) {
          best_key = key;
          best = cand;
        }
      }
    }
    return best;
  };

  // The dominant, third first: the outer pair takes the root and the fifth
  // between them, so the leading tone is the tone the chord is still missing.
  static constexpr int kDominantPcs[3] = {11, 2, 7};
  const int approach_pitch = pick(
      kDominantPcs, coda_dominant, variation_approach >= 0 ? variation_approach : 127, previous,
      {variation_approach}, variation_prev, variation_approach, coda_support, coda_dominant);
  if (approach_pitch < 0)
    return;
  MaterialNote approach;
  approach.start_tick = approach_tick;
  approach.duration = kTicksPerBeat;
  approach.pitch = static_cast<std::uint8_t>(approach_pitch);
  notes.push_back(approach);

  // The closing triad, third first for the same reason: the variation lands on
  // the tonic over a tonic bass, so the third is what makes the chord a triad.
  const int tonic_pcs[3] = {major_close ? 4 : 3, 7, 0};
  const int final_pitch =
      pick(tonic_pcs, coda_tonic, variation_final <= 127 ? variation_final : 127, approach_pitch,
           {variation_head, variation_final}, variation_approach, variation_head, coda_dominant,
           coda_tonic);
  if (final_pitch < 0)
    return;
  MaterialNote last;
  last.start_tick = final_bar_tick;
  last.duration = kTicksPerBar34;
  last.pitch = static_cast<std::uint8_t>(final_pitch);
  notes.push_back(last);
}

/**
 * @brief Resolve the per-cycle voice-presence schedule for a passacaglia.
 *
 * BWV582-style terraced growth derived purely from the period (cycle) count:
 *   - periods >= 3: cycle 0 is a ground-solo intro (V0 and V1 rest); the middle
 *     cycles add V0 over the ground with ONE receding cycle where V1 rests; the
 *     climax cycle sounds all three voices.
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

  if (cycle_count >= 3) {
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

  // --- V2 ground bass: identical pitches to the 2-voice form, statement by
  // statement under the growth articulations; only the voice assignment (set on
  // the span below) moves to V2. ---
  //
  // Late-cycle rhythmic intensification (design value): from the final third
  // of the cycles on, the ground restates each bar as repeated same-pitch
  // quarters (the BWV582-style martellato) instead of one dotted half. That
  // restatement is the replay branch's, so those statements stay sustained in
  // the material and only the earlier ones are articulated here. The pitches
  // never change either way, so the bar-head skeleton stays immutable. At least
  // one unsplit statement always opens the piece (cycles >= 2 guard).
  const int split_cycle = cycles >= 2 ? cycles - (cycles + 2) / 3 : cycles;
  std::vector<MaterialNote>& ground = out.material.passacaglia_ground;
  appendGroundStatements(ground, total_bars, cycle_bars, ground_pitch, split_cycle, total_bars,
                         mode);
  out.material.passacaglia_ground_period = static_cast<Tick>(total_bars) * kTicksPerBar34;
  out.material.passacaglia_ground_cycle = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  // The realized ground, read at beat grain: what the voices written over it
  // are vetted against once a statement is decorated inside the bar.
  const std::vector<std::uint8_t> ground_beats = groundBeatTones(ground, 0, total_bars);
  if (cycles >= 2)
    out.material.passacaglia_ground_split_from = static_cast<Tick>(split_cycle) * period;

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
      if (plan.seventh) {
        chord.quality = plan.minor ? ChordQuality::Minor7 : ChordQuality::Dominant7;
      } else {
        chord.quality = plan.minor ? ChordQuality::Minor : ChordQuality::Major;
      }
    }
    out.harmony.chords.push_back(chord);
  }
  if (total_bars >= 2) {
    // The immutable passacaglia ground approaches the final tonic on D, the
    // fifth of the dominant.  Spell that real sonority as V4/3 (second
    // inversion) rather than pretending the bass is a root-position G.
    ChordEvent& approach = out.harmony.chords[static_cast<std::size_t>(total_bars - 2)];
    approach.root_pc = 7;
    approach.quality = ChordQuality::Dominant7;
    approach.degree = RomanNumeral::V;
    const std::uint8_t approach_bass_pc =
        static_cast<std::uint8_t>(ground_pitch[static_cast<std::size_t>(cycle_bars - 2)] % 12);
    approach.inversion = approach_bass_pc == 2 ? ChordInversion::Second : ChordInversion::Root;
    approach.function = HarmonicFunction::D;
    approach.has_degree = true;
  }
  // The immutable ground's final dominant bass is D (the fifth of V), and the
  // upper variation is free to approach the tonic from scale degree 2.  This
  // is therefore an inverted/upper-voice IAC, not a root-position PAC.  Author
  // the cadence explicitly so the director does not infer Perfect merely from
  // the V-to-I harmonic roots.
  out.harmony.cadences.push_back({bar_tick(total_bars - 1), CadenceType::ImperfectAuthentic});

  // --- Terraced growth schedule (period-count derived). ---
  const std::size_t cycle_count = static_cast<std::size_t>(cycles);
  std::size_t climax_idx = cycle_count <= 1 ? 0 : ((cycle_count - 1) * 4) / 5;
  if (cycle_count >= 2 && climax_idx >= cycle_count - 1)
    climax_idx = cycle_count - 2;
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
  int prev_v0_bar_head = -1;
  // The previous cycle's closing V0 note, carried into the next cycle's
  // read-back registry. The registry is rebuilt per cycle, so without this the
  // concurrent voices have no onset before the cycle's first beat and every
  // motion test there passes on an undefined pair.
  MaterialNote carry_v0{};
  bool carry_v0_valid = false;

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
      // The repair floor rides the arc's register shift with the figuration, so
      // a relocated onset can never land in the V1 counter-figuration's band.
      const std::size_t beat_base = static_cast<std::size_t>(cycle * cycle_bars) * 3;
      const std::vector<std::uint8_t> cycle_beats(
          ground_beats.begin() + static_cast<std::ptrdiff_t>(beat_base),
          ground_beats.begin() +
              static_cast<std::ptrdiff_t>(beat_base + static_cast<std::size_t>(cycle_bars) * 3));
      scrubGroundParallels(v0_notes, block_start, cycle_beats, cycle_bar_plan, out.harmony,
                           prev_v0_bar_head,
                           cycle > 0 ? static_cast<int>(ground_beats[beat_base - 1]) : -1,
                           prev_v0_last, kPassV0BandLo + point.register_shift,
                           /*every_voice_placed=*/false);
      if (!v0_notes.empty()) {
        prev_v0_last = static_cast<int>(v0_notes.back().pitch);
        prev_v0_bar_head = pitchAtBarHead(
            v0_notes, block_start + static_cast<Tick>(cycle_bars - 1) * kTicksPerBar34);
      }

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
      // Carry the last onset each concurrent voice made before this cycle so the
      // first beat has a motion to judge (the ground is period-tiled, so its
      // preceding bar is the cycle's last ground pitch).
      if (carry_v0_valid) {
        registry.record(carry_v0.start_tick, /*voice=*/0, static_cast<int>(carry_v0.pitch),
                        carry_v0.duration);
      }
      if (cycle > 0) {
        registry.record(block_start - kTicksPerBar34, /*voice=*/2,
                        static_cast<int>(ground_pitch[static_cast<std::size_t>(cycle_bars - 1)]),
                        kTicksPerBar34);
      }
      const int v1_tier = (cycle_tier > 0) ? cycle_tier - 1 : 0;
      const int notes_per_beat = notesPerBeatForTier(v1_tier);
      appendCounterFiguration(counter_notes, registry, block_start, cycle_bar_plan,
                              kPassV1BandLo + point.register_shift,
                              kPassV1BandHi + point.register_shift, notes_per_beat, mode);
    }

    carry_v0_valid = !v0_notes.empty();
    if (carry_v0_valid)
      carry_v0 = v0_notes.back();
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
      const Tick landing_tick = piece_end - 2 * kTicksPerBar34 + kTicksPerBeat;
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
      // The supertonic trill falls D -> C, and where the immutable ground
      // approaches its own final tonic from that same degree the two outer
      // voices walk into the piece's last chord in parallel octaves. The trill
      // then takes its termination: the last eighth of the landing bar steps
      // down to the leading tone, so the tonic is reached from B against the
      // ground's D -- contrary motion, and the figure the cadence wants anyway.
      const int penultimate_ground_pc =
          cycle_bars >= 2
              ? static_cast<int>(ground_pitch[static_cast<std::size_t>(cycle_bars - 2)]) % 12
              : -1;
      const bool needs_termination = tritone_seam && penultimate_ground_pc == 2;
      last_var.notes.erase(
          std::remove_if(last_var.notes.begin(), last_var.notes.end(),
                         [&](const MaterialNote& note) { return note.start_tick >= landing_tick; }),
          last_var.notes.end());
      const Tick termination = needs_termination ? kTicksPerBeat / 2 : 0;
      MaterialNote held;
      held.start_tick = landing_tick;
      held.duration = kTicksPerBar34 - kTicksPerBeat - termination;
      held.pitch = static_cast<std::uint8_t>(prefinal);
      last_var.notes.push_back(held);
      if (needs_termination) {
        MaterialNote leading;
        leading.start_tick = held.start_tick + held.duration;
        leading.duration = termination;
        leading.pitch = static_cast<std::uint8_t>(final_tone - 1);
        last_var.notes.push_back(leading);
      }
      MaterialNote last;
      last.start_tick = piece_end - kTicksPerBar34;
      last.duration = kTicksPerBar34;
      last.pitch = static_cast<std::uint8_t>(final_tone);
      last_var.notes.push_back(last);
    }
  }
  if (!counter_notes.empty()) {
    const Tick landing_tick = static_cast<Tick>(total_bars - 2) * kTicksPerBar34 + kTicksPerBeat;
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

  {
    std::vector<const std::vector<MaterialNote>*> upper;
    for (const auto& variation : out.material.passacaglia_variations)
      upper.push_back(&variation.notes);
    for (const TrioVoiceLine& line : out.material.trio_voices)
      upper.push_back(&line.notes);
    withdrawClashingDiminution(ground, upper, total_bars);
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
  const std::size_t cycle_count = static_cast<std::size_t>(cycles);
  std::size_t climax_idx = cycle_count <= 1 ? 0 : ((cycle_count - 1) * 4) / 5;
  if (cycle_count >= 2 && climax_idx >= cycle_count - 1)
    climax_idx = cycle_count - 2;
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

  // --- V1 ground bass: the structural tone on every bar head, statement by
  // statement under the growth articulations. The declared period is the whole
  // line, so the replay branch lays it down once instead of tiling one cycle. ---
  std::vector<MaterialNote>& ground =
      passacaglia ? out.material.passacaglia_ground : out.material.ground_bass;
  appendGroundStatements(ground, total_bars, cycle_bars, ground_pitch, cycles,
                         passacaglia ? total_bars : total_bars - 2, mode);
  const Tick ground_line_ticks = static_cast<Tick>(total_bars) * kTicksPerBar34;
  const Tick ground_cycle_ticks = static_cast<Tick>(cycle_bars) * kTicksPerBar34;
  if (passacaglia) {
    out.material.passacaglia_ground_period = ground_line_ticks;
    out.material.passacaglia_ground_cycle = ground_cycle_ticks;
  } else {
    out.material.ground_bass_period = ground_line_ticks;
    out.material.ground_bass_cycle = ground_cycle_ticks;
  }
  // The realized ground, read at beat grain: what the variation blocks are
  // vetted against once a statement is decorated inside the bar.
  const std::vector<std::uint8_t> ground_beats = groundBeatTones(ground, 0, total_bars);

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
      if (plan.seventh) {
        chord.quality = plan.minor ? ChordQuality::Minor7 : ChordQuality::Dominant7;
      } else {
        chord.quality = plan.minor ? ChordQuality::Minor : ChordQuality::Major;
      }
    }
    out.harmony.chords.push_back(chord);
  }

  // --- V0 variation blocks: one per cycle, arc-driven density / register /
  // figure orientation. The first cycle is a plain Ground-role statement
  // (quarter notes only, no sub-quarter ornaments) so the chaconne form's
  // variation_role_ornament_constraint stays satisfied. ---
  int previous_v0_bar_head = -1;
  int previous_v0_last = -1;
  // The variation's subdivision per statement: the middle voice written below
  // takes half of it, so the variation stays the line the ear follows.
  std::vector<int> variation_notes_per_beat;
  variation_notes_per_beat.reserve(static_cast<std::size_t>(cycles));
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
    variation_notes_per_beat.push_back(notes_per_beat);
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

    const std::size_t beat_base = static_cast<std::size_t>(cycle * cycle_bars) * 3;
    const std::vector<std::uint8_t> cycle_beats(
        ground_beats.begin() + static_cast<std::ptrdiff_t>(beat_base),
        ground_beats.begin() +
            static_cast<std::ptrdiff_t>(beat_base + static_cast<std::size_t>(cycle_bars) * 3));
    scrubGroundParallels(notes, block_start, cycle_beats, cycle_bar_plan, out.harmony,
                         previous_v0_bar_head,
                         cycle > 0 ? static_cast<int>(ground_beats[beat_base - 1]) : -1,
                         previous_v0_last, kV0RepairFloor, /*every_voice_placed=*/!passacaglia);

    // Compact cadential landing on the piece's final bar. The chaconne coda
    // replaces the repeating dominant bass with a tonic, so its upper voice
    // holds the consonant fifth G before resolving to C. The passacaglia keeps
    // its existing supertonic-to-tonic close over its tonic-ending ground.
    if (cycle == cycles - 1 && !notes.empty()) {
      const int near = static_cast<int>(notes.back().pitch);
      const int prefinal_pc = passacaglia ? 2 : 7;
      const int up = near + ((prefinal_pc - (near % 12)) % 12 + 12) % 12;
      int prefinal = (up - near <= near - (up - 12)) ? up : up - 12;
      // Settle the close in the variation's home octave, above the lower
      // voices and below the upper ornament compass.
      while (prefinal > 81)
        prefinal -= 12;
      while (prefinal < 67)
        prefinal += 12;
      if (!passacaglia) {
        const Tick approach_tick =
            static_cast<Tick>(total_bars - 1) * kTicksPerBar34 - kTicksPerBeat;
        // The bass pair this tone is heard against: the coda's held support tone
        // moving to its dominant. Both are design values written further below,
        // so they are recomputed here rather than read back.
        const int bass_prev = static_cast<int>(ground_pitch[static_cast<std::size_t>(
            (total_bars - 2) % static_cast<int>(ground_pitch.size()))]);
        MaterialNote* approach_note = nullptr;
        MaterialNote* before_approach = nullptr;
        for (MaterialNote& note : notes) {
          if (note.start_tick == approach_tick) {
            approach_note = &note;
            break;
          }
          before_approach = &note;
        }
        const int approach_prev =
            before_approach == nullptr ? -1 : static_cast<int>(before_approach->pitch);
        // The tone the landing is actually reached from: the last one the
        // variation still sounds before the final bar, which is the approach
        // tone only when nothing follows it. Both ends of that arrival are
        // design values -- the landing spells the dominant, the coda bass states
        // the tonic, and a dominant over a tonic is a fifth however either is
        // spelt -- so a fault formed there can be answered only on the way in.
        // Everything from the approach beat onward meets a bass holding its
        // dominant, so the way in is free of any fault of its own.
        const Tick final_bar_tick = static_cast<Tick>(total_bars - 1) * kTicksPerBar34;
        const int coda_tonic = static_cast<int>(ground_pitch.front());
        MaterialNote* tail_note = nullptr;
        for (MaterialNote& note : notes) {
          if (note.start_tick >= final_bar_tick)
            break;
          tail_note = &note;
        }
        if (approach_note != nullptr) {
          // Landing on the dominant is the design value; which dominant tone, and
          // in which octave, is not. Root and fifth are the mode-neutral members
          // of the dominant triad -- the third alone changes with the mode -- so
          // either spells the same approach in major and minor. Rank the
          // candidates by how they meet the bass and take the best. A fixed root
          // two octaves above it leapt down into an octave under every ground
          // whose penultimate tone rises, and no later pass can catch that: this
          // write lands after the line's only guard.
          //
          // The penalties are ranked, not summed as equals: a true parallel
          // outranks a battuta, which outranks a contrary arrival on the perfect
          // interval already sounding, which outranks a hidden perfect, which
          // outranks spelling the dominant as its fifth, which outranks distance.
          // Merging them would let the tone dodge the mildest fault by committing
          // the worst. The hidden rung sits below the battuta because a leap to
          // the dominant over a rising bass is ordinary cadential writing where a
          // converging octave is not, but it sits above colour and distance
          // because the compass holds more than one spelling of the dominant and
          // a third of the similar-motion arrivals have a tone that avoids one,
          // for a handful of wider melodic intervals.
          //
          // Both perfect-motion rungs read BOTH ends of the cadence, not just
          // this one. Ranking the approach alone does remove the arrivals it is
          // aimed at, but the tone it then prefers reaches the final tonic by
          // contrary motion off the same interval -- the fault moves to the far
          // end rather than leaving, and the far end is measured too. Reading
          // both ends removes it at both.
          // How a tone on this beat meets the bass it actually sounds over. The
          // penultimate ground tone is still held here -- the coda states its
          // dominant from the final bar -- so the motion this chooser ranks says
          // nothing about the interval standing at the onset, and it carried no
          // term for that at all. It went unnoticed while the motion penalties
          // were doing the steering; with those answered the colour preference
          // decides, and it prefers the dominant's own spelling whatever that
          // spelling clashes with. Read against the bass, so the fourth counts as
          // the dissonance it is under the lowest line.
          //
          // Weighed rather than filtered, and the same weight at both ends of the
          // re-aim below, so the two trade against each other instead of one
          // being answered at the other's expense.
          const auto clashAt = [&](int pitch) {
            return rule_helpers::isConsonantAboveBass(static_cast<std::uint8_t>(pitch),
                                                      static_cast<std::uint8_t>(bass_prev))
                       ? 0
                       : (1 << 9);
          };
          const auto rankDominantApproach = [&](int prev, int* chosen) {
            int best = -1;
            int best_key = 1 << 20;
            for (const int approach_pc : {7, 2}) {
              for (int cand = 67; cand <= 81; ++cand) {
                if (cand % 12 != approach_pc)
                  continue;
                const int parallel_penalty =
                    formsStrictPerfectParallel(prev, cand, bass_prev, kCodaBassDominant) ? (1 << 16)
                                                                                         : 0;
                const int battuta_penalty =
                    formsBattuta(prev, cand, bass_prev, kCodaBassDominant) ? (1 << 12) : 0;
                // When this tone is also the one the landing is reached from, the
                // arrival it makes there is ranked too -- below the fault it would
                // make here and above a battuta. Ranking the two ends as equals
                // lets a candidate that faults at this end tie with one that
                // faults at the far end, and the tie is then settled by colour and
                // distance, which is how a fault gets moved rather than removed.
                // This end is the dearer one: the bass leaves its dominant two
                // octaves below, so a parallel here is an octave, while the
                // landing states the tonic under a dominant, where it is a fifth.
                const int landing_penalty =
                    (tail_note == approach_note &&
                     formsStrictPerfectParallel(cand, prefinal, kCodaBassDominant, coda_tonic))
                        ? (1 << 14)
                        : 0;
                const int anti_penalty =
                    (formsAntiParallelPerfect(prev, cand, bass_prev, kCodaBassDominant) ||
                     (tail_note == approach_note &&
                      formsAntiParallelPerfect(cand, prefinal, kCodaBassDominant, coda_tonic)))
                        ? (1 << 11)
                        : 0;
                // Read at both ends, like the anti-parallel above it and for the
                // same reason: a class ranked at one end only is not removed by
                // the ranking, it is moved to the other end, where the sweep that
                // measures the composed surface cannot see it.
                const int hidden_penalty =
                    (formsPerfectParallel(prev, cand, bass_prev, kCodaBassDominant) ||
                     (tail_note == approach_note &&
                      formsPerfectParallel(cand, prefinal, kCodaBassDominant, coda_tonic)))
                        ? (1 << 10)
                        : 0;
                const int clash_penalty = clashAt(cand);
                const int colour_penalty = (approach_pc == 7) ? 0 : (1 << 8);
                const int step = (prev >= 0) ? std::abs(cand - prev) : 0;
                const int key = parallel_penalty + landing_penalty + battuta_penalty +
                                anti_penalty + hidden_penalty + clash_penalty + colour_penalty +
                                step;
                if (key < best_key) {
                  best_key = key;
                  best = cand;
                }
              }
            }
            if (chosen != nullptr)
              *chosen = best;
            return best_key;
          };
          int chosen = -1;
          int chosen_key = rankDominantApproach(approach_prev, &chosen);
          // Re-aim the tone this approach is reached FROM, wherever that lowers
          // the fault. It is the only free tone in reach, and it is genuinely
          // free: the coda's support tone is held from the start of this bar to
          // the approach beat, so any onset strictly inside that span meets an
          // oblique bass and can fault against nothing.
          //
          // Offered for any fault, not only a parallel. Every tone this compass
          // holds over the coda's dominant is a perfect interval with it -- the
          // two spellings of the dominant an octave apart, and its fifth -- so
          // which fault the approach forms is settled entirely by the interval it
          // is reached BY, and this is the end that settles it. Gated at the
          // parallel, the hidden perfect had a repair it could always reach and
          // was never once offered it.
          //
          // The whole window is read and the lowest key kept, rather than the
          // first tone under a threshold: a nearer tone that only downgrades the
          // fault would otherwise be taken while a clean one stood one step
          // further out. Ties fall to the nearest, and the tone already chosen is
          // the floor, so a re-aim can only lower the key.
          //
          // Held inside the line's own compass rather than the arrival's, and
          // reaching an octave rather than a fourth. The tone being re-aimed
          // belongs to the variation, which need not run in the register the
          // cadence lands in: measured against the arrival's octave instead,
          // every candidate was refused outright wherever the variation sat
          // below it, and where the variation sat an octave below, no reach short
          // of one could put the approach within a step of where it is going.
          int line_lo = 127;
          int line_hi = 0;
          for (const MaterialNote& note : notes) {
            line_lo = std::min(line_lo, static_cast<int>(note.pitch));
            line_hi = std::max(line_hi, static_cast<int>(note.pitch));
          }
          const Tick support_start = final_bar_tick - kTicksPerBar34;
          if (before_approach != nullptr && before_approach->start_tick > support_start) {
            const int prev_original = static_cast<int>(before_approach->pitch);
            int best_prev = -1;
            int best_note = chosen;
            int best_key = chosen_key + clashAt(prev_original);
            if (best_key > 0) {
              for (int dist = 1; dist <= 12; ++dist) {
                for (const int sgn : {-1, 1}) {
                  const int prev_cand = prev_original + sgn * dist;
                  if (prev_cand < line_lo || prev_cand > line_hi ||
                      !detail::inScale(prev_cand, mode)) {
                    continue;
                  }
                  int trial = -1;
                  const int trial_key =
                      rankDominantApproach(prev_cand, &trial) + clashAt(prev_cand);
                  if (trial_key < best_key) {
                    best_key = trial_key;
                    best_note = trial;
                    best_prev = prev_cand;
                  }
                }
              }
            }
            if (best_prev >= 0) {
              before_approach->pitch = static_cast<std::uint8_t>(best_prev);
              chosen = best_note;
            }
          }
          approach_note->pitch = static_cast<std::uint8_t>(chosen >= 0 ? chosen : 67);
        }
        // A figuration tone standing between the approach beat and the landing
        // carries no such ranking of its own: the block's only guard reads the
        // ground at bar heads, and this onset is neither. It is re-aimed to the
        // nearest scale tone of the same compass that leaves the arrival clean.
        //
        // Ranked rather than filtered, and for the same reason the approach beat
        // is. Demanding a fully free arrival outright would reject the whole
        // compass on the bars where none exists and leave the tone exactly as it
        // was -- including where a merely hidden one was in reach and the tone in
        // place is the true parallel. Each accept level re-offers the whole
        // compass, so a clean tone is always preferred to one that only
        // downgrades the fault.
        if (tail_note != nullptr && tail_note != approach_note) {
          const auto tailFaultRank = [&](int cand) {
            if (formsStrictPerfectParallel(cand, prefinal, kCodaBassDominant, coda_tonic))
              return 2;
            if (formsPerfectParallel(cand, prefinal, kCodaBassDominant, coda_tonic) ||
                formsAntiParallelPerfect(cand, prefinal, kCodaBassDominant, coda_tonic))
              return 1;
            return 0;
          };
          const int original = static_cast<int>(tail_note->pitch);
          const int design_rank = tailFaultRank(original);
          for (int accept = 0; accept < design_rank; ++accept) {
            bool placed = false;
            for (int dist = 1; dist <= 12 && !placed; ++dist) {
              for (const int sgn : {-1, 1}) {
                const int cand = original + sgn * dist;
                if (cand < 67 || cand > 81 || !detail::inScale(cand, mode) ||
                    tailFaultRank(cand) > accept)
                  continue;
                tail_note->pitch = static_cast<std::uint8_t>(cand);
                placed = true;
                break;
              }
            }
            if (placed)
              break;
          }
        }
      }
      appendCompactCadentialLanding(notes, static_cast<Tick>(total_bars - 1) * kTicksPerBar34,
                                    kTicksPerBar34, prefinal, prefinal - (passacaglia ? 2 : 7), 3);
    }

    if (!notes.empty()) {
      previous_v0_bar_head =
          pitchAtBarHead(notes, block_start + static_cast<Tick>(cycle_bars - 1) * kTicksPerBar34);
      previous_v0_last = static_cast<int>(notes.back().pitch);
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

  // --- V1 middle voice: the line that realises the chords the ground implies,
  // written after both outer lines so every tone is chosen against them. ---
  std::vector<bool> middle_present(static_cast<std::size_t>(cycles), false);
  if (!passacaglia) {
    std::vector<MaterialNote> middle_notes;
    appendChaconneMiddleVoice(middle_notes, out.material.variations, ground, cycle_bar_plan,
                              variation_notes_per_beat, mode, middle_present);
    if (!out.material.variations.empty()) {
      closeChaconneMiddleVoice(middle_notes, out.material.variations.back().notes, total_bars,
                               static_cast<int>(ground_pitch[static_cast<std::size_t>(
                                   (total_bars - 2) % static_cast<int>(ground_pitch.size()))]),
                               kCodaBassDominant, static_cast<int>(ground_pitch.front()),
                               picardy || !minor);
    }
    if (!middle_notes.empty()) {
      TrioVoiceLine middle_line;
      middle_line.voice = 1;
      middle_line.manual = 1;  // documentary (Swell): V1 = the middle line.
      middle_line.notes = std::move(middle_notes);
      out.material.trio_voices.push_back(std::move(middle_line));
    }
  }

  {
    std::vector<const std::vector<MaterialNote>*> upper;
    for (const auto& variation : out.material.variations)
      upper.push_back(&variation.notes);
    for (const TrioVoiceLine& line : out.material.trio_voices)
      upper.push_back(&line.notes);
    withdrawClashingDiminution(ground, upper, total_bars);
  }

  // --- VoicePlan: V2 ground carrier, V1 middle voice per statement and V0
  // variation carrier per statement. The chaconne replaces the final ground bar
  // with an explicitly declared coda; the register order V0 > V1 > V2 holds at
  // every shared tick, so no voice crossing occurs. ---
  out.voice_plan.num_voices = passacaglia ? 2 : 3;
  const VoiceId ground_voice = passacaglia ? 1 : 2;

  Span ground_span;
  ground_span.id = 0;
  ground_span.start_tick = 0;
  const Tick final_bar_tick = static_cast<Tick>(total_bars - 1) * kTicksPerBar34;
  const Tick final_approach_tick = final_bar_tick - kTicksPerBeat;
  ground_span.end_tick = passacaglia ? static_cast<Tick>(total_bars) * kTicksPerBar34
                                     : final_bar_tick - kTicksPerBar34;
  ground_span.voice = ground_voice;
  ground_span.intent = passacaglia ? VoiceIntent::PassacagliaGround : VoiceIntent::GroundCarrier;
  ground_span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
  out.voice_plan.spans.push_back(ground_span);

  if (!passacaglia) {
    CodaDecl coda;
    coda.voice = ground_voice;
    MaterialNote support;
    support.start_tick = final_bar_tick - kTicksPerBar34;
    support.duration = kTicksPerBar34 - kTicksPerBeat;
    support.pitch = ground_pitch[static_cast<std::size_t>((total_bars - 2) %
                                                          static_cast<int>(ground_pitch.size()))];
    coda.notes.push_back(support);
    MaterialNote dominant;
    dominant.start_tick = final_approach_tick;
    dominant.duration = kTicksPerBeat;
    dominant.pitch = 43;  // G2.
    coda.notes.push_back(dominant);
    MaterialNote tonic;
    tonic.start_tick = final_bar_tick;
    tonic.duration = kTicksPerBar34;
    tonic.pitch = ground_pitch.front();
    coda.notes.push_back(tonic);
    out.material.coda_extensions.push_back(std::move(coda));

    Span coda_span;
    coda_span.id = static_cast<SpanId>(1 + cycles);
    coda_span.start_tick = support.start_tick;
    coda_span.end_tick = tonic.start_tick + tonic.duration;
    coda_span.voice = ground_voice;
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
  }

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

  // The middle voice's statements, after the ids the ground, the coda and the
  // variation statements already hold.
  SpanId next_span_id = static_cast<SpanId>(2 + cycles);
  for (int cycle = 0; cycle < cycles; ++cycle) {
    if (!middle_present[static_cast<std::size_t>(cycle)])
      continue;
    Span middle_span;
    middle_span.id = next_span_id++;
    middle_span.start_tick = static_cast<Tick>(cycle * cycle_bars) * kTicksPerBar34;
    middle_span.end_tick = middle_span.start_tick + period;
    middle_span.voice = 1;
    middle_span.intent = VoiceIntent::TrioVoiceCarrier;
    middle_span.subdivision = Subdivision::Quarter;  // unused by verbatim replay.
    out.voice_plan.spans.push_back(middle_span);
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
  markCycleDominantSevenths(plan, req.mode, /*triad_only_bars=*/{});

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
  std::vector<CycleBar> plan = planFromGround(table.data(), table.size(), minor);
  // Every cycle closes with the cadential suspension installed below, always on
  // the same cycle-relative bar, and that is also the only bar of these grounds
  // whose root falls a fifth. The suspension has the prior claim on it.
  markCycleDominantSevenths(plan, req.mode,
                            /*triad_only_bars=*/{kPassacagliaCycleBars - 2});

  HarnessFixture out = buildPassacagliaThreeVoice(req, kPassacagliaCycleBars, ground_pitch, plan);

  // Cadential 4-3 in the middle counter-line, with a one-beat preparation,
  // one-beat resolution, and one-beat rest. The immutable ground supplies the bass at
  // all three positions; the actual final variation bounds the upper register
  // so the inserted carrier cannot cross either neighbour.
  //
  // The ground restates the same bars throughout, so every cycle closes on the
  // cadence the last one closes on and each of those is a place this figure
  // belongs. A form built on repetition that states its one accented dissonance
  // only in the closing bar has no friction against the ground anywhere else,
  // and that friction is what a passacaglia is for. The chord is the same at
  // every one of these bars because the lookup is already cycle-relative.
  //
  // The closing cadence is offered the figure first. Installing a carrier splits
  // the span it sits in, and the last cadence is the one that must not lose its
  // dissonance to a split made earlier in the piece, so trying it first leaves
  // the shipped closing gesture unchanged whatever the earlier cycles take.
  std::vector<int> suspension_bars;
  suspension_bars.push_back(static_cast<int>(req.bars) - 2);
  for (int cycle_bar = kPassacagliaCycleBars - 2; cycle_bar < static_cast<int>(req.bars) - 2;
       cycle_bar += kPassacagliaCycleBars) {
    suspension_bars.push_back(cycle_bar);
  }

  for (const int sus_bar : suspension_bars) {
    const Tick suspension_tick = static_cast<Tick>(sus_bar) * kTicksPerBar34;
    const Tick preparation_tick = suspension_tick - kTicksPerBeat;
    const Tick resolution_tick = suspension_tick + kTicksPerBeat;
    const Tick period = out.material.passacaglia_ground_period;
    const auto groundPitchAt = [&](Tick tick) {
      return soundingMaterialPitch(out.material.passacaglia_ground,
                                   period == 0 ? tick : tick % period);
    };
    const auto upperPitchAt = [&](Tick tick) {
      int pitch = -1;
      for (const auto& variation : out.material.passacaglia_variations) {
        const int candidate = soundingMaterialPitch(variation.notes, tick);
        if (candidate >= 0)
          pitch = candidate;
      }
      return pitch;
    };
    // The latest V0 onset strictly before a tick. The union-onset reading pairs
    // the last tone that sounded with the next one that starts, so a running
    // figuration must be sampled at its own onsets -- reading V0 one tick back
    // returns the tone it is still holding, which is a different pair.
    // `onset_tick`, when given, receives where that onset falls -- the caller
    // needs it to tell an onset the suspension rewrite will overwrite from one
    // the running figuration keeps.
    const auto upperOnsetBefore = [&](Tick tick, Tick* onset_tick) {
      int pitch = -1;
      Tick best = 0;
      for (const auto& variation : out.material.passacaglia_variations) {
        for (const MaterialNote& note : variation.notes) {
          if (note.start_tick < tick && (pitch < 0 || note.start_tick >= best)) {
            best = note.start_tick;
            pitch = static_cast<int>(note.pitch);
          }
        }
      }
      if (onset_tick != nullptr)
        *onset_tick = best;
      return pitch;
    };
    const auto counterOnsetBefore = [&](Tick tick) {
      int pitch = -1;
      Tick best = 0;
      for (const TrioVoiceLine& line : out.material.trio_voices) {
        for (const MaterialNote& note : line.notes) {
          if (note.start_tick < tick && (pitch < 0 || note.start_tick >= best)) {
            best = note.start_tick;
            pitch = static_cast<int>(note.pitch);
          }
        }
      }
      return pitch;
    };
    const int bass_prep = groundPitchAt(preparation_tick);
    const int bass_sus = groundPitchAt(suspension_tick);
    const int bass_res = groundPitchAt(resolution_tick);
    const int upper_prep = upperPitchAt(preparation_tick);
    const int original_upper_sus = upperPitchAt(suspension_tick);
    const int upper_res = upperPitchAt(resolution_tick);
    const int previous_upper_head = upperPitchAt(suspension_tick - kTicksPerBar34);
    const int previous_ground = groundPitchAt(suspension_tick - kTicksPerBar34);
    // The bar the suspension resolves into is read for the same reason its
    // approach is. The rewrite replaces the V0 tone AT this bar head, so it
    // decides two motions against the ground, not one, and a tone vetted only
    // on the way in is free to leave in perfect motion with the bass. Every
    // cycle cadence has a bar after it that states the ground again.
    const int next_upper_head = upperPitchAt(suspension_tick + kTicksPerBar34);
    const int next_ground = groundPitchAt(suspension_tick + kTicksPerBar34);
    // The bar-head chain above reads the ground at the grain a whole-bar ground
    // moves at. A cycle that states the ground in quarters moves three times
    // inside that span, and the pair a listener hears -- the pair the audit
    // samples -- is the figuration's own last onset against the ground sounding
    // under it, one beat back. The ground-parallel scrub already vetted that
    // onset at beat grain, but this rewrite lands after it and replaces the tone
    // it approved, so the beat-grain reference has to be re-read here or the
    // suspension is free to walk into the ground in fifths.
    Tick prior_upper_onset = 0;
    const int prior_upper = upperOnsetBefore(suspension_tick, &prior_upper_onset);
    const int prior_upper_ground = prior_upper >= 0 ? groundPitchAt(prior_upper_onset) : -1;
    int upper_window_min = 127;
    for (const auto& variation : out.material.passacaglia_variations) {
      for (const MaterialNote& note : variation.notes) {
        if (note.start_tick < resolution_tick + kTicksPerBeat &&
            note.start_tick + note.duration > preparation_tick)
          upper_window_min = std::min(upper_window_min, static_cast<int>(note.pitch));
      }
    }
    const CycleBar& suspension_chord =
        plan[static_cast<std::size_t>(sus_bar % kPassacagliaCycleBars)];
    const int chord_third = suspension_chord.minor ? 3 : 4;
    const std::array<int, 3> chord_pcs = {suspension_chord.root_pc,
                                          (suspension_chord.root_pc + chord_third) % 12,
                                          (suspension_chord.root_pc + 7) % 12};
    bool installed = false;
    // The V0 tone under the suspension is searched over a full octave, not just
    // the fifth around the figuration's own landing: the suspended dissonance
    // must be consonant with it, and the same chord tone taken an octave lower
    // is a normal cadential register choice that often clears a tritone the
    // near octave cannot. Ascending distance keeps the figuration's own tone
    // first, so a cadence that already works is left alone.
    //
    // The whole search runs twice at the closing cadence. The first pass also
    // demands that the pattern's own three tones form no true parallel with the
    // figuration; the second drops that demand, because some cadences admit no
    // parallel-free suspension at all and the form's closing dissonance is worth
    // more than the fault it carries.
    //
    // That trade is the closing gesture's alone. An interior cycle cadence has
    // no comparable claim -- it is one of several identical closes rather than
    // the one the piece ends on -- and buying its dissonance with a true
    // parallel would be paying a cardinal fault for an ornament the cycle can
    // simply go without. Those bars take the figure only when it is clean.
    const bool closing_cadence = sus_bar == static_cast<int>(req.bars) - 2;
    const int strict_passes = closing_cadence ? 2 : 1;
    for (int strict_pass = 0; strict_pass < strict_passes && !installed; ++strict_pass) {
      for (int distance = 0; distance <= 12 && !installed; ++distance) {
        for (int direction : {1, -1}) {
          if (distance == 0 && direction < 0)
            continue;
          const int upper_sus = original_upper_sus + direction * distance;
          const int pc = ((upper_sus % 12) + 12) % 12;
          if (upper_sus < 60 || upper_sus > 86 ||
              (pc != chord_pcs[0] && pc != chord_pcs[1] && pc != chord_pcs[2]))
            continue;
          if (previous_upper_head >= 0 && previous_ground >= 0 &&
              formsPerfectParallel(previous_upper_head, upper_sus, previous_ground, bass_sus))
            continue;
          if (prior_upper >= 0 && prior_upper_ground >= 0 &&
              formsPerfectParallel(prior_upper, upper_sus, prior_upper_ground, bass_sus))
            continue;
          // Held to the first pass only, like the strict check further down. An
          // interior cycle never reaches the second pass and so always honours
          // it; the close may spend a hidden perfect to keep its dissonance,
          // which is the same trade the second pass already makes against the
          // far more expensive true parallel. A closing cadence with no accented
          // dissonance at all is the worse outcome.
          if (strict_pass == 0 && next_upper_head >= 0 && next_ground >= 0 &&
              formsPerfectParallel(upper_sus, next_upper_head, bass_sus, next_ground))
            continue;
          // The same three motions can arrive on a unison the contrary way as
          // readily as they can approach a perfect interval in similar motion,
          // and against a bass that cannot move there is no second chance to
          // avoid it. Held to the first pass for the reason the departure check
          // is: it is the cheapest class the reference corpus prices, and a
          // closing cadence with no accented dissonance is worth less than one
          // that arrives carrying a battuta.
          if (strict_pass == 0 &&
              ((previous_upper_head >= 0 && previous_ground >= 0 &&
                formsBattuta(previous_upper_head, upper_sus, previous_ground, bass_sus)) ||
               (prior_upper >= 0 && prior_upper_ground >= 0 &&
                formsBattuta(prior_upper, upper_sus, prior_upper_ground, bass_sus)) ||
               (next_upper_head >= 0 && next_ground >= 0 &&
                formsBattuta(upper_sus, next_upper_head, bass_sus, next_ground))))
            continue;
          bool creates_augmented_second = false;
          if (req.mode == detail::Mode::Minor) {
            const auto isAbBPair = [](int a, int b) {
              const int a_pc = ((a % 12) + 12) % 12;
              const int b_pc = ((b % 12) + 12) % 12;
              return (a_pc == 8 && b_pc == 11) || (a_pc == 11 && b_pc == 8);
            };
            for (const auto& variation : out.material.passacaglia_variations) {
              for (std::size_t i = 0; i < variation.notes.size(); ++i) {
                const MaterialNote& note = variation.notes[i];
                if (note.start_tick > suspension_tick ||
                    suspension_tick >= note.start_tick + note.duration)
                  continue;
                creates_augmented_second =
                    (i > 0 && isAbBPair(variation.notes[i - 1].pitch, upper_sus)) ||
                    (i + 1 < variation.notes.size() &&
                     isAbBPair(upper_sus, variation.notes[i + 1].pitch));
              }
            }
          }
          if (creates_augmented_second)
            continue;
          const int ceiling = std::min({upper_prep, upper_sus, upper_res, upper_window_min}) - 1;
          for (SuspensionType type :
               {SuspensionType::Sus4_3, SuspensionType::Sus7_6, SuspensionType::Sus9_8}) {
            SuspensionPattern suspension;
            if (!designUpperSuspension(
                    type, preparation_tick, suspension_tick, resolution_tick,
                    /*voice=*/1, static_cast<std::uint8_t>(bass_prep),
                    static_cast<std::uint8_t>(bass_sus), static_cast<std::uint8_t>(bass_res),
                    static_cast<std::uint8_t>(upper_prep), static_cast<std::uint8_t>(upper_sus),
                    static_cast<std::uint8_t>(upper_res),
                    /*band_lo=*/std::max({bass_prep, bass_sus, bass_res}) + 1, ceiling, req.mode,
                    &suspension))
              continue;
            // The suspension's own three tones are a voice, and nothing has judged
            // them: the test above only asks whether relocating V0 to `upper_sus`
            // parallels the ground. Read each of the pattern's motions -- the
            // counter-line's approach into the preparation, the preparation into
            // the dissonance, the dissonance into its resolution -- against the V0
            // tone sounding opposite it, and reject a pattern that ships a true
            // parallel.
            const auto strictAgainst = [](int line_prev, int cand, int other_prev, int other_curr) {
              return line_prev >= 0 && cand >= 0 && other_prev >= 0 && other_curr >= 0 &&
                     formsStrictPerfectParallel(line_prev, cand, other_prev, other_curr);
            };
            // The rewrite replaces only the V0 note that CONTAINS the suspension;
            // a figuration running faster than the pattern keeps its own onsets
            // after it, and one of those -- not `upper_sus` -- is then what the
            // resolution answers.
            Tick res_onset = 0;
            const int res_onset_pitch = upperOnsetBefore(resolution_tick, &res_onset);
            const int upper_res_prev = res_onset > suspension_tick ? res_onset_pitch : upper_sus;
            if (strict_pass == 0 &&
                (strictAgainst(counterOnsetBefore(preparation_tick), suspension.preparation_pitch,
                               upperOnsetBefore(preparation_tick, nullptr), upper_prep) ||
                 strictAgainst(suspension.preparation_pitch, suspension.suspension_pitch,
                               upperOnsetBefore(suspension_tick, nullptr), upper_sus) ||
                 strictAgainst(suspension.suspension_pitch, suspension.resolution_pitch,
                               upper_res_prev, upper_res))) {
              continue;
            }
            for (auto& variation : out.material.passacaglia_variations) {
              std::vector<MaterialNote> rewritten;
              rewritten.reserve(variation.notes.size() + 2);
              for (const MaterialNote& note : variation.notes) {
                const Tick note_end = note.start_tick + note.duration;
                if (note.start_tick > suspension_tick || suspension_tick >= note_end) {
                  rewritten.push_back(note);
                  continue;
                }
                if (note.start_tick < suspension_tick) {
                  MaterialNote before = note;
                  before.duration = suspension_tick - note.start_tick;
                  rewritten.push_back(before);
                }
                MaterialNote accented = note;
                accented.start_tick = suspension_tick;
                accented.duration = std::min(note_end, resolution_tick) - suspension_tick;
                accented.pitch = static_cast<std::uint8_t>(upper_sus);
                rewritten.push_back(accented);
                if (note_end > resolution_tick) {
                  MaterialNote after = note;
                  after.start_tick = resolution_tick;
                  after.duration = note_end - resolution_tick;
                  rewritten.push_back(after);
                }
              }
              variation.notes.swap(rewritten);
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
