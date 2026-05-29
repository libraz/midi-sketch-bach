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
  Tick distance_ticks = 0;
  int interval_semis = 0;
};

// P11 development-section declarations. Each holds (a) the carrier's
// verbatim note material (so CandidateSearch can replay it) and (b) the
// metadata the Validator's P11 rules check. All ticks are absolute.

// One middle entry: the subject restated in a related key. `notes` is
// the subject transposed into that key (built by the planner); the
// Validator's `middle_entry_in_related_key` rule checks that
// `related_key_pc` is a related key of the home tonic (V/vi/IV/ii) and
// that every note pitch class is diatonic to the major scale on
// `related_key_pc`.
struct MiddleEntryDecl {
  VoiceId voice = 0;
  std::uint8_t related_key_pc = 7;  // tonal center; must be V/vi/IV/ii.
  std::vector<MaterialNote> notes;
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
// it (the closure gate only confirms the SubjectVariantApplied bit).
struct SubjectVariantDecl {
  VoiceId voice = 0;
  std::uint8_t transform = 0;
  std::vector<MaterialNote> notes;
};

// One coda extension: a closing phrase after the final cadence. `notes`
// is replayed verbatim; no Validator rule constrains it (the closure
// gate only confirms the CodaCommitted bit).
struct CodaDecl {
  VoiceId voice = 0;
  std::vector<MaterialNote> notes;
};

// P12 rhythm / meter / phrase declarations.

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

// Bundle of pre-determined material fragments available to the planner.
//
// `subject` feeds SubjectCarrier spans; `answer` feeds AnswerCarrier
// spans introduced in Phase 4. Later phases add `countersubject` and
// per-character episode fragments. Each fragment is a flat note list;
// structure (phrase boundaries, repeat units) is the planner's
// responsibility.
struct Material {
  std::vector<MaterialNote> subject;
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
  // P9 (Fortspinnung + Imitation). Empty in Phase 3-8 fixtures.
  std::vector<SequenceTemplate> sequence_templates;
  std::vector<ImitationEntry> imitation_entries;
  // P11 (development section). Empty in Phase 3-10 fixtures.
  std::vector<MiddleEntryDecl> middle_entries;
  std::vector<StrettoDecl> stretto_entries;
  std::vector<PedalPointDecl> pedal_points;
  std::vector<SubjectVariantDecl> subject_variants;
  std::vector<CodaDecl> coda_extensions;
  // P12 (rhythm / meter / phrase). Empty in Phase 3-11 fixtures.
  PhraseStructure phrase_structure;
  std::vector<RhythmFragment> rhythm_fragments;
};

void annotateLeadingToneMarkers(Material& material, std::uint8_t tonic_pc, bool is_minor);
void annotateCadenceCells(Material& material, const HarmonicPlan& harmonic_plan);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_MATERIAL_H
