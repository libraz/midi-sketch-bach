#ifndef BACH_COMPOSER_TEXTURE_HELPERS_H
#define BACH_COMPOSER_TEXTURE_HELPERS_H

#include <array>
#include <cstdint>
#include <vector>

#include "composer/figuration.h"
#include "composer/material.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer {

/// @brief Return the octave displacement that fits a transposed subject in a voice band.
///
/// `base_semis` is applied before fitting (for example, -5 for a real answer).
/// The result is an additional multiple of twelve.  Fitting the actual,
/// transposed range is essential: fitting the untransposed subject and then
/// applying a real answer can drop its lowest notes into the bass band's range.
/// The ceiling-first rule preserves the strict V0 >= V1 >= V2 register order.
int octaveOffsetForBand(const std::array<std::uint8_t, 16>& subject, int base_semis, int voice,
                        const std::array<int, 3>& band_lo, const std::array<int, 3>& band_hi);

/// @brief Decide whether a subject that exposes tonic/dominant motion needs a tonal answer.
bool shouldUseTonalAnswer(const std::array<std::uint8_t, 16>& subject, std::uint8_t tonic_pc);

/// @brief Repeat the mode's four-bar harmony catalogue for a form section.
std::vector<detail::ChordSpec> buildRepeatingChordPlan(int total_bars, detail::Mode mode,
                                                       int harmony_index);

// ---------------------------------------------------------------------------
// Shared texture / parallel-avoidance machinery.
//
// The form builders place every voice as verbatim Material in a deterministic
// voice order. A line built later in that order must be able to read what every
// earlier voice is sounding at a given tick so it can pick an anchor that is
// consonant with the concurrent theme tones and that does not form a parallel
// fifth/octave against any earlier voice (the cardinal Bach prohibition). The
// helpers below provide that read-back registry, the parallel predicate, and a
// tier-scored consonant anchor selector, shared across the form builders.
// ---------------------------------------------------------------------------

/// @brief Interval class (0..11) folded to consonance, true when consonant.
///
/// Consonant interval classes are the unison/octave, m3/M3, P4, P5, and m6/M6
/// families (IC 0,3,4,5,7,8,9 mod 12); the dissonant complement the scorer
/// penalises is {1,2,6,10,11} (m2/M2/TT/m7/M7). A perfect fourth (IC 5) is
/// treated as consonant here because it sits between upper voices over a
/// chord-tone bass. Accepts any signed interval (it is folded into [0,12)).
///
/// @param interval Signed semitone interval (or interval class).
/// @return True when the folded interval class is consonant.
constexpr bool isConsonantIc(int interval) {
  const int ivc = ((interval % 12) + 12) % 12;
  return ivc == 0 || ivc == 3 || ivc == 4 || ivc == 5 || ivc == 7 || ivc == 8 || ivc == 9;
}

/// @brief Test whether two pitches form a consonant interval class.
///
/// Convenience wrapper over isConsonantIc for a pair of MIDI pitches: the
/// external scorer's vertical-dissonance metric samples every beat and flags a
/// beat dissonant when any sounding pair has interval class in {1,2,6,10,11}
/// (m2 / M2 / TT / m7 / M7). The consonant classes are unison/octave (0),
/// m3/M3 (3/4), P4 (5), P5 (7), and m6/M6 (8/9).
///
/// @param pitch_a First MIDI pitch.
/// @param pitch_b Second MIDI pitch.
/// @return True if the pair's interval class is consonant.
constexpr bool isConsonantPair(int pitch_a, int pitch_b) {
  return isConsonantIc(pitch_a - pitch_b);
}

/// @brief True when moving from (line_prev, other_prev) to (cand, other_curr)
///        forms a forbidden parallel or hidden perfect fifth/octave.
///
/// A parallel perfect = both voices change pitch in the same direction and the
/// interval class is 0 (octave/unison) or 7 (fifth) at both the previous and the
/// current onset. A hidden perfect = the same-direction arrival lands on IC 0/7
/// from a different interval with the upper-moving voice leaping (> 2 semitones).
/// Both are the cardinal Bach prohibition between independent voices.
///
/// @param line_prev The line-under-construction's previous pitch (-1 = none).
/// @param cand The line-under-construction's candidate current pitch.
/// @param other_prev The already-placed voice's previous pitch (-1 = silent).
/// @param other_curr The already-placed voice's current pitch (-1 = silent).
/// @return True when the motion forms a forbidden parallel/hidden perfect.
bool formsPerfectParallel(int line_prev, int cand, int other_prev, int other_curr);

