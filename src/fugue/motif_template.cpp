// Implementation of motif templates and goal tone design values.

#include "fugue/motif_template.h"

namespace bach {

GoalTone goalToneForCharacter(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return {0.65f, 0.85f};   // Late-ish, moderately high
    case SubjectCharacter::Playful:
      return {0.50f, 0.90f};   // Center, high peak
    case SubjectCharacter::Noble:
      return {0.70f, 0.80f};   // Late, gentle peak
    case SubjectCharacter::Restless:
      return {0.60f, 0.95f};   // Slightly early, sharp peak
  }
  return {0.65f, 0.85f};
}

std::pair<MotifTemplate, MotifTemplate> motifTemplatesForCharacter(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe: {
      // Severe: Scale ascending + Leap resolving downward.
      // "Ascending scale fragment -> leap + stepwise descent"
      MotifTemplate mot_a;
      mot_a.type = MotifType::Scale;
      mot_a.degree_offsets = {0, 1, 2, 3};        // 4-note ascending scale
      mot_a.durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};

      MotifTemplate mot_b;
      mot_b.type = MotifType::Leap;
      mot_b.degree_offsets = {0, -3, -2, -1, 0};  // Leap down a 4th, step back up
      mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                         kTicksPerBeat / 2, kTicksPerBeat};
      return {mot_a, mot_b};
    }
    case SubjectCharacter::Playful: {
      // Playful: Leap upward + Rhythmic pattern.
      // "Leap up -> rhythmic play with repeated pitches"
      MotifTemplate mot_a;
      mot_a.type = MotifType::Leap;
      mot_a.degree_offsets = {0, 3, 2, 4};       // Leap up a 4th, step back, leap higher
      mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat,
                         kTicksPerBeat / 2, kTicksPerBeat};

      MotifTemplate mot_b;
      mot_b.type = MotifType::Rhythmic;
      mot_b.degree_offsets = {0, 0, -1, -2};      // Repeated note then descending
      mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                         kTicksPerBeat / 2, kTicksPerBeat};
      return {mot_a, mot_b};
    }
    case SubjectCharacter::Noble: {
      // Noble: Sustain + descending Scale.
      // "Long opening tone -> stately descending scale"
      MotifTemplate mot_a;
      mot_a.type = MotifType::Sustain;
      mot_a.degree_offsets = {0, 1, 2};           // Sustained opening + gentle rise
      mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat};

      MotifTemplate mot_b;
      mot_b.type = MotifType::Scale;
      mot_b.degree_offsets = {0, -1, -2, -3, -4}; // Descending scale (5 notes)
      mot_b.durations = {kTicksPerBeat, kTicksPerBeat,
                         kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
      return {mot_a, mot_b};
    }
    case SubjectCharacter::Restless: {
      // Restless: Chromatic crawl + Leap escape.
      // "Chromatic creep upward -> leap and jittery descent"
      MotifTemplate mot_a;
      mot_a.type = MotifType::Chromatic;
      mot_a.degree_offsets = {0, 0, 1, 1, 2};     // Half-step pairs (chromatic)
      mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                         kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat / 2};

      MotifTemplate mot_b;
      mot_b.type = MotifType::Leap;
      mot_b.degree_offsets = {0, -4, -3, -2, -1}; // Wide leap down, step back up
      mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                         kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
      return {mot_a, mot_b};
    }
  }
  // Fallback: Severe
  return motifTemplatesForCharacter(SubjectCharacter::Severe);
}

}  // namespace bach
