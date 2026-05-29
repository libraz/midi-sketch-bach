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
// pitch-modifying passes (see rebuild plan §禁止事項).
enum class NoteSource : std::uint8_t {
  Material = 0,
  Compose = 1,
};

// Bitset over rule IDs. Each bit position is a CandidateSearch rule that
// the chosen candidate satisfies. Provides MUS-ROVER-style interpretable
// rule traces: a downstream analyzer can read this back to explain why
// the candidate was admitted.
//
// 64 bits is a soft cap on rule count for Phase 3-6. If the rule set
// outgrows that, widen to std::array<uint64_t, N> before changing the
// JSON schema.
using RuleIdMask = std::uint64_t;

// Per-note provenance record.
//
// Invariant: every NoteEvent emitted by Renderer carries one
// NoteProvenance. provenance.json is built from this collection.
// generated.json (the evaluation export) excludes these fields.
struct NoteProvenance {
  SpanId span_id = kInvalidSpanId;
  VoiceIntent voice_intent = VoiceIntent::FillerGap;
  float candidate_score = 0.0f;
  NoteSource source = NoteSource::Compose;
  RuleIdMask satisfied_rules = 0;
  std::uint16_t rejected_alternatives = 0;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_PROVENANCE_H
