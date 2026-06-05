#ifndef BACH_COMPOSER_CANDIDATE_H
#define BACH_COMPOSER_CANDIDATE_H

#include <cstdint>

#include "composer/provenance.h"
#include "core/basic_types.h"

namespace bach::composer {

// One proposed pitch placement inside a span.
//
// CandidateSearch enumerates Candidate values, scores them, and either
// commits the top candidate or, if every candidate fails Validator's
// hard constraints, declares the span unsolvable and triggers retry.
//
// Candidates are pure data. No backreferences to legacy state; no
// CollisionResolver coupling.
struct Candidate {
  Tick start_tick = 0;
  Tick duration = 0;
  std::uint8_t pitch = 0;
  float score = 0.0f;
  float shadow_score = 0.0f;
  std::uint8_t shadow_winning_pitch = 0;
  std::uint8_t shadow_winning_pitch_without_markov = 0;
  RuleIdMask satisfied_rules = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_CANDIDATE_H
