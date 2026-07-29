#ifndef BACH_COMPOSER_FIGURATION_PALETTE_H
#define BACH_COMPOSER_FIGURATION_PALETTE_H

#include <cstdint>
#include <vector>

#include "composer/figuration.h"
#include "composer/material.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

namespace bach::composer {

// ---------------------------------------------------------------------------
// Shared figuration palette: the common figuration vocabulary used by the
// form builders. Every pattern in the palette satisfies one contract:
//
//   - each beat (per-beat patterns) or bar downbeat opens on a chord-tone
//     anchor consonant with the active harmony,
//   - the realized line is confined to a register band, folding by octaves
//     (or reversing direction) at the band edges instead of clamping flat,
//   - the surface between anchors is stepwise-dominant (the corpus melodic-
//     interval mass is on single scale steps),
//   - output is a MaterialNote sequence (NoteSource::Material carriers).
//
// The builders keep the grammar (which bars carry which pattern, voices,
// spans, carrier declarations); the palette owns only the note generation.
// ---------------------------------------------------------------------------

/// @brief The palette's figuration vocabulary.
enum class PatternKind : std::uint8_t {
  kSawtooth,     ///< Per-beat chord-tone anchors with stepwise fills toward the next anchor.
  kScalarWave,   ///< Continuous stepwise wave folding at the band edges, downbeat re-anchored.
  kFiguration,   ///< Registry-aware consonant, parallel-free scalar-wave figuration.
  kArpeggio,     ///< Per-beat broken-chord figures over the bar's triad tones.
  kFiguraCorta,  ///< Long-short-short per-beat figure on chord-tone anchors.
  kGesture,      ///< Mordent onset + descending run, then silence for the rest of the bar.
  kChordBlock,   ///< Multi-voice simultaneous triad blocks (half / whole notes).
  kPedalWalk,    ///< Root-fifth walking line in quarters (pedal solo material).
};

// 3/4 bar length in ticks: 3 quarter beats. Mirrors HarmonicPlan::ticksPerBar()
// for a 3/4 plan so builder-side and validator-side bar math agree on the
// ground-variation forms.
constexpr Tick kTicksPerBar34 = 3 * kTicksPerBeat;  // 1440.

// Per-bar harmonic data of one ground cycle: the chord root pitch class, the
// chord quality (minor flag), the lowest variation tone for the V0 figuration,
// and the sounding ground pitch class. The ground pitch class lets the pattern
// builders pick beat anchors that stay consonant with the held bass note.
struct CycleBar {
  std::uint8_t root_pc;
  bool minor;
  int low_tone;            // lowest variation tone (C4-C5 region) for this bar.
  std::uint8_t ground_pc;  // pitch class of the sustained ground note this bar.
};

/**
 * @brief Map a diatonic-degree index to its MIDI pitch.
 *
 * Degree indices count scale members upward from C0 = MIDI 12 (degree 0, well
 * below any figuration register, so indices stay non-negative). Working in
 * degree indices makes fills stepwise by construction.
 *
 * @param degree Diatonic degree index (0 = C0).
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 * @return The MIDI pitch of the degree.
 */
int degreeToMidi(int degree, detail::Mode mode);

/**
 * @brief Invert degreeToMidi for any pitch, snapping down to the nearest
 *        scale member.
 * @param midi MIDI pitch to convert.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 * @return The diatonic degree index of the scale member at or below `midi`.
 */
int midiToDegree(int midi, detail::Mode mode);

/**
 * @brief Octave-fit a pitch class to the MIDI pitch nearest a target center.
 * @param pitch_class Target pitch class (0..11).
 * @param center Register center the result should sit closest to.
 * @return The MIDI pitch of `pitch_class` whose octave is nearest `center`.
 */
int fitPitchClass(int pitch_class, int center);

/**
 * @brief Collect the chord-tone anchor pitch classes for one bar.
 *
 * The anchors are the bar's chord tones (root / third / fifth), each consonant
 * with the held ground (the chord root tracks the ground pitch class). In minor
 * the leading tone B natural (pc 11) is filtered out so the line stays in
 * natural minor and no Ab->B augmented 2nd can arise.
 *
 * @param bar The bar's harmonic data.
 * @param mode Diatonic mode (selects the third quality filter behaviour).
 * @return Up to three consonant chord-tone pitch classes (always non-empty).
 */
std::vector<int> barAnchorPitchClasses(const CycleBar& bar, detail::Mode mode);

/**
 * @brief Append one ground cycle of sawtooth figuration: per-beat chord-tone
 *        anchoring with stepwise diatonic fills, threaded continuously to
 *        minimise leaps (PatternKind::kSawtooth, 3/4 cycle form).
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
void appendSawtoothCycle(std::vector<MaterialNote>& notes, Tick block_start,
                         const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                         int anchor_rotation, int notes_per_beat, detail::Mode mode);

/**
 * @brief Append one ground cycle as a continuous diatonic scalar wave
 *        (stepwise-dominant, no repeated pitches, no leaps)
 *        (PatternKind::kScalarWave, 3/4 cycle form).
 *
 * Unlike the per-beat chord-tone sawtooth (appendSawtoothCycle), this walks one
 * diatonic scale degree per emitted note and folds its direction at the band
 * edges. Single-step motion is the corpus's dominant melodic interval, so the
 * realized line's melodic-interval distribution matches the reference far
 * better (the dominant scorer feature). EVERY beat onset snaps the wave to the
 * nearest chord tone of the bar's chord so each sampled beat onset stays
 * consonant against the held ground; the snap moves at most two scale degrees
 * (a third at worst, never a genuine leap), and the run between onsets keeps
 * the conjunct scalar surface.
 *
 * Variation differentiation (no RNG): `phase_rotation` shifts the wave's start
 * degree and `descending_start` flips the opening direction, so consecutive
 * cycles trace distinct contours without reintroducing leaps.
 *
 * @param notes Destination note vector (the variation's realized line).
 * @param block_start Absolute start tick of the variation block.
 * @param cycle_bar_plan Per-bar harmony + register data for the ground cycle.
 * @param band_lo Register band floor (already lifted by any arc shift).
 * @param band_hi Register band ceiling (already lifted by any arc shift).
 * @param phase_rotation Cycle-driven shift of the wave's start degree.
 * @param descending_start When true, the wave opens descending.
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendScalarWaveCycle(std::vector<MaterialNote>& notes, Tick block_start,
                           const std::vector<CycleBar>& cycle_bar_plan, int band_lo, int band_hi,
                           int phase_rotation, bool descending_start, int notes_per_beat,
                           detail::Mode mode);

/**
 * @brief Append one 4/4 bar of chord-tone-anchored scalar-wave notes
 *        (PatternKind::kScalarWave, 4/4 bar form).
 *
 * The bar opens on a chord tone of `chord` (so a downbeat anchor is consonant),
 * then runs a stepwise scalar wave (ascend then descend) confined to
 * [base_midi, ceil_midi]. Predominantly-stepwise running figuration
 * (BWV565 / BWV538 toccata idiom) -- low melodic-interval cost. The seed
 * `offset` shifts the start degree up the scale before the chord-tone snap.
 *
 * @param dst Note vector receiving the bar's notes.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param chord The bar's chord (downbeat anchor is one of its tones).
 * @param mode Diatonic mode selecting the scale walker.
 * @param notes_per_beat Subdivision density (1 = quarter, 2 = eighth, 4 = 16th).
 * @param base_midi Register floor for the wave.
 * @param ceil_midi Register ceiling for the wave.
 * @param offset Seed-derived start-degree offset above the anchor (used only
 *        for the first bar of a section, where `prev_pitch` is still < 0).
 * @param prev_pitch Running pitch threaded across bars: the previous bar's last
 *        emitted pitch (< 0 on a section's first bar). The bar's downbeat anchor
 *        is the chord tone NEAREST this pitch so consecutive bars chain
 *        conjunctly instead of re-snapping to the band floor each bar (which
 *        produced the wide bar-boundary leaps that dominated the melodic-
 *        interval cost). Updated to the bar's last emitted pitch on return.
 * @param rotate_figures When true, the bar figure rotates by (bar + offset) % 4
 *        (broken-third chain / one fourth-or-fifth dive / sixth-leap cascade /
 *        triad arpeggio sweep), restoring the corpus's third-leap-and-sixth
 *        vocabulary to an otherwise purely stepwise surface; mixing the seed
 *        offset into the phase makes different seeds distribute the figures
 *        differently. Off by default so existing callers stay byte-identical.
 * @param triplet When true, the bar subdivides into sixteenth triplets (6 notes
 *        per beat, 80 ticks each) instead of the `notes_per_beat` grid, running
 *        the same chord-anchored stepwise wave over the denser subdivision (the
 *        toccata's drive into the fugue tightens from sixteenths to triplet
 *        sixteenths). Off by default so existing callers stay byte-identical.
 */
void appendScalarWaveBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                         detail::Mode mode, int notes_per_beat, int base_midi, int ceil_midi,
                         int offset, int& prev_pitch, bool rotate_figures = false,
                         bool triplet = false);

