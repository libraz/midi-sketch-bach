// Rhythm pattern implementation for beat subdivision.

#include "core/rhythm_pattern.h"

namespace bach {

const char* rhythmPatternToString(RhythmPattern pattern) {
  switch (pattern) {
    case RhythmPattern::Straight:       return "Straight";
    case RhythmPattern::DottedEighth:   return "DottedEighth";
    case RhythmPattern::DottedQuarter:  return "DottedQuarter";
    case RhythmPattern::Syncopated:     return "Syncopated";
    case RhythmPattern::LombardReverse: return "LombardReverse";
    case RhythmPattern::Triplet:        return "Triplet";
  }
  return "Unknown";
}

std::vector<Tick> getPatternDurations(RhythmPattern pattern) {
  switch (pattern) {
    case RhythmPattern::Straight:
      return {kTicksPerBeat};  // 480
    case RhythmPattern::DottedEighth:
      return {360, 120};  // dotted 8th + 16th
    case RhythmPattern::DottedQuarter:
      return {720, 240};  // dotted quarter + 8th (spans 2 beats)
    case RhythmPattern::Syncopated:
      return {240, 240};  // equal 8ths with off-beat accent
    case RhythmPattern::LombardReverse:
      return {120, 360};  // 16th + dotted 8th
    case RhythmPattern::Triplet:
      return {160, 160, 160};  // 3 equal subdivisions
  }
  return {kTicksPerBeat};  // fallback: quarter note
}

uint8_t notesPerBeat(RhythmPattern pattern) {
  switch (pattern) {
    case RhythmPattern::Straight:       return 1;
    case RhythmPattern::DottedEighth:   return 2;
    case RhythmPattern::DottedQuarter:  return 1;  // 2 notes / 2 beats = 1/beat
    case RhythmPattern::Syncopated:     return 2;
    case RhythmPattern::LombardReverse: return 2;
    case RhythmPattern::Triplet:        return 3;
  }
  return 1;  // fallback
}

std::vector<RhythmPattern> getAllowedPatterns(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {RhythmPattern::Straight, RhythmPattern::DottedQuarter};
    case SubjectCharacter::Playful:
      return {RhythmPattern::Straight, RhythmPattern::DottedEighth,
              RhythmPattern::Triplet, RhythmPattern::LombardReverse};
    case SubjectCharacter::Noble:
      return {RhythmPattern::Straight, RhythmPattern::DottedQuarter};
    case SubjectCharacter::Restless:
      return {RhythmPattern::DottedEighth, RhythmPattern::Syncopated,
              RhythmPattern::LombardReverse, RhythmPattern::Triplet};
  }
  return {RhythmPattern::Straight};  // fallback
}

RhythmPattern selectPattern(SubjectCharacter character, uint32_t seed_value) {
  auto allowed = getAllowedPatterns(character);
  return allowed[seed_value % allowed.size()];
}

std::vector<Tick> applyPatternToSpan(RhythmPattern pattern, Tick total_ticks) {
  if (total_ticks == 0) {
    return {};
  }

  auto durations = getPatternDurations(pattern);
  std::vector<Tick> result;
  Tick remaining = total_ticks;

  while (remaining > 0) {
    for (const auto& dur : durations) {
      if (remaining == 0) break;
      if (dur <= remaining) {
        result.push_back(dur);
        remaining -= dur;
      } else {
        // Truncate last note to fit exactly
        result.push_back(remaining);
        remaining = 0;
      }
    }
  }

  return result;
}

}  // namespace bach
