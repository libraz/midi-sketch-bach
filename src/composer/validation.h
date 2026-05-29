#ifndef BACH_COMPOSER_VALIDATION_H
#define BACH_COMPOSER_VALIDATION_H

#include <cstdint>
#include <string>
#include <vector>

#include "composer/span.h"

namespace bach::composer {

// Validator outcome enum. Three flat states; no "warn" or "accepted with
// repair" middle case (the rebuild plan explicitly forbids repair-flag
// acceptance).
enum class ValidationStatus : std::uint8_t {
  Ok = 0,
  // Span failed; CandidateSearch must re-generate within the span.
  FailedSpan = 1,
  // Span re-generation exhausted; back-jump (or seed abort if back-jump
  // also fails). No fallback note is emitted.
  FailedSeed = 2,
};

// One rule violation inside a span. `rule_id` is a stable string token
// (e.g., "parallel_fifth", "voice_crossing", "strong_beat_dissonance") for
// human reading and provenance.json emission. The same token is hashed
// to a RuleIdMask bit at search time.
struct ValidationFailure {
  SpanId span_id = kInvalidSpanId;
  std::string rule_id;
};

// Validator report for one pipeline pass over one piece.
//
// `status == Ok` && `failures.empty()` is the only valid success shape.
// `status != Ok` requires at least one failure entry.
struct ValidationReport {
  ValidationStatus status = ValidationStatus::Ok;
  std::vector<ValidationFailure> failures;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VALIDATION_H
