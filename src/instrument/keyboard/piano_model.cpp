// Piano instrument model implementation.

#include "instrument/keyboard/piano_model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace bach {

PianoModel::PianoModel(KeyboardSpanConstraints span_constraints,
                       KeyboardHandPhysics hand_physics)
    : span_constraints_(span_constraints), hand_physics_(hand_physics) {}

PianoModel::PianoModel()
    : span_constraints_(KeyboardSpanConstraints::intermediate()),
      hand_physics_(KeyboardHandPhysics::intermediate()) {}

uint8_t PianoModel::getLowestPitch() const { return kMinPitch; }

uint8_t PianoModel::getHighestPitch() const { return kMaxPitch; }

bool PianoModel::isPitchInRange(uint8_t pitch) const {
  return pitch >= kMinPitch && pitch <= kMaxPitch;
}

VoicingHandAssignment PianoModel::assignHands(
    const std::vector<uint8_t>& pitches) const {
  VoicingHandAssignment result;
  if (pitches.empty()) {
    result.is_valid = true;
    return result;
  }

  // All pitches must be in range.
  for (uint8_t pitch : pitches) {
    if (!isPitchInRange(pitch)) {
      result.is_valid = false;
      return result;
    }
  }

  // Single note: assign to the closer hand based on pitch.
  if (pitches.size() == 1) {
    if (pitches[0] < 60) {
      result.left_pitches.push_back(pitches[0]);
    } else {
      result.right_pitches.push_back(pitches[0]);
    }
    result.is_valid = true;
    return result;
  }

  // Sort pitches ascending (caller should provide sorted, but be safe).
  std::vector<uint8_t> sorted = pitches;
  std::sort(sorted.begin(), sorted.end());

  // Try every split point: left gets [0..split), right gets [split..end).
  // Choose the split that minimizes combined span penalty.
  float best_cost = 1e9f;
  size_t best_split = 0;
  bool found_valid = false;

  for (size_t split = 0; split <= sorted.size(); ++split) {
    std::vector<uint8_t> left_part(sorted.begin(), sorted.begin() + split);
    std::vector<uint8_t> right_part(sorted.begin() + split, sorted.end());

    bool left_ok =
        left_part.empty() || isPlayableByOneHand(left_part, Hand::Left);
    bool right_ok =
        right_part.empty() || isPlayableByOneHand(right_part, Hand::Right);

    if (left_ok && right_ok) {
      float left_span =
          left_part.empty()
              ? 0.0f
              : static_cast<float>(left_part.back() - left_part.front());
      float right_span =
          right_part.empty()
              ? 0.0f
              : static_cast<float>(right_part.back() - right_part.front());
      float cost = left_span + right_span;

      if (!found_valid || cost < best_cost) {
        best_cost = cost;
        best_split = split;
        found_valid = true;
      }
    }
  }

  if (!found_valid) {
    result.is_valid = false;
    return result;
  }

  result.left_pitches.assign(sorted.begin(), sorted.begin() + best_split);
  result.right_pitches.assign(sorted.begin() + best_split, sorted.end());
  result.is_valid = true;
  return result;
}

bool PianoModel::isPlayableByOneHand(const std::vector<uint8_t>& pitches,
                                     Hand /*hand*/) const {
  if (pitches.empty()) return true;
  if (pitches.size() > span_constraints_.max_notes) return false;

  // Check range validity.
  for (uint8_t pitch : pitches) {
    if (!isPitchInRange(pitch)) return false;
  }

  // Check span: difference between lowest and highest pitch.
  std::vector<uint8_t> sorted = pitches;
  std::sort(sorted.begin(), sorted.end());
  uint8_t span = sorted.back() - sorted.front();

  return span <= span_constraints_.max_span;
}

bool PianoModel::isVoicingPlayable(const std::vector<uint8_t>& pitches) const {
  VoicingHandAssignment assignment = assignHands(pitches);
  return assignment.is_valid;
}

