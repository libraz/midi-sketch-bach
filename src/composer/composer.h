#ifndef BACH_COMPOSER_COMPOSER_H
#define BACH_COMPOSER_COMPOSER_H

#include <cstddef>
#include <vector>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/provenance.h"
#include "composer/validation.h"
#include "composer/voice_plan.h"
#include "core/basic_types.h"

namespace bach::composer {

// Final pipeline output. The note list and provenance list are
// parallel: note[i] is produced by provenance[i].
//
// `saturated_positions` counts Compose positions that exhausted all
// candidates and therefore emitted no note (a rest). Carrier spans replay
// Material verbatim and never saturate, so this only ever reflects free
// Compose voices. A non-zero count is NOT a validation failure (resting is
// the correct no-fallback response to "no consonant candidate" — better a
// rest than a wrong note); it is a quality/density signal for in-process
// callers of Composer::run only (the CLI, C API and JSON output do not
// expose it).
struct ComposeResult {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  std::vector<Track> tracks;
  ValidationReport validation;
  std::size_t saturated_positions = 0;
};

// Top-level pipeline. Five steps: Material -> Plan -> Compose ->
// Validate -> Render, executed by run().
//
// No-fallback principle: a Compose position that exhausts all candidates
// emits no note (a rest) rather than a default/fallback pitch. run() makes
// these holes countable in ComposeResult::saturated_positions
// (CandidateSearch::enumerate reports the per-span count) for in-process
// callers. Resting is not itself a failure, so run()
// does NOT set ValidationStatus::FailedSeed for it; FailedSeed stays reserved
// for the future span re-generation / back-jump / seed-retry loop described in
// validation.h, which is NOT implemented yet (tracked future work).
class Composer {
 public:
  ComposeResult run(const Material& material, const HarmonicPlan& harmonic_plan,
                    const VoicePlan& voice_plan) const;
};

// Sentinel kept for the original composer_skeleton_test compatibility.
bool isComposerLibLinked();

}  // namespace bach::composer

#endif  // BACH_COMPOSER_COMPOSER_H