/**
 * @brief Append one 4/4 bar of theme-consonant scalar-wave chord-tone
 *        figuration (PatternKind::kFiguration).
 *
 * Every beat opens on a chord tone of `chord` chosen to be consonant against
 * the concurrent thematic statement (the scorer samples vertical intervals on
 * the beat grid, so the on-beat note is what drives vertical_dissonance). The
 * per-beat anchors form a stepwise chain: each anchor is the nearest consonant
 * chord tone to the PREVIOUS anchor (threaded across bars via `prev_anchor`),
 * gently biased back toward a band centre so the line neither drifts out of band
 * nor leaps between register extremes. The notes between anchors walk by single
 * scale steps so the whole line is conjunct (the corpus melodic-interval
 * distribution is dominated by steps). The downbeat anchor is always a genuine
 * chord tone so figuration_harmonic_consistency passes; band confinement keeps
 * the voice ordering (V0 >= V1 >= V2) intact across an all-Material texture.
 *
 * The wave is parallel-aware (a step that would land a same-direction perfect
 * 5th/8th against an earlier voice reverses, then tries a scale-third skip),
 * harshness-aware (minor-2nd / tritone / major-7th sustains against earlier
 * voices reverse the same way, and consecutive 2nds/7ths against the same
 * voice are rejected), and order-clamped (each note is held inside the window
 * formed by the concurrently sounding lower- and higher-index voices). Every
 * emitted note is recorded back into `registry` so a voice placed later in the
 * same window avoids parallels against this line.
 *
 * @param registry Read/written for the inter-voice parallel-avoidance lookup.
 * @param section Figuration section receiving the bar's notes.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param voice Voice index of the figuration line.
 * @param chord The bar's chord (supplies the per-beat chord tones).
 * @param mode Diatonic mode selecting the scale walker.
 * @param notes_per_beat Subdivision density (1 / 2 / 4).
 * @param offset Seed-derived start-register offset above the band floor.
 * @param prev_anchor Running anchor threaded across bars; updated to the bar's
 *        last anchor so the next bar's first anchor chains stepwise from it.
 * @param band_lo Register band floor for this voice.
 * @param band_hi Register band ceiling for this voice.
 * @param num_voices Total voice count scanned for concurrent motion.
 * @param cadential_close When true the bar's first half is the running wave as
 *        usual, but its second half (from tick position kTicksPerBar/2) sounds
 *        as ONE held anchor tone (a half note on the mid-bar strong beat)
 *        instead of the running subdivision. The held tone uses the same anchor
 *        machinery the wave already uses (consonant chord tone, theme-consonant,
 *        parallel-aware, order-clamped, band-confined) and is additionally
 *        vetted for consonance against every theme tone the registry reports as
 *        sounding anywhere inside the held half-bar window; when no candidate
 *        passes, the normal running wave is emitted for the second half instead
 *        (graceful degradation). This gives the ornament pass a strong-beat
 *        quarter-or-longer top note to place a mandatory section-cadence trill on.
 */
