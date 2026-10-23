#ifndef BACH_COMPOSER_VALIDATOR_H
#define BACH_COMPOSER_VALIDATOR_H

#include <string>
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
// Rule families, each emitting the rule_id literals in validator.cpp:
//   * Counterpoint, always run: vertical rules (parallel, hidden and
//     anti-parallel perfects, battuta, cross relation, voice crossing,
//     strong-beat, vertical and unprepared dissonance, invertibility, doubling)
//     and linear rules (melodic intervals, leading-tone resolution, consecutive
//     leaps), most of them routed through the finding recorder and its geometry
//     table.
//   * Harmonic-plan rules: cadence voice leading, spacing (opt-in per plan),
//     pivot modulation and secondary-dominant resolution.
//   * Declaration rules, inert unless `material` declares the structure:
//     suspensions, tonal answers, countersubjects, episodes, sequences,
//     imitation and middle entries, stretto, pedal points, doublings, phrase
//     periodicity, anacrusis, voice and pedal ranges, ornament expansions.
//   * Form idiom rules (form_idiom_rules.h), checked last.
//
// Validator does not mutate notes. On failure it reports the offending
// span; the caller decides whether to re-generate, back-jump, or abort.
//
// `material` may be left default-constructed, which leaves every declaration
// rule inert.
class Validator {
 public:
  ValidationReport validate(const std::vector<NoteEvent>& notes,
                            const std::vector<NoteProvenance>& provenance,
                            const HarmonicPlan& harmonic_plan,
                            const Material& material = Material{},
                            ValidationScope scope = ValidationScope::Generation) const;
};

/**
 * @brief Look up the geometry of a counterpoint rule.
 *
 * The classification lives in one hand-written table in validator.cpp; the
 * number of operands a rule reports is not a usable proxy for it (two notes of
 * one voice and two notes of two voices are indistinguishable at the call
 * site).
 *
 * @param rule_id Stable rule token as carried by ValidationFailure::rule_id.
 * @return The rule's geometry, or RuleGeometry::Unclassified when the table
 *         has no entry for `rule_id`.
 */
RuleGeometry counterpointRuleGeometry(const std::string& rule_id);

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
