/// @file
/// @brief Implementation of CounterpointValidator - rule validation, compliance reporting.

#include "counterpoint/counterpoint_validator.h"

#include <sstream>

#include "core/basic_types.h"
#include "counterpoint/counterpoint_state.h"

namespace bach {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CounterpointValidator::CounterpointValidator(const IRuleEvaluator& rules)
    : rules_(rules) {}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

std::vector<RuleViolation> CounterpointValidator::validate(
    const CounterpointState& state) const {
  // Determine the full tick range from all voices.
  Tick min_tick = 0;
  Tick max_tick = 0;
  bool found_any = false;

  for (VoiceId vid : state.getActiveVoices()) {
    const auto& notes = state.getVoiceNotes(vid);
    if (notes.empty()) continue;

    Tick voice_start = notes.front().start_tick;
    const auto& last = notes.back();
    Tick voice_end = last.start_tick + last.duration;

    if (!found_any) {
      min_tick = voice_start;
      max_tick = voice_end;
      found_any = true;
    } else {
      if (voice_start < min_tick) min_tick = voice_start;
      if (voice_end > max_tick) max_tick = voice_end;
    }
  }

  if (!found_any) return {};

  return validate(state, min_tick, max_tick);
}

std::vector<RuleViolation> CounterpointValidator::validate(
    const CounterpointState& state, Tick from_tick, Tick to_tick) const {
  return rules_.validate(state, from_tick, to_tick);
}

// ---------------------------------------------------------------------------
// Compliance rate
// ---------------------------------------------------------------------------

size_t CounterpointValidator::estimateIntervalCount(
    const CounterpointState& state) const {
  // Estimate: for each beat position, count voice pairs with notes.
  // Use the full range and assume roughly one interval check per
  // beat per voice pair.
  const auto& voices = state.getActiveVoices();
  if (voices.size() < 2) return 0;

  Tick max_tick = 0;
  for (VoiceId vid : voices) {
    const auto& notes = state.getVoiceNotes(vid);
    if (!notes.empty()) {
      const auto& last = notes.back();
      Tick end_tick = last.start_tick + last.duration;
      if (end_tick > max_tick) max_tick = end_tick;
    }
  }

  if (max_tick == 0) return 0;

  size_t beats = static_cast<size_t>(max_tick / kTicksPerBeat);
  size_t pairs = voices.size() * (voices.size() - 1) / 2;
  return beats * pairs;
}

float CounterpointValidator::getComplianceRate(
    const CounterpointState& state) const {
  auto violations = validate(state);
  size_t total = estimateIntervalCount(state);

  if (total == 0) return 1.0f;

  // Count only errors (severity >= 1), not warnings.
  size_t error_count = 0;
  for (const auto& viol : violations) {
    if (viol.severity >= 1) {
      ++error_count;
    }
  }

  float rate = 1.0f - static_cast<float>(error_count) /
                           static_cast<float>(total);
  if (rate < 0.0f) rate = 0.0f;
  return rate;
}

// ---------------------------------------------------------------------------
// JSON report
// ---------------------------------------------------------------------------

std::string CounterpointValidator::toJson(
    const CounterpointState& state) const {
  auto violations = validate(state);

  std::ostringstream oss;
  oss << "{\"violations\":[";

  for (size_t idx = 0; idx < violations.size(); ++idx) {
    const auto& viol = violations[idx];
    if (idx > 0) oss << ",";
    oss << "{\"voice1\":" << static_cast<int>(viol.voice1)
        << ",\"voice2\":" << static_cast<int>(viol.voice2)
        << ",\"tick\":" << viol.tick
        << ",\"rule\":\"" << viol.rule << "\""
        << ",\"severity\":" << static_cast<int>(viol.severity) << "}";
  }

  oss << "],\"compliance_rate\":" << getComplianceRate(state) << "}";
  return oss.str();
}

// ---------------------------------------------------------------------------
// Text report
// ---------------------------------------------------------------------------

std::string CounterpointValidator::generateReport(
    const CounterpointState& state) const {
  auto violations = validate(state);
  float compliance = getComplianceRate(state);

  std::ostringstream oss;
  oss << "=== Counterpoint Validation Report ===\n";
  oss << "Voices: " << state.voiceCount() << "\n";
  oss << "Violations: " << violations.size() << "\n";
  oss << "Compliance: " << (compliance * 100.0f) << "%\n";

  if (violations.empty()) {
    oss << "No violations found.\n";
  } else {
    oss << "\nDetails:\n";
    for (size_t idx = 0; idx < violations.size(); ++idx) {
      const auto& viol = violations[idx];
      oss << "  [" << (idx + 1) << "] "
          << (viol.severity == 0 ? "WARNING" : "ERROR") << " "
          << viol.rule
          << " at tick " << viol.tick
          << " (bar " << tickToBar(viol.tick) << ", beat "
          << static_cast<int>(beatInBar(viol.tick)) << ")"
          << " voices " << static_cast<int>(viol.voice1)
          << "/" << static_cast<int>(viol.voice2) << "\n";
    }
  }

  oss << "=== End Report ===\n";
  return oss.str();
}

}  // namespace bach
