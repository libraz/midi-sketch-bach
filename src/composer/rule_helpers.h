#ifndef BACH_COMPOSER_RULE_HELPERS_H
#define BACH_COMPOSER_RULE_HELPERS_H

#include <array>
#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

using bach::NoteEvent;

namespace rule_helpers {

// Harmonic primitives.

// Returns the three triad pitch classes (root, third, fifth) for a chord,
// reduced mod 12. Seventh-chord qualities collapse to their underlying
// triad (the seventh is not part of the returned triad).
std::array<std::uint8_t, 3> triadPitchClasses(const ChordEvent& chord);

// Returns the chord active at `at`: the last ChordEvent whose start_tick
// is <= at. Empty plans return a neutral C-major fallback so callers can
// report validation failure without dereferencing an empty vector.
const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at);

// Beat / pitch primitives.

// Returns true iff `tick` lands on a bar downbeat. `ticks_per_bar` is the
// meter-derived bar length (HarmonicPlan::ticksPerBar()); it defaults to the
// global kTicksPerBar (4/4 = 1920) so pre-meter callers stay byte-identical.
bool isStrongBeat(Tick tick, Tick ticks_per_bar = kTicksPerBar);

/// @brief Return the centralized meter/profile-aware strength at a tick.
MetricalStrength metricalStrengthAt(const HarmonicPlan& plan, Tick tick);

/// @brief True for primary or secondary structural accents.
bool isStructuralAccent(const HarmonicPlan& plan, Tick tick);

std::uint8_t pitchClass(std::uint8_t pitch);

// Returns true iff `pitch` is the diatonic leading tone of the plan's key
// (tonic_pc + 11). Used by both candidate enumeration and validator to
// flag notes that require an upward stepwise resolution to the tonic.
bool isLeadingTone(std::uint8_t pitch, const HarmonicPlan& plan);

// Strict leading-tone resolution check. Returns true iff `resolution` is
// the tonic (mod 12), is strictly above `leading`, and the leap is at
// most a whole step. Callers that want the "if leading then must
// resolve; else allow" guard should compose: `!isLeadingTone(prev, plan)
// || isLeadingToneResolution(prev, cand, plan)`.
bool isLeadingToneResolution(std::uint8_t leading, int resolution, const HarmonicPlan& plan);

// The key in force at a tick: the plan's home key updated by every modulation
// whose boundary the tick has reached. This is the single source of truth for
// "what key is it here"; TonalContext is built on top of it.
struct KeyContext {
  std::uint8_t tonic_pc = 0;
  bool is_minor = false;
};

// Consults only the modulation list, never the chord list, so it stays cheap
// enough for the validator's pairwise rule loops.
KeyContext keyAt(const HarmonicPlan& plan, Tick tick);

// Local tonal policy derived from the latest modulation and active harmony.
// `leading_tone_pc` resolves to `resolution_pc`; `has_active_leading_tone` is
// false outside dominant/secondary-dominant contexts.
struct TonalContext {
  std::uint8_t tonic_pc = 0;
  bool is_minor = false;
  bool is_secondary_dominant = false;
  bool has_active_leading_tone = false;
  std::uint8_t leading_tone_pc = 11;
  std::uint8_t resolution_pc = 0;
};

TonalContext tonalContextAt(const HarmonicPlan& plan, Tick tick);
bool isContextualScalePitch(std::uint8_t pitch, const HarmonicPlan& plan, Tick tick,
                            int melodic_motion);
bool isContextualLeadingTone(std::uint8_t pitch, const HarmonicPlan& plan, Tick tick);
bool isContextualLeadingToneResolution(std::uint8_t leading, int resolution,
                                       const HarmonicPlan& plan, Tick tick);
bool isContextualAugmentedMelodicInterval(std::uint8_t from, std::uint8_t to,
                                          const HarmonicPlan& plan, Tick from_tick, Tick to_tick);

// Interval primitives (semitones, can be signed).

bool isPerfectInterval(int semitones);

bool isConsonantInterval(int semitones);

// Common-practice vertical consonance is bass-sensitive: a perfect fourth is
// dissonant when its lower note is the actual bass, but the same interval may
// occur between upper voices when both pitches are consonant above the bass.
// Contextual exceptions (prepared suspensions / declared six-four chords) are
// deliberately handled by the validator because they require material and
// harmonic declarations.
bool isConsonantAboveBass(std::uint8_t pitch, std::uint8_t bass_pitch);
bool isBassSensitiveConsonance(std::uint8_t pitch_a, std::uint8_t pitch_b, std::uint8_t bass_pitch);

