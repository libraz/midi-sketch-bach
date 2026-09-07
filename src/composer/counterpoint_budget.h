#ifndef BACH_COMPOSER_COUNTERPOINT_BUDGET_H
#define BACH_COMPOSER_COUNTERPOINT_BUDGET_H

#include <cstddef>
#include <string>

#include "composer/validation.h"
#include "core/basic_types.h"

namespace bach::composer {

// One (form, vertical rule) pair the form does not yet satisfy.
//
// A vertical rule describes a relation between voices sounding together. The
// composer chose that alignment even when it chose none of the pitches, so the
// finding is never exempted by operand immutability -- but the shipped forms do
// not all satisfy every vertical rule today, and routing every match straight
// to `failures` would stop generation for most of the catalogue at once.
//
// The open list is what makes the closure incremental. A vertical rule with no
// row for a form is CLOSED for that form: the first match fails the piece. A
// rule listed here is measured and exported but never fails, so it can be
// repaired before it is closed. Closing a rule for a form means deleting its
// row, so the table shrinks as the catalogue improves and is empty when the
// gate is fully closed.
//
// Rows may only be REMOVED. Adding one re-opens a rule that a form had already
// stopped breaking, which is a regression; it needs its own argument rather
// than being folded into another change.
//
// Each row also says WHY it is open, because a rule nobody has repaired yet and
// a rule whose repair was measured and refused are different states that a bare
// list cannot tell apart. Re-labelling a row `OpenReason::Unresolved` ->
// `OpenReason::Accepted` is allowed only together with a reason recorded at the
// row: the musical condition that asks for the fault, or the cost the cheapest
// available repair charges the piece for removing it. Re-labelling the other
// way needs nothing -- deciding to look for a cheaper repair is free.
//
// Absence is the closed state on purpose: a vertical rule added later, or a
// form added later, starts fully gated and has to earn each exception, rather
// than shipping silently ungated because nobody remembered to list it.

// Why a form is still allowed to break one vertical rule.
//
// Both values describe an OPEN row and neither fails a piece: the gate reads
// only whether a row exists. The reason separates the rows that are work from
// the rows that are decisions.
enum class OpenReason {
  // The form breaks the rule and should stop. Outstanding work: the row is a
  // debt against the closure, and repairing the form deletes it.
  Unresolved,
  // Settled. Either the writing itself asks for the fault, or the cheapest
  // repair on offer costs the piece more than the fault does. The row stays
  // until someone shows a repair that does not.
  Accepted,
};

struct CounterpointBudgetEntry {
  FormType form;
  const char* rule_id;
  // Defaulted to the debt: a row added without an argument is outstanding work,
  // never an acceptance.
  OpenReason reason = OpenReason::Unresolved;
};

/**
 * @brief Whether a form is still allowed to break one vertical rule.
 *
 * Linear rules are not gated here: a melodic finding whose every note is
 * replayed verbatim from declared material is exempted at the finding recorder,
 * so gating it would describe nothing the composer controls.
 *
 * @param form The form that produced the notes.
 * @param rule_id Stable rule token as carried by ValidationFailure::rule_id.
 * @return True when the rule is vertical and closed for this form, so a match
 *         must fail the piece. False for a rule that is still open, and for
 *         every non-vertical rule.
 */
bool counterpointRuleIsClosed(FormType form, const std::string& rule_id);

/**
 * @brief Fail a report for every vertical rule that is closed for the form.
 *
 * Reads `report->observations`, which the validator populates before any
 * routing decision, so a match suppressed as informational is still seen. For
 * each closed rule that matched at least once, appends one failure naming it.
 * Leaves `status` alone -- the caller owns the `failures`-empty check.
 *
 * @param form The form that produced the notes the report describes.
 * @param report The FinalScore report to gate. Ignored when null.
 */
void applyCounterpointBudget(FormType form, ValidationReport* report);

/**
 * @brief The whole open list, for the regression that holds it tight.
 *
 * @param count Receives the number of entries. Ignored when null.
 * @return Pointer to the first entry; entries are sorted by form ordinal and
 *         then by rule_id.
 */
const CounterpointBudgetEntry* counterpointBudgetTable(std::size_t* count);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_COUNTERPOINT_BUDGET_H
