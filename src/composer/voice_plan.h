#ifndef BACH_COMPOSER_VOICE_PLAN_H
#define BACH_COMPOSER_VOICE_PLAN_H

#include <cstdint>
#include <vector>

#include "composer/span.h"

namespace bach::composer {

// Voice plan: a flat ordered list of spans covering the piece.
//
// The list partitions (voice, time) space — every (voice, tick) pair is
// covered by exactly one span. Sort order is by (voice ascending,
// start_tick ascending). The planner produces this structure once; the
// CandidateSearch consumes it span by span.
//
// VoicePlan does NOT change after planning. Span retry rewrites notes
// within a span, not the span's intent or boundary.
struct VoicePlan {
  std::vector<Span> spans;
  std::uint8_t num_voices = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VOICE_PLAN_H
