#ifndef BACH_COMPOSER_RULE_HELPERS_H
#define BACH_COMPOSER_RULE_HELPERS_H

#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "core/basic_types.h"

namespace bach::composer {

using bach::NoteEvent;

namespace rule_helpers {

// Beat / pitch primitives.

bool isStrongBeat(Tick tick);

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

bool createsCrossRelation(const std::vector<NoteEvent>& placed, VoiceId candidate_voice,
                          std::uint8_t candidate_pitch, Tick cur_tick);

}  // namespace rule_helpers
}  // namespace bach::composer

#endif  // BACH_COMPOSER_RULE_HELPERS_H
