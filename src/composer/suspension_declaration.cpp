#include "composer/suspension_declaration.h"

#include <array>
#include <cstdint>

#include "composer/figuration.h"
#include "composer/rule_helpers.h"

namespace bach::composer {

namespace {

// The seventh of the chord the plan declares at `at`: a pitch class sounded by the
// chord but outside its triad (a minor triad's diminished fifth is not a seventh).
bool isDeclaredChordSeventh(const HarmonicPlan& plan, int pitch, Tick at) {
  const ChordEvent* chord = rule_helpers::chordAt(plan, at);
  if (chord == nullptr)
    return false;
  const int pc = pitch % 12;
  for (const std::uint8_t triad_pc : rule_helpers::triadPitchClasses(*chord)) {
    if (triad_pc == pc)
      return false;
  }
  return pc != (chord->root_pc + 6) % 12 &&
         (rule_helpers::chordPcsAt(plan, at) & detail::pcBit(pc)) != 0;
}

}  // namespace

bool declaredSuspensionOrdered(const DeclaredSuspensionReading& reading) {
  return reading.preparation_tick < reading.suspension_tick &&
         reading.suspension_tick < reading.resolution_tick;
}

bool declaredSuspensionPrepared(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                                SuspensionType type) {
  if (!declaredSuspensionOrdered(reading) || reading.held_pitch <= 0 ||
      reading.lowest_other_at_preparation <= 0)
    return false;
  const auto held = static_cast<std::uint8_t>(reading.held_pitch);
  const auto other = static_cast<std::uint8_t>(reading.lowest_other_at_preparation);
  if (type == SuspensionType::Sus2_3)
    return rule_helpers::isConsonantAboveBass(other, held);
  return rule_helpers::isConsonantAboveBass(held, other) ||
         isDeclaredChordSeventh(plan, reading.held_pitch, reading.preparation_tick);
}

bool declaredSuspensionStepsDown(const DeclaredSuspensionReading& reading) {
  if (!declaredSuspensionOrdered(reading) || reading.held_pitch <= 0 ||
      reading.resolution_pitch <= 0)
    return false;
  const int delta = reading.resolution_pitch - reading.held_pitch;
  return delta == -1 || delta == -2;
}

bool declaredSuspensionAccented(const HarmonicPlan& plan,
                                const DeclaredSuspensionReading& reading) {
  // MetricalStrength orders strongest first, so a stronger tick has the smaller value.
  return static_cast<int>(rule_helpers::metricalStrengthAt(plan, reading.suspension_tick)) <
         static_cast<int>(rule_helpers::metricalStrengthAt(plan, reading.resolution_tick));
}

bool suspensionIntervalClassesHold(SuspensionType type, int suspension_ic, int resolution_ic) {
  switch (type) {
    case SuspensionType::Sus4_3:
      return suspension_ic == 5 && (resolution_ic == 3 || resolution_ic == 4);
    case SuspensionType::Sus7_6:
      return (suspension_ic == 10 || suspension_ic == 11) &&
             (resolution_ic == 8 || resolution_ic == 9);
    case SuspensionType::Sus9_8:
      return (suspension_ic == 1 || suspension_ic == 2) && resolution_ic == 0;
    case SuspensionType::Sus2_3:
      return (suspension_ic == 1 || suspension_ic == 2) &&
             (resolution_ic == 3 || resolution_ic == 4);
  }
  return false;
}

bool declaredSuspensionIntervals(const DeclaredSuspensionReading& reading, SuspensionType type) {
  if (reading.lowest_other_at_suspension <= 0 || reading.lowest_other_at_resolution <= 0 ||
      reading.held_pitch <= 0 || reading.resolution_pitch <= 0)
    return false;
  int suspension_ic = 0;
  int resolution_ic = 0;
  if (type == SuspensionType::Sus2_3) {
    if (reading.lowest_other_at_suspension <= reading.held_pitch ||
        reading.lowest_other_at_resolution <= reading.resolution_pitch)
      return false;
    suspension_ic = (reading.lowest_other_at_suspension - reading.held_pitch) % 12;
    resolution_ic = (reading.lowest_other_at_resolution - reading.resolution_pitch) % 12;
  } else {
    suspension_ic = ((reading.held_pitch - reading.lowest_other_at_suspension) % 12 + 12) % 12;
    resolution_ic =
        ((reading.resolution_pitch - reading.lowest_other_at_resolution) % 12 + 12) % 12;
  }
  return suspensionIntervalClassesHold(type, suspension_ic, resolution_ic);
}

bool declaredSuspensionHolds(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                             SuspensionType type) {
  return declaredSuspensionPrepared(plan, reading, type) && declaredSuspensionStepsDown(reading) &&
         declaredSuspensionAccented(plan, reading) && declaredSuspensionIntervals(reading, type) &&
         reading.suspension_tick - reading.preparation_tick >=
             reading.resolution_tick - reading.suspension_tick;
}

bool classifyDeclaredSuspension(const HarmonicPlan& plan, const DeclaredSuspensionReading& reading,
                                SuspensionType* type) {
  static constexpr std::array<SuspensionType, 4> kTypes = {
      SuspensionType::Sus4_3, SuspensionType::Sus7_6, SuspensionType::Sus9_8,
      SuspensionType::Sus2_3};
  for (const SuspensionType candidate : kTypes) {
    if (declaredSuspensionHolds(plan, reading, candidate)) {
      if (type != nullptr)
        *type = candidate;
      return true;
    }
  }
  return false;
}

bool isDeclaredSuspensionTone(const std::vector<SuspensionPattern>& patterns, VoiceId voice,
                              Tick tick) {
  for (const SuspensionPattern& pattern : patterns) {
    if (pattern.voice != voice)
      continue;
    if ((tick >= pattern.preparation_tick && tick < pattern.resolution_tick) ||
        tick == pattern.resolution_tick)
      return true;
  }
  return false;
}

RuleIdMask declaredSuspensionNoteBits(const std::vector<SuspensionPattern>& patterns, VoiceId voice,
                                      Tick start_tick, Tick duration, std::uint8_t pitch) {
  RuleIdMask bits;
  for (const SuspensionPattern& pattern : patterns) {
    if (pattern.voice != voice)
      continue;
    if (pattern.preparation_tick == start_tick &&
        pattern.resolution_tick == start_tick + duration && pattern.preparation_pitch == pitch &&
        pattern.suspension_pitch == pitch)
      bits |= ruleBitMask(RuleBit::SuspensionPrepared);
    if (pattern.resolution_tick == start_tick && pattern.resolution_pitch == pitch)
      bits |= ruleBitMask(RuleBit::SuspensionResolved);
  }
  return bits;
}

}  // namespace bach::composer
