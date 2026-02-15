// Implementation of motif templates and goal tone design values.

#include "fugue/motif_template.h"

#include <algorithm>

#include "core/rng_util.h"
#include "fugue/archetype_policy.h"
#include "fugue/subject_params.h"

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
  uint32_t idx = template_idx % 5;
  switch (character) {
    case SubjectCharacter::Severe: {
      switch (idx) {
        case 0: {
          // Pair 0: Scale ascending + Leap resolving downward.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, 1, 2, 3};
          mot_a.durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -3, -2, -1, 0};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Scale descending + Leap ascending.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, -1, -2, -1};
          mot_a.durations = {kTicksPerBeat, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::PassingTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, 3, 2, 1, 0};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: Arch (up-down) + Pedal point.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Scale;
          mot_a.degree_offsets = {0, 1, 2, 1, 0};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat,
                             kTicksPerBeat / 2, kTicksPerBeat / 2};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::ClimaxTone, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Sustain;
          mot_b.degree_offsets = {0, 0, -1, 0};
          mot_b.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::StructuralTone,
                             NoteFunction::NeighborTone, NoteFunction::StructuralTone};
          return {mot_a, mot_b};
        }
        case 3: {
          // Pair 3: 5th leap + stepwise fill.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 2, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 4:
        default: {
          // Pair 4 (NEW): P5 leap head + stepwise descent. BWV578-style.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 2, 1};
          mot_a.durations = {kDottedQuarter, kEighthNote, kEighthNote,
                             kEighthNote, kQuarterNote};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
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
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Rhythmic;
          mot_b.degree_offsets = {0, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::NeighborTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Dotted rhythm + Running scale.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Rhythmic;
          mot_a.degree_offsets = {0, 1, 0, 2};
          mot_a.durations = {kTicksPerBeat * 3 / 4, kTicksPerBeat / 4,
                             kTicksPerBeat * 3 / 4, kTicksPerBeat / 4};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::NeighborTone,
                             NoteFunction::StructuralTone, NoteFunction::LeapTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: Skip 3rds + Syncopation.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 2, 1, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::PassingTone, NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Rhythmic;
          mot_b.degree_offsets = {0, -1, 0, -2, -1};
          mot_b.durations = {kTicksPerBeat / 4, kTicksPerBeat * 3 / 4,
                             kTicksPerBeat / 4, kTicksPerBeat * 3 / 4, kTicksPerBeat / 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::NeighborTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 3: {
          // Pair 3: Arpeggio + Neighbor tone.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 2, 4, 2};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::LeapTone, NoteFunction::Resolution};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, 1, 0, -1, 0};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::NeighborTone,
                             NoteFunction::StructuralTone, NoteFunction::NeighborTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 4:
        default: {
          // Pair 4 (NEW): P5 leap head + ornamental turn. Lively rhythm.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 4, 3};
          mot_a.durations = {kEighthNote, kEighthNote, kEighthNote,
                             kEighthNote, kEighthNote};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::NeighborTone, NoteFunction::NeighborTone,
                             NoteFunction::Resolution};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -1};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
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
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -4};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Dotted half + Arch descent.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Sustain;
          mot_a.degree_offsets = {0, 0, 1};
          mot_a.durations = {kTicksPerBeat * 3, kTicksPerBeat, kTicksPerBeat * 2};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::StructuralTone,
                             NoteFunction::PassingTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, 1, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::NeighborTone,
                             NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 2: {
          // Pair 2: 4th leap + Long resolution.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 3, 2};
          mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat * 2};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -1};
          mot_b.durations = {kTicksPerBeat * 2, kTicksPerBeat,
                             kTicksPerBeat, kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 3: {
          // Pair 3: Pedal sustain + Rising 3rds.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Sustain;
          mot_a.degree_offsets = {0, 0, 0, 1};
          mot_a.durations = {kTicksPerBeat * 2, kTicksPerBeat, kTicksPerBeat, kTicksPerBeat};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::StructuralTone,
                             NoteFunction::StructuralTone, NoteFunction::PassingTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -2, -1, -3, -2};
          mot_b.durations = {kTicksPerBeat, kTicksPerBeat / 2,
                             kTicksPerBeat, kTicksPerBeat / 2, kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::LeapTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 4:
        default: {
          // Pair 4 (NEW): P5 leap head + sustained descent. Stately.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 2};
          mot_a.durations = {kHalfNote, kQuarterNote, kQuarterNote, kHalfNote};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3};
          mot_b.durations = {kTicksPerBeat * 2, kTicksPerBeat,
                             kTicksPerBeat, kTicksPerBeat * 2};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::CadentialTone};
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
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::NeighborTone, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Leap;
          mot_b.degree_offsets = {0, -4, -3, -2, -1};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 1: {
          // Pair 1: Tritone + Chromatic recovery.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 3, 4, 3};
          mot_a.durations = {kTicksPerBeat / 2, kTicksPerBeat / 2,
                             kTicksPerBeat / 2, kTicksPerBeat / 2};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::LeapTone, NoteFunction::Resolution};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Chromatic;
          mot_b.degree_offsets = {0, -1, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::NeighborTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
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
          mot_a.functions = {NoteFunction::SequenceHead, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::ClimaxTone,
                             NoteFunction::PassingTone, NoteFunction::PassingTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Chromatic;
          mot_b.degree_offsets = {0, -1, -3, -2};
          mot_b.durations = {kTicksPerBeat / 2, kTicksPerBeat,
                             kTicksPerBeat / 2, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::LeapTone, NoteFunction::Resolution};
          return {mot_a, mot_b};
        }
        case 3: {
          // Pair 3: Fragment + Rapid descending scale.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Rhythmic;
          mot_a.degree_offsets = {0, 1, 0, 2, 1};
          mot_a.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 2, kTicksPerBeat / 4, kTicksPerBeat / 4};
          mot_a.functions = {NoteFunction::SequenceHead, NoteFunction::PassingTone,
                             NoteFunction::NeighborTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Scale;
          mot_b.degree_offsets = {0, -1, -2, -3, -4};
          mot_b.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
          mot_b.functions = {NoteFunction::SequenceHead, NoteFunction::PassingTone,
                             NoteFunction::PassingTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
        case 4:
        default: {
          // Pair 4 (NEW): P5 leap head + chromatic descent. Short values.
          MotifTemplate mot_a;
          mot_a.type = MotifType::Leap;
          mot_a.degree_offsets = {0, 4, 3, 2, 1};
          mot_a.durations = {kEighthNote, kSixteenthNote, kSixteenthNote,
                             kSixteenthNote, kEighthNote};
          mot_a.functions = {NoteFunction::StructuralTone, NoteFunction::LeapTone,
                             NoteFunction::Resolution, NoteFunction::PassingTone,
                             NoteFunction::StructuralTone};
          MotifTemplate mot_b;
          mot_b.type = MotifType::Chromatic;
          mot_b.degree_offsets = {0, -1, 0, -1, -2};
          mot_b.durations = {kTicksPerBeat / 4, kTicksPerBeat / 4,
                             kTicksPerBeat / 4, kTicksPerBeat / 4, kTicksPerBeat};
          mot_b.functions = {NoteFunction::StructuralTone, NoteFunction::PassingTone,
                             NoteFunction::NeighborTone, NoteFunction::PassingTone,
                             NoteFunction::CadentialTone};
          return {mot_a, mot_b};
        }
      }
    }
  }
  // Fallback: Severe pair 0.
  return motifTemplatesForCharacter(SubjectCharacter::Severe, 0);
}

GoalTone goalToneForCharacter(SubjectCharacter character, std::mt19937& rng,
                               const ArchetypePolicy& policy) {
  GoalTone goal = goalToneForCharacter(character, rng);
  goal.position_ratio = std::clamp(goal.position_ratio,
                                    policy.min_climax_position,
                                    policy.max_climax_position);
  goal.pitch_ratio = std::clamp(goal.pitch_ratio,
                                 policy.min_climax_pitch,
                                 policy.max_climax_pitch);
  return goal;
}

// ---------------------------------------------------------------------------
// Kerngestalt core cells (16 cells: 4 types x 4 variants)
// ---------------------------------------------------------------------------

const KerngestaltCell& getCoreCell(KerngestaltType type, int index) {
  using RT = RhythmToken;
  using K = RT::Kind;

  // 16 core cells indexed by type * 4 + (index % 4).
  // Notation: S=Short, M=Medium, L=Long, DL=Dotted-long, DS=Dotted-short.
  static const KerngestaltCell kCells[16] = {
      // --- IntervalDriven (A0-A3) ---
      // A0: 5th leap, quarter-eighth
      {KerngestaltType::IntervalDriven,
       {+7},
       {{K::M, 1}, {K::S, 1}},
       true},
      // A1: 4th up + step down
      {KerngestaltType::IntervalDriven,
       {+5, -2},
       {{K::M, 1}, {K::S, 1}, {K::M, 1}},
       true},
      // A2: 4th down + step up, dotted
      {KerngestaltType::IntervalDriven,
       {-5, +2},
       {{K::DL, 1}, {K::DS, 1}, {K::M, 1}},
       true},
      // A3: major 3rd + minor 3rd
      {KerngestaltType::IntervalDriven,
       {+4, +3},
       {{K::S, 1}, {K::S, 1}, {K::L, 2}},
       true},

      // --- ChromaticCell (B0-B3) ---
      // B0: ascending chromatic + step back
      {KerngestaltType::ChromaticCell,
       {+1, +1, -1},
       {{K::S, 1}, {K::S, 1}, {K::S, 1}, {K::M, 1}},
       false},
      // B1: descending chromatic + step up
      {KerngestaltType::ChromaticCell,
       {-1, -1, +1},
       {{K::S, 1}, {K::S, 1}, {K::S, 1}, {K::M, 1}},
       false},
      // B2: chromatic neighbor + whole step
      {KerngestaltType::ChromaticCell,
       {+1, -1, +2},
       {{K::S, 1}, {K::S, 1}, {K::DL, 1}, {K::DS, 1}},
       true},
      // B3: extended descending chromatic
      {KerngestaltType::ChromaticCell,
       {-1, +1, -1, -1},
       {{K::S, 1}, {K::S, 1}, {K::S, 1}, {K::S, 1}, {K::M, 1}},
       false},

      // --- Arpeggio (C0-C3) ---
      // C0: major triad (root position)
      {KerngestaltType::Arpeggio,
       {+4, +3},
       {{K::DL, 1}, {K::DS, 1}, {K::M, 1}},
       true},
      // C1: minor triad (root position)
      {KerngestaltType::Arpeggio,
       {+3, +4},
       {{K::DL, 1}, {K::DS, 1}, {K::M, 1}},
       true},
      // C2: diminished triad + step
      {KerngestaltType::Arpeggio,
       {+3, +3, +1},
       {{K::S, 1}, {K::S, 1}, {K::S, 1}, {K::L, 2}},
       true},
      // C3: broken chord (up-down-up)
      {KerngestaltType::Arpeggio,
       {+4, -3, +4},
       {{K::M, 1}, {K::S, 1}, {K::S, 1}, {K::M, 1}},
       true},

      // --- Linear (D0-D3) ---
      // D0: whole-tone ascending
      {KerngestaltType::Linear,
       {+2, +2, +2},
       {{K::M, 1}, {K::M, 1}, {K::M, 1}, {K::M, 1}},
       true},
      // D1: mixed step ascending
      {KerngestaltType::Linear,
       {+2, +1, +2},
       {{K::M, 1}, {K::M, 1}, {K::M, 1}, {K::M, 1}},
       true},
      // D2: stepwise ascending (4 intervals)
      {KerngestaltType::Linear,
       {+1, +2, +1, +2},
       {{K::S, 1}, {K::S, 1}, {K::S, 1}, {K::S, 1}, {K::M, 1}},
       true},
      // D3: descending steps, dotted pairs
      {KerngestaltType::Linear,
       {-2, -2, -1},
       {{K::DL, 1}, {K::DS, 1}, {K::DL, 1}, {K::DS, 1}},
       true},
  };

  int type_idx = static_cast<int>(type);
  return kCells[type_idx * 4 + (index % 4)];
}

// ---------------------------------------------------------------------------
// Kerngestalt type selection (character x archetype -> primary/secondary)
// ---------------------------------------------------------------------------

KerngestaltType selectKerngestaltType(SubjectCharacter character,
                                      FugueArchetype archetype,
                                      std::mt19937& rng) {
  using KT = KerngestaltType;

  struct TypePair {
    KT primary;
    KT secondary;
  };

  // Mapping table: [character][archetype] = {primary (80%), secondary (20%)}.
  //                  Compact            Cantabile          Invertible         Chromatic
  static const TypePair kMapping[4][4] = {
      // Severe
      {{KT::Linear, KT::IntervalDriven},
       {KT::IntervalDriven, KT::Linear},
       {KT::Linear, KT::IntervalDriven},
       {KT::ChromaticCell, KT::Linear}},
      // Playful
      {{KT::IntervalDriven, KT::Arpeggio},
       {KT::Arpeggio, KT::IntervalDriven},
       {KT::IntervalDriven, KT::Arpeggio},
       {KT::ChromaticCell, KT::IntervalDriven}},
      // Noble
      {{KT::Linear, KT::IntervalDriven},
       {KT::IntervalDriven, KT::Linear},
       {KT::Linear, KT::IntervalDriven},
       {KT::ChromaticCell, KT::Linear}},
      // Restless
      {{KT::ChromaticCell, KT::IntervalDriven},
       {KT::IntervalDriven, KT::ChromaticCell},
       {KT::ChromaticCell, KT::IntervalDriven},
       {KT::ChromaticCell, KT::Linear}},
  };

  int char_idx = static_cast<int>(character);
  int arch_idx = static_cast<int>(archetype);
  const auto& pair = kMapping[char_idx][arch_idx];

  return rng::rollProbability(rng, 0.80f) ? pair.primary : pair.secondary;
}

}  // namespace bach
