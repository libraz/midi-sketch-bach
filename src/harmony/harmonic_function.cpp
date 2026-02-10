// Implementation of harmonic function classification and secondary dominants.

#include "harmony/harmonic_function.h"

#include "core/pitch_utils.h"

namespace bach {

HarmonicFunction classifyFunction(ChordDegree degree, bool /*is_minor*/) {
  switch (degree) {
    case ChordDegree::I:
      return HarmonicFunction::Tonic;
    case ChordDegree::ii:
      return HarmonicFunction::Subdominant;
    case ChordDegree::iii:
      return HarmonicFunction::Mediant;
    case ChordDegree::IV:
      return HarmonicFunction::Subdominant;
    case ChordDegree::V:
      return HarmonicFunction::Dominant;
    case ChordDegree::vi:
      return HarmonicFunction::Tonic;
    case ChordDegree::viiDim:
      return HarmonicFunction::Dominant;
    case ChordDegree::bII:
      return HarmonicFunction::Subdominant;
    case ChordDegree::V_of_V:
    case ChordDegree::V_of_vi:
    case ChordDegree::V_of_IV:
    case ChordDegree::V_of_ii:
    case ChordDegree::V_of_iii:
      return HarmonicFunction::Applied;
    case ChordDegree::bVI:
    case ChordDegree::bVII:
    case ChordDegree::bIII:
      return HarmonicFunction::Mediant;
  }
  return HarmonicFunction::Dominant;
}

const char* harmonicFunctionToString(HarmonicFunction func) {
  switch (func) {
    case HarmonicFunction::Tonic:       return "Tonic";
    case HarmonicFunction::Subdominant: return "Subdominant";
    case HarmonicFunction::Dominant:    return "Dominant";
    case HarmonicFunction::Mediant:     return "Mediant";
    case HarmonicFunction::Applied:    return "Applied";
  }
  return "Unknown";
}

Chord createSecondaryDominant(ChordDegree target, const KeySignature& key_sig) {
  // The secondary dominant is built a perfect 5th below the target's root.
  // Its quality is always Dominant7.
  uint8_t target_semitones = key_sig.is_minor ? degreeMinorSemitones(target)
                                              : degreeSemitones(target);

  // Root of secondary dominant = target root - perfect 5th (7 semitones)
  // = target root + perfect 4th (5 semitones) mod 12
  int tonic_midi = static_cast<int>(key_sig.tonic);
  int target_root_pc = (tonic_midi + target_semitones) % 12;
  int sec_dom_root_pc = (target_root_pc + 7) % 12;  // P5 above target = V/target

  Chord chord;
  chord.degree = ChordDegree::V;  // Functionally a V chord
  chord.quality = ChordQuality::Dominant7;
  // Place in octave 4
  chord.root_pitch = static_cast<uint8_t>(60 + sec_dom_root_pc);
  if (chord.root_pitch < 60) chord.root_pitch += 12;
  chord.inversion = 0;

  return chord;
}

Chord createNeapolitanSixth(const KeySignature& key_sig) {
  // bII = major triad on lowered 2nd degree, in first inversion.
  int tonic_midi = static_cast<int>(key_sig.tonic);
  int flat_two_pc = (tonic_midi + 1) % 12;  // semitone above tonic

  Chord chord;
  chord.degree = ChordDegree::ii;  // Closest standard degree
  chord.quality = ChordQuality::Major;
  chord.root_pitch = static_cast<uint8_t>(60 + flat_two_pc);
  if (chord.root_pitch < 60) chord.root_pitch += 12;
  chord.inversion = 1;  // First inversion (6 position)

  return chord;
}

bool isValidFunctionalProgression(HarmonicFunction from, HarmonicFunction to) {
  // Standard functional progressions:
  //   T -> S (I -> IV, I -> ii)
  //   T -> D (I -> V)
  //   S -> D (IV -> V, ii -> V)
  //   S -> T (plagal: IV -> I)
  //   D -> T (cadential: V -> I)
  //   M -> anywhere (iii is ambiguous, always valid)
  //   anything -> M (color chords always valid as targets)
  //   Applied -> anywhere (secondary dominants resolve freely)
  //   anything -> Applied (tonicization is always valid)
  if (from == HarmonicFunction::Mediant || to == HarmonicFunction::Mediant) {
    return true;
  }
  if (from == HarmonicFunction::Applied || to == HarmonicFunction::Applied) {
    return true;
  }

  // D -> S is the only retrogression considered non-standard.
  if (from == HarmonicFunction::Dominant && to == HarmonicFunction::Subdominant) {
    return false;
  }

  return true;
}

bool isValidDegreeProgression(ChordDegree from, ChordDegree to, bool is_minor) {
  HarmonicFunction func_from = classifyFunction(from, is_minor);
  HarmonicFunction func_to = classifyFunction(to, is_minor);
  return isValidFunctionalProgression(func_from, func_to);
}

ChordDegree getSecondaryDominantTarget(ChordDegree degree) {
  switch (degree) {
    case ChordDegree::V_of_V:   return ChordDegree::V;
    case ChordDegree::V_of_vi:  return ChordDegree::vi;
    case ChordDegree::V_of_IV:  return ChordDegree::IV;
    case ChordDegree::V_of_ii:  return ChordDegree::ii;
    case ChordDegree::V_of_iii: return ChordDegree::iii;
    default:                    return ChordDegree::I;
  }
}

bool isSecondaryDominant(ChordDegree degree) {
  switch (degree) {
    case ChordDegree::V_of_V:
    case ChordDegree::V_of_vi:
    case ChordDegree::V_of_IV:
    case ChordDegree::V_of_ii:
    case ChordDegree::V_of_iii:
      return true;
    default:
      return false;
  }
}

}  // namespace bach