void appendFigurationWaveBar(ThemeToneRegistry& registry, FigurationSection& section, int bar,
                             int voice, const detail::ChordSpec& chord, detail::Mode mode,
                             int notes_per_beat, int offset, int& prev_anchor, int band_lo,
                             int band_hi, VoiceId num_voices, bool cadential_close = false);

/**
 * @brief Firing counters for appendFigurationWaveBar's reactive layers.
 *
 * Each counter increments only when the layer actually CHANGED the note the
 * wave was about to emit (a veto that confirmed the default costs nothing).
 * The counters measure how often the wave's design space is hostile enough to
 * need reactive escapes: a layer that no longer fires under the designed
 * chord plans is dead machinery and a candidate for removal. Measurement
 * only -- generation never reads these.
 */
struct WaveVetoStats {
  long anchor_parallel_displaced = 0;  ///< Beat anchor moved off a parallel arrival.
  long wobble_breaker_fired = 0;       ///< Two-pitch oscillation escaped by register move.
  long step_parallel_adjusted = 0;     ///< Wave step reversed / skipped off a parallel.
  long step_harsh_adjusted = 0;        ///< Wave step reversed / skipped off a sharp clash.
  long order_clamp_changed = 0;        ///< Step pinned into the concurrent voice-order window.
  long window_expanded = 0;            ///< Working window stretched to contain a snapped anchor.

  void reset() { *this = WaveVetoStats{}; }
  long total() const {
    return anchor_parallel_displaced + wobble_breaker_fired + step_parallel_adjusted +
           step_harsh_adjusted + order_clamp_changed + window_expanded;
  }
};

