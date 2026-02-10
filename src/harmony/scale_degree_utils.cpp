// Shared scale degree utilities for pitch calculation.

#include "harmony/scale_degree_utils.h"

namespace bach {

const int kMajorScaleIntervals[7] = {0, 2, 4, 5, 7, 9, 11};
const int kMinorScaleIntervals[7] = {0, 2, 3, 5, 7, 8, 10};

const int* scaleIntervalsForMode(bool is_minor) {
  return is_minor ? kMinorScaleIntervals : kMajorScaleIntervals;
}

int degreeToPitchOffset(int degree, bool is_minor) {
  const int* intervals = scaleIntervalsForMode(is_minor);
  if (degree < 0) {
    int octave_down = (-degree + 6) / 7;
    int wrapped = degree + octave_down * 7;
    return intervals[wrapped % 7] - octave_down * 12;
  }
  int octave = degree / 7;
  int scale_idx = degree % 7;
  return intervals[scale_idx] + octave * 12;
}

std::vector<int> getChordDegrees(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
    case ChordQuality::Minor:
    case ChordQuality::Diminished:
    case ChordQuality::Augmented:
      return {0, 2, 4};

    case ChordQuality::Dominant7:
    case ChordQuality::Minor7:
    case ChordQuality::MajorMajor7:
    case ChordQuality::Diminished7:
    case ChordQuality::HalfDiminished7:
      return {0, 2, 4, 6};

    case ChordQuality::AugmentedSixth:
    case ChordQuality::AugSixthItalian:
    case ChordQuality::AugSixthFrench:
    case ChordQuality::AugSixthGerman:
      return {0, 2, 4};
  }
  return {0, 2, 4};
}

}  // namespace bach
