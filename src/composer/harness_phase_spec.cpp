#include "composer/harness_fixture.h"

namespace bach::composer {

HarnessPhaseSpec phaseSpec(HarnessPhase phase) {
  switch (phase) {
    case HarnessPhase::FugueSubject2v:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/8,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::FugueSubject2vShort:
      return {phase, /*voices=*/2, /*bars=*/4, /*subject_bars=*/4,
              false, false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::FugueAnswer2v:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        false,      false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::FugueSubject3v:
      return {phase, /*voices=*/3, /*bars=*/12, /*subject_bars=*/12,
              false, false,        false,       false,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueExposition3v:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueAnswerSuspension:
      return {phase, /*voices=*/2, /*bars=*/8, /*subject_bars=*/4,
              true,  false,        true,       false,
              false, false,        false,      false,
              false, false,        false,      false,
              false};
    case HarnessPhase::FugueExpositionEpisode:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       true,
              false, false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueExpositionTonalAnswer:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              true,  false,        false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueHarmonized:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueModulating:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueFortspinnung:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        true,
              true,  false,        false,       false,
              false};
    case HarnessPhase::FugueThirdEntry:
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         true,        false,
              false, false,        false,       false,
              false};
    case HarnessPhase::FugueDevelopment:
      // 28 bar / 3 voice. Material assembly reuses with_answer +
      // with_third_entry (subject 0-3, answer 4-7, V2 re-entry 8-11);
      // with_development drives the bars 12-27 carriers and its own
      // voice plan. Degree tagging is on (as in FugueHarmonized) so the
      // strong-4th candidate pre-filter — gated on chord.has_degree —
      // stays active for the exposition's Compose counterlines; without
      // it the composer would pick a strong-beat perfect 4th in the
      // (V0, V1) upper pair and trip fourth_only_on_weak_beat. Modulation
      // stays off (no chromatic idioms); the all-Material development
      // needs no degree/modulation help and the provenance only needs the
      // development bits.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, true,         false,       false,
              false};
    case HarnessPhase::FugueRhythmic:
      // 28 bar / 3 voice. Same exposition assembly as FugueDevelopment (with_answer
      // + with_third_entry + degree tagging for the strong-4th
      // pre-filter), but with_rhythm drives the bars 12-27 rhythm section
      // and its own voice plan instead of with_development.
      return {phase, /*voices=*/3, /*bars=*/28, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        true,        false,
              false};
    case HarnessPhase::FugueTextured:
      // 16 bar / 3 voice. Reuses the FugueHarmonized exposition assembly (with_answer
      // + with_third_entry + degree tagging for the strong-4th
      // pre-filter); with_texture attaches the texture/expression plan that
      // the Composer's post-pass consumes. Voice density already varies
      // because V2 enters only at bar 8 (2 voices bars 0-7, 3 voices bars
      // 8-15). Modulation stays off so the scored content matches the clean
      // FugueHarmonized exposition.
      return {phase, /*voices=*/3, /*bars=*/16, /*subject_bars=*/4,
              true,  true,         false,       false,
              false, true,         false,       false,
              false, false,        false,       true,
              false};
    case HarnessPhase::FugueComplete:
      // 42 bar / 3 voice. All thirteen device flags true. A dedicated
      // self-contained builder (buildFugueCompleteFixture) constructs the whole
      // fixture when with_nct is set, so the other fugue layouts above stay
      // byte-identical (this spec's flags only gate the dispatch).
      return {phase, /*voices=*/3, /*bars=*/42, /*subject_bars=*/4,
              true,  true,         true,        true,
              true,  true,         true,        true,
              true,  true,         true,        true,
              true};
    case HarnessPhase::CelloPrelude:
      // 8 bar / 1 voice. Solo String Flow. No subject/answer; the whole piece
      // is a single ArpeggioFlow span built by buildCelloPreludeFixture. Every
      // device flag is false; with_arpeggio_flow (the trailing defaulted
      // field) is set true to route the dispatch.
      return {phase,      /*voices=*/1,
              /*bars=*/8, /*subject_bars=*/0,
              false,      false,
              false,      false,
              false,      false,
              false,      false,
              false,      false,
              false,      false,
              false,      /*with_arpeggio_flow=*/true};
    case HarnessPhase::Chaconne:
      // 16 bar / 2 voice. Solo String Arch (BWV1004 Chaconne). No
      // subject/answer; an immutable ground bass (V1 GroundCarrier) underpins
      // four variation blocks (V0 VariationCarrier) built by
      // buildChaconneFixture. Every device flag is false; with_chaconne_arch
      // (the second trailing defaulted field) is set true to route the
      // dispatch.
      return {phase,
              /*voices=*/2,
              /*bars=*/16,
              /*subject_bars=*/0,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/true};
    case HarnessPhase::OrganPrelude:
      // 16 bar / 2 voice. Organ Prelude (free / sectional form). No
      // subject/answer; V0 carries three FigurationCarrier sections (the third
      // a cadenza) and V1 carries a bass-support section plus a final
      // dominant-pedal-prep section, all built by buildOrganPreludeFixture. Every
      // device flag is false; with_organ_prelude (the trailing defaulted field)
      // is set true to route the dispatch.
      return {phase,
              /*voices=*/2,
              /*bars=*/16,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/true};
    case HarnessPhase::OrganToccata:
      // 16 bar / 1 voice. Organ Toccata (4 archetypes). No subject/answer; a
      // single V0 carries one-or-more ToccataCarrier sections of continuous
      // C-major scalar-wave figuration, all built by buildOrganToccataFixture. The
      // archetype (= seed % 4) selects the section layout. Every device flag is
      // false; with_organ_toccata (the trailing defaulted field) is set true to
      // route the dispatch.
      return {phase,
              /*voices=*/1,
              /*bars=*/16,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/true};
    case HarnessPhase::ChoralePrelude:
      // 16 bar / 2 voice. Organ Chorale Prelude (cantus firmus + counterpoint).
      // No subject/answer; V1 carries the fixed chorale tune as a
      // CantusFirmusCarrier (embellished, downbeats == immutable skeleton) and
      // V0 carries a predominantly-stepwise FigurationCarrier scalar wave riding
      // above it, all built by buildChoralePreludeFixture. Every device flag is false;
      // with_organ_chorale (the trailing defaulted field) is set true to route
      // the dispatch.
      return {phase,
              /*voices=*/2,
              /*bars=*/16,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/true};
    case HarnessPhase::Passacaglia:
      // 24 bar / 2 voice. Organ Passacaglia (ground bass + variations + climax).
      // No subject/answer; V1 carries an immutable 8-bar ground bass repeated 3x
      // (PassacagliaGround) and V0 carries one PassacagliaVariation block per
      // cycle (rising density, the last cycle is_climax), all built by
      // buildPassacagliaFixture. Every device flag is false; with_organ_passacaglia
      // (the trailing defaulted field) is set true to route the dispatch.
      return {phase,
              /*voices=*/2,
              /*bars=*/24,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/true};
    case HarnessPhase::TrioSonata:
      // 16 bar / 3 voice. Organ Trio Sonata (three independent voices). No
      // subject/answer; V0/V1/V2 each carry a TrioVoiceCarrier scalar-wave line
      // of distinct density (sixteenths / eighths / quarters), built by
      // buildTrioSonataFixture. Every device flag is false; with_trio (the trailing
      // defaulted field) is set true to route the dispatch.
      return {phase,
              /*voices=*/3,
              /*bars=*/16,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/false,
              /*with_trio=*/true};
    case HarnessPhase::Fantasia:
      // 16 bar / 1 voice. Organ Fantasia (free sectional, multi-style). No
      // subject/answer; a single V0 carries four contrasting FantasiaCarrier
      // sections (Free / Fugal / Toccata / Chordal) of distinct density +
      // register, built by buildFantasiaFixture. Every device flag is false;
      // with_fantasia (the trailing defaulted field) is set true to route the
      // dispatch.
      return {phase,
              /*voices=*/1,
              /*bars=*/16,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/false,
              /*with_trio=*/false,
              /*with_fantasia=*/true};
    case HarnessPhase::KeyboardSuite:
      // 20 bar / 2 voice. Keyboard suite (5 movements x 4 bars). No
      // subject/answer; V0 carries five movement spans (FigurationCarrier for the
      // Prelude + Courante, FantasiaCarrier for the Allemande / Sarabande / Gigue)
      // and V1 carries a GroundCarrier bass tiled 5x, all built by
      // buildKeyboardSuiteFixture. Reuses existing carriers/bits, adding no new
      // VoiceIntent or RuleBit. Every device flag is false; with_suite (the
      // trailing defaulted field) is set true to route the dispatch.
      return {phase,
              /*voices=*/2,
              /*bars=*/20,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/false,
              /*with_trio=*/false,
              /*with_fantasia=*/false,
              /*with_suite=*/true};
    case HarnessPhase::PreludeAndFugue:
      // 24 bar / 3 voice. WTC Prelude+Fugue pair (8-bar prelude +
      // 16-bar fugue). No subject/answer via the generic cascade; the prelude's
      // FigurationCarrier sections (V0 + V1) and the fugue's inline exposition
      // (SubjectCarrier / AnswerCarrier) are all built by buildPreludeAndFugueFixture.
      // Reuses existing carriers/bits, adding no new VoiceIntent or RuleBit.
      // Every device flag is false; with_wtc_pair (the trailing defaulted field)
      // is set true to route the dispatch.
      return {phase,
              /*voices=*/3,
              /*bars=*/24,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/false,
              /*with_trio=*/false,
              /*with_fantasia=*/false,
              /*with_suite=*/false,
              /*with_wtc_pair=*/true};
    case HarnessPhase::GoldbergVariations:
      // 20 bar / 3 voice. Dedicated Goldberg immutable-bass-variation
      // skeleton (aria + 4 variations x 4 bars). No subject/answer; V0 carries
      // five GoldbergVariation blocks and V2 carries a Goldberg aria-bass outline
      // tiled 5x, all built by buildGoldbergVariationsFixture. Every device flag is
      // false; with_goldberg (the trailing defaulted field) is set true to route
      // the dispatch.
      return {phase,
              /*voices=*/3,
              /*bars=*/20,
              /*subject_bars=*/0,
              /*with_answer=*/false,
              /*with_third_entry=*/false,
              /*with_suspension=*/false,
              /*with_episode=*/false,
              /*with_tonal_answer=*/false,
              /*with_degree_tagging=*/false,
              /*with_modulation=*/false,
              /*with_fortspinnung=*/false,
              /*with_imitation_entry=*/false,
              /*with_development=*/false,
              /*with_rhythm=*/false,
              /*with_texture=*/false,
              /*with_nct=*/false,
              /*with_arpeggio_flow=*/false,
              /*with_chaconne_arch=*/false,
              /*with_organ_prelude=*/false,
              /*with_organ_toccata=*/false,
              /*with_organ_chorale=*/false,
              /*with_organ_passacaglia=*/false,
              /*with_trio=*/false,
              /*with_fantasia=*/false,
              /*with_suite=*/false,
              /*with_wtc_pair=*/false,
              /*with_goldberg=*/true};
  }
  return {phase, 2,     8,     8,     false, false, false, false, false,
          false, false, false, false, false, false, false, false};
}

}  // namespace bach::composer
