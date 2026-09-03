#ifndef BACH_COMPOSER_VALIDATION_H
#define BACH_COMPOSER_VALIDATION_H

#include <cstdint>
#include <string>
#include <vector>

#include "composer/span.h"
#include "core/basic_types.h"

namespace bach::composer {

// Validator outcome enum. Three flat states; no "warn" or "accepted with
// repair" middle case (repair-flag acceptance is not a permitted outcome).
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
// to a RuleIdMask bit at search time. `kind` classifies the violation
// per the project FailKind taxonomy (see core/basic_types.h):
// StructuralFail (missing voices, malformed cadence layout) vs
// MusicalFail (counterpoint / harmony rule violation).
struct ValidationFailure {
  SpanId span_id = kInvalidSpanId;
  std::string rule_id;
  FailKind kind = FailKind::MusicalFail;
};

struct SubjectFeatures {
  int length = 0;
  int range_semitones = 0;
  int unique_pitch_classes = 0;
  int opening_interval = 0;
  int unique_intervals = 0;
  int max_leap = 0;
};

struct StreamSegregationSpan {
  SpanId span_id = kInvalidSpanId;
  int detected_stream_count = 1;
  int cell_based_stream_count = 1;
  int cell_count = 0;
  bool disagrees_with_cell_counterpoint = false;
  int stream_separation_semitones = 0;
  std::vector<int> transition_note_indices;
};

struct VoiceTextureMetrics {
  VoiceId voice = 0;
  double silence_ratio = 0.0;
  int max_repeated_run = 0;
  int min_pitch = 0;
  int max_pitch = 0;
};

struct TextureMetrics {
  int max_active_voices = 0;
  double avg_active_voices = 0.0;
  // Tick-weighted fraction of the piece span where EXACTLY ONE voice is
  // sounding. Shares the same half-open segment decomposition as
  // `avg_active_voices` (sorted union of note onsets/offsets, span
  // [first onset, last offset]); meter-independent (tick-weighted only).
  // 0.0 for empty input.
  double mono_ratio = 0.0;
  int compass_violation_count = 0;
  double register_overlap_ratio = 0.0;
  std::vector<VoiceTextureMetrics> voices;
};

// Which musical relation a counterpoint rule constrains.
//
// A LINEAR rule describes one voice's own melodic succession. When every note
// in a finding is replayed verbatim from declared material, the composer chose
// none of those intervals and cannot repair them without editing the material
// itself, so exempting the finding is sound.
//
// A VERTICAL rule describes a relation between voices sounding together, or
// between a voice and the harmonic plan it was placed against. The composer
// chose that alignment even when it chose none of the pitches, so immutability
// of the operands never exempts it.
enum class RuleGeometry : std::uint8_t {
  // Not a musical classification: a rule id reached the finding recorder
  // without an entry in the rule-geometry table.
  Unclassified = 0,
  Linear = 1,
  Vertical = 2,
};

// Per-rule tally of counterpoint findings for one validation pass.
//
// The counts are taken before any routing decision, so a finding that never
// becomes a failure is still countable. `total - gated - exempted` is the
// number of findings recorded as informational.
struct RuleObservation {
  std::string rule_id;
  int total = 0;     // times the rule matched, before any routing decision
  int gated = 0;     // routed to failures
  int exempted = 0;  // suppressed because every operand is an immutable input
};

// Validator report for one pipeline pass over one piece.
//
// `status == Ok` && `failures.empty()` is the only valid success shape.
// `status != Ok` requires at least one failure entry.
struct ValidationReport {
  ValidationStatus status = ValidationStatus::Ok;
  std::vector<ValidationFailure> failures;
  // Soft observations that are recorded but never gate generation. Unlike
  // `failures`, an entry here does NOT set `status` to FailedSpan and does NOT
  // break the "failures empty" success contract. Used by rules that describe a
  // stylistic property the existing corpus does not uniformly satisfy (e.g.
  // strict octave-invertibility of a countersubject), so they can be reported
  // for provenance/audit without spuriously failing established pieces.
  std::vector<ValidationFailure> informational;
  // Counterpoint rule tallies. Unlike `informational`, which is recorded only
  // for fully authored findings and never leaves the process, this channel is
  // always populated and always exported (generated.v1
  // `counterpoint_observations`), so a rule that is exempted from `failures`
  // stays countable outside the composer. One entry per rule that matched at
  // least once, sorted by `rule_id`.
  std::vector<RuleObservation> observations;
  std::vector<SubjectFeatures> subject_features;
  std::vector<StreamSegregationSpan> stream_segregation;
  std::vector<TextureMetrics> texture_metrics;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_VALIDATION_H