// A cross relation is the SAME scale degree sounding with two different
// chromatic inflections, so the verdict depends on the key rather than on the
// bare semitone distance: D/Eb are two adjacent degrees of C minor but a
// degree and its raised form in C major. Each pitch class is mapped to the
// degree it represents (itself when diatonic, otherwise the degree it is an
// inflection of, preferring the degree a semitone below so a chromatic tone
// reads as raised); the pair is a cross relation iff both map to the same
// degree. The minor reference scale is the harmonic minor, which makes the
// melodic-minor 6th and the natural 7th read as inflections of it.
//
// The key context is required, never defaulted: judging in the wrong key both
// invents cross relations between ordinary neighbouring degrees and hides the
// real ones. Long pieces modulate, so pass the LOCAL key at the tick being
// judged (see keyAt), not the home key.
bool isCrossRelationPc(std::uint8_t a, std::uint8_t b, std::uint8_t tonic_pc, bool is_minor);

// Melodic-interval rules (mirror Validator Rule P1: forbidden melodic
// leaps for Compose voices). Shared so the CandidateSearch pre-filter
// rejects exactly the leaps the Validator would later reject — keeping
// the two in lockstep instead of letting the search emit a note that
// fails validation and bounces the seed.
//
// isAugmentedMelodicInterval: tritone (6 semis, indistinguishable from an
//   augmented 4th in MIDI) or an augmented 2nd/7th (3 semis between scale
//   degrees one step or a seventh apart). `plan` supplies the diatonic set.
// isDiminishedMelodicInterval: tritone (6) or major 7th (11, the diminished
//   octave spelling).
bool isAugmentedMelodicInterval(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan);

bool isDiminishedMelodicInterval(std::uint8_t from, std::uint8_t to);

// Union of the two rules above plus the bare tritone: true iff a melodic
// leap from `from` to `to` is one the Validator forbids for Compose notes.
// Callers that need the secondary-dominant exemption must apply it before
// calling (the Validator skips the rule when a has_secondary_of chord is
// active at either endpoint).
bool isForbiddenMelodicLeap(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan,
                            Tick from_tick = 0, Tick to_tick = 0);

// Pitch-at-time queries over the composer's incremental commit log.
// `notes` is voice-grouped: spans for one voice are contiguous, but
// voices are not interleaved by start_tick, so loops must `continue`
// rather than `break` on a higher start_tick.

std::uint8_t voicePitchAt(const std::vector<NoteEvent>& notes, VoiceId voice, Tick tick);

std::uint8_t sameVoiceStartingAt(const std::vector<NoteEvent>& placed, VoiceId voice, Tick tick);

// Pre-commit rule checks. Each returns true iff committing
// `(candidate_pitch, candidate_voice)` at `cur_tick` would violate the
// named rule against any already-placed voice in `placed`.

bool createsVoiceCrossing(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick);

bool createsVerticalDissonance(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                               std::uint8_t candidate_pitch, Tick cur_tick);

bool createsParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                            std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                            Tick prev_tick);

// Like createsParallelPerfect, but for the case where a faster other voice has
// an onset between the candidate voice's previous onset (prev_tick) and
// cur_tick. createsParallelPerfect compares against the other voice's pitch at
// prev_tick, which can miss a parallel the validator (sampling the union of
// onsets) catches at the intermediate union tick. This complements that check
// by comparing the candidate's move against the other voice's latest
// intermediate onset. Used where a slow Compose voice accompanies a fast
// Material figuration voice.
bool createsParallelPerfectAcrossOnset(const std::vector<NoteEvent>& placed,
                                       VoiceId candidate_voice, std::uint8_t candidate_pitch,
                                       Tick cur_tick, std::uint8_t prev_pitch, Tick prev_tick);

// Like createsParallelPerfect but restricted to parallel OCTAVES (unison
// reduced mod 12 == 0). Parallel fifths are NOT checked. Cadence cells
// are allowed to bypass the general parallel-perfect rule because they
// pin specific bass pitch classes, but parallel octaves remain a hard
// stylistic prohibition (the bass line cannot ride in lockstep with an
// upper voice an octave higher into the cadence).
bool createsParallelOctave(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                           std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                           Tick prev_tick);

