#ifndef BACH_COMPOSER_RULE_HELPERS_H
#define BACH_COMPOSER_RULE_HELPERS_H

#include <array>
#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
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
// is <= at. The plan's chord list is assumed non-empty and sorted by
// start_tick.
const ChordEvent& activeChord(const HarmonicPlan& plan, Tick at);

// Beat / pitch primitives.

// Returns true iff `tick` lands on a bar downbeat. `ticks_per_bar` is the
// meter-derived bar length (HarmonicPlan::ticksPerBar()); it defaults to the
// global kTicksPerBar (4/4 = 1920) so pre-meter callers stay byte-identical.
bool isStrongBeat(Tick tick, Tick ticks_per_bar = kTicksPerBar);

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

// Interval primitives (semitones, can be signed).

bool isPerfectInterval(int semitones);

bool isConsonantInterval(int semitones);

bool isCrossRelationPc(std::uint8_t a, std::uint8_t b);

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
bool isForbiddenMelodicLeap(std::uint8_t from, std::uint8_t to, const HarmonicPlan& plan);

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

bool createsHiddenParallelPerfect(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                                  std::uint8_t candidate_pitch, Tick cur_tick,
                                  std::uint8_t prev_pitch, Tick prev_tick);

// Faster-voice complement of createsHiddenParallelPerfect (see
// createsParallelPerfectAcrossOnset for the blind-spot rationale).
bool createsHiddenParallelPerfectAcrossOnset(const std::vector<NoteEvent>& placed,
                                             VoiceId candidate_voice, std::uint8_t candidate_pitch,
                                             Tick cur_tick, std::uint8_t prev_pitch,
                                             Tick prev_tick);

bool createsCrossRelation(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick);

}  // namespace rule_helpers
}  // namespace bach::composer

#endif  // BACH_COMPOSER_RULE_HELPERS_H
