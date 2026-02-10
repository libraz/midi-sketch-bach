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
  MajorMajor7,
  Diminished7,       // Fully diminished seventh (dim3 + dim3 + dim3)
  HalfDiminished7,   // Half-diminished seventh (min3 + dim3 + maj3)
  AugmentedSixth     // Italian/French/German augmented sixth
};

/// Roman numeral chord degree (functional harmony).
enum class ChordDegree : uint8_t {
  I = 0,
  ii,
  iii,
  IV,
  V,
  vi,
  viiDim,
  bII,       // Neapolitan (lowered II)
  V_of_V,    // Secondary dominant: V/V
  V_of_vi,   // Secondary dominant: V/vi
  V_of_IV,   // Secondary dominant: V/IV
  V_of_ii    // Secondary dominant: V/ii
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

struct HarmonicEvent;

/// @brief Check if a MIDI pitch is a chord tone of the given harmonic event.
///
/// A pitch is a chord tone if its pitch class matches the root, 3rd, or 5th
/// of the chord (accounting for chord quality).
///
/// @param pitch MIDI note number.
/// @param event Harmonic event with chord information.
/// @return true if the pitch is a chord tone.
bool isChordTone(uint8_t pitch, const HarmonicEvent& event);

}  // namespace bach

#endif  // BACH_HARMONY_CHORD_TYPES_H
