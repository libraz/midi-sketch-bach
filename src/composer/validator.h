#ifndef BACH_COMPOSER_VALIDATOR_H
#define BACH_COMPOSER_VALIDATOR_H

#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "core/basic_types.h"

namespace bach::composer {

// Composer validator.
//
// Phase 2 checks:
//   * Parallel perfect 5ths and 8ths between any pair of voices.
//   * Strong-beat dissonance: a note on beat 1 of a bar whose pitch
//     class is not part of the active chord triad.
//
// Validator does not mutate notes. On failure it reports the offending
// span; the caller decides whether to re-generate, back-jump, or abort.
class Validator {
 public:
  ValidationReport validate(const std::vector<NoteEvent>& notes,
                            const std::vector<NoteProvenance>& provenance,
                            const HarmonicPlan& harmonic_plan) const;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VALIDATOR_H
