// Implementation of chord type utilities -- diatonic quality lookup and string conversion.

#include "harmony/chord_types.h"

#include "core/pitch_utils.h"
#include "harmony/harmonic_event.h"

namespace bach {

ChordQuality majorKeyQuality(ChordDegree degree) {
  // Major key diatonic triads: I=Maj, ii=min, iii=min, IV=Maj, V=Maj, vi=min, vii=dim
  switch (degree) {
    case ChordDegree::I:       return ChordQuality::Major;
    case ChordDegree::ii:      return ChordQuality::Minor;
    case ChordDegree::iii:     return ChordQuality::Minor;
    case ChordDegree::IV:      return ChordQuality::Major;
    case ChordDegree::V:       return ChordQuality::Major;
    case ChordDegree::vi:      return ChordQuality::Minor;
    case ChordDegree::viiDim:  return ChordQuality::Diminished;
    case ChordDegree::bII:     return ChordQuality::Major;
    case ChordDegree::V_of_V:  return ChordQuality::Dominant7;
    case ChordDegree::V_of_vi: return ChordQuality::Dominant7;
    case ChordDegree::V_of_IV: return ChordQuality::Dominant7;
    case ChordDegree::V_of_ii: return ChordQuality::Dominant7;
  }
  return ChordQuality::Major;
}

ChordQuality minorKeyQuality(ChordDegree degree) {
  // Minor key triads using harmonic minor practice (Bach standard):
  // i=min, ii=dim, III=Maj, iv=min, V=Maj (raised 7th), VI=Maj, vii=dim (raised 7th)
  switch (degree) {
    case ChordDegree::I:       return ChordQuality::Minor;
    case ChordDegree::ii:      return ChordQuality::Diminished;
    case ChordDegree::iii:     return ChordQuality::Major;
    case ChordDegree::IV:      return ChordQuality::Minor;
    case ChordDegree::V:       return ChordQuality::Major;
    case ChordDegree::vi:      return ChordQuality::Major;
    case ChordDegree::viiDim:  return ChordQuality::Diminished;
    case ChordDegree::bII:     return ChordQuality::Major;
    case ChordDegree::V_of_V:  return ChordQuality::Dominant7;
    case ChordDegree::V_of_vi: return ChordQuality::Dominant7;
    case ChordDegree::V_of_IV: return ChordQuality::Dominant7;
    case ChordDegree::V_of_ii: return ChordQuality::Dominant7;
  }
  return ChordQuality::Minor;
}

uint8_t degreeSemitones(ChordDegree degree) {
  // Major scale degree offsets: C D E F G A B = 0 2 4 5 7 9 11
  switch (degree) {
    case ChordDegree::I:       return 0;
    case ChordDegree::ii:      return 2;
    case ChordDegree::iii:     return 4;
    case ChordDegree::IV:      return 5;
    case ChordDegree::V:       return 7;
    case ChordDegree::vi:      return 9;
    case ChordDegree::viiDim:  return 11;
    case ChordDegree::bII:     return 1;   // Lowered 2nd = 1 semitone
    case ChordDegree::V_of_V:  return 2;   // D in C major (V/V root)
    case ChordDegree::V_of_vi: return 4;   // E in C major (V/vi root)
    case ChordDegree::V_of_IV: return 0;   // C in C major (V/IV root)
    case ChordDegree::V_of_ii: return 9;   // A in C major (V/ii root)
  }
  return 0;
}

uint8_t degreeMinorSemitones(ChordDegree degree) {
  // Natural minor scale degree offsets: 0 2 3 5 7 8 10
  switch (degree) {
    case ChordDegree::I:       return 0;
    case ChordDegree::ii:      return 2;
    case ChordDegree::iii:     return 3;
    case ChordDegree::IV:      return 5;
    case ChordDegree::V:       return 7;
    case ChordDegree::vi:      return 8;
    case ChordDegree::viiDim:  return 10;
    case ChordDegree::bII:     return 1;
    case ChordDegree::V_of_V:  return 2;
    case ChordDegree::V_of_vi: return 3;
    case ChordDegree::V_of_IV: return 0;
    case ChordDegree::V_of_ii: return 8;
  }
  return 0;
}

const char* chordQualityToString(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:            return "Major";
    case ChordQuality::Minor:            return "Minor";
    case ChordQuality::Diminished:       return "Diminished";
    case ChordQuality::Augmented:        return "Augmented";
    case ChordQuality::Dominant7:        return "Dominant7";
    case ChordQuality::Minor7:           return "Minor7";
    case ChordQuality::MajorMajor7:      return "MajorMajor7";
    case ChordQuality::Diminished7:      return "Diminished7";
    case ChordQuality::HalfDiminished7:  return "HalfDiminished7";
    case ChordQuality::AugmentedSixth:   return "AugmentedSixth";
  }
  return "Unknown";
}

const char* chordDegreeToString(ChordDegree degree) {
  switch (degree) {
    case ChordDegree::I:       return "I";
    case ChordDegree::ii:      return "ii";
    case ChordDegree::iii:     return "iii";
    case ChordDegree::IV:      return "IV";
    case ChordDegree::V:       return "V";
    case ChordDegree::vi:      return "vi";
    case ChordDegree::viiDim:  return "viiDim";
    case ChordDegree::bII:     return "bII";
    case ChordDegree::V_of_V:  return "V/V";
    case ChordDegree::V_of_vi: return "V/vi";
    case ChordDegree::V_of_IV: return "V/IV";
    case ChordDegree::V_of_ii: return "V/ii";
  }
  return "?";
}

bool isChordTone(uint8_t pitch, const HarmonicEvent& event) {
  int pitch_class = getPitchClass(pitch);
  int root_pc = getPitchClass(event.chord.root_pitch);

  if (pitch_class == root_pc) return true;

  int third_interval = 0;
  int fifth_interval = 0;

  switch (event.chord.quality) {
    case ChordQuality::Major:
    case ChordQuality::Dominant7:
    case ChordQuality::MajorMajor7:
      third_interval = 4;
      fifth_interval = 7;
      break;
    case ChordQuality::Minor:
    case ChordQuality::Minor7:
      third_interval = 3;
      fifth_interval = 7;
      break;
    case ChordQuality::Diminished:
      third_interval = 3;
      fifth_interval = 6;
      break;
    case ChordQuality::Augmented:
      third_interval = 4;
      fifth_interval = 8;
      break;
    case ChordQuality::Diminished7:
      third_interval = 3;
      fifth_interval = 6;
      break;
    case ChordQuality::HalfDiminished7:
      third_interval = 3;
      fifth_interval = 6;
      break;
    case ChordQuality::AugmentedSixth:
      third_interval = 4;
      fifth_interval = 8;
      break;
  }

  int third_pc = (root_pc + third_interval) % 12;
  int fifth_pc = (root_pc + fifth_interval) % 12;

  if (pitch_class == third_pc || pitch_class == fifth_pc) return true;

  // Check 7th for seventh chord qualities.
  int seventh_interval = -1;
  switch (event.chord.quality) {
    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
    case ChordQuality::HalfDiminished7:
      seventh_interval = 10;  // Minor 7th
      break;
    case ChordQuality::MajorMajor7:
      seventh_interval = 11;  // Major 7th
      break;
    case ChordQuality::Diminished7:
      seventh_interval = 9;   // Diminished 7th
      break;
    default:
      break;
  }

  if (seventh_interval >= 0) {
    int seventh_pc = (root_pc + seventh_interval) % 12;
    if (pitch_class == seventh_pc) return true;
  }

  return false;
}

}  // namespace bach
