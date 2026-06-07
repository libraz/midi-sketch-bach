#ifndef BACH_COMPOSER_VOICE_INTENT_H
#define BACH_COMPOSER_VOICE_INTENT_H

#include <cstdint>

namespace bach::composer {

// Forward declaration of the provenance rule-bit enum. The full definition
// lives in provenance.h, which (transitively, via span.h) includes this
// header — so this header must not include provenance.h. Forward-declaring
// the fixed-underlying-type enum lets IntentDescriptor name the primary bit
// without reintroducing a circular include.
enum RuleBit : std::uint8_t;

// Musical role assigned to a voice over a time span.
//
// Values mirror the legacy bach::VoiceIntent enum (src/fugue/thematic_plan.h),
// but this header is declared independently so the composer subsystem does
// NOT include the legacy thematic_plan header. The legacy header also
// declares ThematicProtectionLevel and ViolationBudget, concepts the composer
// subsystem intentionally does not adopt. Pulling thematic_plan.h would
// reintroduce that vocabulary into the composer's translation units.
//
// Boundary enforced by ci/check_composer_isolation.sh (forbidden include
// pattern "#include \"fugue/thematic_plan").
enum class VoiceIntent : std::uint8_t {
  // Voice carrying the protected subject material. Pitches and durations
  // are inputs to candidate search, not search outputs.
  SubjectCarrier = 0,

  // Voice in dialogic short-cell exchange with the subject. Used when the
  // subject is in another voice and this voice answers with a tail or
  // motif fragment.
  RepeatedReplyCell = 1,

  // Stepwise or sequential counterline (countersubject-like). Subject to
  // full candidate evaluation; not protected.
  SequentialCounterline = 2,

  // Bass/inner voice supporting current harmony and cadence preparation.
  HarmonicSupport = 3,

  // Cadence preparation and closing formula. Span boundaries align with
  // HarmonicPlan cadence anchors.
  CadentialClosure = 4,

  // Low-priority filler; freely re-generated if validation fails.
  FillerGap = 5,

  // Voice carrying the protected answer material (fugue comes). Same
  // verbatim-replay semantics as SubjectCarrier but the source pitch
  // sequence lives in `Material::answer` instead of `Material::subject`.
  // Used to place a real or tonal answer in the second voice
  // of an exposition.
  AnswerCarrier = 6,

  // Voice carrying a SuspensionPattern (preparation / suspended dissonance /
  // resolution triad). Source is `Material::suspension_patterns`. This
  // intent lets the planner pin strict-counterpoint dissonance
  // handling without re-deriving prep-sus-res via the candidate cascade.
  SuspensionCarrier = 7,

  // Episode span derived from Material::subject via a motif transform
  // (Original / Invert / Retrograde / Augment / Diminish). Source is
  // `Material::episodes`; CandidateSearch replays the derived note
  // sequence verbatim. This intent restricts episodes to
  // motif-operation outputs rather than free counterpoint.
  Episode = 8,

  // Voice carrying the fixed countersubject material during the answer
  // entry. Source is `Material::countersubject`; CandidateSearch
  // replays verbatim. This intent lets the original subject
  // voice (post-subject statement) supply an obbligato counterline
  // against the AnswerCarrier instead of a free SequentialCounterline.
  CountersubjectCarrier = 9,

  // Voice carrying a Fortspinnung sequence: one seed motif transposed
  // by a fixed pattern (descending 5ths / descending step / ascending
  // step) over N successive steps. Source is
  // `Material::sequence_templates`; CandidateSearch replays the
  // expanded note list verbatim. This intent lets episodic
  // continuations be planned as deterministic sequences instead
  // of free counterpoint.
  FortspinnungSpan = 10,

  // Fugue development section: middle entry / stretto / pedal /
  // coda / subject variants. All seven intents below carry their
  // pitch material verbatim from a dedicated Material vector (see
  // material.h), so they are NoteSource::Material like the other
  // carriers. CandidateSearch replays the source notes (score = 1.0)
  // and stamps the matching provenance bit.

  // Subject restated in a related key (V / vi / IV / ii). Source is
  // `Material::middle_entries[i].notes` (the i whose voice matches the
  // span's voice). Stamps RuleBit::MiddleEntryCommitted.
  MiddleEntryCarrier = 11,

  // Follower entry of a stretto: a subject restatement that begins
  // before the leader's statement ends. Source is
  // `Material::stretto_entries[i].follower_notes` (matched by
  // follower_voice). Stamps RuleBit::StrettoCommitted.
  StrettoCarrier = 12,

