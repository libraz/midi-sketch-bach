// Configuration types for the Chaconne engine (Arch system, BWV1004 style).

#ifndef BACH_SOLO_STRING_ARCH_CHACONNE_CONFIG_H
#define BACH_SOLO_STRING_ARCH_CHACONNE_CONFIG_H

#include <random>
#include <set>
#include <vector>

#include "analysis/fail_report.h"
#include "core/basic_types.h"
#include "harmony/key.h"
#include "solo_string/arch/chaconne_scheme.h"
#include "solo_string/arch/ground_bass.h"
#include "solo_string/arch/variation_types.h"

namespace bach {

/// @brief Configuration for a single chaconne variation.
///
/// Each variation has an immutable VariationRole (set at construction via the
/// variation plan) and a VariationType that must be compatible with that role.
/// The key may differ from the global key for the major section.
struct ChaconneVariation {
  int variation_number = 0;
  VariationRole role = VariationRole::Establish;  ///< Immutable once set
  VariationType type = VariationType::Theme;
  TextureType primary_texture = TextureType::SingleLine;
  KeySignature key = {Key::D, true};  ///< D minor default
  bool is_major_section = false;      ///< True for Illuminate variations
};

/// @brief Design values for the climax (output directly, not searched).
///
/// These are fixed design decisions, not parameters to optimize.
/// The climax is placed at a specific position and uses predetermined
/// texture, register, and harmonic density values.
///
/// Principle: Trust Design Values. Do not search or score; output directly.
struct ClimaxDesign {
  int accumulate_variations = 3;        ///< Exactly 3 (design invariant)
  float harmonic_weight_peak = 2.0f;    ///< Harmonic weight multiplier at climax
  bool unlock_max_register = true;      ///< Allow full instrument range
  bool allow_full_chords = true;        ///< Enable FullChords texture
  float position_ratio_min = 0.70f;     ///< Climax starts at 70% of piece
  float position_ratio_max = 0.85f;     ///< Climax ends by 85% of piece

  // Fixed design values (no search -- Principle 4).
  TextureType fixed_texture = TextureType::FullChords;
  uint8_t fixed_register_low = 55;      ///< G3 (violin low register)
  uint8_t fixed_register_high = 93;     ///< A6 (near violin maximum)
  float fixed_harmonic_density = 0.9f;  ///< High density for dramatic effect
};

/// @brief Constraints for the major section (separate personality).
///
/// The major section in a chaconne (typically the Illuminate phase)
/// has a fundamentally different character from the surrounding minor
/// sections. It uses lighter textures, lower density, and a more
/// constrained register.
struct MajorSectionConstraints {
  bool allow_full_chords = false;  ///< No full chords in major section
  std::set<TextureType> allowed_textures = {
    TextureType::SingleLine,
    TextureType::Arpeggiated,
    TextureType::Bariolage
  };
  float rhythm_density_cap = 0.6f;  ///< Lower rhythmic density
  uint8_t register_low = 55;        ///< G3
  uint8_t register_high = 84;       ///< C6
};

/// @brief Texture distribution target for a variation group within the chaconne arc.
///
/// Provides soft targets for the proportion of SingleLine, DoubleStop (2-voice),
/// and Chord (3+ voice) textures within a group of variations. These targets
/// guide the texture selection to follow the BWV1004 reference distribution:
/// 1-voice 61%, 2-voice 20%, 3-voice 19%, avg active voices 1.57.
struct TextureArcTarget {
  float single_line_ratio = 0.60f;  ///< Target SingleLine proportion.
  float double_stop_ratio = 0.20f;  ///< Target 2-voice proportion.
  float chord_ratio = 0.20f;        ///< Target 3+ voice proportion.
};

/// @brief Get texture arc target for a variation role and its position within the arc.
///
/// The arc follows BWV1004 structure:
///   - Early (Establish/Develop): heavier SingleLine (70/20/10)
///   - Middle (Destabilize/Illuminate): balanced (55/25/20)
///   - Late (Accumulate/Resolve): Accumulate uses chords (45/25/30),
///     Resolve returns to simpler texture (70/20/10)
///
/// @param role Variation role.
/// @return TextureArcTarget with soft ratio targets.
TextureArcTarget getTextureArcTarget(VariationRole role);

/// @brief Full configuration for chaconne generation.
///
/// This is the top-level config struct for generating BWV1004-style chaconnes.
/// The variation plan (role sequence) is config-fixed and seed-independent:
/// structural order is a design decision, not a random variable.
struct ChaconneConfig {
  KeySignature key = {Key::D, true};  ///< D minor (BWV1004)
  uint16_t bpm = 60;
  uint32_t seed = 0;                  ///< 0 = auto (time-based)
  InstrumentType instrument = InstrumentType::Violin;

