// Key signature and key relationship utilities for Bach harmony.

#ifndef BACH_HARMONY_KEY_H
#define BACH_HARMONY_KEY_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief A key signature combining tonic pitch class and mode.
///
/// This is the primary key representation for the harmony module.
/// It bundles Key (pitch class) with mode (major/minor) into one value.
struct KeySignature {
  Key tonic = Key::C;
  bool is_minor = false;

  /// @brief Equality comparison.
  bool operator==(const KeySignature& other) const {
    return tonic == other.tonic && is_minor == other.is_minor;
  }

  /// @brief Inequality comparison.
  bool operator!=(const KeySignature& other) const {
    return !(*this == other);
  }
};

/// @brief Get the dominant key (perfect 5th above, same mode).
/// @param key_sig The source key signature.
/// @return KeySignature of the dominant key.
KeySignature getDominant(const KeySignature& key_sig);

/// @brief Get the subdominant key (perfect 4th above / 5th below, same mode).
/// @param key_sig The source key signature.
/// @return KeySignature of the subdominant key.
KeySignature getSubdominant(const KeySignature& key_sig);

/// @brief Get the relative major or minor key.
/// @param key_sig The source key signature.
/// @return If major, the relative minor (3 semitones below tonic).
///         If minor, the relative major (3 semitones above tonic).
KeySignature getRelative(const KeySignature& key_sig);

/// @brief Get the parallel major or minor key (same tonic, opposite mode).
/// @param key_sig The source key signature.
/// @return KeySignature with same tonic but flipped mode.
KeySignature getParallel(const KeySignature& key_sig);

/// @brief Check if two keys are closely related (distance <= 1 on circle of 5ths).
/// @param lhs First key signature.
/// @param rhs Second key signature.
/// @return True if the keys are within 1 step on the circle of fifths,
///         accounting for relative/parallel relationships.
bool isCloselyRelated(const KeySignature& lhs, const KeySignature& rhs);

/// @brief Get circle-of-fifths distance between two key signatures.
/// @param lhs First key signature.
/// @param rhs Second key signature.
/// @return Minimum number of steps on the circle of fifths (0-6).
///         For keys of different modes, calculates through the relative key.
int circleOfFifthsDistance(const KeySignature& lhs, const KeySignature& rhs);

/// @brief Get all closely related keys (distance 0-1 on circle of fifths).
/// @param key_sig The source key signature.
/// @return Vector of closely related key signatures (includes the source key itself,
///         dominant, subdominant, relative, and parallel).
std::vector<KeySignature> getCloselyRelatedKeys(const KeySignature& key_sig);

/// @brief Get MIDI pitch of the tonic in a given octave.
/// @param key The key (pitch class).
/// @param octave The octave number (default 4, where C4 = MIDI 60).
/// @return MIDI pitch number for the tonic in that octave.
uint8_t tonicPitch(Key key, int octave = 4);

/// @brief Parse a key signature from a string.
/// @param str String such as "C_major", "g_minor", "D_major", "eb_minor".
///        Case of the first letter indicates nothing; the "_major"/"_minor" suffix
///        determines the mode. The note name before "_" determines the tonic.
/// @return Parsed KeySignature. Defaults to C major on unrecognized input.
KeySignature keySignatureFromString(const std::string& str);

/// @brief Convert a key signature to a string.
/// @param key_sig The key signature to convert.
/// @return String such as "C_major", "G_minor".
std::string keySignatureToString(const KeySignature& key_sig);

}  // namespace bach

#endif  // BACH_HARMONY_KEY_H