/// @brief True only for a TRUE parallel perfect (IC 0/7 of the same class at
///        both onsets under same-direction motion); the hidden-perfect
///        extension of formsPerfectParallel is excluded.
///
/// The relaxed tier for guards facing a forced arrival: when an immutable tone
/// (a CF skeleton note) meets a band-pinned tone (the bass root) on a perfect
/// interval, every approach candidate is at least a hidden perfect, so a guard
/// demanding full freedom keeps whatever it started with -- including a true
/// parallel. Checking this predicate instead lets it displace off the true
/// parallel and accept the unavoidable hidden.
///
/// @param line_prev The line-under-construction's previous pitch (-1 = none).
/// @param cand The line-under-construction's candidate current pitch.
/// @param other_prev The already-placed voice's previous pitch (-1 = silent).
/// @param other_curr The already-placed voice's current pitch (-1 = silent).
/// @return True when the motion forms a true parallel perfect.
bool formsStrictPerfectParallel(int line_prev, int cand, int other_prev, int other_curr);

/// @brief Motion of one already-placed voice across two consecutive onsets.
struct ConcurrentMotion {
  VoiceId voice = 0;  // the already-placed voice this motion belongs to.
  int prev = -1;      // sounding pitch at the previous onset (-1 = silent).
  int curr = -1;      // sounding pitch at the current onset (-1 = silent).
};

/// @brief Registry of every already-placed note (theme AND figuration).
///
/// A line built later in the deterministic voice order reads this registry to
/// learn what every earlier voice sounds at a given tick. This drives two
/// things: (1) the consonance-aware figuration anchor picks a chord tone that is
/// consonant with the concurrent theme tone, and (2) the parallel-aware anchor
/// avoids same-direction arrivals on interval class 0/7 against any earlier
/// voice.
class ThemeToneRegistry {
 public:
  /// @brief One placed note for the inter-voice lookup.
  struct ThemeTone {
    Tick tick = 0;
    Tick duration = 0;
    VoiceId voice = 0;
    int pitch = 0;
  };

  /// @brief Record a placed note (theme or figuration) for the inter-voice
  ///        lookup.
  ///
  /// Quarter-note thematic onsets pass their quarter duration; figuration notes
  /// pass their actual sub-beat duration so the concurrent-pitch lookup resolves
  /// the right sounding pitch at any onset, not only beat onsets.
  void record(Tick tick, VoiceId voice, int pitch, Tick duration);

  /// @brief Pitch sounding in `voice` at `tick`, or -1 when the voice is silent.
  ///
  /// Returns the latest-onset note of `voice` whose [start, start+duration)
  /// window contains `tick`. Used to read what an earlier-placed voice sounds at
  /// a beat onset so the line under construction can avoid a parallel against it.
  int soundingPitchInVoice(VoiceId voice, Tick tick) const;

  /// @brief Collect concurrent theme-tone pitches at `tick` excluding `voice`.
  ///
  /// The figuration on `voice` must not clash with any statement sounding at the
  /// same beat. An onset match against each registered note's [start, start+dur)
  /// window captures the concurrent tone (theme tones span a quarter; figuration
  /// tones span their own sub-beat duration).
  void concurrentThemePitches(Tick tick, VoiceId voice, std::vector<int>& out_pitches) const;

  /// @brief Gather each earlier voice's (prev, curr) motion around a beat onset.
  ///
  /// For every voice other than `voice`, reads the pitch it sounds at `tick` and
  /// at `prev_tick`. The line under construction consults these so its own beat
  /// anchor never forms a parallel (same-direction arrival on interval class
  /// 0/7) with a voice already placed. Only voices sounding at the current onset
  /// are returned; a voice silent at `tick` cannot be in a parallel here.
  ///
  /// @param prev_tick Previous onset.
  /// @param tick Current onset.
  /// @param voice The line-under-construction's voice (excluded from the scan).
  /// @param num_voices Number of voices to scan (each lower index sounds higher).
  /// @param out_motions Receives one ConcurrentMotion per sounding earlier voice.
  void concurrentMotions(Tick prev_tick, Tick tick, VoiceId voice, VoiceId num_voices,
                         std::vector<ConcurrentMotion>& out_motions) const;

 private:
  std::vector<ThemeTone> tones_;
};

/// @brief Derive and realize a shared two-pass countersubject.
///
/// Pass 1 scores one in-band diatonic anchor per source note, preferring
/// consonance, contrary motion, and short repeat runs. Pass 2 realizes those
/// anchors with a complementary quarter/run/arpeggio rhythm while the source
/// pitch is held. Fugue and sectional fugue-tail builders share this exact
/// path so pitch scoring and rhythmic realization cannot drift.
void appendScoredCountersubject(const std::vector<MaterialNote>& source, VoiceId voice, Tick start,
                                Tick end, int band_lo, int band_hi, detail::Mode mode,
                                std::vector<MaterialNote>& destination,
                                ThemeToneRegistry& registry);

