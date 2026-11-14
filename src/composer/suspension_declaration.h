#ifndef BACH_COMPOSER_SUSPENSION_DECLARATION_H
#define BACH_COMPOSER_SUSPENSION_DECLARATION_H

#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

/// @brief The sounding facts a declared suspension is judged on.
///
/// `held_pitch` is the tone sounding in the suspended voice from the preparation
/// through the suspension tick. The `lowest_other_*` fields are the lowest pitch
/// of any other voice sounding at that tick, or -1 when no other voice sounds.
struct DeclaredSuspensionReading {
  Tick preparation_tick = 0;
  Tick suspension_tick = 0;
  Tick resolution_tick = 0;
  int held_pitch = 0;
  int resolution_pitch = 0;
  int lowest_other_at_preparation = -1;
  int lowest_other_at_suspension = -1;
  int lowest_other_at_resolution = -1;
};

/// @brief The lowest pitch any voice of `voices` other than `voice` sounds at `tick`, or -1
///        when none sounds: the operand of every `lowest_other_*` field.
///
/// `pitch_at(other, tick)` returns the pitch sounding in `other`, 0 when it is silent. A pass
/// that changes the texture after a suspension is declared reads the operand through this
/// same selection, so it guards exactly what the Validator measures.
template <typename Voices, typename PitchAt>
int lowestOtherSoundingPitch(const Voices& voices, VoiceId voice, Tick tick, PitchAt pitch_at) {
  int lowest = -1;
  for (const VoiceId other : voices) {
    if (other == voice)
      continue;
    const int pitch = static_cast<int>(pitch_at(other, tick));
    if (pitch != 0 && (lowest < 0 || pitch < lowest))
      lowest = pitch;
  }
  return lowest;
}

/// @brief True when the readings are ordered preparation < suspension < resolution.
bool declaredSuspensionOrdered(const DeclaredSuspensionReading& reading);

/// @brief Preparation rule: the held tone is consonant against the lowest other voice at
///        the preparation tick (or, for a 2-3, that voice is consonant above the held
///        tone), or it is the seventh of the chord the plan declares there.
bool declaredSuspensionPrepared(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                                SuspensionType type);

/// @brief Resolution rule: the suspended voice steps down by one or two semitones.
bool declaredSuspensionStepsDown(const DeclaredSuspensionReading& reading);

/// @brief Accent rule: the suspension tick is metrically stronger than the resolution tick.
bool declaredSuspensionAccented(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading);

/// @brief Interval rule: the dissonance and its resolution form the type's interval pair
///        against the lowest other voice (a 2-3 requires that voice above the held tone).
bool declaredSuspensionIntervals(const DeclaredSuspensionReading& reading, SuspensionType type);

/// @brief The type's interval table over interval classes (semitones mod 12).
bool suspensionIntervalClassesHold(SuspensionType type, int suspension_ic, int resolution_ic);

/// @brief True when every declared-suspension rule holds for `type`: the single-voice rules
///        (preparation, step down, accent, intervals) and a held tone at least as long before
///        the suspension tick as after it.
bool declaredSuspensionHolds(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                             SuspensionType type);

/// @brief Classifies a tie into the suspension type whose rules all hold.
/// @param[out] type The matching type; untouched when none matches.
/// @return True when some type validates; the Validator accepts exactly these ties.
bool classifyDeclaredSuspension(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                                SuspensionType* type);

/// @brief True for the held span [preparation_tick, resolution_tick) and the resolution
///        onset of any pattern declared in `voice`.
bool isDeclaredSuspensionTone(const std::vector<SuspensionPattern>& patterns, VoiceId voice,
                              Tick tick);

/// @brief Provenance bits a declared suspension gives the note sounding
///        [start_tick, start_tick + duration) at `pitch` in `voice`.
///
/// SuspensionPrepared: the note spans exactly [preparation_tick, resolution_tick) at the
/// pattern's held pitch, so a note cut short by span clipping gets none.
/// SuspensionResolved: the note starts at resolution_tick at the resolution pitch.
/// Bits of every pattern are ORed, so a note that resolves one pattern and prepares the
/// next carries both.
RuleIdMask declaredSuspensionNoteBits(const std::vector<SuspensionPattern>& patterns, VoiceId voice,
                                      Tick start_tick, Tick duration, std::uint8_t pitch);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_SUSPENSION_DECLARATION_H
