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
  Phase7 = 8,         // Phase6 layout + ChordEvent degree/inversion/function tagging
  Phase8 = 9,         // Phase7 layout + ModulationEvent at midpoint + secondary dominant
  Phase9 = 10,        // Phase8 layout + FortspinnungSpan + ImitationEntry declarations
  Phase10 = 11,       // Phase8 layout (3v/16bar/subject+answer+third-entry +
                      // modulation); distinguished only so closure can
                      // target it for the InvertibleAt8va bit.
  Phase11 = 12,       // 3 voice, 28 bar. Exposition (bars 0-11, subject +
                      // answer + V2 re-entry) followed by an all-Material
                      // development (bars 12-27): middle entry, pedal,
                      // subject variant, stretto, coda.
  Phase12 = 13,       // 3 voice, 28 bar. Phase11-style exposition (bars
                      // 0-11) followed by an all-Material rhythm section
                      // (bars 12-27): dotted figure, anacrusis, syncopated
                      // consequent, hemiola, rhythmic-motif recurrence on a
                      // 4-bar phrase grid.
  Phase13 = 14,       // 3 voice, 16 bar. Phase7-style exposition (subject +
                      // answer + V2 re-entry + degree tagging) overlaid with
                      // a texture/expression plan: per-voice MIDI ranges, an
                      // organ-manual routing, a per-voice articulation span,
                      // an Affekt velocity curve, and a pedal voice. Voice
                      // density already varies (V2 enters at bar 8); the
                      // post-pass stamps the four P13 bits.
  Phase14 = 15        // 3 voice, 42 bar. A single all-technique fugue that
                      // exercises every device P3-P14 in one continuous
                      // layout: exposition (subject + real answer + V2
                      // re-entry), fortspinnung, countersubject, modulation
                      // with secondary dominant / borrowed iv / Picardy
                      // third, non-chord-tone figures, suspension, middle
                      // entry, dominant pedal, diminution, stretto, episode,
                      // a rhythm section (dotted / anacrusis / syncopation /
                      // hemiola / recurrence), a texture/expression plan,
                      // and a coda. Built by a dedicated self-contained
                      // builder (buildPhase14Fixture) so the P3-P13 layouts
                      // stay byte-identical.
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
  bool with_modulation;       // Phase8: ModulationEvent + V/V + borrowed iv + Picardy 3rd
  bool with_fortspinnung;     // Phase9: V0 FortspinnungSpan + SequenceTemplate
  bool with_imitation_entry;  // Phase9: ImitationEntry declaration (subject→answer)
  bool with_development;      // Phase11: 28-bar layout + development carriers
  bool with_rhythm;           // Phase12: 28-bar layout + rhythm/phrase carriers
  bool with_texture;          // Phase13: texture/expression plan + post-pass
  bool with_nct;              // Phase14: dedicated 42-bar all-technique fugue
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
