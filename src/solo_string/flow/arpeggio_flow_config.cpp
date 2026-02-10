// Implementation of flow configuration validation and default creation.

#include "solo_string/flow/arpeggio_flow_config.h"

#include <cmath>

namespace bach {

bool validateGlobalArcConfig(const GlobalArcConfig& config) {
  if (config.phase_assignment.empty()) {
    return false;
  }

  // Track the current phase progression and count Peak sections.
  int peak_count = 0;
  ArcPhase prev_phase = ArcPhase::Ascent;
  bool first = true;

  for (const auto& [section_id, phase] : config.phase_assignment) {
    if (phase == ArcPhase::Peak) {
      ++peak_count;
    }

    if (first) {
      // First section must be Ascent (cannot start at Peak or Descent).
      if (phase != ArcPhase::Ascent) {
        return false;
      }
      prev_phase = phase;
      first = false;
      continue;
    }

    // Monotonic order check: phase can stay the same or advance, never go back.
    // Ascent(0) -> Peak(1) -> Descent(2)
    if (static_cast<uint8_t>(phase) < static_cast<uint8_t>(prev_phase)) {
      return false;
    }

    prev_phase = phase;
  }

  // Exactly one Peak section required.
  return peak_count == 1;
}

GlobalArcConfig createDefaultArcConfig(int num_sections) {
  GlobalArcConfig config;

  if (num_sections < 3) {
    return config;  // Empty -- invalid, caller should check
  }

  // Place Peak at approximately 65% position (config-fixed, seed-independent).
  // For 6 sections: peak_index = ceil(6 * 0.65) - 1 = 3 (0-based), so section 3.
  int peak_index = static_cast<int>(std::ceil(num_sections * 0.65)) - 1;

  // Clamp to valid range: at least 1 Ascent section and 1 Descent section.
  if (peak_index < 1) {
    peak_index = 1;
  }
  if (peak_index >= num_sections - 1) {
    peak_index = num_sections - 2;
  }

  config.phase_assignment.reserve(static_cast<size_t>(num_sections));

  for (int idx = 0; idx < num_sections; ++idx) {
    ArcPhase phase;
    if (idx < peak_index) {
      phase = ArcPhase::Ascent;
    } else if (idx == peak_index) {
      phase = ArcPhase::Peak;
    } else {
      phase = ArcPhase::Descent;
    }
    config.phase_assignment.emplace_back(static_cast<SectionId>(idx), phase);
  }

  return config;
}

}  // namespace bach
