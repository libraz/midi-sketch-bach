// Violin instrument model implementation.

#include "instrument/bowed/violin_model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bach {

ViolinModel::ViolinModel()
    : tuning_(kOpenStrings, kOpenStrings + kNumStrings) {}

bool ViolinModel::isPitchPlayable(uint8_t pitch) const {
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

bool ViolinModel::isOpenString(uint8_t pitch) const {
  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch == kOpenStrings[idx]) return true;
  }
  return false;
}

std::vector<FingerPosition> ViolinModel::getPositionsForPitch(uint8_t pitch) const {
  std::vector<FingerPosition> positions;
  if (pitch < kLowestPitch || pitch > kHighestPitch) return positions;

  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch < kOpenStrings[idx]) continue;

    uint8_t semitones_above = pitch - kOpenStrings[idx];
    if (semitones_above > kMaxSemitonesAboveOpen[idx]) continue;

    FingerPosition pos;
    pos.string_idx = idx;
    pos.pitch_offset = static_cast<int8_t>(semitones_above);

    // Position calculation: each position covers roughly 4 semitones.
    // Violin positions are slightly more compact than cello.
    if (semitones_above == 0) {
      pos.position = 0;  // Open string
    } else {
      // Positions: 1st = semitones 1-4, 2nd = 3-6, 3rd = 5-8, etc.
      // Using simplified model: position = ceil(semitones / 3.5).
      pos.position = static_cast<uint8_t>((semitones_above + 2) / 3);
      if (pos.position == 0) pos.position = 1;
    }

    positions.push_back(pos);
  }

  // Sort by ergonomic preference: lower position number first, then higher string
  // (violin prefers higher strings in general for timbral reasons).
  std::sort(positions.begin(), positions.end(),
            [](const FingerPosition& lhs, const FingerPosition& rhs) {
              if (lhs.position != rhs.position) return lhs.position < rhs.position;
              return lhs.string_idx > rhs.string_idx;  // Prefer higher string
            });

  return positions;
}

bool ViolinModel::isDoubleStopFeasible(uint8_t pitch_a, uint8_t pitch_b) const {
  if (!isPitchPlayable(pitch_a) || !isPitchPlayable(pitch_b)) return false;
  if (pitch_a == pitch_b) return false;

  auto positions_a = getPositionsForPitch(pitch_a);
  auto positions_b = getPositionsForPitch(pitch_b);

  for (const auto& pos_a : positions_a) {
    for (const auto& pos_b : positions_b) {
      if (pos_a.string_idx == pos_b.string_idx) continue;

      int string_distance = static_cast<int>(pos_a.string_idx) -
                            static_cast<int>(pos_b.string_idx);
      if (string_distance < 0) string_distance = -string_distance;

      if (string_distance == 1) {
        // Adjacent strings -- check position compatibility.
        int position_diff = static_cast<int>(pos_a.position) -
                            static_cast<int>(pos_b.position);
        if (position_diff < 0) position_diff = -position_diff;

        if (position_diff <= 1) return true;
      }
    }
  }
  return false;
}

float ViolinModel::doubleStopCost(uint8_t pitch_a, uint8_t pitch_b) const {
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

      float cost = 0.0f;
      uint8_t max_position = std::max(pos_a.position, pos_b.position);
      cost += static_cast<float>(max_position) * 0.04f;
      cost += static_cast<float>(position_diff) * 0.08f;

      // Open string double stops are easiest.
      if (pos_a.position == 0 || pos_b.position == 0) {
        cost *= 0.5f;
      }

      // High position double stops are harder.
      bool uses_high = (pitch_a >= kHighPositionThreshold[pos_a.string_idx]) ||
                       (pitch_b >= kHighPositionThreshold[pos_b.string_idx]);
      if (uses_high) {
        cost += kHighPositionCost;
      }

      best_cost = std::min(best_cost, cost);
    }
  }

  return best_cost;
}

bool ViolinModel::requiresArpeggiation(const std::vector<uint8_t>& pitches) const {
  if (pitches.size() <= 2) return false;

  // Three or more notes always require arpeggiation on a bowed instrument.
  return true;
}

