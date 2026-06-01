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
                      // modulation); a distinct entry so it can be targeted
                      // for the InvertibleAt8va bit.
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
                      // post-pass stamps the four texture bits.
  Phase14 = 15        // 3 voice, 42 bar. A single all-technique fugue that
                      // exercises every contrapuntal device in one continuous
                      // layout: exposition (subject + real answer + V2
                      // re-entry), fortspinnung, countersubject, modulation
                      // with secondary dominant / borrowed iv / Picardy
                      // third, non-chord-tone figures, suspension, middle
                      // entry, dominant pedal, diminution, stretto, episode,
                      // a rhythm section (dotted / anacrusis / syncopation /
                      // hemiola / recurrence), a texture/expression plan,
                      // and a coda. Built by a dedicated self-contained
                      // builder (buildPhase14Fixture) so the other fugue
                      // layouts stay byte-identical.
  ,
  Phase15 = 16,  // 1 voice, 8 bar. Solo String Flow (BWV1007 Cello Suite
                 // Prelude style): a single monophonic broken-chord arpeggio
                 // that projects two implicit voices (a recurring low bass
                 // stream and a melodic top stream). Built by a dedicated
                 // self-contained builder (buildPhase15Fixture).
  Phase16 = 17,  // 2 voice, 16 bar. Solo String Arch (BWV1004 Chaconne):
                 // immutable 4-bar ground bass repeated 4x (V1 GroundCarrier)
                 // + 4 variation blocks (V0 VariationCarrier) with roles
                 // Ground/Respond/Propel/Assert and rising texture density.
                 // Self-contained builder (buildPhase16Fixture).
  Phase17 = 18,  // 2 voice, 16 bar. Organ Prelude (free / sectional form, BWV846
                 // C-major style): V0 carries three FigurationCarrier sections of
                 // fast broken-chord / scalar figuration (the third is a cadenza),
                 // V1 supplies a bass with a final sustained dominant pedal that
                 // prepares the following fugue (is_pedal_prep). Self-contained
                 // builder (buildPhase17Fixture).
  Phase18 = 19   // 1 voice, 16 bar. Organ Toccata (4 archetypes). A single
                 // virtuosic voice of continuous C-major scalar-wave figuration
                 // (gate-3-clearing predominantly-stepwise motion). The archetype
                 // (Dramaticus/Perpetuus/Concertato/Sectionalis = seed % 4)
                 // differs only in section structure, not pitch language; all
                 // sections carry character = Severe (compatible with every
                 // archetype). Each section is a ToccataCarrier span; the first
                 // bar of each section is is_section_head. Self-contained builder
                 // (buildPhase18Fixture).
  ,
  Phase19 = 20,  // 2 voice, 16 bar. Organ Chorale Prelude (cantus firmus +
                 // counterpoint). V1 carries the fixed chorale tune (cantus
                 // firmus) as a CantusFirmusCarrier replaying an embellished
                 // line whose bar downbeats equal the immutable skeleton; V0
                 // carries a predominantly-stepwise FigurationCarrier scalar
                 // wave riding ABOVE the CF (the gate-3-clearing figuration).
                 // Self-contained builder (buildPhase19Fixture).
  Phase20 = 21,  // 2 voice, 24 bar. Organ Passacaglia (ground bass + variations +
                 // climax). V1 carries an immutable 8-bar ground bass repeated 3x
                 // (PassacagliaGround); V0 carries one PassacagliaVariation block
                 // per ground cycle, rising density, with the last cycle flagged
                 // is_climax (the registral peak). Each variation bar is a
                 // gate-3-clearing C-minor scalar wave (reuses phase16ScaleUp).
                 // Self-contained builder (buildPhase20Fixture).
  Phase21 = 22,  // 3 voice, 16 bar. Organ Trio Sonata (three INDEPENDENT voices:
                 // V0 = RH/Great high, V1 = LH/Swell mid, V2 = Pedal low). All
                 // three are TrioVoiceCarrier Material lines; each is a
                 // gate-3-clearing C-major scalar wave (reuses phase17ScaleUp) with
                 // a DISTINCT note density / onset rhythm per voice so the new
                 // voice_independence_threshold rule (soft MusicalFail below 0.6)
                 // passes comfortably. Verticals are consonant (upper voices in
                 // thirds/sixths, pedal on chord roots). Self-contained builder
                 // (buildPhase21Fixture).
  Phase22 = 23,  // 1 voice, 16 bar. Organ Fantasia (free sectional, multi-style).
                 // A single voice organized into 4 CONTRASTING FantasiaCarrier
                 // sections (Free sparse low quarters / Fugal mid eighths /
                 // Toccata dense high sixteenths / Chordal mid half-notes). Each
                 // section is a gate-3-clearing C-major scalar wave (reuses
                 // phase17ScaleUp); contrast is achieved via distinct density +
                 // register per section, not via wide leaps, so melodic_interval
                 // nll stays low. The new section_contrast_required rule (a soft
                 // MusicalFail) passes because every adjacent pair differs in
                 // density and/or register. Self-contained builder
                 // (buildPhase22Fixture).
  Phase23 = 24,  // 2 voice, 20 bar. Keyboard suite (5 movements x 4
                 // bars). An assembly reusing existing carriers/bits, adding no
                 // new VoiceIntent or RuleBit: V0 carries five
                 // movement spans (Prelude + Courante = FigurationCarrier;
                 // Allemande / Sarabande / Gigue = FantasiaCarrier of contrasting
                 // Fugal / Chordal / Toccata density), each a gate-3-clearing
                 // C-major scalar wave (phase17ScaleUp). V1 carries a GroundCarrier
                 // bass replaying a 4-bar C-major figure tiled 5x (5 clean cycles,
                 // ground_bass_immutable stays clean). Self-contained builder
                 // (buildPhase23Fixture).
  Phase24 = 25   // 3 voice, 24 bar. WTC-style Prelude+Fugue pair
                 // (8-bar prelude + 16-bar fugue, C major). An assembly reusing
                 // existing carriers/bits, adding no new VoiceIntent or RuleBit.
                 // Prelude (bars 0-7): V0 two
                 // FigurationCarrier sections (sixteenth scalar wave) + V1
                 // FigurationCarrier eighth bass support; the final prelude
                 // figuration section is is_pedal_prep so PedalPreparation links
                 // into the fugue. Fugue (bars 8-23): a compact exposition built
                 // inline with the buildPhase14Fixture scalar subject content -- V0
                 // SubjectCarrier (bars 8-11), V1 AnswerCarrier (real answer -P4,
                 // bars 12-15), V2 SubjectCarrier re-entry (-P8, bars 16-19), V0
                 // SubjectCarrier stretto-leader restatement (bars 20-23).
                 // Self-contained builder (buildPhase24Fixture).
  ,
  Phase25 = 26  // 2 voice, 20 bar. Goldberg-style immutable-bass
                // variation skeleton (aria + 4 variations x 4 bars, C major;
                // a reduced realization of BWV988). An assembly reusing the
                // Phase20 Passacaglia carriers, adding no new VoiceIntent or
                // RuleBit. V1 carries an immutable 4-bar Goldberg-style bass
                // (PassacagliaGround) tiled 5x across all 20 bars
                // (passacaglia_ground_period = 4 * kTicksPerBar; 5 clean cycles
                // keep passacaglia_ground_immutable clean). V0 carries five
                // PassacagliaVariation blocks (Aria + Var1..Var4) of rising
                // density (half/quarter sarabande / quarters / eighths / eighths
                // / sixteenths), each a gate-3-clearing C-major scalar wave
                // (reuses phase17ScaleUp). The last block (Var4) is is_climax
                // (fires ClimaxPlaced). Self-contained builder
                // (buildPhase25Fixture).
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
  // Phase15: dedicated single-voice BWV1007 arpeggio-flow builder. Defaulted
  // so the 17-field positional aggregate initialisers for the fugue layouts
  // above stay valid (this field value-initialises to false for them).
  bool with_arpeggio_flow = false;
  // Phase16: dedicated two-voice BWV1004 chaconne-arch builder. Defaulted so
  // the positional aggregate initialisers for the fugue layouts stay valid
  // (this field value-initialises to false for them).
  bool with_chaconne_arch = false;
  // Phase17: dedicated two-voice organ-prelude builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_organ_prelude = false;
  // Phase18: dedicated single-voice organ-toccata builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_organ_toccata = false;
  // Phase19: dedicated two-voice organ-chorale-prelude builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_organ_chorale = false;
  // Phase20: dedicated two-voice organ-passacaglia builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_organ_passacaglia = false;
  // Phase21: dedicated three-voice organ-trio-sonata builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_trio = false;
  // Phase22: dedicated single-voice organ-fantasia builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_fantasia = false;
  // Phase23: dedicated two-voice keyboard-suite builder. Defaulted so the
  // positional aggregate initialisers for the fugue layouts stay valid (this
  // field value-initialises to false for them).
  bool with_suite = false;
  // Phase24: dedicated three-voice WTC Prelude+Fugue pair builder. Defaulted so
  // the positional aggregate initialisers for the fugue layouts stay valid
  // (this field value-initialises to false for them).
  bool with_wtc_pair = false;
  // Phase25: dedicated two-voice Goldberg-style immutable-bass-variation
  // builder. Defaulted so the positional aggregate initialisers for the fugue
  // layouts stay valid (this field value-initialises to false for them).
  bool with_goldberg = false;
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
