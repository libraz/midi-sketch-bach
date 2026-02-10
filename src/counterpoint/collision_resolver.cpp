/// @file
/// @brief Implementation of CollisionResolver - 5-stage pitch conflict resolution cascade.

#include "counterpoint/collision_resolver.h"

#include <cmath>
#include <cstdlib>

#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"

namespace bach {

// ---------------------------------------------------------------------------
// Safety check
// ---------------------------------------------------------------------------

bool CollisionResolver::isSafeToPlace(const CounterpointState& state,
                                      const IRuleEvaluator& rules,
                                      VoiceId voice_id, uint8_t pitch,
                                      Tick tick, Tick /*duration*/) const {
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    // Check consonance on strong beats.
    bool is_strong = (beatInBar(tick) == 0 || beatInBar(tick) == 2);
    int ivl = std::abs(static_cast<int>(pitch) -
                       static_cast<int>(other_note->pitch));
    if (is_strong && !rules.isIntervalConsonant(ivl, is_strong)) {
      return false;
    }

    // Build a temporary state to check parallels.
    // Simulate adding the note and check for parallel perfects.
    const NoteEvent* prev_self = state.getLastNote(voice_id);
    const NoteEvent* prev_other = nullptr;

    // Find previous note for the other voice before tick.
    const auto& other_notes = state.getVoiceNotes(other);
    for (auto iter = other_notes.rbegin(); iter != other_notes.rend();
         ++iter) {
      if (iter->start_tick < tick) {
        prev_other = &(*iter);
        break;
      }
    }

    if (prev_self && prev_other) {
      int prev_ivl = std::abs(static_cast<int>(prev_self->pitch) -
                              static_cast<int>(prev_other->pitch));
      int curr_ivl = ivl;

      // Check parallel perfect consonances.
      int prev_reduced = ((prev_ivl % 12) + 12) % 12;
      int curr_reduced = ((curr_ivl % 12) + 12) % 12;

      bool prev_perfect = (prev_reduced == interval::kUnison ||
                           prev_reduced == interval::kPerfect5th);
      bool curr_perfect = (curr_reduced == interval::kUnison ||
                           curr_reduced == interval::kPerfect5th);

      if (prev_perfect && curr_perfect && prev_reduced == curr_reduced) {
        MotionType motion = rules.classifyMotion(
            prev_self->pitch, pitch,
            prev_other->pitch, other_note->pitch);
        if (motion == MotionType::Parallel ||
            motion == MotionType::Similar) {
          return false;
        }
      }
    }
  }

  // Check voice range.
  const auto* range = state.getVoiceRange(voice_id);
  if (range && (pitch < range->low || pitch > range->high)) {
    // Outside range is allowed (soft penalty) but we flag it as unsafe
    // if it is more than an octave outside.
    int distance = 0;
    if (pitch < range->low) {
      distance = range->low - pitch;
    } else {
      distance = pitch - range->high;
    }
    if (distance > 12) return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Strategy cascade
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::tryStrategy(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    const std::string& strategy) const {
  PlacementResult result;
  result.strategy = strategy;

  if (strategy == "original") {
    if (isSafeToPlace(state, rules, voice_id, desired_pitch, tick,
                      duration)) {
      result.pitch = desired_pitch;
      result.penalty = 0.0f;
      result.accepted = true;
    }
    return result;
  }

  if (strategy == "chord_tone") {
    // Try consonant intervals with the lowest sounding voice.
    // Check pitches at consonant intervals from desired.
    static constexpr int kConsonantOffsets[] = {0, 3, 4, 7, 8, 9, -3,
                                                -4, -7, -8, -9, 12, -12};
    float best_penalty = 2.0f;

    for (int offset : kConsonantOffsets) {
      int candidate = static_cast<int>(desired_pitch) + offset;
      if (candidate < 0 || candidate > 127) continue;

      auto cand_pitch = static_cast<uint8_t>(candidate);
      if (isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                        duration)) {
        float penalty = static_cast<float>(std::abs(offset)) / 12.0f *
                        0.3f;
        if (penalty < best_penalty) {
          best_penalty = penalty;
          result.pitch = cand_pitch;
          result.penalty = penalty;
          result.accepted = true;
        }
      }
    }
    return result;
  }

  if (strategy == "step_shift") {
    float best_penalty = 2.0f;

    for (int delta = 1; delta <= max_search_range_; ++delta) {
      // Try both directions: up and down.
      for (int sign = -1; sign <= 1; sign += 2) {
        int candidate = static_cast<int>(desired_pitch) + delta * sign;
        if (candidate < 0 || candidate > 127) continue;

        auto cand_pitch = static_cast<uint8_t>(candidate);
        if (isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                          duration)) {
          float penalty = static_cast<float>(delta) / 12.0f * 0.5f;
          if (penalty < best_penalty) {
            best_penalty = penalty;
            result.pitch = cand_pitch;
            result.penalty = penalty;
            result.accepted = true;
          }
        }
      }
      // Stop as soon as we find a valid pitch at this distance.
      if (result.accepted) return result;
    }
    return result;
  }

  if (strategy == "octave_shift") {
    // Try one octave up, then one octave down.
    for (int shift : {12, -12}) {
      int candidate = static_cast<int>(desired_pitch) + shift;
      if (candidate < 0 || candidate > 127) continue;

      auto cand_pitch = static_cast<uint8_t>(candidate);
      if (isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                        duration)) {
        result.pitch = cand_pitch;
        result.penalty = 0.7f;
        result.accepted = true;
        return result;
      }
    }
    return result;
  }

  if (strategy == "rest") {
    // Last resort: give up.
    result.pitch = 0;
    result.penalty = 1.0f;
    result.accepted = false;
    return result;
  }

  return result;
}

PlacementResult CollisionResolver::findSafePitch(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick,
    Tick duration) const {
  // 5-stage strategy cascade.
  static const char* kStrategies[] = {
      "original", "chord_tone", "step_shift", "octave_shift", "rest"};

  for (const char* strategy : kStrategies) {
    PlacementResult result = tryStrategy(state, rules, voice_id,
                                         desired_pitch, tick, duration,
                                         strategy);
    if (result.accepted) return result;
  }

  // If all strategies fail, return a rest result.
  PlacementResult rest;
  rest.pitch = 0;
  rest.penalty = 1.0f;
  rest.strategy = "rest";
  rest.accepted = false;
  return rest;
}

PlacementResult CollisionResolver::resolvePedal(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick,
    Tick duration) const {
  // Same cascade as findSafePitch, but add voice-range penalty.
  PlacementResult result =
      findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);

  if (result.accepted) {
    // Add range penalty if the final pitch is outside the voice range.
    const auto* range = state.getVoiceRange(voice_id);
    if (range) {
      float range_penalty = 0.0f;
      if (result.pitch < range->low) {
        range_penalty =
            static_cast<float>(range->low - result.pitch) * 0.05f;
      } else if (result.pitch > range->high) {
        range_penalty =
            static_cast<float>(result.pitch - range->high) * 0.05f;
      }
      result.penalty += range_penalty;
    }
  }

  return result;
}

void CollisionResolver::setMaxSearchRange(int semitones) {
  if (semitones > 0) {
    max_search_range_ = semitones;
  }
}

}  // namespace bach
