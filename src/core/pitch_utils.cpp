// Implementation of pitch utility functions.

#include "core/pitch_utils.h"

#include <cstdlib>

#include "core/interval.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

IntervalQuality classifyInterval(int semitones) {
  // Normalize to 0-11 range (reduce compound intervals)
  int normalized = interval_util::compoundToSimple(semitones);

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
  int norm1 = interval_util::compoundToSimple(interval1);
  int norm2 = interval_util::compoundToSimple(interval2);
  return (norm1 == interval::kPerfect5th) && (norm2 == interval::kPerfect5th);
}

bool isParallelOctaves(int interval1, int interval2) {
  int norm1 = interval_util::compoundToSimple(interval1);
  int norm2 = interval_util::compoundToSimple(interval2);
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

bool isDiatonicInKey(int pitch, Key key, bool is_minor) {
  int key_offset = static_cast<int>(key);
  int pitch_class = (((pitch % 12) + 12) % 12 - key_offset + 12) % 12;

  if (!is_minor) {
    for (int i = 0; i < 7; ++i) {
      if (kScaleMajor[i] == pitch_class) return true;
    }
    return false;
  }

  // Minor keys: check union of natural, harmonic, and melodic minor scales.
  // Bach routinely uses raised 6th (melodic) and raised 7th (harmonic/melodic),
  // so all three scale forms are valid diatonic pitch classes.
  for (int i = 0; i < 7; ++i) {
    if (kScaleNaturalMinor[i] == pitch_class) return true;
    if (kScaleHarmonicMinor[i] == pitch_class) return true;
    if (kScaleMelodicMinor[i] == pitch_class) return true;
  }
  return false;
}

const char* intervalToName(int semitones) {
  int normalized = interval_util::compoundToSimple(semitones);
  switch (normalized) {
    case 0:  return "unison";
    case 1:  return "minor 2nd";
    case 2:  return "major 2nd";
    case 3:  return "minor 3rd";
    case 4:  return "major 3rd";
    case 5:  return "perfect 4th";
    case 6:  return "tritone";
    case 7:  return "perfect 5th";
    case 8:  return "minor 6th";
    case 9:  return "major 6th";
    case 10: return "minor 7th";
    case 11: return "major 7th";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// Scale / chord tone collection
// ---------------------------------------------------------------------------

std::vector<uint8_t> getScaleTones(Key key, bool is_minor, uint8_t low_pitch,
                                   uint8_t high_pitch) {
  std::vector<uint8_t> tones;
  ScaleType scale_type = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  for (int pitch = static_cast<int>(low_pitch);
       pitch <= static_cast<int>(high_pitch); ++pitch) {
    if (scale_util::isScaleTone(static_cast<uint8_t>(pitch), key, scale_type)) {
      tones.push_back(static_cast<uint8_t>(pitch));
    }
  }

  return tones;
}

std::vector<uint8_t> getChordTones(const Chord& chord, int octave) {
  std::vector<uint8_t> tones;
  tones.reserve(3);

  int root = (octave + 1) * 12 + (static_cast<int>(chord.root_pitch) % 12);

  // Determine third interval based on quality.
  int third_offset = 4;  // Major third default.
  if (chord.quality == ChordQuality::Minor ||
      chord.quality == ChordQuality::Diminished ||
      chord.quality == ChordQuality::Minor7) {
    third_offset = 3;  // Minor third.
  }

  // Determine fifth interval based on quality.
  int fifth_offset = 7;  // Perfect fifth default.
  if (chord.quality == ChordQuality::Diminished) {
    fifth_offset = 6;  // Diminished fifth.
  } else if (chord.quality == ChordQuality::Augmented) {
    fifth_offset = 8;  // Augmented fifth.
  }

  auto clamp_midi = [](int pitch) -> uint8_t {
    if (pitch < 0) return 0;
    if (pitch > 127) return 127;
    return static_cast<uint8_t>(pitch);
  };

  tones.push_back(clamp_midi(root));
  tones.push_back(clamp_midi(root + third_offset));
  tones.push_back(clamp_midi(root + fifth_offset));

  return tones;
}

std::vector<uint8_t> collectChordTonesInRange(const Chord& chord,
                                              uint8_t low, uint8_t high) {
  std::vector<uint8_t> tones;
  int root_pc = static_cast<int>(chord.root_pitch) % 12;

  int third_offset = 4;
  if (chord.quality == ChordQuality::Minor ||
      chord.quality == ChordQuality::Diminished ||
      chord.quality == ChordQuality::Minor7) {
    third_offset = 3;
  }
  int fifth_offset = 7;
  if (chord.quality == ChordQuality::Diminished) {
    fifth_offset = 6;
  } else if (chord.quality == ChordQuality::Augmented) {
    fifth_offset = 8;
  }

  int intervals[] = {0, third_offset, fifth_offset};

  for (int pitch = static_cast<int>(low); pitch <= static_cast<int>(high);
       ++pitch) {
    int pc = pitch % 12;
    for (int intv : intervals) {
      if (pc == (root_pc + intv) % 12) {
        tones.push_back(static_cast<uint8_t>(pitch));
        break;
      }
    }
  }
  return tones;
}

bool isAllowedChromatic(uint8_t pitch, Key key, ScaleType scale,
                        const HarmonicEvent* harm_ev) {
  int key_offset = static_cast<int>(key);
  int pc = static_cast<int>(pitch) % 12;

  // 1. Raised 7th in harmonic minor is always allowed.
  if (scale == ScaleType::HarmonicMinor || scale == ScaleType::NaturalMinor) {
    int raised_7th = (key_offset + kScaleHarmonicMinor[6]) % 12;
    if (pc == raised_7th) return true;
  }

  // 2. Chord tones of the current harmonic event (secondary dominants etc.).
  if (harm_ev != nullptr && isChordTone(pitch, *harm_ev)) return true;

  return false;
}

}  // namespace bach
