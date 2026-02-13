// Canon type definitions for Goldberg Variations canon generation.

#ifndef BACH_FORMS_GOLDBERG_CANON_CANON_TYPES_H
#define BACH_FORMS_GOLDBERG_CANON_CANON_TYPES_H

#include <cstdint>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Canon transformation type.
///
/// Regular = diatonic transposition only (Var 3, 6, 9, 18, 24, 27).
/// Inverted = diatonic inversion + transposition (Var 15, possibly Var 12).
enum class CanonTransform : uint8_t {
  Regular,   ///< Diatonic transposition only.
  Inverted   ///< Diatonic inversion + transposition.
};

/// @brief Rhythmic imitation mode for canon.
enum class CanonRhythmicMode : uint8_t {
  StrictRhythm,    ///< Exact rhythmic match (BWV 988 standard).
  Proportional     ///< Duration ratio transform (future: augmentation canon).
};

/// @brief Full specification for a single canon variation.
///
/// Encodes the interval, transform, key, delay, and rhythmic mode needed
/// to derive comes from dux. All 9 BWV 988 canons use delay_bars=1.
struct CanonSpec {
  int canon_interval;              ///< Diatonic degree: 0=unison, 1=2nd, ... 8=9th.
  CanonTransform transform;
  KeySignature key;
  MinorModeProfile minor_profile;  ///< For minor key canons.
  int delay_bars = 1;              ///< Delay in bars (all 9 BWV 988 canons use 1).
  CanonRhythmicMode rhythmic_mode = CanonRhythmicMode::StrictRhythm;
};

/// @brief Metrical hierarchy constraints for canon generation.
///
/// Penalty/bonus constants governing how dissonances and structural
/// alignments are scored during canon dux note selection.
struct CanonMetricalRules {
  static constexpr float kUnpreparedDissonancePenalty = 10.0f;
  static constexpr float kSuspensionPenalty = 0.5f;
  static constexpr float kAppogggiaturaPenalty = 2.0f;
  static constexpr float kAccentedPassingPenalty = 3.0f;
  static constexpr bool kComesEntryOnStrongOnly = true;
  static constexpr float kCadenceMisalignPenalty = 5.0f;
  static constexpr float kClimaxPositionBonus = -0.5f;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_CANON_CANON_TYPES_H
