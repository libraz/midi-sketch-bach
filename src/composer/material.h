#ifndef BACH_COMPOSER_MATERIAL_H
#define BACH_COMPOSER_MATERIAL_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "composer/harmonic_plan.h"
#include "core/basic_types.h"

namespace bach::composer {

// One note inside a pre-determined material fragment (subject, answer,
// fixed counterline, etc.). Pitch and duration are inputs to the
// composer pipeline; CandidateSearch does not enumerate alternatives.
struct MaterialNote {
  Tick start_tick = 0;
  Tick duration = 0;
  std::uint8_t pitch = 0;
};

enum class MaterialFragment : std::uint8_t {
  Subject = 0,
  Answer = 1,
  TonalAnswer = 2,
  Countersubject = 3,
};

struct LeadingToneMarker {
  MaterialFragment fragment = MaterialFragment::Subject;
  std::size_t leading_index = 0;
  std::size_t resolution_index = 0;
  Tick leading_tick = 0;
  Tick resolution_tick = 0;
  std::uint8_t leading_pitch = 0;
  std::uint8_t resolution_pitch = 0;
  std::uint8_t tonic_pc = 0;
};

struct CadenceCell {
  CadenceType type = CadenceType::Perfect;
  Tick approach_tick = 0;
  Tick cadence_tick = 0;
  std::uint8_t soprano_approach_pc = 0;
  std::uint8_t soprano_cadence_pc = 0;
  std::uint8_t bass_approach_pc = 0;
  std::uint8_t bass_cadence_pc = 0;
};

// Suspension dissonance classification by the interval the held note
// forms with the bass (and the step direction of its resolution).
// Matches the legacy bach::SuspensionType taxonomy verbatim so the new
// Composer rules read the same names baroque theorists do.
enum class SuspensionType : std::uint8_t {
  Sus4_3,  // 4th over the bass resolves down by step to a 3rd.
  Sus7_6,  // 7th resolves down by step to a 6th.
  Sus9_8,  // 9th resolves down by step to an octave.
  Sus2_3,  // 2nd over a held upper voice resolves UP by step (bass susp.).
};

// One suspension occurrence in the planner's material: three explicit
// note positions (preparation, suspended dissonance, resolution) on a
// single voice. CandidateSearch replays these verbatim when a span's
// intent is SuspensionCarrier; the Validator checks preparation
// consonance and resolution step direction.
struct SuspensionPattern {
  SuspensionType type = SuspensionType::Sus4_3;
  Tick preparation_tick = 0;  // Consonant prep beat (note begins).
  Tick suspension_tick = 0;   // Strong-beat dissonance (held over).
  Tick resolution_tick = 0;   // Stepwise resolution.
  std::uint8_t preparation_pitch = 0;
  std::uint8_t suspension_pitch = 0;
  std::uint8_t resolution_pitch = 0;
  VoiceId voice = 0;
};

// Motif-transform descriptor for an Episode span. Holds (a) what subject
// fragment to derive from, (b) which transform to apply, (c) where to
// place the result, and (d) the transform parameters. CandidateSearch
// emits the derived notes verbatim when a span's intent is Episode; the
// Validator re-derives via the same descriptor and asserts byte-match.
//
// `source_start_index` and `source_count` index into `Material::subject`.
// `source_count == 0` is shorthand for "the entire subject from
// source_start_index to the end".
//
// Enum lives in composer/motif_ops.h to keep all transform vocabulary
// in one place; forward-declared here so the header stays standalone.
struct EpisodeFragment {
  std::uint8_t transform = 0;  // EpisodeMotifTransform value (cast at use).
  std::size_t source_start_index = 0;
  std::size_t source_count = 0;
  VoiceId voice = 0;
  Tick target_start_tick = 0;
  std::uint8_t invert_pivot = 60;
  int augment_factor = 2;
  int diminish_factor = 2;
};

// One Fortspinnung sequence: a short seed motif transposed by a fixed
// pattern over N successive steps. Bach's three canonical sequence
// patterns (descending 5ths / descending step / ascending step) are
// represented as semitone offsets applied per step.
//
//   DescendingFifths: each step transposes -7 semitones (P5 down) from
//                     the previous step. Standard "circle of fifths"
//                     fortspinnung (e.g. WTC I C minor BWV 847 b. 9-10).
//   DescendingStep:   each step transposes -2 semitones (whole step
//                     down). "Sequenza ad gradum".
//   AscendingStep:    each step transposes +2 semitones (whole step
//                     up). Climbing fortspinnung.
//
// The seed motif (`seed_pitches` + `seed_durations`) is the first step
// played at `target_start_tick`. Steps 1..num_steps-1 are the seed
// transposed by step_offset*step_index semitones and time-shifted by
// step_length_ticks*step_index. CandidateSearch replays all steps as a
// flat note list when the span's intent is FortspinnungSpan.
enum class SequencePattern : std::uint8_t {
  DescendingFifths = 0,
  DescendingStep = 1,
  AscendingStep = 2,
};

struct SequenceTemplate {
  SequencePattern pattern = SequencePattern::DescendingFifths;
  Tick target_start_tick = 0;  // Where step 0 (the seed) begins.
  Tick step_length_ticks = 0;  // Time offset between successive steps.
  std::uint8_t num_steps = 1;  // Total steps (including the seed).
  VoiceId voice = 0;
  std::vector<std::uint8_t> seed_pitches;
  std::vector<Tick> seed_durations;
};

// One imitation entry declaration: a follower voice restates a fixed
// material fragment (subject / answer) at a declared rhythmic distance
// and pitch-class interval relative to the leader voice. Used to
// document expected stretto / canonic entries; the Validator checks
// that the actual Material notes match the declaration.
//
//   leader_fragment   — which Material fragment is being imitated.
//   follower_fragment — the follower's Material fragment.
//   distance_ticks    — how many ticks the follower trails the leader.
//   interval_semis    — pitch offset (follower = leader + interval).
//                        +7  = P5 up, -5 = P5 down, +12 = P8 up,
//                        -12 = P8 down, -5/-7 are the canonical
//                        Bach answer intervals (real answer = -5).
struct ImitationEntry {
  MaterialFragment leader_fragment = MaterialFragment::Subject;
  MaterialFragment follower_fragment = MaterialFragment::Answer;
  VoiceId leader_voice = 0;
  VoiceId follower_voice = 1;
  std::size_t leader_start_index = 0;
  std::size_t follower_start_index = 0;
  std::size_t note_count = 0;  // 0 = all available notes from both start indices.
  Tick distance_ticks = 0;
  int interval_semis = 0;
  // A tonal answer's first note may use a tonic/dominant mutation rather than
  // the real-answer transposition used by its remaining contour. Builders
  // declare that underlying interval explicitly so short/truncated answers
  // remain verifiable without consulting an unused real-answer fragment.
  int tonal_base_interval_semis = 0;
  bool has_tonal_base_interval = false;
};

// Development-section declarations. Each holds (a) the carrier's
// verbatim note material (so CandidateSearch can replay it) and (b) the
// metadata the Validator's development-section rules check. All ticks are
// absolute.

// One middle entry: the subject restated in a related key. `notes` is
// the subject transposed into that key (built by the planner); the
// Validator's `middle_entry_in_related_key` rule checks that
// `related_key_pc` is a related key of the home tonic (V/vi/IV/ii) and
// that every note pitch class is diatonic to the major scale on
// `related_key_pc`.
struct MiddleEntryTransformRegion {
  Tick start_tick = 0;
  Tick end_tick = 0;
  // motif_ops::EpisodeMotifTransform value. Kept byte-sized here so material.h
  // does not depend on motif_ops.h (which itself consumes MaterialNote).
  std::uint8_t transform = 0;
};

struct MiddleEntryDecl {
  VoiceId voice = 0;
  std::uint8_t related_key_pc = 7;  // tonal center; must be V/vi/IV/ii.
  std::vector<MaterialNote> notes;
  // Non-original entry windows. CandidateSearch stamps
  // SubjectVariantApplied on every note in these ranges.
  std::vector<MiddleEntryTransformRegion> transform_regions;
};

// One stretto: a follower restatement that overlaps the leader's
// statement in time. The leader is an ordinary SubjectCarrier; this
// struct documents the follower. The Validator's `stretto_overlap_valid`
// rule checks that (a) the follower enters strictly inside the leader's
// window (genuine overlap) and (b) follower_notes[i].pitch ==
// material.subject[i].pitch + interval_semis (the follower is the
// subject transposed by the declared interval).
struct StrettoDecl {
  VoiceId leader_voice = 0;
  VoiceId follower_voice = 1;
  Tick leader_entry_tick = 0;
  Tick leader_length_ticks = 0;  // leader fragment duration.
  Tick follower_entry_tick = 0;
  int interval_semis = 0;  // follower = subject + interval_semis.
  std::vector<MaterialNote> follower_notes;
};

// One pedal point: a single sustained note. The Validator's
// `pedal_point_tonic_or_dominant` rule checks that `pitch % 12` is the
// tonic or dominant pitch class of the home key.
struct PedalPointDecl {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick duration = 0;
  std::uint8_t pitch = 0;
  bool is_dominant = false;  // false => tonic pedal (documentary).
};

// One subject variant: the subject restated under a motif transform
// (augmentation / diminution / inversion). `transform` mirrors
// motif_ops::EpisodeMotifTransform (cast at use). `notes` is the
// pre-derived result the carrier replays; no Validator rule constrains
// it (the provenance only carries the SubjectVariantApplied bit).
struct SubjectVariantDecl {
  VoiceId voice = 0;
  std::uint8_t transform = 0;
  std::vector<MaterialNote> notes;
};

// One coda extension: a closing phrase after the final cadence. `notes`
// is replayed verbatim; no Validator rule constrains it (the provenance
// only carries the CodaCommitted bit).
struct CodaDecl {
  VoiceId voice = 0;
  std::vector<MaterialNote> notes;
};

// Solo String Arch (BWV1004 Chaconne). VariationRole is the
// per-variation function over the repeating ground bass:
//   Ground  = plain statement, no ornamental subdivision (must stay un-embellished).
//   Respond = answers the ground with light activity.
//   Propel  = drives forward with increased rhythmic density.
//   Assert  = climactic, densest figuration.
enum class VariationRole : std::uint8_t {
  Ground = 0,
  Respond = 1,
  Propel = 2,
  Assert = 3,
};

// One variation block over the (repeating) ground bass: its function role,
// a density tier (notes-per-bar class, monotone-ish across the set), and the
// realized upper-voice line. A VariationCarrier span replays `notes` verbatim.
// The Validator's variation_role_ornament_constraint checks that a Ground-role
// variation contains no note shorter than a quarter (no ornaments).
struct VariationDecl {
  VariationRole role = VariationRole::Ground;
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  int density_level = 0;
  std::vector<MaterialNote> notes;
};

// Organ Prelude. One figuration section of a free-form prelude: a window
// of fast figuration on one voice. `is_cadenza` marks the free run before the
// final cadence; `is_pedal_prep` marks a sustained dominant pedal preparing the
// next fugue. A FigurationCarrier span replays `notes` verbatim. The Validator's
// figuration_harmonic_consistency rule checks the on-beat notes are chord tones.
struct FigurationSection {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  bool is_cadenza = false;
  bool is_pedal_prep = false;
  std::vector<MaterialNote> notes;
};

// Organ Toccata. The four Bach toccata archetypes:
// Dramaticus (BWV565 dramatic opening + free figuration),
// Perpetuus (BWV538 continuous sixteenths), Concertato (BWV564 forte/piano
// contrast), Sectionalis (BWV540 clear section breaks).
enum class ToccataArchetype : std::uint8_t {
  Dramaticus = 0,
  Perpetuus = 1,
  Concertato = 2,
  Sectionalis = 3,
};

// One section of a toccata. `archetype` + `character` document the piece-level
// design (the Validator's toccata_archetype_compatible rule checks the pair).
// `is_section_head` marks the first section of a new sectional block (stamps
// SectionTransition). A ToccataCarrier span replays `notes` verbatim.
struct ToccataSection {
  ToccataArchetype archetype = ToccataArchetype::Dramaticus;
  SubjectCharacter character = SubjectCharacter::Severe;
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  bool is_section_head = false;
  std::vector<MaterialNote> notes;
};

// Organ Passacaglia. An 8-bar immutable ground bass
// repeated under variations of rising density; one or more
// variation blocks are flagged is_climax (the registral peak). Like the
// chaconne arch but with an 8-bar period and a climax marker.
struct PassacagliaVariation {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  int density_level = 0;
  bool is_climax = false;
  std::vector<MaterialNote> notes;
};

// Organ Trio Sonata. One independent voice line of an organ trio sonata
// (RH = Great / LH = Swell / Pedal). `notes` is the realized monophonic line for
// `voice`, replayed verbatim by a TrioVoiceCarrier span (NoteSource::Material)
// exactly like the other carriers. `manual` is the documentary organ-manual id
// (0=Great, 1=Swell, 3=Pedal). The three lines' pairwise voice independence is
// measured by the Validator's voice_independence_threshold rule from the notes
// carrying the TrioVoiceIndependent bit, not from this struct directly.
struct TrioVoiceLine {
  VoiceId voice = 0;
  std::uint8_t manual = 0;  // documentary: 0=Great, 1=Swell, 3=Pedal.
  std::vector<MaterialNote> notes;
};

// Organ Fantasia (free sectional, multi-style). A fantasia organizes a
// single voice into CONTRASTING sections. `style` documents the section's
// rhetorical character (Free / Fugal / Toccata / Chordal); `density_level` is
// the notes-per-bar tier (the concrete, self-contained contrast measure). A
// FantasiaCarrier span replays `notes` verbatim (window-matched). The Validator's
// section_contrast_required rule checks that ADJACENT sections differ in
// density_level OR mean register, so the defining technique (section contrast)
// is enforced.
enum class FantasiaStyle : std::uint8_t {
  Free = 0,     // improvisatory opening (sparse, free figuration).
  Fugal = 1,    // imitative middle (moderate density).
  Toccata = 2,  // virtuosic running figuration (dense).
  Chordal = 3,  // declamatory chordal / homophonic close.
};

struct FantasiaSection {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  bool is_section_head = false;
  FantasiaStyle style = FantasiaStyle::Free;
  int density_level = 0;  // notes-per-bar tier; distinguishing contrast trait.
  std::vector<MaterialNote> notes;
};

// Rhythm / meter / phrase declarations.

// Phrase grid for the piece. `phrase_start_ticks` lists the downbeat tick
// of every phrase (including the first at 0, or at anacrusis_ticks when
// the piece opens with an upbeat). The Validator's
// phrase_periodicity_4_or_8_bar rule checks that consecutive starts differ
// by 4 or 8 bars; anacrusis_consistent checks the upbeat declaration.
struct PhraseStructure {
  bool has_anacrusis = false;
  Tick anacrusis_ticks = 0;  // upbeat length before the first downbeat.
  std::vector<Tick> phrase_start_ticks;
};

// One rhythm fragment: verbatim notes (pitch + tick + duration) plus a
// feature tag that selects the provenance bit when a RhythmCarrier span
// replays it. The rhythm itself (dotted, syncopated, hemiola regrouping,
// upbeat) lives in the note durations/onsets; the planner builds them.
struct RhythmFragment {
  enum class Feature : std::uint8_t {
    Anacrusis,    // upbeat pickup → AnacrusisActive
    Hemiola,      // 3-against-2 regrouping at a cadence → HemiolaInserted
    Dotted,       // dotted figure (no dedicated bit; rhythm is the feature)
    Syncopation,  // off-beat onset + tie (no dedicated bit)
    Recurrence,   // rhythmic-motif restatement → RhythmicMotifRecurrence
  };
  Feature feature = Feature::Dotted;
  VoiceId voice = 0;
  std::vector<MaterialNote> notes;
};

// Texture / instrument / expression plan. Unlike the carrier declarations
// above (which feed CandidateSearch verbatim-replay spans), these
// are render-time attributes the Composer's texture-expression post-pass
// reads after candidate placement: per-voice MIDI ranges, organ-manual
// routing, articulation regions, and an Affekt-driven velocity curve.
// Default-constructed (all vectors empty, affekt_curve_active=false) the
// post-pass is a no-op, so fixtures without a texture plan are unaffected.

// Inclusive MIDI pitch bounds for one voice. The Validator's
// `voice_range_integrity` rule fails any note in `voice` outside [lo, hi];
// the post-pass stamps VoiceRangeKept on every in-range note.
struct VoiceRangeDecl {
  VoiceId voice = 0;
  std::uint8_t lo = 0;
  std::uint8_t hi = 127;
};

// Organ-manual routing for one voice (documentary id: 0=Great, 1=Swell,
// 2=Choir, 3=Pedal). The post-pass stamps ManualAssigned on every note
// whose voice has a routing entry.
struct ManualRouting {
  VoiceId voice = 0;
  std::uint8_t manual = 0;
};

// Articulation applied to a voice over a tick window (0=legato, 1=detache,
// 2=staccato). The post-pass stamps ArticulationApplied and shortens the
// note's gate time deterministically. Pitch and onset stay unchanged, so
// melodic and contrapuntal identity remain intact while MIDI note-off timing
// expresses the declared articulation.
struct ArticulationSpan {
  VoiceId voice = 0;
  Tick start_tick = 0;
  Tick end_tick = 0;
  std::uint8_t kind = 0;
};

// Render-time texture / expression bundle. See per-field structs.
struct TexturePlan {
  std::vector<VoiceRangeDecl> voice_ranges;
  std::vector<ManualRouting> manual_assignments;
  std::vector<ArticulationSpan> articulations;
  // Affekt velocity curve. When active, the post-pass replaces each note's
  // velocity with a phrase-arch value scaled by `affekt_character`
  // (a SubjectCharacter cast, documentary) and stamps AffektCurveApplied.
  bool affekt_curve_active = false;
  std::uint8_t affekt_character = 0;
  // Voice subject to the pedal-range soft penalty (C1-D3). 0xFF = none.
  // The Validator's `pedal_range_soft_penalty` rule guards this voice.
  VoiceId pedal_voice = 0xFF;
};

// Solo String Flow (BWV1007). A single-voice broken-chord arpeggio.
// `notes` is the realized monophonic line, replayed verbatim by an
// ArpeggioFlow span (NoteSource::Material) exactly like the other carriers.
//
// Implicit voice tracking. A solo-string arpeggio projects more than one
// melodic line out of one sounding voice: the listener tracks the recurring
// low note as a "bass" line and the recurring high note as an upper line.
// The figure is regular — every `group_size` consecutive notes form one
// arpeggio cell — so implicit-voice membership is positional: within each
// group, slot 0 is the bass implicit voice and slot (group_size - 1) is the
// top implicit voice. The Validator reconstructs these two streams to check
// implicit_voice_counterpoint (each stream is a melodically valid line) and
// arpeggio_no_parallel_perfect (the bass and top streams do not move in
// parallel perfect 5ths/8ves across successive cells).
struct ArpeggioTemplate {
  std::vector<MaterialNote> notes;
  int group_size = 4;  // notes per arpeggio cell (e.g. 4 = sixteenth figure).
};

// Bundle of pre-determined material fragments available to the planner.
//
// `subject` feeds SubjectCarrier spans; `answer` feeds AnswerCarrier
// spans. Additional fields supply `countersubject` and per-character
// episode fragments. Each fragment is a flat note list; structure
// (phrase boundaries, repeat units) is the planner's responsibility.
//
// DESIGN DEBT (god-struct): this struct carries one flat carrier-payload
// field per device (subject / answer / tonal_answer / countersubject /
// nct_figures, plus the per-decl vectors below). The CandidateSearch replay
// dispatch must hand-map each VoiceIntent to its field. The intended future
// shape is to group the verbatim flat-vector payloads behind an
// intent-keyed accessor (see carrierPayload below / the IntentDescriptor
// table in voice_intent.h) so the dispatch becomes a single lookup. The
// regrouping is deferred because it would ripple into the harness fixture's
// aggregate initialisers; the fields are intentionally left flat for now.
struct Material {
  std::vector<MaterialNote> subject;
  // Number of notes in the canonical subject statement at the head of
  // `subject`. Later SubjectCarrier restatements share the same replay vector
  // but must not inflate subject analysis into a 32/48-note "theme".
  // Zero preserves legacy fixtures where the whole vector is canonical.
  std::size_t canonical_subject_note_count = 0;
  std::vector<MaterialNote> answer;
  // Tonal answer: subject mutated so the V-degree pitches map to I in
  // the answer (vs `answer` which is the literal real-answer = subject -
  // P4 transposition). When non-empty AND `use_tonal_answer` is set,
  // AnswerCarrier spans replay `tonal_answer` instead of `answer`.
  // Both vectors may be populated simultaneously so the harness can
  // switch by flag.
  std::vector<MaterialNote> tonal_answer;
  bool use_tonal_answer = false;
  // Countersubject: fixed counterline that runs in the subject voice
  // when the answer enters. CountersubjectCarrier spans replay this
  // verbatim.
  std::vector<MaterialNote> countersubject;
  std::vector<LeadingToneMarker> leading_tone_markers;
  std::vector<CadenceCell> cadence_cells;
  std::vector<SuspensionPattern> suspension_patterns;
  std::vector<EpisodeFragment> episodes;
  // Fortspinnung + Imitation.
  std::vector<SequenceTemplate> sequence_templates;
  std::vector<ImitationEntry> imitation_entries;
  // Development section.
  std::vector<MiddleEntryDecl> middle_entries;
  std::vector<StrettoDecl> stretto_entries;
  std::vector<PedalPointDecl> pedal_points;
  std::vector<SubjectVariantDecl> subject_variants;
  std::vector<CodaDecl> coda_extensions;
  // Rhythm / meter / phrase.
  PhraseStructure phrase_structure;
  std::vector<RhythmFragment> rhythm_fragments;
  // Non-chord-tone figures. Verbatim single-voice NCT figures
  // (cambiata / echappee / anticipation / nota cambiata) replayed by a
  // NctCarrier span.
  std::vector<MaterialNote> nct_figures;
  // Texture / instrument / expression. Default-constructed it is a no-op
  // post-pass.
  TexturePlan texture_plan;
  // Solo String Flow. Populated only by the CelloPrelude BWV1007-style arpeggio
  // fixture. Replayed by an ArpeggioFlow span on a single voice.
  ArpeggioTemplate arpeggio_template;
  // Solo String Arch. The ground bass is immutable: GroundCarrier replays it
  // tiled every `ground_bass_period` ticks. `variations` feed VariationCarrier.
  std::vector<MaterialNote> ground_bass;
  Tick ground_bass_period = 0;
  std::vector<VariationDecl> variations;
  // Organ Prelude (free sectional form). Populated only by the OrganPrelude
  // organ-prelude fixture. Each section is replayed verbatim by a
  // FigurationCarrier span whose window matches it.
  std::vector<FigurationSection> figuration_sections;
  // Organ Toccata (4 archetypes). Populated only by the OrganToccata organ-toccata
  // fixture. Each section is replayed verbatim by a ToccataCarrier span whose
  // window matches it; the Validator's
  // toccata_archetype_compatible rule checks each section's (character,
  // archetype) pair.
  std::vector<ToccataSection> toccata_sections;
  // Organ Chorale Prelude. The cantus firmus is a fixed chorale tune
  // (one structural tone per bar) and is immutable. A
  // CantusFirmusCarrier span replays either the plain skeleton (`cantus_firmus`)
  // or an embellished line (`cf_embellished`) whose bar-downbeat tones still equal
  // the skeleton — the Validator's cantus_firmus_immutable rule checks exactly
  // that the bar-downbeat replayed tones match `cantus_firmus`.
  //   cf_placement: documentary CF voice placement (0=Soprano,1=Tenor,2=Bass).
  //   cf_is_embellished: when true the carrier replays `cf_embellished` and
  //     stamps CFEmbellishmentApplied.
  std::vector<MaterialNote> cantus_firmus;   // skeleton, one tone per bar.
  std::vector<MaterialNote> cf_embellished;  // ornamented CF (downbeats == skeleton).
  bool cf_is_embellished = false;
  std::uint8_t cf_placement = 0;  // documentary.
  // Organ Passacaglia. The ground bass is immutable: PassacagliaGround replays
  // it tiled every
  // `passacaglia_ground_period` ticks; `passacaglia_variations`
  // feed PassacagliaVariation spans (one per ground cycle, rising density, the
  // last and optionally a mid cycle flagged is_climax).
  //
  // `passacaglia_ground_split_from`: design value for the late-cycle rhythmic
  // intensification of the ground. From this absolute tick on, the replay
  // subdivides each tiled ground note into repeated same-pitch quarters (the
  // BWV582-style martellato restatement). 0 = never split. Pitches never
  // change, so the bar-head skeleton stays immutable.
  std::vector<MaterialNote> passacaglia_ground;  // immutable 8-bar bass, cycle-relative ticks.
  Tick passacaglia_ground_period = 0;
  Tick passacaglia_ground_split_from = 0;
  std::vector<PassacagliaVariation> passacaglia_variations;
  // Goldberg Variations. This is deliberately separate from Passacaglia:
  // one compressed four-bar aria-bass phrase carries 32 structural tones and
  // is mapped unchanged through aria, 30 variation slots, and da capo.
  std::vector<MaterialNote> goldberg_aria_bass;
  Tick goldberg_aria_bass_period = 0;
  std::vector<PassacagliaVariation> goldberg_variations;
  std::vector<MaterialNote> goldberg_inner_voice;
  // Organ Trio Sonata. The (up to three) independent voice lines of a trio
  // sonata; each is replayed verbatim by a TrioVoiceCarrier span matched by
  // voice. The Validator's voice_independence_threshold rule measures their
  // pairwise independence.
  std::vector<TrioVoiceLine> trio_voices;
  // Organ Fantasia. The contrasting sections of a free sectional fantasia;
  // each is replayed verbatim by a FantasiaCarrier span matched by window.
  // The Validator's section_contrast_required rule
  // measures the contrast (density / register) between adjacent sections.
  std::vector<FantasiaSection> fantasia_sections;
};

void annotateLeadingToneMarkers(Material& material, std::uint8_t tonic_pc, bool is_minor);
void annotateCadenceCells(Material& material, const HarmonicPlan& harmonic_plan);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_MATERIAL_H
