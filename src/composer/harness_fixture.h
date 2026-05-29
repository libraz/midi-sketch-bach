#ifndef BACH_COMPOSER_HARNESS_FIXTURE_H
#define BACH_COMPOSER_HARNESS_FIXTURE_H

#include <cstdint>

#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "composer/voice_plan.h"

namespace bach::composer {

// Phase identifier for the harness catalog. The Python closure harness
// drives these layouts through bach_cli.
enum class HarnessPhase : std::uint8_t {
  Phase3 = 0,         // 2 voice, 8 bar, subject-only
  Phase35 = 1,        // 2 voice, 4 bar, subject-only
  Phase4 = 2,         // 2 voice, 8 bar, subject + answer
  Phase5 = 3,         // 3 voice, 12 bar, subject-only
  Phase6 = 4,         // 3 voice, 16 bar, subject + answer + V2 re-entry
  Phase4Sus = 5,      // Phase4 layout + one Sus7_6 carrier span in V0
  Phase6Episode = 6,  // Phase6 layout + V0 Episode span (Original transform)
  Phase6Tonal = 7,    // Phase6 layout + tonal_answer + V0 CountersubjectCarrier
  Phase7 = 8          // Phase6 layout + ChordEvent degree/inversion/function tagging
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
  bool with_suspension;       // Phase4Sus: V1 suspension carrier injected
  bool with_episode;          // Phase6Episode: V0 Episode span at bars [bars-4, bars)
  bool with_tonal_answer;     // Phase6Tonal: AnswerCarrier reads from tonal_answer + V0 CS
  bool with_degree_tagging;   // Phase7: ChordEvent degree/inversion/function populated
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
