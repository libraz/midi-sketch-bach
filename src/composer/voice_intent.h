#ifndef BACH_COMPOSER_VOICE_INTENT_H
#define BACH_COMPOSER_VOICE_INTENT_H

#include <cstdint>

namespace bach::composer {

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
  AnswerCarrier = 6
};

// Pure helper. No formatting library dependency.
const char* voiceIntentToString(VoiceIntent intent);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VOICE_INTENT_H
