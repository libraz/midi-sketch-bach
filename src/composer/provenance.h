#ifndef BACH_COMPOSER_PROVENANCE_H
#define BACH_COMPOSER_PROVENANCE_H

#include <cstdint>

#include "composer/span.h"
#include "composer/voice_intent.h"

namespace bach::composer {

// Where a note's pitch and duration came from.
//
// Material  = input motif (subject, answer, fixed counterline). Pitches
//             and durations are inputs, not search outputs. CandidateSearch
//             does not enumerate alternatives for Material notes.
// Compose   = chosen by CandidateSearch from the candidate set. Every
//             Compose note is reproducible from (span_id, candidate_score,
//             satisfied_rules).
//
// No other values are permitted. In particular there is no "Repair" or
// "Snap" source; the composer pipeline does not run post-generation
// pitch-modifying passes.
enum class NoteSource : std::uint8_t {
  Material = 0,
  Compose = 1,
};

// Bitset over rule IDs. Each bit position is a CandidateSearch rule that
// the chosen candidate satisfies. Provides MUS-ROVER-style interpretable
// rule traces: a downstream analyzer can read this back to explain why
// the candidate was admitted.
//
// 64 bits is a soft cap on rule count. If the rule set outgrows that,
// widen to std::array<uint64_t, N> before changing the JSON schema.
using RuleIdMask = std::uint64_t;

enum RuleBit : std::uint8_t {
  ChordTone = 0,
  StrongBeatConsonance = 1,
  SmallStep = 2,
  ParallelPerfectChecked = 3,
  VoiceCrossingChecked = 4,
  LeapResolutionChecked = 5,
  WeakBeatPassingChecked = 6,
  VerticalConsonanceChecked = 7,
  LeadingToneResolved = 8,
  HiddenParallelChecked = 9,
  CrossRelationChecked = 10,
  CadenceCellCommitted = 11,
  CadenceVoiceLeadingChecked = 12,
  // Suspension + non-chord-tone recognition.
  SuspensionPrepared = 13,
  SuspensionResolved = 14,
  CambiataDetected = 15,
  EchappeeDetected = 16,
  AnticipationDetected = 17,
  NotaCambiataDetected = 18,
  // Episode = motif transform derived.
  EpisodeMotifSourced = 19,
  // Tonal answer + Countersubject.
  TonalAnswerMapped = 20,
  CountersubjectActive = 21,
  // Functional harmony: degree/inversion/doubling/spacing.
  // ChordToneRoman: the candidate's pitch class belongs to the
  //   triad/seventh chord declared by ChordEvent.degree+quality.
  // InversionLabel: the active ChordEvent has has_degree=true and
  //   the bass voice's pitch class matches ChordEvent.inversion's
  //   chord factor (root/third/fifth/seventh).
  // DoublingChecked: the candidate participated in a doubling
  //   verification at its tick (no leading-tone double, no 7th
  //   double).
  // SpacingChecked: the candidate participated in spacing
  //   verification (upper-voice intervals stayed within one octave).
  ChordToneRoman = 22,
  InversionLabel = 23,
  DoublingChecked = 24,
  SpacingChecked = 25,
  // Modulation / Tonicization / Borrowed chords.
  // ModulationCommitted: the candidate sits at-or-after a
  //   HarmonicPlan::ModulationEvent tick and its pitch class is
  //   diatonic in the new key area.
  // SecondaryDominantResolved: the candidate's chord follows a
  //   has_secondary_of=true chord and its degree matches the
  //   secondary_of value (i.e. the secondary dominant resolved as
  //   declared).
  // PicardyThird: the candidate is the major third of a final tonic
  //   chord in a minor-mode piece (is_picardy=true on the chord).
  // ModalMixture: the candidate's chord carries is_borrowed=true
  //   (loan from the parallel mode).
  ModulationCommitted = 26,
  SecondaryDominantResolved = 27,
  PicardyThird = 28,
  ModalMixture = 29,
  // Fortspinnung + Sequence + Imitation.
  // FortspinnungSourced: the candidate's pitch and tick came from a
  //   SequenceTemplate replay (not free-search and not Episode replay).
  //   Set by CandidateSearch on every note of a FortspinnungSpan.
  // SequenceStep: the candidate sits at-or-after a SequenceTemplate
  //   step boundary AND its pitch matches the seed motif transposed by
  //   the pattern's step semitone offset.
  // ImitationEntryMatched: the candidate participates in a declared
  //   ImitationEntry (leader voice's first note tick, or follower
  //   voice's first note tick at leader.tick + distance with the
  //   declared interval offset).
  FortspinnungSourced = 30,
  SequenceStep = 31,
  ImitationEntryMatched = 32,
  // Invertible counterpoint at the octave.
  // InvertibleAt8va: the Compose candidate, against the sounding
  //   upper-adjacent voice at this tick, does NOT form a perfect 4th
  //   on a strong beat (a 4th inverts to a 5th under octave inversion),
  //   i.e. the candidate passed the invertibility check. Only set when
  //   placed_notes is available and the pair is an upper-adjacent pair.
  InvertibleAt8va = 33,
  // Fugue development section. Each bit is stamped by CandidateSearch on
  // the verbatim-replayed notes of the matching development carrier intent,
  // so the provenance records that the development device actually shipped
  // (not just that the intent was planned).
  // MiddleEntryCommitted: notes of a MiddleEntryCarrier span (subject
  //   restated in a related key V/vi/IV/ii).
  // StrettoCommitted: notes of a StrettoCarrier span (overlapping
  //   follower entry).
  // PedalCommitted: the held note of a PedalCarrier span (tonic or
  //   dominant pedal point).
  // CodaCommitted: notes of a CodaCarrier span (post-cadence closing
  //   extension).
  // SubjectVariantApplied: notes of a SubjectCarrier{Augmented,
  //   Diminished,Inverted} span (subject under a motif transform).
  MiddleEntryCommitted = 34,
  StrettoCommitted = 35,
  PedalCommitted = 36,
  CodaCommitted = 37,
  SubjectVariantApplied = 38,
  // Rhythm / meter / phrase. Stamped by CandidateSearch on the
  // verbatim-replayed notes of a RhythmCarrier span.
  // AnacrusisActive: notes of an anacrusis (upbeat) rhythm fragment.
  // HemiolaInserted: notes of a hemiola regrouping at a cadence approach.
  // PhrasePeriodicityKept: a carrier note whose onset lands on a declared
  //   phrase start (confirms the 4/8-bar phrase grid).
  // RhythmicMotifRecurrence: notes of a fragment that restates a rhythmic
  //   motif in another voice.
  AnacrusisActive = 39,
  HemiolaInserted = 40,
  PhrasePeriodicityKept = 41,
  RhythmicMotifRecurrence = 42,
  // Texture / instrument / expression. Stamped by the Composer's
  // texture-expression post-pass (composer.cpp), not by CandidateSearch:
  // these are render-time attributes derived from Material::texture_plan,
  // applied to every emitted note after candidate placement and before
  // validation. Each bit records that the corresponding device actually
  // shipped.
  // VoiceRangeKept: the note's pitch lies inside the declared MIDI range
  //   for its voice (Material::texture_plan.voice_ranges). The negative
  //   counterpart is the Validator's voice_range_integrity rule.
  // ManualAssigned: the note's voice has an OrganManual routing
  //   (Material::texture_plan.manual_assignments).
  // ArticulationApplied: an articulation span covers the note's voice and
  //   onset (Material::texture_plan.articulations).
  // AffektCurveApplied: the note received an Affekt-driven velocity from
  //   the active velocity curve (Material::texture_plan.affekt_curve_active).
  VoiceRangeKept = 43,
  ManualAssigned = 44,
  ArticulationApplied = 45,
  AffektCurveApplied = 46,
  // Solo String Flow (BWV1007). Stamped by CandidateSearch on every
  // ArpeggioFlow carrier note.
  // ArpeggioFlowActive: the note belongs to a broken-chord arpeggio replayed
  //   from Material::arpeggio_template (the negative counterpart is the
  //   Validator's arpeggio_no_parallel_perfect rule).
  // ImplicitVoiceTracked: the note's positional implicit-voice membership
  //   (bass / inner / top stream within its arpeggio cell) is established, so
  //   the Validator's implicit_voice_counterpoint rule covers it.
  ArpeggioFlowActive = 47,
  ImplicitVoiceTracked = 48,
  // Solo String Arch (BWV1004 Chaconne). Stamped by CandidateSearch on
  // the verbatim-replayed carrier notes.
  // GroundBassReplayed: a note of a GroundCarrier span (one statement of the
  //   immutable ground bass; the negative counterpart is the Validator's
  //   ground_bass_immutable rule).
  // VariationRoleApplied: a note of a VariationCarrier span (it belongs to a
  //   variation that carries a VariationRole).
  // TextureDensityShift: the first note of a variation whose density tier
  //   differs from the immediately preceding variation (confirms the
  //   texture-density progression across variations actually shipped).
  GroundBassReplayed = 49,
  VariationRoleApplied = 50,
  TextureDensityShift = 51,
  // Organ Prelude (free sectional form). Stamped by CandidateSearch on
  // FigurationCarrier notes.
  // FigurationCommitted: a note of a figuration section (fast broken-chord /
  //   scale run); negative counterpart is the Validator's
  //   figuration_harmonic_consistency rule.
  // CadenzaApplied: a note of a section flagged is_cadenza (free figuration
  //   before the final cadence).
  // PedalPreparation: a note of a section flagged is_pedal_prep (a sustained
  //   dominant pedal preparing the following fugue).
  FigurationCommitted = 52,
  CadenzaApplied = 53,
  PedalPreparation = 54,
  // Organ Toccata (4 archetypes). Stamped by CandidateSearch on
  // ToccataCarrier notes.
  // ToccataArchetypeApplied: a note of a toccata section (one of the 4
  //   archetypes Dramaticus/Perpetuus/Concertato/Sectionalis).
  // SectionTransition: the first note of a section whose archetype-driven
  //   section block changes (confirms the sectional layout shipped); negative
  //   counterpart context is the Validator's toccata_archetype_compatible rule.
  ToccataArchetypeApplied = 55,
  SectionTransition = 56,
  // Organ Chorale Prelude. Stamped by CandidateSearch on
  // CantusFirmusCarrier notes.
  // CantusFirmusReplayed: a note of the cantus firmus carrier (the fixed
  //   chorale tune); negative counterpart is cantus_firmus_immutable.
  // CFEmbellishmentApplied: the CF carrier replayed an embellished (ornamented)
  //   cantus firmus rather than the plain skeleton.
  CantusFirmusReplayed = 57,
  CFEmbellishmentApplied = 58,
  // Organ Passacaglia. Stamped by CandidateSearch.
  // PassacagliaGroundReplayed: a note of the immutable 8-bar passacaglia ground
  //   (negative counterpart: passacaglia_ground_immutable).
  // VariationApplied: a note of a passacaglia variation block.
  // ClimaxPlaced: a note of a variation block flagged is_climax (the dynamic /
  //   registral peak placed mid- and end-piece).
  PassacagliaGroundReplayed = 59,
  VariationApplied = 60,
  ClimaxPlaced = 61,
  // Organ Trio Sonata. Stamped by CandidateSearch on every note of a
  // TrioVoiceCarrier span.
  // TrioVoiceIndependent: a note of one of the (up to three) independent trio
  //   voices. The Validator's voice_independence_threshold rule collects every
  //   note carrying this bit, groups them by voice, and soft-fails (MusicalFail)
  //   when the pairwise voice-independence score falls below 0.6.
  TrioVoiceIndependent = 62,
  // Organ Fantasia. Stamped by CandidateSearch on every note of a
  // FantasiaCarrier span.
  // FantasiaSectionContrast: a note of one of the (>= 2) contrasting fantasia
  //   sections. The Validator's section_contrast_required rule collects every
  //   note carrying this bit, groups them into sections by window, and soft-fails
  //   (MusicalFail) when any pair of ADJACENT sections is not sufficiently
  //   contrasting (near-identical note density AND mean register).
  FantasiaSectionContrast = 63,
};

// Per-note provenance record.
//
// Invariant: every NoteEvent emitted by Renderer carries one
// NoteProvenance. provenance.json is built from this collection.
// generated.json (the evaluation export) excludes these fields.
struct NoteProvenance {
  SpanId span_id = kInvalidSpanId;
  // Invariant ("roles are const"): copied from the owning Span::intent at
  // note emission and never mutated downstream. Non-const only so
  // NoteProvenance stays an aggregate; enforcing const would ripple into
  // aggregate initialisers and is out of scope.
  VoiceIntent voice_intent = VoiceIntent::FillerGap;
  float candidate_score = 0.0f;
  NoteSource source = NoteSource::Compose;
  RuleIdMask satisfied_rules = 0;
  std::uint16_t rejected_alternatives = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_PROVENANCE_H
