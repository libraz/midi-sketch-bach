#ifndef BACH_COMPOSER_FREE_COUNTERPOINT_SEARCH_H
#define BACH_COMPOSER_FREE_COUNTERPOINT_SEARCH_H

#include <cstddef>
#include <vector>

#include "composer/candidate.h"
#include "composer/candidate_search.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/span.h"

namespace bach::composer {

/**
 * @brief Generate a Compose span's notes by scored free-counterpoint search.
 *
 * Lays down one note per stride position across the span, enumerating diatonic
 * pitches around the voice center, filtering them through the melodic and
 * vertical rule cascade, and ranking the survivors with corpus-Gaussian and
 * Markov melodic scoring plus local rule adjustments.
 *
 * This is not the default generation path. Every shipped form emits only
 * carrier spans, so `CandidateSearch::enumerate` returns from one of its
 * verbatim-replay branches and never reaches this function. It is reached only
 * when `ComposeRequest::enable_free_counterpoint` is set (`bach_cli
 * --free-counterpoint`), which reclassifies a trio's accompanimental inner
 * voice from carrier replay to this search. That option is a measurement knob:
 * it lowers model probability and trips validation on many seeds, so the
 * carrier-assembly default remains the quality path.
 *
 * @param span Compose span to fill; positions run from `start_tick` to
 *   `end_tick` at the stride implied by `span.subdivision`.
 * @param harmonic_plan Chord and modulation context used for both filtering
 *   and provenance-bit accrual.
 * @param material Supplies the cadence cells whose approach/cadence pitch
 *   classes are forced at the ticks they touch.
 * @param context Per-voice cursor (previous pitches, center, placed notes of
 *   the already-committed voices) carried across span boundaries.
 * @param saturated_positions If non-null, incremented once per position that
 *   exhausted with no admissible candidate (a silent hole). The caller
 *   escalates a non-zero count to ValidationStatus::FailedSeed.
 * @return Candidates in tick order, one per position that yielded a pitch.
 */
std::vector<Candidate> composeFreeSpan(const Span& span, const HarmonicPlan& harmonic_plan,
                                       const Material& material, const CandidateContext& context,
                                       std::size_t* saturated_positions);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FREE_COUNTERPOINT_SEARCH_H
