// Type definitions for chaconne variations (Arch system).

#ifndef BACH_SOLO_STRING_ARCH_VARIATION_TYPES_H
#define BACH_SOLO_STRING_ARCH_VARIATION_TYPES_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Texture types for chaconne variations.
///
/// Each texture represents a distinct way of presenting the musical material
/// within a variation. The choice of texture is constrained by VariationRole
/// and whether the variation falls in the major section.
enum class TextureType : uint8_t {
  SingleLine,        ///< Single melody line (simplest)
  ImpliedPolyphony,  ///< Alternating voices suggesting 2+ parts
  FullChords,        ///< 3-4 note simultaneous chords (climax only)
  Arpeggiated,       ///< Broken chords (reuses Flow ArpeggioPattern)
  ScalePassage,      ///< Scale-based passage work
  Bariolage          ///< Open string alternation
};

/// @brief Variation character types.
///
/// VariationType describes the musical character of a variation,
/// independent of its structural role (VariationRole). Each role
/// permits at most 2 character types.
enum class VariationType : uint8_t {
  Theme,      ///< Original theme statement
  Lyrical,    ///< Singing, cantabile character
  Rhythmic,   ///< Rhythmic drive and energy
  Virtuosic,  ///< Technically demanding
  Chordal     ///< Harmonic/chordal emphasis
};

/// @brief Convert TextureType to human-readable string.
/// @param type The texture type to convert.
/// @return Null-terminated string representation.
const char* textureTypeToString(TextureType type);

/// @brief Convert VariationType to human-readable string.
/// @param type The variation type to convert.
/// @return Null-terminated string representation.
const char* variationTypeToString(VariationType type);

/// @brief Get allowed VariationTypes for a given VariationRole.
///
/// Each role permits at most 2 character types:
/// - Establish:    Theme, Lyrical
/// - Develop:      Rhythmic, Lyrical
/// - Destabilize:  Virtuosic, Rhythmic
/// - Illuminate:   Lyrical, Chordal
/// - Accumulate:   Virtuosic, Chordal
/// - Resolve:      Theme only (1 type)
///
/// @param role The variation role to query.
/// @return Vector of permitted VariationType values.
std::vector<VariationType> getAllowedTypes(VariationRole role);

/// @brief Check if a VariationType is allowed for a VariationRole.
/// @param type The variation type to check.
/// @param role The variation role to check against.
/// @return true if the type is permitted for that role.
bool isTypeAllowedForRole(VariationType type, VariationRole role);

/// @brief Check if a VariationRole sequence is in valid order.
///
/// The valid fixed order for a chaconne is:
///   Establish -> Develop -> Destabilize -> Illuminate
///   -> Destabilize -> Accumulate(x3) -> Resolve
///
/// Validation rules:
/// - VariationRole values must be monotonically non-decreasing,
///   except that Destabilize may appear both before and after Illuminate.
/// - Accumulate must appear exactly 3 times.
/// - The final role must be Resolve, and Resolve must appear exactly once.
/// - Resolve variations must use Theme type (checked separately).
///
/// @param roles The sequence of VariationRole values to validate.
/// @return true if the sequence satisfies all ordering constraints.
bool isRoleOrderValid(const std::vector<VariationRole>& roles);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_VARIATION_TYPES_H