/// @brief Process-wide accumulator for the wave's reactive-layer firings.
WaveVetoStats& waveVetoStats();

/**
 * @brief Append one ground cycle of broken-chord arpeggio figuration
 *        (PatternKind::kArpeggio, 3/4 cycle form).
 *
 * Each bar realizes its triad as three stacked chord tones (bass / mid / top)
 * octave-fit into the register band, then every beat traces one of four fixed
 * figure contours over those tones (mid-bass-mid-top, mid-top-mid-bass,
 * bass-mid-top-mid, top-mid-bass-mid). Every emitted note is a chord tone, so
 * each beat onset is consonant with the held ground by construction.
 *
 * Variation differentiation (no RNG): `figure_index` selects the contour.
 *
 * @param notes Destination note vector (the variation's realized line).
 * @param block_start Absolute start tick of the variation block.
 * @param cycle_bar_plan Per-bar harmony + register data for the ground cycle.
 * @param band_lo Register band floor (already lifted by any arc shift).
 * @param band_hi Register band ceiling (already lifted by any arc shift).
 * @param figure_index Selects the figure contour (taken modulo the table size).
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat (2 takes the
 *        contour's opening pair per beat; 1 walks the contour one element per
 *        beat, so consecutive notes never repeat one pitch).
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendArpeggioCycle(std::vector<MaterialNote>& notes, Tick block_start,
                         const std::vector<CycleBar>& cycle_bar_plan, int band_lo, int band_hi,
                         int figure_index, int notes_per_beat, detail::Mode mode);

/**
 * @brief Append one ground cycle of figura corta figuration
 *        (PatternKind::kFiguraCorta, 3/4 cycle form).
 *
 * Every beat carries the long-short-short figura corta cell (eighth + two
 * sixteenths, filling exactly one beat). The long note opens on a chord-tone
 * anchor resolved exactly like the sawtooth's anchor chain (nearest-octave
 * fitting, gentle re-centering, hard octave fold), and the two shorts step
 * diatonically toward the next beat's anchor, so the surface stays conjunct
 * and every beat onset is consonant with the held ground.
 *
 * Variation differentiation (no RNG): `anchor_rotation` rotates which chord
 * tone opens each bar's anchor group.
 *
 * @param notes Destination note vector (the variation's realized line).
 * @param block_start Absolute start tick of the variation block.
 * @param cycle_bar_plan Per-bar harmony + register data for the ground cycle.
 * @param register_shift Semitone register lift from the arc (raises the band).
 * @param anchor_rotation Cycle-driven rotation of the chord-tone anchor order.
 * @param mode Diatonic mode (Major / Minor) selecting the scale.
 */
void appendFiguraCortaCycle(std::vector<MaterialNote>& notes, Tick block_start,
                            const std::vector<CycleBar>& cycle_bar_plan, int register_shift,
                            int anchor_rotation, detail::Mode mode);

/**
 * @brief Append one 4/4 bar of broken-chord arpeggio figuration
 *        (PatternKind::kArpeggio, 4/4 bar form).
 *
 * The bar's triad is realized as three stacked chord tones inside the band,
 * and every beat traces one of the four fixed figure contours over those
 * tones (see appendArpeggioCycle). Every emitted note is a chord tone, so each
 * beat onset is consonant against any concurrent chord tone below it.
 *
 * @param notes Destination note vector.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param chord The bar's chord (supplies the triad tones).
 * @param mode Diatonic mode (reserved; chord tones need no scale walk).
 * @param band_lo Register band floor.
 * @param band_hi Register band ceiling.
 * @param figure_index Selects the figure contour (taken modulo the table size).
 * @param notes_per_beat Subdivision: 1 / 2 / 4 notes per beat.
 */
void appendArpeggioBar(std::vector<MaterialNote>& notes, int bar, const detail::ChordSpec& chord,
                       detail::Mode mode, int band_lo, int band_hi, int figure_index,
                       int notes_per_beat);

