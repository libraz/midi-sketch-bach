// Cello instrument model implementation.

#include "instrument/bowed/cello_model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bach {

CelloModel::CelloModel()
    : tuning_(kOpenStrings, kOpenStrings + kNumStrings) {}

bool CelloModel::isPitchPlayable(uint8_t pitch) const {
  if (pitch < kLowestPitch || pitch > kHighestPitch) return false;

  // Check if at least one string can produce this pitch.
  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch >= kOpenStrings[idx] &&
        pitch <= kOpenStrings[idx] + kMaxSemitonesAboveOpen[idx]) {
      return true;
    }
  }
  return false;
}

bool CelloModel::isOpenString(uint8_t pitch) const {
  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch == kOpenStrings[idx]) return true;
  }
  return false;
}

std::vector<FingerPosition> CelloModel::getPositionsForPitch(uint8_t pitch) const {
  std::vector<FingerPosition> positions;
  if (pitch < kLowestPitch || pitch > kHighestPitch) return positions;

  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch < kOpenStrings[idx]) continue;

    uint8_t semitones_above = pitch - kOpenStrings[idx];
    if (semitones_above > kMaxSemitonesAboveOpen[idx]) continue;

    FingerPosition pos;
    pos.string_idx = idx;
    pos.pitch_offset = static_cast<int8_t>(semitones_above);

    // Position calculation: each position covers roughly 4 semitones in
    // lower positions, fewer in thumb position due to shorter string segments.
    if (semitones_above == 0) {
      pos.position = 0;  // Open string
    } else if (pitch < kThumbPositionThreshold[idx]) {
      // Normal positions: ~4 semitones per position (1st through 4th+).
      pos.position = static_cast<uint8_t>((semitones_above - 1) / 4 + 1);
    } else {
      // Thumb position: higher position numbers.
      uint8_t above_threshold = pitch - kThumbPositionThreshold[idx];
      // Thumb positions cover ~3 semitones each (shorter vibrating length).
      pos.position = static_cast<uint8_t>(4 + above_threshold / 3);
    }

    positions.push_back(pos);
  }

  // Sort by ergonomic preference: lower position number first, then lower string.
  std::sort(positions.begin(), positions.end(),
            [](const FingerPosition& lhs, const FingerPosition& rhs) {
              if (lhs.position != rhs.position) return lhs.position < rhs.position;
              return lhs.string_idx < rhs.string_idx;
            });

  return positions;
}

bool CelloModel::isDoubleStopFeasible(uint8_t pitch_a, uint8_t pitch_b) const {
  if (!isPitchPlayable(pitch_a) || !isPitchPlayable(pitch_b)) return false;
  if (pitch_a == pitch_b) return false;  // Same pitch is not a double stop

  // Find all possible strings for each pitch.
  auto positions_a = getPositionsForPitch(pitch_a);
  auto positions_b = getPositionsForPitch(pitch_b);

  // Check if any combination uses adjacent strings.
  for (const auto& pos_a : positions_a) {
    for (const auto& pos_b : positions_b) {
      if (pos_a.string_idx == pos_b.string_idx) continue;  // Same string

      int string_distance = static_cast<int>(pos_a.string_idx) -
                            static_cast<int>(pos_b.string_idx);
      if (string_distance < 0) string_distance = -string_distance;

      if (string_distance == 1) {
        // Adjacent strings -- check position compatibility.
        // Both fingers must be reachable in a similar position (within 3 semitones).
        int position_diff = static_cast<int>(pos_a.position) -
                            static_cast<int>(pos_b.position);
        if (position_diff < 0) position_diff = -position_diff;

        if (position_diff <= 1) return true;
      }
    }
  }
  return false;
}