// Anti-parallel perfect: the pair leaves a perfect fifth or octave and lands on
// the same class again by CONTRARY motion. The checks above all require
// same-direction motion, so this class is invisible to every one of them, and
// the search needs it separately or it keeps offering candidates the validator
// then refuses.
bool createsAntiParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                std::uint8_t candidate_pitch, Tick cur_tick,
                                std::uint8_t prev_pitch, Tick prev_tick);

// Ottava battuta: contrary motion into a perfect octave or unison the pair was
// not already on, with the upper voice leaping down into it. Contrary motion,
// so it is disjoint from every parallel check above rather than a variant of
// one, and the search needs it separately or it keeps offering candidates the
// validator then refuses.
bool createsBattuta(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                    std::uint8_t candidate_pitch, Tick cur_tick, std::uint8_t prev_pitch,
                    Tick prev_tick);

bool createsHiddenParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                  std::uint8_t candidate_pitch, Tick cur_tick,
                                  std::uint8_t prev_pitch, Tick prev_tick);

// Faster-voice complement of createsHiddenParallelPerfect (see
// createsParallelPerfectAcrossOnset for the blind-spot rationale).
bool createsHiddenParallelPerfectAcrossOnset(const std::vector<NoteEvent>& placed,
                                             VoiceId candidate_voice, std::uint8_t candidate_pitch,
                                             Tick cur_tick, std::uint8_t prev_pitch,
                                             Tick prev_tick);

// `plan` supplies the local key: each pair is judged in the key of the later
// of the two onsets, so an alteration arriving after a modulation is read in
// the key it arrives in.
bool createsCrossRelation(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick, const HarmonicPlan& plan);

// Material queries.

// Returns the cadence cell whose approach or cadence tick is exactly `tick`,
// or nullptr when no cell touches it. Both candidate enumeration paths use it
// to discover the forced approach/cadence pitch classes at a position.
const CadenceCell* cadenceCellAt(const Material& material, Tick tick);

// Provenance-bit helpers. Both share the same shape: OR the bits the named
// idiom implies into `rules`, leaving every other bit untouched.

// Functional-harmony helper: set the four functional-harmony provenance
// bits on `rules` when the active chord opts into the strict regime
// (has_degree=true). Caller passes `pc` (candidate pitch class) and
// `is_chord_tone` so the helper doesn't recompute triad arithmetic.
//
//   ChordToneRoman  — set when the candidate is a chord tone of a
//                     degree-tagged chord. Stricter sibling of
//                     RuleBit::ChordTone (which fires regardless of
//                     has_degree).
//   InversionLabel  — set when the candidate's pitch class matches the
//                     chord's declared bass pc (i.e. the candidate
//                     could serve as the bass for the inversion). The
//                     bit fires per-voice; whichever voice carries the
//                     bass note will be the one that lights the bit.
//   DoublingChecked — set unconditionally inside a has_degree chord
//                     region: the doubling rules in the Validator (no
//                     leading-tone double, no 7th double) sweep the
//                     tick.
//   SpacingChecked  — set unconditionally inside a has_degree chord
//                     region: spacing rule sweeps the tick.
void applyP7Bits(RuleIdMask& rules, const ChordEvent& chord, std::uint8_t pc, bool is_chord_tone);

// Modulation/tonicization helper: set the four corresponding provenance
// bits when the surrounding context matches each idiom.
//
//   ModulationCommitted        — the active chord sits at or after a
//                                ModulationEvent boundary (the plan has
//                                committed to a new key area, and this
//                                candidate's pitch is participating in
//                                that area).
//   SecondaryDominantResolved  — the active chord is the resolution of
//                                a previous secondary dominant: the
//                                most recent chord with has_secondary_of=
//                                true (strictly before the active chord)
//                                declares secondary_of equal to the
//                                active chord's degree.
//   PicardyThird               — the active chord is the final tonic
//                                with is_picardy=true and the candidate
//                                lands on the major third (root + 4).
//   ModalMixture               — the active chord declares is_borrowed=
//                                true (a parallel-mode loan) and the
//                                candidate is a chord tone of that
//                                chord.
void applyP8Bits(RuleIdMask& rules, const HarmonicPlan& plan, const ChordEvent& chord,
                 std::uint8_t pc, bool is_chord_tone);

}  // namespace rule_helpers
}  // namespace bach::composer

#endif  // BACH_COMPOSER_RULE_HELPERS_H