KeyboardPlayabilityCost PianoModel::calculateTransitionCost(
    const KeyboardState& from_state,
    const std::vector<uint8_t>& to_pitches) const {
  KeyboardPlayabilityCost result;

  if (to_pitches.empty()) {
    result.is_playable = true;
    result.total = 0.0f;
    return result;
  }

  VoicingHandAssignment assignment = assignHands(to_pitches);
  if (!assignment.is_valid) {
    result.is_playable = false;
    result.total = 1e6f;
    return result;
  }

  // Left hand cost: movement from current center position.
  result.left_hand_cost = computeHandCost(assignment.left_pitches, from_state.left);

  // Right hand cost: movement from current center position.
  result.right_hand_cost = computeHandCost(assignment.right_pitches, from_state.right);

  // Transition cost: distance each hand must travel from current position.
  float left_travel = 0.0f;
  if (!assignment.left_pitches.empty()) {
    uint8_t new_center = static_cast<uint8_t>(
        (assignment.left_pitches.front() + assignment.left_pitches.back()) / 2);
    int distance = std::abs(static_cast<int>(new_center) -
                            static_cast<int>(from_state.left.center_pitch));
    left_travel = static_cast<float>(distance) * hand_physics_.jump_cost_per_semitone;
  }
  float right_travel = 0.0f;
  if (!assignment.right_pitches.empty()) {
    uint8_t new_center = static_cast<uint8_t>(
        (assignment.right_pitches.front() + assignment.right_pitches.back()) / 2);
    int distance = std::abs(static_cast<int>(new_center) -
                            static_cast<int>(from_state.right.center_pitch));
    right_travel = static_cast<float>(distance) * hand_physics_.jump_cost_per_semitone;
  }
  result.transition_cost = left_travel + right_travel;

  result.total =
      result.left_hand_cost + result.right_hand_cost + result.transition_cost;
  result.is_playable = true;
  return result;
}

std::vector<uint8_t> PianoModel::suggestPlayableVoicing(
    const std::vector<uint8_t>& desired_pitches) const {
  if (desired_pitches.empty()) return {};

  // First, clamp all pitches to range.
  std::vector<uint8_t> clamped;
  clamped.reserve(desired_pitches.size());
  for (uint8_t pitch : desired_pitches) {
    clamped.push_back(clampPitch(pitch));
  }
  std::sort(clamped.begin(), clamped.end());

  // Remove duplicates after clamping.
  clamped.erase(std::unique(clamped.begin(), clamped.end()), clamped.end());

  // If playable as-is, return directly.
  if (isVoicingPlayable(clamped)) return clamped;

  // Strategy 1: Try octave transposition of extreme notes.
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::vector<uint8_t> adjusted = clamped;
    // Move the lowest note up an octave if span is too wide.
    if (adjusted.size() >= 2 &&
        (adjusted.back() - adjusted.front()) > 2 * span_constraints_.max_span) {
      if (adjusted.front() + 12 <= adjusted.back()) {
        adjusted.front() = adjusted.front() + 12;
        std::sort(adjusted.begin(), adjusted.end());
      }
    }
    if (isVoicingPlayable(adjusted)) return adjusted;
  }

  // Strategy 2: Drop notes from the middle until playable.
  // Keep bass and soprano, remove inner voices one at a time.
  std::vector<uint8_t> reduced = clamped;
  while (reduced.size() > 2 && !isVoicingPlayable(reduced)) {
    // Remove the note closest to the center of the voicing.
    size_t mid_idx = reduced.size() / 2;
    reduced.erase(reduced.begin() + mid_idx);
  }
  if (isVoicingPlayable(reduced)) return reduced;

  // Strategy 3: Return just bass and soprano.
  if (clamped.size() >= 2) {
    std::vector<uint8_t> pair = {clamped.front(), clamped.back()};
    if (isVoicingPlayable(pair)) return pair;
  }

  // Last resort: return single closest-to-center pitch.
  return {clampPitch(clamped[clamped.size() / 2])};
}

const KeyboardSpanConstraints& PianoModel::getSpanConstraints() const {
  return span_constraints_;
}

const KeyboardHandPhysics& PianoModel::getHandPhysics() const {
  return hand_physics_;
}

float PianoModel::computeHandCost(const std::vector<uint8_t>& pitches,
                                  const HandState& hand_state) const {
  if (pitches.empty()) return 0.0f;

  float cost = 0.0f;

  // Span cost: penalty for stretching beyond normal span.
  uint8_t span = pitches.back() - pitches.front();
  if (span > span_constraints_.normal_span) {
    int stretch = span - span_constraints_.normal_span;
    cost += static_cast<float>(stretch) * hand_physics_.stretch_cost_per_semitone;
  }

  // Movement cost: distance from current hand center.
  uint8_t new_center =
      static_cast<uint8_t>((pitches.front() + pitches.back()) / 2);
  int distance = std::abs(static_cast<int>(new_center) -
                          static_cast<int>(hand_state.center_pitch));
  if (distance > span_constraints_.normal_span) {
    cost += hand_physics_.cross_over_cost;
  }
  cost += static_cast<float>(distance) * hand_physics_.jump_cost_per_semitone;

  // Fatigue contribution.
  cost += hand_state.fatigue * 0.1f;

  return cost;
}

uint8_t PianoModel::clampPitch(uint8_t pitch) const {
  if (pitch < kMinPitch) return kMinPitch;
  if (pitch > kMaxPitch) return kMaxPitch;
  return pitch;
}

}  // namespace bach
