// Implementation of pitch utility functions.

#include "core/pitch_utils.h"

#include <cstdlib>

namespace bach {

IntervalQuality classifyInterval(int semitones) {
  // Normalize to 0-11 range (reduce compound intervals)
  int normalized = std::abs(semitones) % 12;

  switch (normalized) {
    // Perfect consonances: unison, perfect 5th, octave (0 after mod 12)
    case interval::kUnison:
    case interval::kPerfect5th:
      return IntervalQuality::PerfectConsonance;

    // Imperfect consonances: minor/major 3rd, minor/major 6th
    case interval::kMinor3rd:
    case interval::kMajor3rd:
    case interval::kMinor6th:
    case interval::kMajor6th:
      return IntervalQuality::ImperfectConsonance;

    // Dissonances: 2nds, perfect 4th, tritone, 7ths
    // Note: perfect 4th treated as dissonant in two-voice counterpoint.
    // Callers handling suspensions or 6/4 chords may override.
    case interval::kMinor2nd:
    case interval::kMajor2nd:
    case interval::kPerfect4th:
    case interval::kTritone:
    case interval::kMinor7th:
    case interval::kMajor7th:
    default:
      return IntervalQuality::Dissonance;
  }
}

bool isParallelFifths(int interval1, int interval2) {
  int norm1 = std::abs(interval1) % 12;
  int norm2 = std::abs(interval2) % 12;
  return (norm1 == interval::kPerfect5th) && (norm2 == interval::kPerfect5th);
}

bool isParallelOctaves(int interval1, int interval2) {
  int norm1 = std::abs(interval1) % 12;
  int norm2 = std::abs(interval2) % 12;
  return (norm1 == interval::kUnison) && (norm2 == interval::kUnison);
}

const int* getScaleIntervals(ScaleType scale) {
  switch (scale) {
    case ScaleType::Major:         return kScaleMajor;
    case ScaleType::NaturalMinor:  return kScaleNaturalMinor;
    case ScaleType::HarmonicMinor: return kScaleHarmonicMinor;
    case ScaleType::MelodicMinor:  return kScaleMelodicMinor;
    case ScaleType::Dorian:        return kScaleDorian;
    case ScaleType::Mixolydian:    return kScaleMixolydian;
  }
  return kScaleMajor;  // Fallback
}

int degreeToPitch(int degree, int base_note, int key_offset, ScaleType scale) {
  const int* intervals = getScaleIntervals(scale);

  // Handle negative degrees and multi-octave spans
  int octave_shift = 0;
  int normalized_degree = degree;

  if (normalized_degree >= 0) {
    octave_shift = normalized_degree / 7;
    normalized_degree = normalized_degree % 7;
  } else {
    // For negative degrees: -1 means one below root, etc.
    // E.g. degree -1 -> octave_shift = -1, normalized_degree = 6
    octave_shift = (normalized_degree - 6) / 7;  // Floor division for negatives
    normalized_degree = ((normalized_degree % 7) + 7) % 7;
  }

  return base_note + key_offset + intervals[normalized_degree] + (octave_shift * 12);
}

std::string pitchToNoteName(uint8_t pitch) {
  int pitch_class = getPitchClass(pitch);
  int octave = getOctave(pitch);
  return std::string(kNoteNames[pitch_class]) + std::to_string(octave);
}

uint8_t transposePitch(uint8_t pitch, Key key) {
  int offset = static_cast<int>(key);
  int transposed = static_cast<int>(pitch) + offset;

  // Clamp to valid MIDI range
  if (transposed < 0) transposed = 0;
  if (transposed > 127) transposed = 127;

  return static_cast<uint8_t>(transposed);
}

}  // namespace bach
