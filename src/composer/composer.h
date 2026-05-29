#ifndef BACH_COMPOSER_COMPOSER_H
#define BACH_COMPOSER_COMPOSER_H

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
struct ComposeResult {
  std::vector<NoteEvent> notes;
  std::vector<NoteProvenance> provenance;
  std::vector<Track> tracks;
  ValidationReport validation;
};

// Top-level pipeline. Five steps: Material -> Plan -> Compose ->
// Validate -> Render, executed by run().
//
// No retries are wired up at the Phase 2 stage — if Validator reports
// FailedSpan, the result is returned as-is so the caller can inspect.
// Span re-generation and back-jumping land in Phase 3 when the search
// rule set is richer than two checks.
class Composer {
 public:
  ComposeResult run(const Material& material, const HarmonicPlan& harmonic_plan,
                    const VoicePlan& voice_plan) const;
};

// Sentinel kept for the original composer_skeleton_test compatibility.
bool isComposerLibLinked();

}  // namespace bach::composer

#endif  // BACH_COMPOSER_COMPOSER_H
