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
};

void annotateLeadingToneMarkers(Material& material, std::uint8_t tonic_pc, bool is_minor);
void annotateCadenceCells(Material& material, const HarmonicPlan& harmonic_plan);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_MATERIAL_H
