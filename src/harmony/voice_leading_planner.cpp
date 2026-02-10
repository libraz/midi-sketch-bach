// Implementation of voice leading planner -- fixed design-value tables.

#include "harmony/voice_leading_planner.h"

namespace bach {

// ---------------------------------------------------------------------------
// Phase-preferred intervals (Principle 4: design values, not searched)
// ---------------------------------------------------------------------------

std::vector<uint8_t> getPhasePreferredIntervals(FuguePhase phase) {
  switch (phase) {
    case FuguePhase::Establish:
      // Stable imperfect consonances: minor 3rd, major 3rd, minor 6th, major 6th.
      return {3, 4, 8, 9};

    case FuguePhase::Develop:
      // Wider variety with more tension: 3rds, perfect 4th, perfect 5th, 6ths.
      return {3, 4, 5, 7, 8, 9};

    case FuguePhase::Resolve:
      // Resolution and clarity: 3rds, perfect 5th, octave.
      return {3, 4, 7, 12};
  }

  // Fallback (should not reach here).
  return {3, 4, 7};
}

// ---------------------------------------------------------------------------
// Contrary motion target by character
// ---------------------------------------------------------------------------

float getContraryMotionTarget(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return 0.55f;   // Strict counterpoint demands high contrary motion.
    case SubjectCharacter::Playful:
      return 0.40f;   // Lighter texture allows more parallel/similar motion.
    case SubjectCharacter::Noble:
      return 0.50f;   // Balanced approach.
    case SubjectCharacter::Restless:
      return 0.45f;   // Moderate, permits chromatic parallel motion.
  }

  return 0.50f;
}

// ---------------------------------------------------------------------------
// Suspension density by character x phase
// ---------------------------------------------------------------------------

float getSuspensionDensity(SubjectCharacter character, FuguePhase phase) {
  // Fixed lookup table: [character][phase].
  // Rows: Severe, Playful, Noble, Restless.
  // Columns: Establish, Develop, Resolve.
  static constexpr float kDensityTable[4][3] = {
      {0.15f, 0.30f, 0.20f},  // Severe
      {0.10f, 0.10f, 0.10f},  // Playful
      {0.25f, 0.25f, 0.25f},  // Noble
      {0.35f, 0.35f, 0.35f},  // Restless
  };

  int char_idx = static_cast<int>(character);
  int phase_idx = static_cast<int>(phase);

  if (char_idx < 0 || char_idx > 3) char_idx = 0;
  if (phase_idx < 0 || phase_idx > 2) phase_idx = 0;

  return kDensityTable[char_idx][phase_idx];
}

// ---------------------------------------------------------------------------
// Composite planner
// ---------------------------------------------------------------------------

VoiceLeadingHints planVoiceLeading(SubjectCharacter character, FuguePhase phase,
                                   uint8_t num_voices) {
  VoiceLeadingHints hints;

  hints.preferred_intervals = getPhasePreferredIntervals(phase);
  hints.contrary_motion_target = getContraryMotionTarget(character);
  hints.suspension_density = getSuspensionDensity(character, phase);

  // Chord tone target: fixed design value.
  // Higher voice counts require slightly more chord-tone adherence to avoid clashes.
  if (num_voices >= 4) {
    hints.chord_tone_target = 0.80f;
  } else if (num_voices >= 3) {
    hints.chord_tone_target = 0.75f;
  } else {
    hints.chord_tone_target = 0.70f;
  }

  return hints;
}

}  // namespace bach