  /// @deprecated Use custom_scheme instead. Ignored when custom_scheme is non-empty.
  std::vector<NoteEvent> ground_bass_notes;

  /// Custom harmonic scheme. If empty, createForKey(key) is used.
  std::vector<SchemeEntry> custom_scheme;

  /// Variation plan. If empty, createStandardVariationPlan(key) is used.
  std::vector<ChaconneVariation> variations;

  /// Fixed design values for the climax.
  ClimaxDesign climax;

  /// Constraints for the major section.
  MajorSectionConstraints major_constraints;

  /// Maximum retries per variation before reporting failure.
  int max_variation_retries = 3;

  /// Target number of variations (0 = use default plan).
  /// When > 0, createScaledVariationPlan() is used instead of the standard plan.
  int target_variations = 0;
};

/// @brief Create a standard variation plan for a chaconne.
///
/// Generates ~10 variations following the fixed structural order:
///
///   Minor front:  Establish(Theme), Develop(Rhythmic), Destabilize(Virtuosic)
///   Major:        Illuminate(Lyrical), Illuminate(Chordal)
///   Minor back:   Destabilize(Virtuosic), Accumulate(Virtuosic),
///                 Accumulate(Chordal), Accumulate(Virtuosic), Resolve(Theme)
///
/// Major section variations use the parallel major key and have
/// is_major_section = true.
///
/// @param key The key signature (major section uses parallel major).
/// @param rng Mersenne Twister RNG for VariationType selection.
/// @return Vector of ChaconneVariation with roles and types assigned.
std::vector<ChaconneVariation> createStandardVariationPlan(const KeySignature& key,
                                                            std::mt19937& rng);

/// @brief Create a scaled variation plan for a chaconne with a target variation count.
///
/// Generates a plan for the specified number of variations following the BWV1004
/// structural pattern. Fixed roles (Establish=1, Accumulate=3, Resolve=1) are
/// preserved; the remaining variations are distributed across Develop,
/// Destabilize, and Illuminate blocks using historical proportions.
///
/// For counts > Short (10), post-major Illuminate "islands" are inserted
/// within Destabilize blocks at regular intervals. Islands use
/// is_major_section=false and related major keys.
///
/// When target_variations <= 10, returns the same output as
/// createStandardVariationPlan().
///
/// @param key The key signature (major section uses parallel major).
/// @param target_variations Number of variations to generate (minimum 10).
/// @param rng Mersenne Twister RNG for VariationType selection.
/// @return Vector of ChaconneVariation with roles and types assigned.
std::vector<ChaconneVariation> createScaledVariationPlan(const KeySignature& key,
                                                          int target_variations,
                                                          std::mt19937& rng);

/// @brief Validate a variation plan for structural correctness.
///
/// Checks:
/// - Role order is valid (isRoleOrderValid)
/// - Accumulate appears exactly 3 times
/// - Resolve is the final variation and uses Theme type
/// - Every variation's type is allowed for its role
///
/// @param plan The variation plan to validate.
/// @return true if the plan satisfies all structural constraints.
bool validateVariationPlan(const std::vector<ChaconneVariation>& plan);

/// @brief Validate a variation plan and return a structured FailReport.
///
/// Performs the same checks as validateVariationPlan but returns a FailReport
/// with individual FailIssue entries for each detected problem:
///
/// - Empty plan (ConfigFail, Critical, rule="empty_plan")
/// - Invalid type for role (ConfigFail, Critical, rule="invalid_type_for_role")
/// - Accumulate count != 3 (ConfigFail, Critical, rule="accumulate_count")
/// - Final variation not Resolve/Theme (ConfigFail, Critical, rule="final_not_resolve")
/// - Invalid role order (ConfigFail, Critical, rule="invalid_role_order")
///
/// @param plan The variation plan to validate.
/// @return FailReport containing all detected issues.
FailReport validateVariationPlanReport(const std::vector<ChaconneVariation>& plan);

}  // namespace bach

#endif  // BACH_SOLO_STRING_ARCH_CHACONNE_CONFIG_H
