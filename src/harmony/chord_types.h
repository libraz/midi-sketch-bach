// Chord types and diatonic harmony utilities for Bach generation.

#ifndef BACH_HARMONY_CHORD_TYPES_H
#define BACH_HARMONY_CHORD_TYPES_H

#include <cstdint>

namespace bach {

/// Quality of a chord (triad or seventh).
enum class ChordQuality : uint8_t {
  Major,
  Minor,
  Diminished,
  Augmented,
  Dominant7,
  Minor7,
  MajorMajor7
};

/// Roman numeral chord degree (functional harmony).
enum class ChordDegree : uint8_t {
  I = 0,
  ii,
  iii,
  IV,
  V,
  vi,
  viiDim
};

/// A chord with degree, quality, root pitch, and inversion.
struct Chord {
  ChordDegree degree = ChordDegree::I;
  ChordQuality quality = ChordQuality::Major;
  uint8_t root_pitch = 0;    // MIDI pitch of chord root
  uint8_t inversion = 0;     // 0=root, 1=first, 2=second
};

/// @brief Get chord quality for a diatonic degree in major key.
/// @param degree The chord degree (I through viiDim).
/// @return ChordQuality for that degree in a major key.
ChordQuality majorKeyQuality(ChordDegree degree);

/// @brief Get chord quality for a diatonic degree in minor key (natural minor).
/// @param degree The chord degree (I through viiDim).
/// @return ChordQuality for that degree in a natural minor key.
ChordQuality minorKeyQuality(ChordDegree degree);

/// @brief Get the semitone offset from tonic for a chord degree in major scale.
/// @param degree The chord degree.
/// @return Number of semitones above the tonic (0-11).
uint8_t degreeSemitones(ChordDegree degree);

/// @brief Get the semitone offset from tonic for a chord degree in natural minor.
/// @param degree The chord degree.
/// @return Number of semitones above the tonic (0-11).
uint8_t degreeMinorSemitones(ChordDegree degree);

/// @brief Convert ChordQuality to a human-readable string.
/// @param quality The chord quality.
/// @return Null-terminated string such as "Major", "Minor", "Diminished".
const char* chordQualityToString(ChordQuality quality);

/// @brief Convert ChordDegree to a human-readable string.
/// @param degree The chord degree.
/// @return Null-terminated string such as "I", "ii", "V".
const char* chordDegreeToString(ChordDegree degree);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_TYPES_H