/// @brief Replace part of one carrier span with an explicit suspension carrier.
///
/// The original carrier is split at preparation and after resolution, so its
/// authored notes cannot overlap the three-note prep/suspension/resolution
/// replay. Span ids remain unique and all unrelated spans are preserved.
/// Returns false when no containing span exists or the declaration is invalid.
bool installSuspensionCarrier(Material& material, VoicePlan& voice_plan,
                              const SuspensionPattern& pattern);

/// @brief Return the latest-onset material pitch sounding at a tick, or -1.
int soundingMaterialPitch(const std::vector<MaterialNote>& notes, Tick tick);

/// @brief Design a bass-verified upper-voice suspension inside a register band.
///
/// Supports 4-3, 7-6, and 9-8. The preparation pitch equals the suspended
/// pitch, is consonant over `bass_at_preparation`, and resolves down one
/// diatonic-sized semitone step to the type-specific consonance. Returns false
/// when the supplied bass motion and band admit no valid pattern.
bool designUpperSuspension(SuspensionType type, Tick preparation_tick, Tick suspension_tick,
                           Tick resolution_tick, VoiceId voice, std::uint8_t bass_at_preparation,
                           std::uint8_t bass_at_suspension, std::uint8_t bass_at_resolution,
                           std::uint8_t upper_at_preparation, std::uint8_t upper_at_suspension,
                           std::uint8_t upper_at_resolution, int band_lo, int band_hi,
                           detail::Mode mode, SuspensionPattern* pattern);

/// @brief Pick the band chord tone that is consonant and parallel-free.
///
/// Enumerates the chord tones of `chord` inside the [band_lo, band_hi] register
/// band and returns the one nearest `target` that is (1) consonant against every
/// concurrent theme pitch and (2) does not form a parallel / hidden perfect
/// fifth or octave with any earlier-placed voice, given this line's own previous
/// beat anchor (`line_prev`). The downbeat anchor must always be a genuine chord
/// tone (figuration_harmonic_consistency checks the bar downbeat), so on the
/// downbeat this only ever returns chord tones. The selection is a strict
/// deterministic priority: a consonant, parallel-free chord tone first; then
/// (with `parallel_free_over_consonant`) a parallel-free tone with the mildest
/// clash profile (sharp ic 1/6/11 clashes weighted double); then a consonant
/// tone that forms a parallel; then the parallel-free clashing tone when the
/// flag is off; then the least-dissonant tone. The flag encodes a per-form
/// contract: fugue-family figuration prefers the parallel-free escape (the
/// parallel fifth/octave is the cardinal prohibition, and earlier-placed
/// verbatim or ornament lines can pin the vertical so that no consonant
/// parallel-free tone exists -- a mild passing clash or an oblique repeat is
/// the correct escape there), while ground-variation forms hold every beat
/// onset mutually consonant over the held ground and keep consonance first.
///
/// `target` is the previous beat's anchor, so consecutive anchors chain to the
/// nearest admissible chord tone of each other -- the per-beat anchor chain
/// therefore stays conjunct rather than leaping between register extremes.
///
/// The voice-ordering window keeps the per-tick register order intact: a
/// lower-indexed voice sounds higher, so the anchor stays at or below every
/// concurrent lower-index voice and at or above every concurrent higher-index
/// voice even when a verbatim entry briefly leaves its own band.
///
/// @param chord The bar's chord (supplies the triad pitch classes).
/// @param voice Voice index (selects the order window side per concurrent voice).
/// @param band_lo Register band floor for this voice.
/// @param band_hi Register band ceiling for this voice.
/// @param target Preferred register centre (the wave is built around it).
/// @param theme_pitches Concurrent thematic pitches to stay consonant against.
/// @param line_prev This line's previous beat anchor (-1 = none yet).
/// @param motions Earlier voices' (prev, curr) motion around this onset.
/// @param mode Diatonic mode selecting the in-scale test for non-downbeat tones.
/// @param downbeat True only on the bar downbeat, which must be a genuine chord
///        tone (figuration_harmonic_consistency); off-downbeat beats may anchor
///        on any consonant diatonic tone, widening the parallel-free options so
///        two figuration voices over one triad are not forced into a parallel.
/// @param window_pitches Pitches other voices sound INSIDE this anchor's sustain
///        window (after the onset). A sustained anchor (e.g. a quarter-note bass
///        under an eighth-note line placed earlier) can be onset-consonant yet
///        clash with a mid-beat attack above it; among equally onset-consonant
///        candidates the one with fewer window clashes wins. Tie-breaker only:
///        onset consonance always dominates, so an empty vector (the default)
///        reproduces the previous behaviour exactly.
/// @return A chord-tone (downbeat) or diatonic (off-beat) MIDI pitch in band.
int consonantChordTone(const detail::ChordSpec& chord, int voice, int band_lo, int band_hi,
                       int target, const std::vector<int>& theme_pitches, int line_prev,
                       const std::vector<ConcurrentMotion>& motions, detail::Mode mode,
                       bool downbeat, const std::vector<int>& window_pitches = {},
                       bool parallel_free_over_consonant = false);

