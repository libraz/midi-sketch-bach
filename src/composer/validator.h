#ifndef BACH_COMPOSER_VALIDATOR_H
#define BACH_COMPOSER_VALIDATOR_H

#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "core/basic_types.h"

namespace bach::composer {

// Validation has two distinct responsibilities. Generation checks only notes
// that CandidateSearch can replace, while FinalScore audits every sounding
// source after immutable material replay and ornament expansion.
enum class ValidationScope : std::uint8_t {
  Generation = 0,
  FinalScore = 1,
};

// Composer validator.
//
// Counterpoint checks (always run):
//   * Parallel perfect 5ths and 8ths between any pair of voices.
//   * Strong-beat dissonance: a note on beat 1 of a bar whose pitch
//     class is not part of the active chord triad.
//
// Suspension checks (when material.suspension_patterns is non-empty):
//   * suspension_preparation: the preparation note is consonant against
//     the bass voice and ties (same pitch) into the suspension note.
//   * suspension_resolution_step_down: suspension → resolution moves by
//     a single diatonic step in the direction the SuspensionType
//     prescribes (down for 4-3 / 7-6 / 9-8, up for 2-3).
//
// Validator does not mutate notes. On failure it reports the offending
// span; the caller decides whether to re-generate, back-jump, or abort.
//
// `material` may be left default-constructed; callers that do not
// declare suspension patterns or cadence cells get the same behavior as
// before P4. Validator never reads MaterialNote pitches — only the
// pre-declared marker structs (cadence_cells, suspension_patterns).
class Validator {
 public:
  ValidationReport validate(const std::vector<NoteEvent>& notes,
                            const std::vector<NoteProvenance>& provenance,
                            const HarmonicPlan& harmonic_plan,
                            const Material& material = Material{},
                            ValidationScope scope = ValidationScope::Generation) const;
};

/**
 * @brief Compute the piece-level texture metrics over a note list.
 *
 * Used by the Validator while validating and by the ornament post-pass to
 * refresh the embedded metrics after note expansion, so the metrics carried in
 * the report always describe the note list actually emitted downstream.
 *
 * @param notes The notes to measure (any voice set; empty yields zero metrics).
 * @return Aggregate and per-voice texture metrics for `notes`.
 */
TextureMetrics computeTextureMetrics(const std::vector<NoteEvent>& notes);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VALIDATOR_H