  // Sustained tonic or dominant pedal point in the bass. Source is
  // `Material::pedal_points[i]` (matched by voice); emits one held
  // note. Stamps RuleBit::PedalCommitted.
  PedalCarrier = 13,

  // Closing extension after the final cadence. Source is
  // `Material::coda_extensions[i].notes` (matched by voice). Stamps
  // RuleBit::CodaCommitted.
  CodaCarrier = 14,

  // Subject restated under a motif transform. The three variants share
  // one replay path and one provenance bit (SubjectVariantApplied);
  // the distinct names document the applied transform. Source is
  // `Material::subject_variants[i].notes` (matched by voice).
  SubjectCarrierAugmented = 15,
  SubjectCarrierDiminished = 16,
  SubjectCarrierInverted = 17,

  // Rhythm / meter / phrase. A carrier that replays a verbatim
  // rhythm fragment (anacrusis pickup, hemiola regrouping, dotted figure,
  // syncopation, or a rhythmic-motif recurrence). Source is
  // `Material::rhythm_fragments` (matched by voice); each fragment's
  // feature tag selects the provenance bit. Notes whose onset lands on a
  // declared phrase start additionally carry PhrasePeriodicityKept.
  RhythmCarrier = 18,

  // A carrier that replays a verbatim single-voice non-chord-tone
  // (NCT) figure (cambiata / echappee / anticipation / nota cambiata).
  // Source is `Material::nct_figures` (matched by voice); CandidateSearch
  // replays each note verbatim (score = 1.0) like the other Material
  // carriers but does NOT stamp the NCT provenance bits. The figure bits
  // (CambiataDetected / EchappeeDetected / AnticipationDetected /
  // NotaCambiataDetected) are stamped later by the Composer's NCT
  // post-pass, which re-runs the standalone nct_detector figures on the
  // final sorted single-voice note list (the only place the full melodic
  // neighbourhood needed by the detectors exists).
  NctCarrier = 19,

  // Solo String Flow (BWV1007). A single voice that arpeggiates the
  // underlying harmony as a continuous broken-chord line. Source is
  // `Material::arpeggio_template.notes`; CandidateSearch replays each note
  // verbatim (score = 1.0) like the other Material carriers and stamps
  // ArpeggioFlowActive + ImplicitVoiceTracked. The implicit-voice streams
  // the line projects are checked by the Validator's implicit_voice_*
  // rules, not re-derived here.
  ArpeggioFlow = 20,

  // Solo String Arch (BWV1004 Chaconne). Both are Material verbatim
  // carriers. GroundCarrier replays the immutable ground bass (period-tiled
  // across the whole piece); VariationCarrier replays one upper-voice
  // variation line. CandidateSearch stamps the arch bits on their notes.
  GroundCarrier = 21,
  VariationCarrier = 22,

  // Organ Prelude (free sectional form). A single voice that carries one
  // figuration section of a free-form prelude: a window of fast figuration
  // (broken-chord / scale run). Source is `Material::figuration_sections`
  // (matched by window); CandidateSearch replays each note verbatim (score =
  // 1.0) and stamps FigurationCommitted (plus CadenzaApplied / PedalPreparation
  // when the section is flagged is_cadenza / is_pedal_prep). The on-beat notes'
  // chord-tone membership is checked by the Validator's
  // figuration_harmonic_consistency rule, not re-derived here.
  FigurationCarrier = 23,

  // Organ Toccata (4 archetypes). A single voice that carries one
  // section of a virtuosic toccata: a window of fast scalar-wave figuration.
  // Source is `Material::toccata_sections` (matched by window); CandidateSearch
  // replays each note verbatim (score = 1.0) and stamps ToccataArchetypeApplied
  // on every note (plus SectionTransition on the first note when the section is
  // flagged is_section_head). The (character, archetype) compatibility is
  // checked by the Validator's toccata_archetype_compatible rule, not here.
  ToccataCarrier = 24,

  // Organ Chorale Prelude. A single voice that carries the fixed chorale
  // tune (cantus firmus): one structural tone per bar, optionally embellished
  // with stepwise passing notes whose bar-downbeat tones still equal the
  // skeleton. Source is `Material::cf_embellished` when
  // `Material::cf_is_embellished` is set, otherwise `Material::cantus_firmus`;
  // CandidateSearch replays each note verbatim (score = 1.0) and stamps
  // CantusFirmusReplayed (plus CFEmbellishmentApplied on every note when the
  // embellished line is replayed). The cantus firmus is immutable:
  // its bar-downbeat tones are checked by the Validator's cantus_firmus_immutable
  // rule, not re-derived here.
  CantusFirmusCarrier = 25,

