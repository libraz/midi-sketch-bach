// Harmonic function classification and secondary dominant generation.

#ifndef BACH_HARMONY_HARMONIC_FUNCTION_H
#define BACH_HARMONY_HARMONIC_FUNCTION_H

#include <cstdint>

#include "harmony/chord_types.h"
#include "harmony/key.h"

namespace bach {

/// Harmonic function categories for Riemann-style analysis.
enum class HarmonicFunction : uint8_t {
  Tonic,        // I, vi (rest, stability)
  Subdominant,  // IV, ii, bII (forward motion)
  Dominant,     // V, viiDim (tension, demands resolution)
  Mediant,      // iii, bVI, bVII, bIII (color, ambiguity)
  Applied       // Secondary dominants V/x (tonicization)
};

/// @brief Classify a chord degree into its harmonic function.
/// @param degree The chord degree to classify.
/// @param is_minor True for minor key context.
/// @return The harmonic function of the degree.
HarmonicFunction classifyFunction(ChordDegree degree, bool is_minor);

/// @brief Convert HarmonicFunction to human-readable string.
/// @param func The harmonic function enum value.
/// @return Null-terminated string representation.
const char* harmonicFunctionToString(HarmonicFunction func);

/// @brief Create a secondary dominant chord (V/x).
///
/// Generates the dominant chord that would resolve to the target degree.
/// For example, V/V in C major produces a D major chord.
///
/// @param target The degree to which the secondary dominant resolves.
/// @param key_sig The current key signature.
/// @return Chord representing the secondary dominant (always Dominant7 quality).
Chord createSecondaryDominant(ChordDegree target, const KeySignature& key_sig);

/// @brief Create a Neapolitan sixth chord (bII in first inversion).
///
/// The Neapolitan is a major triad built on the lowered second degree,
/// used in first inversion (bII6). Common in Bach's minor-key works.
///
/// @param key_sig The current key signature.
/// @return Chord representing the Neapolitan sixth (bII6).
Chord createNeapolitanSixth(const KeySignature& key_sig);

/// @brief Check if a progression follows standard functional harmony rules.
///
/// Valid progressions: T->S, T->D, S->D, S->T, D->T, Applied->target.
/// Invalid progressions: D->S (retrogression).
///
/// @param from Source harmonic function.
/// @param to Target harmonic function.
/// @return True if the progression is standard.
bool isValidFunctionalProgression(HarmonicFunction from, HarmonicFunction to);

/// @brief Check if a specific degree transition is a valid functional progression.
/// @param from Source chord degree.
/// @param to Target chord degree.
/// @param is_minor Whether the key context is minor.
/// @return True if the progression is functionally valid.
bool isValidDegreeProgression(ChordDegree from, ChordDegree to, bool is_minor = false);

/// @brief Get the resolution target for a secondary dominant.
/// @param degree The secondary dominant degree (V/V, V/vi, V/IV, V/ii, V/iii).
/// @return The chord degree the secondary dominant resolves to.
///         Returns ChordDegree::I if the degree is not a secondary dominant.
ChordDegree getSecondaryDominantTarget(ChordDegree degree);

/// @brief Check if a chord degree is a secondary dominant.
/// @param degree The chord degree to check.
/// @return True if the degree is V/V, V/vi, V/IV, V/ii, or V/iii.
bool isSecondaryDominant(ChordDegree degree);

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_FUNCTION_H
