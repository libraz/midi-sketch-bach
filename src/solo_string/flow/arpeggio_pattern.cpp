// Implementation of arpeggio pattern generation and validation.

#include "solo_string/flow/arpeggio_pattern.h"

#include <algorithm>

namespace bach {

const char* arpeggioPatternTypeToString(ArpeggioPatternType type) {
  switch (type) {
    case ArpeggioPatternType::Rising:        return "Rising";
    case ArpeggioPatternType::Falling:       return "Falling";
    case ArpeggioPatternType::Oscillating:   return "Oscillating";
    case ArpeggioPatternType::PedalPoint:    return "PedalPoint";
    case ArpeggioPatternType::ScaleFragment: return "ScaleFragment";
  }
  return "Unknown";
}

const char* patternRoleToString(PatternRole role) {
  switch (role) {
    case PatternRole::Drive:   return "Drive";
    case PatternRole::Expand:  return "Expand";
    case PatternRole::Sustain: return "Sustain";
    case PatternRole::Release: return "Release";
  }
  return "Unknown";
}

bool isPatternRoleOrderValid(const std::vector<PatternRole>& roles) {
  if (roles.empty()) {
    return true;
  }

  // Order: Drive(0) -> Expand(1) -> Sustain(2) -> Release(3)
  // Must be monotonically non-decreasing.
  for (size_t idx = 1; idx < roles.size(); ++idx) {
    if (static_cast<uint8_t>(roles[idx]) < static_cast<uint8_t>(roles[idx - 1])) {
      return false;
    }
  }

  return true;
}

std::vector<ArpeggioPatternType> getAllowedPatternsForPhase(ArcPhase phase) {
  switch (phase) {
    case ArcPhase::Ascent:
      // Ascending energy: no Falling (register shrinking), no PedalPoint (static).
      return {ArpeggioPatternType::Rising,
              ArpeggioPatternType::Oscillating,
              ArpeggioPatternType::ScaleFragment};

    case ArcPhase::Peak:
      // Maximum variety at climax: all types allowed.
      return {ArpeggioPatternType::Rising,
              ArpeggioPatternType::Falling,
              ArpeggioPatternType::Oscillating,
              ArpeggioPatternType::PedalPoint,
              ArpeggioPatternType::ScaleFragment};

    case ArcPhase::Descent:
      // Winding down: no Rising (register expansion), no ScaleFragment (forward motion).
      return {ArpeggioPatternType::Falling,
              ArpeggioPatternType::Oscillating,
              ArpeggioPatternType::PedalPoint};
  }

  // Fallback: Rising only (should not be reached).
  return {ArpeggioPatternType::Rising};
}

namespace {

/// @brief Arrange degrees in ascending order for a Rising pattern.
/// @param degrees Input scale degrees.
/// @return Sorted degrees low to high.
std::vector<int> arrangeRising(std::vector<int> degrees) {
  std::sort(degrees.begin(), degrees.end());
  return degrees;
}

/// @brief Arrange degrees in descending order for a Falling pattern.
/// @param degrees Input scale degrees.
/// @return Sorted degrees high to low.
std::vector<int> arrangeFalling(std::vector<int> degrees) {
  std::sort(degrees.begin(), degrees.end(), std::greater<int>());
  return degrees;
}

/// @brief Arrange degrees in alternating low/high order for an Oscillating pattern.
/// @param degrees Input scale degrees.
/// @return Degrees arranged as low-high-low-high interleave.
std::vector<int> arrangeOscillating(std::vector<int> degrees) {
  if (degrees.size() <= 1) {
    return degrees;
  }

  std::sort(degrees.begin(), degrees.end());

  std::vector<int> result;
  result.reserve(degrees.size());

  size_t low_idx = 0;
  size_t high_idx = degrees.size() - 1;
  bool pick_low = true;

  while (low_idx <= high_idx) {
    if (pick_low) {
      result.push_back(degrees[low_idx]);
      ++low_idx;
    } else {
      result.push_back(degrees[high_idx]);
      if (high_idx == 0) break;  // Prevent underflow on unsigned
      --high_idx;
    }
    pick_low = !pick_low;
  }

  return result;
}

/// @brief Arrange degrees with the lowest repeated between others (pedal point).
/// @param degrees Input scale degrees.
/// @return Degrees with lowest note interleaved: [low, d1, low, d2, low, d3, ...].
std::vector<int> arrangePedalPoint(std::vector<int> degrees) {
  if (degrees.size() <= 1) {
    return degrees;
  }

  std::sort(degrees.begin(), degrees.end());
  int pedal = degrees[0];

  std::vector<int> result;
  result.reserve(degrees.size() * 2);

  for (size_t idx = 1; idx < degrees.size(); ++idx) {
    result.push_back(pedal);
    result.push_back(degrees[idx]);
  }

  return result;
}

/// @brief Arrange degrees as consecutive scale steps for a ScaleFragment.
///
/// Takes the chord degrees and fills in stepwise motion between them.
/// If input is {0, 4, 7}, output might be {0, 1, 2, 3, 4, 5, 6, 7}
/// but capped at the original count for conciseness.
/// @param degrees Input scale degrees.
/// @return Consecutive integers from min to max of input degrees,
///         limited to original degree count.
std::vector<int> arrangeScaleFragment(std::vector<int> degrees) {
  if (degrees.empty()) {
    return degrees;
  }

  std::sort(degrees.begin(), degrees.end());
  int min_deg = degrees.front();
  int max_deg = degrees.back();

  std::vector<int> result;
  result.reserve(degrees.size());

  // Fill stepwise from min, limited to the number of original degrees.
  for (int deg = min_deg;
       deg <= max_deg && result.size() < degrees.size();
       ++deg) {
    result.push_back(deg);
  }

  // If not enough steps (very narrow range), pad with available degrees.
  while (result.size() < degrees.size()) {
    result.push_back(max_deg);
  }

  return result;
}

/// @brief Select a pattern type based on ArcPhase and PatternRole.
///
/// Deterministic mapping (no randomness):
/// - Drive + Ascent/Peak -> Rising
/// - Expand + any -> Oscillating
/// - Sustain + any -> PedalPoint
/// - Release + Descent -> Falling
/// - Otherwise -> first allowed pattern for the phase.
///
/// @param phase Current ArcPhase.
/// @param role Current PatternRole.
/// @return Selected ArpeggioPatternType.
ArpeggioPatternType selectPatternType(ArcPhase phase, PatternRole role) {
  // Deterministic role-based selection.
  switch (role) {
    case PatternRole::Drive:
      if (phase == ArcPhase::Ascent || phase == ArcPhase::Peak) {
        return ArpeggioPatternType::Rising;
      }
      // Descent + Drive: Falling makes more sense than Rising.
      return ArpeggioPatternType::Falling;

    case PatternRole::Expand:
      return ArpeggioPatternType::Oscillating;

    case PatternRole::Sustain:
      return ArpeggioPatternType::PedalPoint;

    case PatternRole::Release:
      if (phase == ArcPhase::Descent || phase == ArcPhase::Peak) {
        return ArpeggioPatternType::Falling;
      }
      // Ascent + Release: use ScaleFragment for gentle stepwise motion.
      return ArpeggioPatternType::ScaleFragment;
  }

  // Fallback: first allowed pattern for the phase.
  auto allowed = getAllowedPatternsForPhase(phase);
  return allowed.empty() ? ArpeggioPatternType::Rising : allowed[0];
}

/// @brief Arrange degrees according to pattern type.
/// @param degrees Input scale degrees.
/// @param type The arpeggio pattern type.
/// @return Degrees arranged per the pattern type.
std::vector<int> arrangeDegrees(const std::vector<int>& degrees,
                                ArpeggioPatternType type) {
  switch (type) {
    case ArpeggioPatternType::Rising:
      return arrangeRising(degrees);
    case ArpeggioPatternType::Falling:
      return arrangeFalling(degrees);
    case ArpeggioPatternType::Oscillating:
      return arrangeOscillating(degrees);
    case ArpeggioPatternType::PedalPoint:
      return arrangePedalPoint(degrees);
    case ArpeggioPatternType::ScaleFragment:
      return arrangeScaleFragment(degrees);
  }
  return degrees;
}

}  // namespace

ArpeggioPattern generatePattern(const std::vector<int>& chord_degrees,
                                ArcPhase phase, PatternRole role,
                                bool use_open_strings) {
  ArpeggioPattern pattern;
  pattern.role = role;
  pattern.use_open_string = use_open_strings;
  pattern.notes_per_beat = 4;  // Standard 16th-note arpeggio

  // Select type based on phase and role.
  pattern.type = selectPatternType(phase, role);

  // Validate against allowed patterns for the phase. If the selected type
  // is not allowed, fall back to the first allowed type.
  auto allowed = getAllowedPatternsForPhase(phase);
  bool type_allowed = false;
  for (auto allowed_type : allowed) {
    if (allowed_type == pattern.type) {
      type_allowed = true;
      break;
    }
  }
  if (!type_allowed && !allowed.empty()) {
    pattern.type = allowed[0];
  }

  // Arrange chord degrees according to the selected pattern type.
  if (chord_degrees.empty()) {
    // Default triad: root, 3rd, 5th.
    pattern.degrees = arrangeDegrees({0, 2, 4}, pattern.type);
  } else {
    pattern.degrees = arrangeDegrees(chord_degrees, pattern.type);
  }

  return pattern;
}

}  // namespace bach