  // Organ Passacaglia (ground bass + variations + climax). Both are
  // Material verbatim carriers, like the GroundCarrier / VariationCarrier pair.
  // PassacagliaGround replays the immutable 8-bar passacaglia ground bass
  // (period-tiled across the whole piece, like the GroundCarrier);
  // PassacagliaVariation replays one upper-voice variation block over one ground
  // cycle. CandidateSearch stamps PassacagliaGroundReplayed on the ground notes
  // and VariationApplied on every variation note (OR ClimaxPlaced when the
  // variation block is flagged is_climax — the dynamic / registral peak placed
  // mid- and end-piece). The ground is immutable: its cycle-folded
  // pitches are checked by the Validator's passacaglia_ground_immutable rule.
  PassacagliaGround = 26,
  PassacagliaVariation = 27,

  // Organ Trio Sonata (three independent voices). A Material verbatim
  // carrier that replays one of the (up to three) independent voice lines of an
  // organ trio sonata: RH (Great), LH (Swell), Pedal. Source is the matching
  // entry in `Material::trio_voices` (matched by voice); CandidateSearch replays
  // each note verbatim (score = 1.0) and stamps TrioVoiceIndependent on every
  // note. The defining technique is voice independence: the three lines must
  // move with enough non-parallel / rhythmically-distinct motion that the
  // Validator's voice_independence_threshold rule (a soft MusicalFail below
  // 0.6) passes. The lines carry that bit so the rule can collect exactly the
  // trio voices and measure their pairwise independence; the per-voice register
  // and rhythm separation is the fixture's responsibility.
  TrioVoiceCarrier = 28,

  // Organ Fantasia (free sectional, multi-style). A Material verbatim
  // carrier that replays one CONTRASTING section of a free fantasia (e.g. a
  // free/improvisatory opening, a fugal-ish middle, a toccata-like run, a
  // chordal close). Source is the matching entry in `Material::fantasia_sections`
  // (matched by window); CandidateSearch replays each note verbatim (score =
  // 1.0) and stamps FantasiaSectionContrast on every note. The defining
  // technique is SECTION CONTRAST: adjacent sections must differ in their
  // distinguishing trait (note density level OR mean register), which the
  // Validator's section_contrast_required rule (a soft MusicalFail) measures
  // from the notes carrying this bit. The per-section density / register
  // separation is the fixture's responsibility.
  FantasiaCarrier = 29
};

// How CandidateSearch turns a span of this intent into Candidates.
//
//   kCompose        — free candidate search (the non-carrier intents).
//   kVerbatimVector — replay a flat vector of MaterialNotes verbatim
//                     (subject / answer / countersubject / development
//                     carriers / rhythm / nct).
//   kTransform      — replay subject fragments through a motif transform
//                     (Episode).
//   kSequence       — replay a Fortspinnung sequence template.
//   kTriple         — replay a three-note suspension (prep / sus / res).
enum class ReplayKind : std::uint8_t {
  kCompose = 0,
  kVerbatimVector = 1,
  kTransform = 2,
  kSequence = 3,
  kTriple = 4,
};

// Single source of truth for the per-intent metadata that was previously
// dual-encoded across voice_intent.cpp (voiceIntentToString), composer.cpp
// (isCarrierIntent), and candidate_search.cpp (per-intent replay branches).
//
//   name           — display string (source of truth for voiceIntentToString).
//   is_carrier     — true iff the intent replays Material verbatim and is
//                    therefore NoteSource::Material (source of truth for
//                    isCarrierIntent).
//   replay         — how CandidateSearch dispatches the span.
//   provenance_bit — the primary provenance bit this intent stamps, where
//                    one applies. `has_provenance_bit` gates its validity
//                    (some carriers stamp a bit chosen at the call site, and
//                    Compose / Suspension stamp no single primary bit).
struct IntentDescriptor {
  const char* name;
  bool is_carrier;
  ReplayKind replay;
  RuleBit provenance_bit;
  bool has_provenance_bit;
};

// Table lookup keyed by VoiceIntent. Defined for every enumerator value
// 0..FantasiaCarrier(29); a static_assert in voice_intent.cpp guards
// completeness.
const IntentDescriptor& describeIntent(VoiceIntent intent);

// Pure helper. No formatting library dependency. Delegates to
// describeIntent(intent).name.
const char* voiceIntentToString(VoiceIntent intent);

// True iff the intent replays Material verbatim (NoteSource::Material).
// Delegates to describeIntent(intent).is_carrier.
bool isCarrierIntent(VoiceIntent intent);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VOICE_INTENT_H
