// Arpeggio pattern types and generation for Solo String Flow (BWV1007 style).

#ifndef BACH_SOLO_STRING_FLOW_ARPEGGIO_PATTERN_H
#define BACH_SOLO_STRING_FLOW_ARPEGGIO_PATTERN_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Arpeggio pattern types for Flow generation.
///
/// Each type determines how chord tones are arranged within a beat or bar.
/// These map to fundamental arpeggio figures found in BWV1007 and similar works.
enum class ArpeggioPatternType : uint8_t {
  Rising,        // Low to high (basic ascending arpeggio)
  Falling,       // High to low (descending arpeggio)
  Oscillating,   // Alternating low/high (e.g. low-high-low-high)
  PedalPoint,    // Lowest note repeated between other chord tones
  ScaleFragment  // Stepwise motion mixed with chord tones
};

/// @brief Dramaturgical role of a pattern within a section.
///
/// Controls the function of a pattern in the section's internal narrative.
/// Order within a section: Drive -> Expand -> Sustain -> Release (no reversal).
enum class PatternRole : uint8_t {
  Drive,    // Push forward, establish momentum
  Expand,   // Widen register, build tension
  Sustain,  // Stay in harmonic resonance
  Release   // Release tension, prepare for next section
};

/// @brief A single arpeggio pattern specification.
///
/// Combines a pattern type with its dramaturgical role and note-level details.
/// The degrees vector specifies which scale degrees are used (0-based from root).
struct ArpeggioPattern {
  ArpeggioPatternType type = ArpeggioPatternType::Rising;
  PatternRole role = PatternRole::Drive;
  int notes_per_beat = 4;         // Usually 4 (16th notes at given tempo)
  std::vector<int> degrees;       // Scale degrees, e.g. {0, 2, 4, 7} = root, 3rd, 5th, 7th
  bool use_open_string = false;   // Whether to prefer open-string resonance
};

/// @brief Convert ArpeggioPatternType to human-readable string.
/// @param type The pattern type to convert.
/// @return Null-terminated string representation.
const char* arpeggioPatternTypeToString(ArpeggioPatternType type);

/// @brief Convert PatternRole to human-readable string.
/// @param role The pattern role to convert.
/// @return Null-terminated string representation.
const char* patternRoleToString(PatternRole role);

/// @brief Check if PatternRole order is valid within a section.
///
/// Valid order is monotonically non-decreasing with the mapping:
///   Drive=0, Expand=1, Sustain=2, Release=3.
/// E.g., {Drive, Drive, Expand, Sustain} is valid;
///        {Expand, Drive, Release} is not.
///
/// @param roles Vector of PatternRole values in sequence order.
/// @return true if the roles follow valid monotonic order, false otherwise.
///         Empty vectors are considered valid.
bool isPatternRoleOrderValid(const std::vector<PatternRole>& roles);

/// @brief Get allowed ArpeggioPatternTypes for a given ArcPhase.
///
/// Phase restrictions:
/// - Ascent: Rising, Oscillating, ScaleFragment (no Falling, no PedalPoint)
/// - Peak: all types allowed (maximum variety at climax)
/// - Descent: Falling, Oscillating, PedalPoint (no Rising, no ScaleFragment)
///
/// @param phase Current ArcPhase of the section.
/// @return Vector of permitted ArpeggioPatternType values.
std::vector<ArpeggioPatternType> getAllowedPatternsForPhase(ArcPhase phase);

/// @brief Generate a pattern for a given chord, ArcPhase, and PatternRole.
///
/// Creates an ArpeggioPattern with degrees arranged according to the pattern type
/// selected for the given phase and role. The pattern type is deterministic:
/// - Drive + Ascent/Peak -> Rising
/// - Expand + any -> Oscillating
/// - Sustain + any -> PedalPoint
/// - Release + Descent -> Falling
/// - Otherwise -> first allowed pattern for the phase
///
/// @param chord_degrees Scale degrees available from the chord (e.g. {0, 4, 7}).
/// @param phase Current ArcPhase.
/// @param role Current PatternRole.
/// @param use_open_strings Whether to prefer open string usage.
/// @return A configured ArpeggioPattern with degrees arranged per the type.
ArpeggioPattern generatePattern(const std::vector<int>& chord_degrees,
                                ArcPhase phase, PatternRole role,
                                bool use_open_strings);

}  // namespace bach

#endif  // BACH_SOLO_STRING_FLOW_ARPEGGIO_PATTERN_H