float ViolinModel::stringCrossingCost(uint8_t from_string,
                                      uint8_t to_string) const {
  if (from_string >= kNumStrings || to_string >= kNumStrings) return 1e6f;
  if (from_string == to_string) return 0.0f;

  int distance = static_cast<int>(from_string) - static_cast<int>(to_string);
  if (distance < 0) distance = -distance;

  // Violin string crossings are slightly easier than cello due to flatter bridge.
  // Adjacent crossings: cost 0.08. Skipping: quadratically more costly.
  return static_cast<float>(distance) * kStringCrossCostPerString +
         static_cast<float>((distance - 1) * (distance - 1)) * 0.08f;
}

BowedPlayabilityCost ViolinModel::calculateCost(uint8_t pitch) const {
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
  auto positions = getPositionsForPitch(pitch);
  if (!positions.empty()) {
    uint8_t best_position = positions[0].position;
    if (best_position <= 5) {
      // First 5 positions are comfortable on violin.
      result.left_hand_cost = static_cast<float>(best_position) * 0.03f;
    } else {
      result.left_hand_cost = 0.15f + static_cast<float>(best_position - 5) * 0.06f;
    }
  }

  // High position adds extra cost.
  if (isHighPosition(pitch)) {
    result.left_hand_cost += kHighPositionCost;
  }

  result.total = result.left_hand_cost;
  return result;
}

BowedPlayabilityCost ViolinModel::calculateTransitionCost(
    uint8_t from_pitch, uint8_t to_pitch,
    const BowedPerformerState& state) const {
  BowedPlayabilityCost result;

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

  // Position shift cost.
  auto dest_positions = getPositionsForPitch(to_pitch);
  if (!dest_positions.empty()) {
    int pos_diff = static_cast<int>(dest_positions[0].position) -
                   static_cast<int>(state.current_position);
    if (pos_diff < 0) pos_diff = -pos_diff;

    if (pos_diff > 0) {
      result.shift_cost = static_cast<float>(pos_diff) * kShiftCostPerPosition;

      // Extra cost when crossing the high-position boundary.
      bool was_high = (from_pitch > 0) && isHighPosition(from_pitch);
      bool now_high = isHighPosition(to_pitch);
      if (was_high != now_high) {
        result.shift_cost += kHighPositionCost;
      }
    }
  }

  result.total = result.left_hand_cost + result.string_crossing_cost + result.shift_cost;
  return result;
}

void ViolinModel::updateState(BowedPerformerState& state, uint8_t pitch) const {
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

BowedPerformerState ViolinModel::createInitialState() const {
  BowedPerformerState state;
  state.bow_direction = BowDirection::Down;
  state.current_string = 0;
  state.current_position = 0;
  state.last_pitch = 0;
  state.current_tick = 0;
  state.fatigue = 0.0f;
  return state;
}

bool ViolinModel::isHighPosition(uint8_t pitch) const {
  // Find the most comfortable string and check high-position threshold.
  for (uint8_t idx = kNumStrings; idx > 0; --idx) {
    uint8_t str = idx - 1;
    if (pitch >= kOpenStrings[str] &&
        pitch <= kOpenStrings[str] + kMaxSemitonesAboveOpen[str]) {
      return pitch >= kHighPositionThreshold[str];
    }
  }
  return false;
}

bool ViolinModel::findBestString(uint8_t pitch, uint8_t& out_string_idx,
                                 uint8_t& out_semitones) const {
  uint8_t best_semitones = 255;
  bool found = false;

  for (uint8_t idx = 0; idx < kNumStrings; ++idx) {
    if (pitch < kOpenStrings[idx]) continue;

    uint8_t semitones = pitch - kOpenStrings[idx];
    if (semitones > kMaxSemitonesAboveOpen[idx]) continue;

    if (semitones < best_semitones) {
      best_semitones = semitones;
      out_string_idx = idx;
      out_semitones = semitones;
      found = true;
    }
  }

  return found;
}

uint8_t ViolinModel::findStringForPitch(uint8_t pitch) const {
  uint8_t string_idx = kNumStrings;
  uint8_t semitones = 0;
  if (findBestString(pitch, string_idx, semitones)) {
    return string_idx;
  }
  return kNumStrings;
}

}  // namespace bach