/**
 * @brief Append one 4/4 bar of figura corta figuration
 *        (PatternKind::kFiguraCorta, 4/4 bar form).
 *
 * Every beat carries either the long-short-short figura corta cell (eighth +
 * two sixteenths) or, when `dotted` is true, a dotted-eighth + sixteenth
 * variant. The four beat anchors are contour-indexed picks from the
 * chord-tone ladder above `start` (`figure` 0 -- the default, byte-identical
 * for existing callers -- is the low-amplitude wave a0, a1, a2, a1; the other
 * contours widen or redirect the anchor windows so repeated corta bars do not
 * stamp one bigram family), and the two shorts step diatonically toward the
 * next beat's anchor, stopping short of it so the next onset is a fresh
 * attack. Every beat onset is a chord tone.
 *
 * @param notes Destination note vector.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param start The bar's opening anchor (already snapped to a chord tone).
 * @param chord The bar's chord (supplies the anchor wave's triad tones).
 * @param mode Diatonic mode selecting the scale walker for the shorts.
 * @param figure Anchor-contour selector (mod 4; 0 = legacy wave).
 * @param dotted Use the dotted-eighth + sixteenth cell.
 */
void appendFiguraCortaBar(std::vector<MaterialNote>& notes, int bar, int start,
                          const detail::ChordSpec& chord, detail::Mode mode, int figure = 0,
                          bool dotted = false);

/**
 * @brief Append one 4/4 bar opening gesture: a mordent onset followed by a
 *        descending run, with the rest of the bar silent
 *        (PatternKind::kGesture).
 *
 * The gesture is a written-out mordent (upper diatonic neighbour, then the
 * main tone -- the highest triad tone whose upper neighbour still fits the
 * band) followed by a descending diatonic sixteenth run from the main tone.
 * The run stops at the band floor instead of repeating it, and the remainder
 * of the bar is left silent (rhetorical rest), so the emitted durations cover
 * at most the bar's first two beats.
 *
 * @param dst Note vector receiving the gesture's notes.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param chord The bar's chord (supplies the main-tone triad).
 * @param mode Diatonic mode selecting the scale walker.
 * @param band_lo Register band floor.
 * @param band_hi Register band ceiling.
 * @param octave_drop Number of octaves to lower the whole gesture (0 = the
 *        default high statement, byte-identical for existing callers). The drop
 *        is pulled back toward 0 in whole octaves until the main-tone anchor
 *        still sits at or above `band_lo`, so a drop that would sink the anchor
 *        below the voice band uses the lowest octave that fits (the descending
 *        run tail may reach below `band_lo` -- the rhetorical low flourish).
 */
void appendGestureBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                      detail::Mode mode, int band_lo, int band_hi, int octave_drop = 0);

/**
 * @brief Append one 4/4 bar of multi-voice simultaneous triad blocks
 *        (PatternKind::kChordBlock).
 *
 * The bar's triad is realized as stacked chord tones below `top_hi` (top, then
 * the next triad tone below, then the next), one per voice in descending
 * order, so the per-tick voice order V0 >= V1 >= V2 holds by construction.
 * Each voice strikes the same block rhythm: `block_dur` ticks per block
 * (half / whole notes). Triads only -- diminished-seventh voicings are out of
 * the palette's vocabulary.
 *
 * @param voice_notes One destination vector per voice (index = voice id);
 *        the vector count selects how many triad tones are stacked (max 3).
 * @param bar Absolute bar index (4/4 bar grid).
 * @param chord The bar's chord (supplies the triad tones).
 * @param mode Diatonic mode (reserved for scale-aware voicings).
 * @param top_hi Ceiling for the top voice's chord tone.
 * @param block_dur Block length in ticks (half or whole note).
 */
void appendChordBlockBar(std::vector<std::vector<MaterialNote>>& voice_notes, int bar,
                         const detail::ChordSpec& chord, detail::Mode mode, int top_hi,
                         Tick block_dur);

/**
 * @brief Append one 4/4 bar of root-fifth walking line in quarters
 *        (PatternKind::kPedalWalk).
 *
 * Beats alternate the chord root and its fifth, each octave-fit nearest the
 * previous emitted pitch (threaded across bars via `prev_pitch`) and clamped
 * into the band, so the walk stays conjunct in contour (never more than a
 * fifth between consecutive notes) while sounding only root / fifth tones --
 * the ground-style walking idiom for pedal solo material.
 *
 * @param dst Note vector receiving the bar's notes.
 * @param bar Absolute bar index (4/4 bar grid).
 * @param chord The bar's chord (supplies root and fifth).
 * @param mode Diatonic mode (reserved for scale-aware passing tones).
 * @param band_lo Register band floor.
 * @param band_hi Register band ceiling.
 * @param prev_pitch Running pitch threaded across bars (< 0 on the first bar,
 *        which then opens on the root nearest the band centre). Updated to the
 *        bar's last emitted pitch on return.
 */
void appendPedalWalkBar(std::vector<MaterialNote>& dst, int bar, const detail::ChordSpec& chord,
                        detail::Mode mode, int band_lo, int band_hi, int& prev_pitch);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FIGURATION_PALETTE_H
