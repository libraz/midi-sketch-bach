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
// declares ThematicProtectionLevel and ViolationBudget, both forbidden by
// the rebuild plan's "移植しない概念" section. Pulling thematic_plan.h would
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
  // Used by Phase 4 to place a real or tonal answer in the second voice
  // of an exposition.
  AnswerCarrier = 6,

  // Voice carrying a SuspensionPattern (preparation / suspended dissonance /
  // resolution triad). Source is `Material::suspension_patterns`. Phase 4
  // adds this intent so the planner can pin strict-counterpoint dissonance
  // handling without re-deriving prep-sus-res via the candidate cascade.
  SuspensionCarrier = 7,

  // Episode span derived from Material::subject via a motif transform
  // (Original / Invert / Retrograde / Augment / Diminish). Source is
  // `Material::episodes`; CandidateSearch replays the derived note
  // sequence verbatim. Phase 5 adds this intent so episodes are
  // restricted to motif-operation outputs rather than free counterpoint.
  Episode = 8,

  // Voice carrying the fixed countersubject material during the answer
  // entry. Source is `Material::countersubject`; CandidateSearch
  // replays verbatim. Phase 6 adds this intent so the original subject
  // voice (post-subject statement) supplies an obbligato counterline
  // against the AnswerCarrier instead of a free SequentialCounterline.
  CountersubjectCarrier = 9,

  // Voice carrying a Fortspinnung sequence: one seed motif transposed
  // by a fixed pattern (descending 5ths / descending step / ascending
  // step) over N successive steps. Source is
  // `Material::sequence_templates`; CandidateSearch replays the
  // expanded note list verbatim. Phase 9 adds this intent so episodic
  // continuations can be planned as deterministic sequences instead
  // of free counterpoint.
  FortspinnungSpan = 10,

  // P11 (Fugue development section: middle entry / stretto / pedal /
  // coda / subject variants). All seven intents below carry their
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

  // P12 (Rhythm / meter / phrase). A carrier that replays a verbatim
  // rhythm fragment (anacrusis pickup, hemiola regrouping, dotted figure,
  // syncopation, or a rhythmic-motif recurrence). Source is
  // `Material::rhythm_fragments` (matched by voice); each fragment's
  // feature tag selects the provenance bit. Notes whose onset lands on a
  // declared phrase start additionally carry PhrasePeriodicityKept.
  RhythmCarrier = 18,

  // P14. A carrier that replays a verbatim single-voice non-chord-tone
  // (NCT) figure (cambiata / echappee / anticipation / nota cambiata).
  // Source is `Material::nct_figures` (matched by voice); CandidateSearch
  // replays each note verbatim (score = 1.0) like the other Material
  // carriers but does NOT stamp the NCT provenance bits. The figure bits
  // (CambiataDetected / EchappeeDetected / AnticipationDetected /
  // NotaCambiataDetected) are stamped later by the Composer's NCT
  // post-pass, which re-runs the standalone nct_detector figures on the
  // final sorted single-voice note list (the only place the full melodic
  // neighbourhood needed by the detectors exists).
  NctCarrier = 19
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
// 0..NctCarrier(19); a static_assert in voice_intent.cpp guards completeness.
const IntentDescriptor& describeIntent(VoiceIntent intent);

// Pure helper. No formatting library dependency. Delegates to
// describeIntent(intent).name.
const char* voiceIntentToString(VoiceIntent intent);

// True iff the intent replays Material verbatim (NoteSource::Material).
// Delegates to describeIntent(intent).is_carrier.
bool isCarrierIntent(VoiceIntent intent);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VOICE_INTENT_H
