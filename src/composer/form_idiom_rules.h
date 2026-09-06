#ifndef BACH_COMPOSER_FORM_IDIOM_RULES_H
#define BACH_COMPOSER_FORM_IDIOM_RULES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "composer/voice_onset_index.h"
#include "core/basic_types.h"

namespace bach::composer {

/**
 * @brief Everything the form idiom rules read, bundled as a read-only view.
 *
 * The references alias the caller's score for the duration of one
 * checkFormIdiomRules call; the context owns nothing and outlives nothing.
 */
struct FormIdiomContext {
  const std::vector<NoteEvent>& notes;
  const std::vector<NoteProvenance>& provenance;
  const HarmonicPlan& harmonic_plan;
  const Material& material;
  const VoiceOnsetIndex& onset_index;
  /// Meter-derived bar length (HarmonicPlan::ticksPerBar()).
  Tick ticks_per_bar;
};

/**
 * @brief Triad pitch classes (root, third, fifth) of a chord, reduced mod 12.
 *
 * Seventh qualities collapse to their underlying triad; the seventh is not
 * part of the returned set.
 */
std::array<std::uint8_t, 3> triadFor(const ChordEvent& chord);

/**
 * @brief True iff the provenance entry at `index` carries `bit`.
 *
 * An index past the end of `provenance` carries no bit, so an unaligned or
 * absent provenance list simply makes every bit-gated rule inert.
 */
bool hasRuleBit(const std::vector<NoteProvenance>& provenance, std::size_t index, RuleBit bit);

/**
 * @brief Check the idiomatic conditions each individual form must satisfy.
 *
 * These are not the counterpoint rules every voice obeys; they are the
 * conditions that make a piece recognisable as its own form. The implicit
 * voices of a solo-string arpeggio must behave like real voices; an immutable
 * ground (chaconne, passacaglia, Goldberg aria bass) must recur unaltered; an
 * organ-prelude figuration must outline the harmony it decorates; a toccata
 * archetype must suit the affect it is paired with; a chorale prelude's cantus
 * firmus must survive embellishment; a trio sonata's three voices must stay
 * independent; a fantasia's adjacent sections must contrast; a fugue's
 * countersubject is measured for invertibility at the octave.
 *
 * Findings are appended to `out_report` in rule order: a MusicalFail or
 * StructuralFail for the gating rules, and an informational entry for
 * countersubject invertibility, which is measured but never gates.
 *
 * @param context Read-only view of the score, its provenance and its plan.
 * @param out_report Report the findings are appended to; must not be null.
 */
void checkFormIdiomRules(const FormIdiomContext& context, ValidationReport* out_report);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_FORM_IDIOM_RULES_H