/// @brief Replace a line's closing two bars with the shared cadential landing.
///
/// The Baroque closing formula every form lands on: the figuration stops
/// running, a long penultimate tone takes the cadential trill, and the final
/// tone is held plain. Erases every note of `line` at or after the
/// penultimate bar (`penult_bar_start`) and appends:
///   * an eighth-note approach run through the cadence's first structural
///     beat (four eighths in 4/4, two in 3/4) that walks diatonically INTO the
///     pre-final tone — ascending when the run fits the band (in minor the
///     ascent into the leading tone raises the sixth degree, melodic-minor
///     fashion), otherwise descending from above;
///   * the held pre-final tone from beat three in 4/4 or beat two in 3/4 — the
///     trill site on the real beat grid inside the cadence window;
///   * a full-bar final tone — the held resolution (a voice's last attack is
///     never ornamented).
///
/// @param line The voice's note list (modified in place).
/// @param penult_bar_start Tick of the penultimate bar's downbeat.
/// @param ticks_per_bar Bar length (1920 = 4/4, 1440 = 3/4).
/// @param prefinal The held pre-final pitch (leading tone or supertonic —
///        whichever the caller's penultimate harmony supports).
/// @param final_pitch The held final pitch (the tonic in cadencing forms).
/// @param mode Diatonic mode for the approach run.
/// @param band_lo Approach register floor: a run that would dip below it
///        flips to the descending shape.
/// @param downbeat_chord When non-null, the approach's first (downbeat) tone
///        is snapped to the nearest chord tone of this chord, satisfying the
///        figuration downbeat chord-tone rule for FigurationCarrier lines.
/// @param prefer_descending Force the descending approach shape (a run from
///        above into the pre-final tone). Ground-variation forms use it when
///        the penultimate bar's immutable ground tone clashes with the
///        ascending run's downbeat.
/// @param lift_to_context Re-octave the whole formula (+12) when the line it
///        interrupts runs high: if the last figuration pitch before the
///        landing sits more than a major sixth above the approach run's entry
///        tone, the close would fall off a registral cliff at the seam, so
///        the formula lifts an octave instead (pitch classes are preserved,
///        so every consonance relation against the other voices is
///        unchanged). The lift is skipped when it would carry the trill above
///        d''' (MIDI 86), the top of the Bach keyboard compass.
/// @param ts_numerator Notated beats per bar; 3 selects the real second beat.
void appendCadentialLanding(std::vector<MaterialNote>& line, Tick penult_bar_start,
                            Tick ticks_per_bar, int prefinal, int final_pitch, detail::Mode mode,
                            int band_lo, const detail::ChordSpec* downbeat_chord = nullptr,
                            bool prefer_descending = false, bool lift_to_context = false,
                            std::uint8_t ts_numerator = 4);

/// @brief Emit an actual cadential I6/4 -> V -> I upper-voice cell.
///
/// The first beat holds the tonic above a dominant pedal (the dissonant fourth
/// of I in second inversion); the next beat resolves down to the leading tone
/// over the same dominant bass, then the following bar resolves to the tonic.
void appendCadentialSixFourLanding(std::vector<MaterialNote>& line, Tick penult_bar_start,
                                   Tick ticks_per_bar, int tonic, int leading_tone);

/// @brief Replace a line's final bar with the compact cadential landing.
///
/// The single-bar variant for forms whose penultimate-bar harmony cannot host
/// a long pre-final tone (the chaconne: its immutable ground reaches the
/// dominant only in the final bar). The final bar splits into a held
/// pre-final tone through the first strong beat (the trill site) and the held
/// final tone for the remainder. In 3/4, that beat is beat two rather than the
/// bar's arithmetic midpoint.
///
/// @param line The voice's note list (modified in place).
/// @param final_bar_start Tick of the final bar's downbeat.
/// @param ticks_per_bar Bar length (1920 = 4/4, 1440 = 3/4).
/// @param prefinal The held pre-final pitch (first half of the bar).
/// @param final_pitch The held final pitch (second half of the bar).
/// @param ts_numerator Notated beats per bar; controls the cadential beat.
void appendCompactCadentialLanding(std::vector<MaterialNote>& line, Tick final_bar_start,
                                   Tick ticks_per_bar, int prefinal, int final_pitch,
                                   std::uint8_t ts_numerator = 4);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_TEXTURE_HELPERS_H
