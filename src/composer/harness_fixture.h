#ifndef BACH_COMPOSER_HARNESS_FIXTURE_H
#define BACH_COMPOSER_HARNESS_FIXTURE_H

#include <cstdint>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/voice_plan.h"

namespace bach::composer {

// Phase identifier for the harness catalog. Mirrors the
// `TwentySeedHarness.PhaseN` gtest layout one-to-one. Adding a new
// phase requires extending both the runtime case map and the harness
// test, since the closure thresholds live alongside the test.
enum class HarnessPhase : std::uint8_t {
  Phase3 = 0,   // 2 voice, 8 bar, subject-only
  Phase35 = 1,  // 2 voice, 4 bar, subject-only
  Phase4 = 2,   // 2 voice, 8 bar, subject + answer
  Phase5 = 3,   // 3 voice, 12 bar, subject-only
  Phase6 = 4,   // 3 voice, 16 bar, subject + answer + V2 re-entry
};

// Static layout for one harness phase. Catalogs and seed-to-fixture
// derivations are factored out (kSubjectPatterns / kHarmonyPatterns
// live in harness_fixture.cpp) so PhaseSpec is data-only.
struct HarnessPhaseSpec {
  HarnessPhase phase;
  std::uint8_t voices;        // 2 or 3
  std::uint8_t bars;          // total bars
  std::uint8_t subject_bars;  // V0 SubjectCarrier length
  bool with_answer;           // Phase 4+: V1 AnswerCarrier
  bool with_third_entry;      // Phase 6: V2 SubjectCarrier re-entry
};

// Resolve the static layout for a phase. Returns the same struct on
// every call (no allocation).
HarnessPhaseSpec phaseSpec(HarnessPhase phase);

// Compose the (Material, HarmonicPlan, VoicePlan) triple a harness or
// CLI invocation needs to drive Composer. Catalog walk is purely
// seed-driven, so calling buildHarnessFixture(p, s) repeatedly yields
// byte-identical fixtures.
struct HarnessFixture {
  Material material;
  HarmonicPlan harmony;
  VoicePlan voice_plan;
};

HarnessFixture buildHarnessFixture(HarnessPhase phase, int seed);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_HARNESS_FIXTURE_H