float CelloModel::doubleStopCost(uint8_t pitch_a, uint8_t pitch_b) const {
  if (!isDoubleStopFeasible(pitch_a, pitch_b)) return 1e6f;

  auto positions_a = getPositionsForPitch(pitch_a);
  auto positions_b = getPositionsForPitch(pitch_b);

  float best_cost = 1e6f;

  for (const auto& pos_a : positions_a) {
    for (const auto& pos_b : positions_b) {
      if (pos_a.string_idx == pos_b.string_idx) continue;

      int string_distance = static_cast<int>(pos_a.string_idx) -
                            static_cast<int>(pos_b.string_idx);
      if (string_distance < 0) string_distance = -string_distance;
      if (string_distance != 1) continue;

      int position_diff = static_cast<int>(pos_a.position) -
                          static_cast<int>(pos_b.position);
      if (position_diff < 0) position_diff = -position_diff;
      if (position_diff > 1) continue;

      // Cost: higher positions are harder, position mismatch adds stretch.
      float cost = 0.0f;
      uint8_t max_position = std::max(pos_a.position, pos_b.position);
      cost += static_cast<float>(max_position) * 0.05f;
      cost += static_cast<float>(position_diff) * 0.1f;

      // Open string double stops are easiest.
      if (pos_a.position == 0 || pos_b.position == 0) {
        cost *= 0.5f;
      }

      // Thumb position double stops are harder.
      bool uses_thumb = (pitch_a >= kThumbPositionThreshold[pos_a.string_idx]) ||
                        (pitch_b >= kThumbPositionThreshold[pos_b.string_idx]);
      if (uses_thumb) {
        cost += kThumbPositionCost;
      }

      best_cost = std::min(best_cost, cost);
    }
  }

  return best_cost;
}

bool CelloModel::requiresArpeggiation(const std::vector<uint8_t>& pitches) const {
  if (pitches.size() <= 2) return false;

  // Three or more notes always require arpeggiation on a bowed instrument.
  // The bow can only sustain at most 2 adjacent strings simultaneously.
  return true;
}

float CelloModel::stringCrossingCost(uint8_t from_string,
                                     uint8_t to_string) const {
  if (from_string >= kNumStrings || to_string >= kNumStrings) return 1e6f;
  if (from_string == to_string) return 0.0f;

  int distance = static_cast<int>(from_string) - static_cast<int>(to_string);
  if (distance < 0) distance = -distance;

  // Adjacent string crossings are natural (cost 0.1).
  // Skipping one string is moderately costly (cost 0.3).
  // Skipping two strings is very costly (cost 0.6).
  return static_cast<float>(distance) * kStringCrossCostPerString +
         static_cast<float>((distance - 1) * (distance - 1)) * 0.1f;
}

BowedPlayabilityCost CelloModel::calculateCost(uint8_t pitch) const {
  BowedPlayabilityCost result;

  uint8_t string_idx = 0;
  uint8_t semitones = 0;
  if (!findBestString(pitch, string_idx, semitones)) {
    result.is_playable = false;
    result.total = 1e6f;
    return result;
  }

  result.is_playable = true;

  // Open string: zero left-hand cost.
  if (semitones == 0) {
    result.total = 0.0f;
    return result;
  }

  // Left-hand cost: increases with position height.
  // First 4 positions (1st through 4th) are comfortable.
  auto positions = getPositionsForPitch(pitch);
  if (!positions.empty()) {
    uint8_t best_position = positions[0].position;
    if (best_position <= 4) {
      result.left_hand_cost = static_cast<float>(best_position) * 0.04f;
    } else {
      result.left_hand_cost = 0.16f + static_cast<float>(best_position - 4) * 0.08f;
    }
  }

  // Thumb position adds extra cost.
  if (requiresThumbPosition(pitch)) {
    result.left_hand_cost += kThumbPositionCost;
  }

  result.total = result.left_hand_cost;
  return result;
}

