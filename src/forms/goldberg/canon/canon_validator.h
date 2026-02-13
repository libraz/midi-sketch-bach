// Post-generation validator for canon integrity in Goldberg Variations.

#ifndef BACH_FORMS_GOLDBERG_CANON_CANON_VALIDATOR_H
#define BACH_FORMS_GOLDBERG_CANON_CANON_VALIDATOR_H

#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/canon/canon_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"

namespace bach {

/// @brief Validation result for canon integrity.
///
/// Tracks pitch, timing, and duration violations between dux and comes voices.
/// A canon passes validation if pitch_accuracy >= 0.95 and no timing or
/// duration violations are found.
struct CanonValidationResult {
  bool passed = true;
  int total_pairs = 0;          ///< Number of dux-comes pairs checked.
  int pitch_violations = 0;     ///< Pairs where comes pitch != expected transform.
  int timing_violations = 0;    ///< Pairs where comes tick != dux tick + delay.
  int duration_violations = 0;  ///< Pairs where comes duration != dux duration.
  float pitch_accuracy = 1.0f;  ///< Ratio of correct pitch pairs.
  std::vector<std::string> messages;  ///< Diagnostic messages for failures.
};

/// @brief Validate that the canonic relationship between dux and comes is preserved.
///
/// Checks:
///   1. Pitch transformation: comes pitch == transform(dux pitch) for each pair.
///   2. Timing: comes entry == dux entry + delay (in ticks).
///   3. Duration: comes duration == dux duration (StrictRhythm mode).
///
/// Pairs are matched by temporal alignment: for each dux note, the validator
/// searches for a comes note at (dux.start_tick + delay_ticks), with a
/// tolerance of +/-1 tick for rounding.
///
/// @param dux_notes Notes of the dux (leader) voice.
/// @param comes_notes Notes of the comes (follower) voice.
/// @param spec Canon specification (interval, transform, key, delay).
/// @param time_sig Time signature for delay-to-tick conversion.
/// @return Validation result with pass/fail status and diagnostics.
CanonValidationResult validateCanonIntegrity(
    const std::vector<NoteEvent>& dux_notes,
    const std::vector<NoteEvent>& comes_notes,
    const CanonSpec& spec,
    const TimeSignature& time_sig);

/// @brief Validate climax alignment with the structural grid.
///
/// Checks whether both dux and comes melodic peaks (highest pitch) fall
/// within 2 bars of an Intensification position in the structural grid.
///
/// @param dux_notes Notes of the dux voice.
/// @param comes_notes Notes of the comes voice.
/// @param grid The 32-bar structural grid.
/// @param time_sig Time signature for tick-to-bar conversion.
/// @return True if both peaks are within 2 bars of Intensification.
bool validateClimaxAlignment(
    const std::vector<NoteEvent>& dux_notes,
    const std::vector<NoteEvent>& comes_notes,
    const GoldbergStructuralGrid& grid,
    const TimeSignature& time_sig);

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_CANON_CANON_VALIDATOR_H
