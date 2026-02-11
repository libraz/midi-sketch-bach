// Implementation of motif templates and goal tone design values.

#include "fugue/motif_template.h"

#include <algorithm>

#include "core/rng_util.h"

namespace bach {

GoalTone goalToneForCharacter(SubjectCharacter character, std::mt19937& rng) {
  GoalTone goal;
  switch (character) {
    case SubjectCharacter::Severe:
      goal = {0.65f, 0.85f};   // Late-ish, moderately high
      break;
    case SubjectCharacter::Playful:
      goal = {0.50f, 0.90f};   // Center, high peak
      break;
    case SubjectCharacter::Noble:
      goal = {0.70f, 0.80f};   // Late, gentle peak
      break;
    case SubjectCharacter::Restless:
      goal = {0.60f, 0.95f};   // Slightly early, sharp peak
      break;
    default:
      goal = {0.65f, 0.85f};
      break;
  }

  // Apply small per-seed variation (Principle 3: subtle variety, not more parameters).
  goal.position_ratio = std::clamp(
      goal.position_ratio + rng::rollFloat(rng, -0.05f, 0.05f), 0.1f, 0.9f);
  goal.pitch_ratio = std::clamp(
      goal.pitch_ratio + rng::rollFloat(rng, -0.03f, 0.03f), 0.5f, 1.0f);

  return goal;
}

std::pair<MotifTemplate, MotifTemplate> motifTemplatesForCharacter(
    SubjectCharacter character, uint32_t template_idx) {
  uint32_t idx = template_idx % 4;
  switch (character) {
    case SubjectCharacter::Severe: {
      switch (idx) {
        case 0: {
          // Pair 0: Scale ascending + Leap resolving downward.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, 1, 2, 3};
          mot_a.durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -3, -2, -1, 0};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Scale descending + Leap ascending.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, -1, -2, -1};
          mot_a.durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, 3, 2, 1, 0};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: Arch (up-down) + Pedal point.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, 1, 2, 1, 0};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat,
                             kTicksPerBeat / 2, kTicksPerBeat / 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Sustain;
          mot_b.degree_offsets = {0, 0, -1, 0};
          mot_b.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
        case 3:
        default: {
          // Pair 3: 5th leap + stepwise fill.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 2, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
      }
    }
    case SubjectCharacter::Playful: {
      switch (idx) {
        case 0: {
          // Pair 0: Leap up + Rhythmic pattern.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 3, 2, 4};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat,
                             kTicksPerBeat / 2, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Rhythmic;
          mot_b.degree_offsets = {0, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Dotted rhythm + Running scale.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Rhythmic;
          mot_a.degree_offsets = {0, 1, 0, 2};
          mot_a.durations = {kTicksPerBeat * 3 / 4, kTicksPerBeat / 4,
                             kTicksPerBeat * 3 / 4, kTicksPerBeat / 4};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: Skip 3rds + Syncopation.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 2, 1, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Rhythmic;
          mot_b.degree_offsets = {0, -1, 0, -2, -1};
          mot_b.durations = {kTicksPerBeat / 4, kTicksPerBeat * 3 / 4,
                             kTicksPerBeat / 4, kTicksPerBeat * 3 / 4, kTicksPerBeat / 2};
          return {mot_a, mot_b};
        }
        case 3:
        default: {
          // Pair 3: Arpeggio + Neighbor tone.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 2, 4, 2};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, 1, 0, -1, 0};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat};
          return {mot_a, mot_b};
        }
      }
    }
    case SubjectCharacter::Noble: {
      switch (idx) {
        case 0: {
          // Pair 0: Sustain + descending Scale.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Sustain;
          mot_a.degree_offsets = {0, 1, 2};
          mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -4};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Dotted half + Arch descent.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Sustain;
          mot_a.degree_offsets = {0, 0, 1};
          mot_a.durations = {kTicksPerBeat * 3, kTicksPerBeat, kTicksPerBeat * 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, 1, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: 4th leap + Long resolution.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 3, 2};
          mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat * 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -1};
          mot_b.durations = {kTicksPerBeat * 2, kTicksPerBeat,
                             kTicksPerBeat, kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
        case 3:
        default: {
          // Pair 3: Pedal sustain + Rising 3rds.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Sustain;
          mot_a.degree_offsets = {0, 0, 0, 1};
          mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -2, -1, -3, -2};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          return {mot_a, mot_b};
        }
      }
    }
    case SubjectCharacter::Restless: {
      switch (idx) {
        case 0: {
          // Pair 0: Chromatic crawl + Leap escape.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Chromatic;
          mot_a.degree_offsets = {0, 1, 0, 1, 2};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat / 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -4, -3, -2, -1};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Tritone + Chromatic recovery.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 3, 4, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat / 2};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Chromatic;
          mot_b.degree_offsets = {0, -1, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: 16th note run + Augmented 2nd.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, 1, 2, 3, 2, 1};
          mot_a.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 4, kTicksPerBeat / 4};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Chromatic;
          mot_b.degree_offsets = {0, -1, -3, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat,
                             kTicksPerBeat / 2, kTicksPerBeat};
          return {mot_a, mot_b};
        }
        case 3:
        default: {
          // Pair 3: Fragment + Rapid descending scale.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Rhythmic;
          mot_a.degree_offsets = {0, 1, 0, 2, 1};
          mot_a.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat / 4};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -4};
          mot_b.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
          return {mot_a, mot_b};
        }
      }
    }
  }
  // Fallback: Severe pair 0.
  return motifTemplatesForCharacter(SubjectCharacter::Severe, 0);
}

}  // namespace bach
