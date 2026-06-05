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

// Consonant interval-class set (mod 12): unison/octave, m3/M3, P4, P5, m6/M6.
// The complement {1,2,6,10,11} (m2/M2/TT/m7/M7) is what the audio scorer counts
// as a vertical dissonance when sampled beat-by-beat against the held ground.
constexpr bool isConsonantIc(int interval_class) {
  const int ivc = ((interval_class % 12) + 12) % 12;
  return ivc == 0 || ivc == 3 || ivc == 4 || ivc == 5 || ivc == 7 || ivc == 8 || ivc == 9;
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
 * Phase 1 resolves the full sequence of beat anchors (cycle_bars * 3 of them) as
 * diatonic-degree indices. Each beat onset is a chord tone of its bar's chord,
 * octave-fit to the octave NEAREST the previous anchor and clamped into a FIXED
 * register band so the descending chord roots do not drag the line down an
 * octave -- successive onsets therefore never leap more than a tritone, and the
 * line stays in the C4-C5 region above the ground. Because every anchor is a
 * chord tone consonant with the sustained ground, every beat-onset note the
 * audio scorer samples is consonant with the held bass (vertical-dissonance
 * ratio ~0).
 *
 * Phase 2 emits the notes: each beat opens on its anchor, then fills the
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
      while (fit > center + 12) fit -= 12;
      while (fit < center - 12) fit += 12;
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
// Ground (one dotted-half per bar):
//   Minor -> C-minor descending tetrachord C3 Bb2 Ab2 G2 (48 46 44 43), the
//            Phase16 chaconne ground rhythmically reworked to 3/4 (one note per
//            bar instead of one whole-note per 4/4 bar).
//   Major -> C-major descending line C3 B2 A2 G2 (48 47 45 43): the same
//            descending-fourth C->G contour, diatonic to C major.
//
// Harmony per cycle (one chord per bar): i / VII(or iv) / VI(or iv) / V in
// minor (lament descent), I / V / vi / V in major. The variation start tones
// trace the bar's bass an octave up (C4-region), so each bar's wave opens on a
// chord-consonant tone.
// ---------------------------------------------------------------------------
HarnessFixture buildChaconneForm(const ResolvedRequest& req) {
  const bool minor = req.mode == detail::Mode::Minor;

  // Ground pitches (cycle-relative, one per bar). Descending-fourth C->G.
  const std::vector<std::uint8_t> ground_pitch =
      minor ? std::vector<std::uint8_t>{48, 46, 44, 43}   // C3 Bb2 Ab2 G2
            : std::vector<std::uint8_t>{48, 47, 45, 43};  // C3 B2  A2  G2

  // Per-bar harmony + variation start tone for one 4-bar cycle.
  //   Minor: i (C) - VII (Bb) - VI (Ab) - V (G).
  //   Major: I (C) - V  (G)   - vi (A)  - V (G).
  // CycleBar fields: {chord root pc, minor?, low tone (C4 region), ground pc}.
  // The ground pc is the ground note's pitch class for that bar; the chord root
  // tracks it, so every chord-tone anchor is consonant with the held ground.
  std::vector<CycleBar> plan;
  if (minor) {
    plan = {
        {0, true, 60, 0},     // i  : ground C, start C4.
        {10, false, 58, 10},  // VII: ground Bb, start Bb3.
        {8, false, 56, 8},    // VI : ground Ab, start Ab3.
        {7, false, 55, 7},    // V  : ground G,  start G3.
    };
  } else {
    plan = {
        {0, false, 60, 0},   // I : ground C, start C4.
        {7, false, 55, 11},  // V6: ground B (chord G), start G3.
        {9, true, 57, 9},    // vi: ground A, start A3.
        {7, false, 55, 7},   // V : ground G, start G3.
    };
  }

  return buildGroundVariationForm(req, kChaconneCycleBars, ground_pitch, plan,
                                  /*passacaglia=*/false);
}

// ---------------------------------------------------------------------------
// Passacaglia: 3/4, 8-bar ground period (BWV582 model).
//
// Ground (one dotted-half per bar):
//   Minor -> kGroundMinorDescent C3 Bb2 Ab2 G2 F2 Eb2 D2 C2 (48 46 44 43 41 39
//            38 36), the BWV582-style descending lament line.
//   Major -> diatonic C-major descent C3 B2 A2 G2 F2 E2 D2 C2 (48 47 45 43 41
//            40 38 36): the same one-octave descent, diatonic to C major.
//
// Harmony per cycle (one chord per bar): the chord root tracks the ground pitch
// class per bar (the simplest valid mapping consistent with the bass), with the
// quality the diatonic triad quality on that scale degree.
// ---------------------------------------------------------------------------
HarnessFixture buildPassacagliaForm(const ResolvedRequest& req) {
  const bool minor = req.mode == detail::Mode::Minor;

  const std::vector<std::uint8_t> ground_pitch =
      minor
          ? std::vector<std::uint8_t>(detail::kGroundMinorDescent.begin(),
                                      detail::kGroundMinorDescent.end())  // 48 46 44 43 41 39 38 36
          : std::vector<std::uint8_t>{48, 47, 45, 43, 41, 40, 38, 36};    // 48 47 45 43 41 40 38 36

  // Per-bar harmony + variation start tone for one 8-bar cycle. The chord root
  // is the ground pitch class for that bar; the variation start tone is the
  // ground pitch lifted two octaves into the C4-C5 region.
  // CycleBar fields: {chord root pc, minor?, low tone (C4 region), ground pc}.
  // The chord root equals the ground pitch class every bar, so every chord-tone
  // anchor is consonant with the held ground.
  std::vector<CycleBar> plan;
  if (minor) {
    // C-minor scale-degree triad qualities: i (C) VII (Bb) VI (Ab) V (G, major
    // = harmonic-minor dominant) iv (F) III (Eb) ii0->ii (D, treated minor) i (C).
    plan = {
        {0, true, 60, 0},     // bar 0  i   : ground C,  start C4.
        {10, false, 58, 10},  // bar 1  VII : ground Bb, start Bb3.
        {8, false, 56, 8},    // bar 2  VI  : ground Ab, start Ab3.
        {7, false, 55, 7},    // bar 3  V   : ground G,  start G3.
        {5, true, 53, 5},     // bar 4  iv  : ground F,  start F3.
        {3, false, 51, 3},    // bar 5  III : ground Eb, start Eb3.
        {2, true, 50, 2},     // bar 6  ii  : ground D,  start D3.
        {0, true, 60, 0},     // bar 7  i   : ground C,  start C4 (cadential return).
    };
  } else {
    // C-major scale-degree triad qualities: I (C) vii0->VII(B treated major to
    // stay consonant) vi (A) V (G) IV (F) iii (E) ii (D) I (C).
    plan = {
        {0, false, 60, 0},    // bar 0  I  : ground C, start C4.
        {11, false, 59, 11},  // bar 1  (B): ground B, start B3.
        {9, true, 57, 9},     // bar 2  vi : ground A, start A3.
        {7, false, 55, 7},    // bar 3  V  : ground G, start G3.
        {5, false, 53, 5},    // bar 4  IV : ground F, start F3.
        {4, true, 52, 4},     // bar 5  iii: ground E, start E3.
        {2, true, 50, 2},     // bar 6  ii : ground D, start D3.
        {0, false, 60, 0},    // bar 7  I  : ground C, start C4 (cadential return).
    };
  }

  return buildGroundVariationForm(req, kPassacagliaCycleBars, ground_pitch, plan,
                                  /*passacaglia=*/true);
}

}  // namespace bach::composer
