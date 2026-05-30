#ifndef BACH_COMPOSER_SPAN_H
#define BACH_COMPOSER_SPAN_H

#include <cstdint>

#include "composer/voice_intent.h"
#include "core/basic_types.h"

namespace bach::composer {

// Stable identifier for one time-region-per-voice work unit.
//
// Spans are the unit of candidate search and the unit of re-generation on
// validation failure. When the Validator rejects a candidate, the search
// retries within the same span; when retries exhaust, the engine
// back-jumps to the span that introduced the constraint (PWMC-style
// intelligent backtracking, see backup/rebuild_plan_2026-05-28.md).
using SpanId = std::uint32_t;

inline constexpr SpanId kInvalidSpanId = static_cast<SpanId>(-1);

// Note-placement granularity inside a Span. Quarter (one note per beat)
// is the default and matches the original 2-voice Phase 2 deliverable.
// Eighth (two notes per beat) opts a span into continuous eighth-note
// motion — useful for Bach-style countersubject lines whose duration
// distribution differs sharply from steady quarters.
enum class Subdivision : std::uint8_t {
  Quarter = 0,
  Eighth = 1,
};

// One voice's slice of one time region.
//
// Span boundaries align with the HarmonicPlan: cadence anchors, subject
// entries, and phrase joins all become span boundaries. Bar-level
// granularity is the default; sub-bar spans are allowed where the
// HarmonicPlan changes mid-bar.
struct Span {
  SpanId id = kInvalidSpanId;
  Tick start_tick = 0;
  Tick end_tick = 0;
  VoiceId voice = 0;
  // Invariant ("roles are const"): set at construction (planner) and never
  // mutated downstream. The field is non-const only so Span stays an
  // aggregate for the harness fixture's aggregate initialisers; enforcing
  // const here would ripple into those inits and is therefore out of scope.
  VoiceIntent intent = VoiceIntent::FillerGap;
  Subdivision subdivision = Subdivision::Quarter;
  // Optional per-span tessitura anchor for Compose candidate search. 0 means
  // "use the global per-voice default" (Composer::kVoiceCenter); any non-zero
  // value overrides it for this span only. Used where a voice's default alto/
  // tenor center sits too far below a high subject for the P7 spacing
  // pre-filter (gap <= octave) to admit any candidate — e.g. the Phase14
  // exposition counterline beneath a subject that climbs to G5/A5. Carrier
  // spans ignore this field (they replay Material verbatim).
  std::uint8_t voice_center = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_SPAN_H