BowedPlayabilityCost CelloModel::calculateTransitionCost(
    uint8_t from_pitch, uint8_t to_pitch,
    const BowedPerformerState& state) const {
  BowedPlayabilityCost result;

  // Check playability of destination pitch.
  if (!isPitchPlayable(to_pitch)) {
    result.is_playable = false;
    result.total = 1e6f;
    return result;
  }

  result.is_playable = true;

  // Base cost of the destination note.
  auto dest_cost = calculateCost(to_pitch);
  result.left_hand_cost = dest_cost.left_hand_cost;

  // String crossing cost.
  uint8_t dest_string = findStringForPitch(to_pitch);
  if (dest_string < kNumStrings) {
    result.string_crossing_cost = stringCrossingCost(state.current_string, dest_string);
  }

  // Position shift cost: based on distance between current and target position.
  auto dest_positions = getPositionsForPitch(to_pitch);
  if (!dest_positions.empty()) {
    int pos_diff = static_cast<int>(dest_positions[0].position) -
                   static_cast<int>(state.current_position);
    if (pos_diff < 0) pos_diff = -pos_diff;

    if (pos_diff > 0) {
      result.shift_cost = static_cast<float>(pos_diff) * kShiftCostPerPosition;

      // Extra cost for crossing the thumb position boundary.
      bool was_thumb = (from_pitch > 0) && requiresThumbPosition(from_pitch);
      bool now_thumb = requiresThumbPosition(to_pitch);
      if (was_thumb != now_thumb) {
        result.shift_cost += kThumbPositionCost;
      }
    }
  }

  result.total = result.left_hand_cost + result.string_crossing_cost + result.shift_cost;
  return result;
}

void CelloModel::updateState(BowedPerformerState& state, uint8_t pitch) const {
  state.last_pitch = pitch;

  // Update string.
  uint8_t string_idx = findStringForPitch(pitch);
  if (string_idx < kNumStrings) {
    state.current_string = string_idx;
  }

  // Update position.
  auto positions = getPositionsForPitch(pitch);
  if (!positions.empty()) {
    state.current_position = positions[0].position;
  }

  // Alternate bow direction.
  state.bow_direction = (state.bow_direction == BowDirection::Down)
                            ? BowDirection::Up
                            : BowDirection::Down;
}

BowedPerformerState CelloModel::createInitialState() const {
  BowedPerformerState state;
  state.bow_direction = BowDirection::Down;
  state.current_string = 0;
  state.current_position = 0;
  state.last_pitch = 0;
  state.current_tick = 0;
  state.fatigue = 0.0f;
  return state;
}

bool CelloModel::requiresThumbPosition(uint8_t pitch) const {
  // Find the most comfortable string for this pitch and check threshold.
  for (uint8_t idx = kNumStrings; idx > 0; --idx) {
    uint8_t str = idx - 1;
    if (pitch >= kOpenStrings[str] &&
        pitch <= kOpenStrings[str] + kMaxSemitonesAboveOpen[str]) {
      return pitch >= kThumbPositionThreshold[str];
    }
  }
  return false;
}

bool CelloModel::findBestString(uint8_t pitch, uint8_t& out_string_idx,
                                uint8_t& out_semitones) const {
  // Prefer the string that gives the lowest position (most comfortable).
  // For cello: higher strings (closer to idx 3) are preferred when the pitch
  // can be played in a lower position there.
  uint8_t best_semitones = 255;
  bool found = false;

  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch < kOpenStrings[idx]) continue;

    uint8_t semitones = pitch - kOpenStrings[idx];
    if (semitones > kMaxSemitonesAboveOpen[idx]) continue;

    // Prefer the string giving the lowest semitone count (lowest position).
    if (semitones < best_semitones) {
      best_semitones = semitones;
      out_string_idx = idx;
      out_semitones = semitones;
      found = true;
    }
  }

  return found;
}

uint8_t CelloModel::findStringForPitch(uint8_t pitch) const {
  uint8_t string_idx = kNumStrings;  // Invalid sentinel
  uint8_t semitones = 0;
  if (findBestString(pitch, string_idx, semitones)) {
    return string_idx;
  }
  return kNumStrings;
}

}  // namespace bach
