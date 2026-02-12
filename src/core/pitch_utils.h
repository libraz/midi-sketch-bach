// Pitch utilities for Bach MIDI generation -- counterpoint intervals,
// scale support, pitch ranges, and note name conversion.

#ifndef BACH_CORE_PITCH_UTILS_H
#define BACH_CORE_PITCH_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

// Forward declarations for chord tone utilities.
struct Chord;

// ---------------------------------------------------------------------------
// Interval constants (semitones)
// ---------------------------------------------------------------------------

namespace interval {

constexpr int kUnison = 0;
constexpr int kMinor2nd = 1;
constexpr int kMajor2nd = 2;
constexpr int kMinor3rd = 3;
constexpr int kMajor3rd = 4;
constexpr int kPerfect4th = 5;
constexpr int kTritone = 6;
constexpr int kPerfect5th = 7;
constexpr int kMinor6th = 8;
constexpr int kMajor6th = 9;
constexpr int kMinor7th = 10;
constexpr int kMajor7th = 11;
constexpr int kOctave = 12;

}  // namespace interval

// ---------------------------------------------------------------------------
// Scale interval arrays (semitones from root, 7 degrees)
// ---------------------------------------------------------------------------

constexpr int kScaleMajor[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleNaturalMinor[7] = {0, 2, 3, 5, 7, 8, 10};
constexpr int kScaleHarmonicMinor[7] = {0, 2, 3, 5, 7, 8, 11};
constexpr int kScaleMelodicMinor[7] = {0, 2, 3, 5, 7, 9, 11};
constexpr int kScaleDorian[7] = {0, 2, 3, 5, 7, 9, 10};
constexpr int kScaleMixolydian[7] = {0, 2, 4, 5, 7, 9, 10};

// ---------------------------------------------------------------------------
// Note names and diatonic lookup
// ---------------------------------------------------------------------------

/// Note names for pitch classes 0-11 (C=0).
constexpr const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// True if the pitch class belongs to C major diatonic scale.
constexpr bool kDiatonicPitchClass[12] = {
    true, false, true, false, true, true, false, true, false, true, false, true};

// ---------------------------------------------------------------------------
// Organ pitch ranges (MIDI note numbers)
// ---------------------------------------------------------------------------

namespace organ_range {

// Manual I (Great): C2-C6
constexpr uint8_t kManual1Low = 36;
constexpr uint8_t kManual1High = 96;

// Manual II (Swell): C2-C6
constexpr uint8_t kManual2Low = 36;
constexpr uint8_t kManual2High = 96;

// Manual III (Positiv): C3-C6
constexpr uint8_t kManual3Low = 48;
constexpr uint8_t kManual3High = 96;

// Pedal: C1-D3 (soft penalty, not hard reject)
constexpr uint8_t kPedalLow = 24;
constexpr uint8_t kPedalHigh = 50;

}  // namespace organ_range

// ---------------------------------------------------------------------------
// Solo string pitch ranges
// ---------------------------------------------------------------------------

namespace string_range {

// Cello: C2-A5
constexpr uint8_t kCelloLow = 36;
constexpr uint8_t kCelloHigh = 81;

// Violin: G3-C7
constexpr uint8_t kViolinLow = 55;
constexpr uint8_t kViolinHigh = 96;

// Guitar: E2-B5
constexpr uint8_t kGuitarLow = 40;
constexpr uint8_t kGuitarHigh = 83;

}  // namespace string_range

// ---------------------------------------------------------------------------
// Counterpoint interval classification
// ---------------------------------------------------------------------------

/// Interval quality for counterpoint rule evaluation.
enum class IntervalQuality : uint8_t {
  PerfectConsonance,    // Unison, 5th, octave
  ImperfectConsonance,  // 3rd, 6th
  Dissonance            // 2nd, 4th (context-dependent), tritone, 7th
};

/// @brief Classify an interval (in semitones) for counterpoint evaluation.
/// @param semitones Absolute interval size in semitones (0-12+).
/// @return IntervalQuality classification.
///
/// Perfect 4th is classified as Dissonance here (two-voice counterpoint
/// default). Callers handling suspension or 6/4 contexts should override.
IntervalQuality classifyInterval(int semitones);

/// @brief Check if two successive intervals form parallel fifths.
/// @param interval1 First interval in semitones (between two voices).
/// @param interval2 Second interval in semitones (between same two voices).
/// @return True if both intervals are perfect fifths (mod 12).
bool isParallelFifths(int interval1, int interval2);

/// @brief Check if two successive intervals form parallel octaves.
/// @param interval1 First interval in semitones.
/// @param interval2 Second interval in semitones.
/// @return True if both intervals are perfect octaves/unisons (mod 12).
bool isParallelOctaves(int interval1, int interval2);

// ---------------------------------------------------------------------------
// Pitch utility functions
// ---------------------------------------------------------------------------

/// @brief Extract pitch class (0-11) from MIDI note number.
/// @param pitch MIDI note number (0-127).
/// @return Pitch class where C=0, C#=1, ..., B=11.
inline int getPitchClass(uint8_t pitch) {
  return static_cast<int>(pitch) % 12;
}

/// @brief Get octave number from MIDI note number.
/// @param pitch MIDI note number.
/// @return Octave number (C4 = octave 4, MIDI 60).
inline int getOctave(uint8_t pitch) {
  return static_cast<int>(pitch) / 12 - 1;
}

/// @brief Check if a MIDI pitch belongs to the C major diatonic scale.
/// @param pitch MIDI note number.
/// @return True if the pitch class is in {C, D, E, F, G, A, B}.
inline bool isDiatonic(int pitch) {
  int pitch_class = ((pitch % 12) + 12) % 12;  // Handle negative pitches
  return kDiatonicPitchClass[pitch_class];
}

/// @brief Clamp a pitch value to a valid range.
/// @param pitch Input pitch (may be out of range).
/// @param low Minimum MIDI note number.
/// @param high Maximum MIDI note number.
/// @return Clamped pitch value.
inline uint8_t clampPitch(int pitch, uint8_t low, uint8_t high) {
  if (pitch < static_cast<int>(low)) return low;
  if (pitch > static_cast<int>(high)) return high;
  return static_cast<uint8_t>(pitch);
}

/// @brief Get the scale interval array for a given scale type.
/// @param scale Scale type to look up.
/// @return Pointer to array of 7 intervals (semitones from root).
const int* getScaleIntervals(ScaleType scale);

/// @brief Convert a scale degree to a MIDI pitch number.
/// @param degree Scale degree (0-based: 0=root, 1=2nd, ..., 6=7th).
///        Supports negative degrees and degrees >= 7 (spans octaves).
/// @param base_note Base MIDI note for degree 0 in C major context.
/// @param key_offset Semitone offset for key transposition (0=C, 2=D, etc.).
/// @param scale Scale type (default: Major).
/// @return MIDI note number.
int degreeToPitch(int degree, int base_note, int key_offset,
                  ScaleType scale = ScaleType::Major);

/// @brief Convert a MIDI note number to a human-readable note name.
/// @param pitch MIDI note number (0-127).
/// @return String like "C4", "F#3", "Bb5".
std::string pitchToNoteName(uint8_t pitch);

/// @brief Transpose a pitch from C major to the target key.
/// @param pitch MIDI note number in C major context.
/// @param key Target key.
/// @return Transposed MIDI note number.
uint8_t transposePitch(uint8_t pitch, Key key);

/// @brief Calculate the absolute interval between two pitches in semitones.
/// @param pitch_a First MIDI note number.
/// @param pitch_b Second MIDI note number.
/// @return Non-negative interval in semitones.
inline int absoluteInterval(uint8_t pitch_a, uint8_t pitch_b) {
  int diff = static_cast<int>(pitch_a) - static_cast<int>(pitch_b);
  return diff < 0 ? -diff : diff;
}

/// @brief Calculate the directed interval (signed) from pitch_a to pitch_b.
/// @param pitch_a Starting pitch.
/// @param pitch_b Ending pitch.
/// @return Positive if ascending, negative if descending.
inline int directedInterval(uint8_t pitch_a, uint8_t pitch_b) {
  return static_cast<int>(pitch_b) - static_cast<int>(pitch_a);
}

/// @brief Check if a MIDI pitch belongs to the diatonic scale of the given key.
/// @param pitch MIDI note number.
/// @param key Tonic pitch class.
/// @param is_minor True for natural minor, false for major.
/// @return True if the pitch class falls on a diatonic degree of the key.
bool isDiatonicInKey(int pitch, Key key, bool is_minor);

/// @brief Convert an interval in semitones to a human-readable name.
/// @param semitones Interval size (0-12+). Compound intervals are reduced mod 12.
/// @return String such as "unison", "minor 2nd", "perfect 5th", "tritone".
const char* intervalToName(int semitones);

// ---------------------------------------------------------------------------
// Scale / chord tone collection
// ---------------------------------------------------------------------------

/// @brief Get scale tones within a pitch range for the given key context.
/// @param key Musical key (pitch class of tonic).
/// @param is_minor True for minor mode (uses harmonic minor), false for major.
/// @param low_pitch Lowest MIDI pitch to include.
/// @param high_pitch Highest MIDI pitch to include.
/// @return Vector of scale-member MIDI pitches in ascending order.
std::vector<uint8_t> getScaleTones(Key key, bool is_minor, uint8_t low_pitch,
                                   uint8_t high_pitch);

/// @brief Get chord tones as MIDI pitches for a given chord and base octave.
///
/// Returns root, third, and fifth of the chord in the specified octave.
/// Quality determines the third and fifth intervals.
///
/// @param chord The chord to extract tones from.
/// @param octave Base octave for pitch calculation.
/// @return Vector of 3 MIDI pitch values (root, third, fifth).
std::vector<uint8_t> getChordTones(const struct Chord& chord, int octave);

/// @brief Collect all chord tones within a pitch range across octaves.
/// @param chord The chord to extract tones from.
/// @param low Lowest MIDI pitch to include.
/// @param high Highest MIDI pitch to include.
/// @return Vector of MIDI pitches that are chord tones within the range.
std::vector<uint8_t> collectChordTonesInRange(const struct Chord& chord,
                                              uint8_t low, uint8_t high);

// ---------------------------------------------------------------------------
// Chromatic alteration validation
// ---------------------------------------------------------------------------

// Forward declaration to avoid circular dependency.
struct HarmonicEvent;

/// @brief Check if a pitch is allowed as a chromatic alteration in the given key context.
///
/// Allows: (1) raised 7th in harmonic minor, (2) chord tones of the current
/// harmonic event (secondary dominants etc.), (3) nothing else.
///
/// @param pitch MIDI note number.
/// @param key Current key context.
/// @param scale Scale type.
/// @param harm_ev Pointer to the current harmonic event (may be null).
/// @return True if the pitch is a permitted chromatic alteration.
bool isAllowedChromatic(uint8_t pitch, Key key, ScaleType scale,
                        const HarmonicEvent* harm_ev);

/// @brief Calculate the octave shift needed to bring a pitch difference close to zero.
/// @param pitch_diff Signed pitch difference in semitones.
/// @return Multiple of 12 that, when subtracted from the difference, minimizes |result|.
///
/// Example: nearestOctaveShift(14) -> 12, nearestOctaveShift(-14) -> -12.
inline int nearestOctaveShift(int pitch_diff) {
  return (pitch_diff >= 0) ? ((pitch_diff + 6) / 12) * 12
                           : -(((-pitch_diff + 5) / 12) * 12);
}

/// @brief Find the index of the closest pitch in a tone vector.
/// @param tones Vector of MIDI pitches.
/// @param target Target MIDI pitch to find closest match for.
/// @return Index of the closest tone (0 if tones is empty).
size_t findClosestToneIndex(const std::vector<uint8_t>& tones, uint8_t target);

}  // namespace bach

#endif  // BACH_CORE_PITCH_UTILS_H
