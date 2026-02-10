// Implementation of chord type utilities -- diatonic quality lookup and string conversion.

#include "harmony/chord_types.h"

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
  }
  return ChordQuality::Major;
}

ChordQuality minorKeyQuality(ChordDegree degree) {
  // Natural minor diatonic triads: i=min, ii=dim, III=Maj, iv=min, v=min, VI=Maj, VII=Maj
  switch (degree) {
    case ChordDegree::I:       return ChordQuality::Minor;
    case ChordDegree::ii:      return ChordQuality::Diminished;
    case ChordDegree::iii:     return ChordQuality::Major;
    case ChordDegree::IV:      return ChordQuality::Minor;
    case ChordDegree::V:       return ChordQuality::Minor;
    case ChordDegree::vi:      return ChordQuality::Major;
    case ChordDegree::viiDim:  return ChordQuality::Major;
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
  }
  return 0;
}

const char* chordQualityToString(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:       return "Major";
    case ChordQuality::Minor:       return "Minor";
    case ChordQuality::Diminished:  return "Diminished";
    case ChordQuality::Augmented:   return "Augmented";
    case ChordQuality::Dominant7:   return "Dominant7";
    case ChordQuality::Minor7:      return "Minor7";
    case ChordQuality::MajorMajor7: return "MajorMajor7";
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
  }
  return "?";
}

}  // namespace bach
