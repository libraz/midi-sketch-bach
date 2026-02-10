/// @file
/// @brief Implementation of CollisionResolver - 6-stage pitch conflict resolution cascade.

#include "counterpoint/collision_resolver.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/pitch_utils.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "counterpoint/species_rules.h"

namespace bach {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// @brief Check if a tick falls near a cadence position.
/// @param tick The tick to check.
/// @param cadence_ticks Sorted cadence tick positions.
/// @return True if tick is within kTicksPerBeat of any cadence tick.
static bool isNearCadence(Tick tick, const std::vector<Tick>& cadence_ticks) {
  for (Tick cad_tick : cadence_ticks) {
    Tick distance = (tick >= cad_tick) ? (tick - cad_tick) : (cad_tick - tick);
    if (distance <= kTicksPerBeat) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Safety check
// ---------------------------------------------------------------------------

bool CollisionResolver::isSafeToPlace(const CounterpointState& state,
                                      const IRuleEvaluator& rules,
                                      VoiceId voice_id, uint8_t pitch,
                                      Tick tick, Tick /*duration*/,
                                      uint8_t next_pitch) const {
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
      // When next_pitch is known, check if this dissonance forms a valid
      // non-harmonic tone pattern (passing tone or neighbor tone).
      // Use Fifth species rules which allow the broadest non-harmonic tones.
      bool allowed_as_nht = false;
      if (next_pitch > 0) {
        const NoteEvent* prev_self = state.getLastNote(voice_id);
        if (prev_self) {
          SpeciesRules species_rules(SpeciesType::Fifth);
          bool is_passing = species_rules.isValidPassingTone(
              prev_self->pitch, pitch, next_pitch);
          bool is_neighbor = species_rules.isValidNeighborTone(
              prev_self->pitch, pitch, next_pitch);
          if (is_passing || is_neighbor) {
            allowed_as_nht = true;
          }
        }
      }
      if (!allowed_as_nht) {
        return false;
      }
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

    // Cadence-aware voice leading: when near a cadence tick, compute a bonus
    // for leading-tone resolution (pitch % 12 == 11 resolving up by semitone)
    // or dominant 7th resolution (resolving stepwise down).
    bool near_cadence = !cadence_ticks_.empty() && isNearCadence(tick, cadence_ticks_);
    const NoteEvent* prev_note = state.getLastNote(voice_id);
    int prev_pitch_class = -1;
    if (prev_note) {
      prev_pitch_class = static_cast<int>(prev_note->pitch) % 12;
    }

    for (int delta = 1; delta <= max_search_range_; ++delta) {
      // Try both directions: up and down.
      for (int sign = -1; sign <= 1; sign += 2) {
        int candidate = static_cast<int>(desired_pitch) + delta * sign;
        if (candidate < 0 || candidate > 127) continue;

        auto cand_pitch = static_cast<uint8_t>(candidate);
        if (isSafeToPlace(state, rules, voice_id, cand_pitch, tick,
                          duration)) {
          float penalty = static_cast<float>(delta) / 12.0f * 0.5f;

          // Apply cadence voice-leading bonus.
          if (near_cadence && prev_note) {
            int directed = static_cast<int>(cand_pitch) - static_cast<int>(prev_note->pitch);

            // Leading tone (B in C major, pitch class 11) resolving up by semitone.
            if (prev_pitch_class == 11 && directed == 1) {
              penalty -= 0.3f;
              if (penalty < 0.0f) penalty = 0.0f;
            }

            // 7th of V7 chord resolving stepwise downward (-1 or -2 semitones).
            // The 7th of a V7 in C major is F (pitch class 5).
            // More generally, any pitch that was the 7th resolving down.
            if (prev_pitch_class == 5 && (directed == -1 || directed == -2)) {
              penalty -= 0.3f;
              if (penalty < 0.0f) penalty = 0.0f;
            }
          }

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
      if (!isSafeToPlace(state, rules, voice_id, cand_pitch, tick, duration))
        continue;

      // Reject if the octave shift would cross an adjacent voice.
      if (wouldCrossVoice(state, voice_id, cand_pitch, tick))
        continue;

      result.pitch = cand_pitch;
      result.penalty = 0.7f;
      result.accepted = true;
      return result;
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
    Tick duration, uint8_t next_pitch) const {
  // 6-stage strategy cascade (suspension inserted between chord_tone and step_shift).
  static const char* kStrategies[] = {
      "original", "chord_tone", "suspension", "step_shift", "octave_shift", "rest"};

  for (const char* strategy : kStrategies) {
    // The suspension strategy uses a dedicated method.
    if (std::string(strategy) == "suspension") {
      PlacementResult result =
          trySuspension(state, rules, voice_id, desired_pitch, tick, duration);
      if (result.accepted) return result;
      continue;
    }

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

// ---------------------------------------------------------------------------
// Source-aware strategy cascade
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::findSafePitch(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    BachNoteSource source) const {
  ProtectionLevel level = getProtectionLevel(source);

  // Select allowed strategies based on protection level.
  struct StrategyList {
    const char** strategies;
    int count;
  };

  static const char* kImmutable[] = {"original", "rest"};
  static const char* kStructural[] = {"original", "octave_shift", "rest"};
  static const char* kFlexible[] = {
      "original", "chord_tone", "suspension", "step_shift", "octave_shift", "rest"};

  StrategyList list;
  switch (level) {
    case ProtectionLevel::Immutable:
      list = {kImmutable, 2};
      break;
    case ProtectionLevel::Structural:
      list = {kStructural, 3};
      break;
    case ProtectionLevel::Flexible:
      list = {kFlexible, 6};
      break;
  }

  for (int idx = 0; idx < list.count; ++idx) {
    // The suspension strategy uses a dedicated method.
    if (std::string(list.strategies[idx]) == "suspension") {
      PlacementResult result =
          trySuspension(state, rules, voice_id, desired_pitch, tick, duration);
      if (result.accepted) return result;
      continue;
    }

    PlacementResult result = tryStrategy(state, rules, voice_id,
                                         desired_pitch, tick, duration,
                                         list.strategies[idx]);
    if (result.accepted) return result;
  }

  PlacementResult rest;
  rest.pitch = 0;
  rest.penalty = 1.0f;
  rest.strategy = "rest";
  rest.accepted = false;
  return rest;
}

// ---------------------------------------------------------------------------
// Suspension strategy
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::trySuspension(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t /*desired_pitch*/, Tick tick,
    Tick duration) const {
  PlacementResult result;
  result.strategy = "suspension";

  // A suspension requires a previous pitch to hold.
  if (tick < kTicksPerBeat) return result;

  // Find the pitch this voice had on the previous beat.
  Tick prev_tick = tick - kTicksPerBeat;
  const NoteEvent* prev_note = state.getNoteAt(voice_id, prev_tick);
  if (!prev_note) return result;

  uint8_t held_pitch = prev_note->pitch;

  // Check if holding this pitch creates a dissonance with other voices,
  // and whether stepping down by 1 or 2 semitones resolves it consonantly.
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    int ivl = std::abs(static_cast<int>(held_pitch) -
                       static_cast<int>(other_note->pitch));
    bool is_strong = (beatInBar(tick) == 0 || beatInBar(tick) == 2);
    if (!rules.isIntervalConsonant(ivl, is_strong)) {
      // Verify the resolution: step down from held_pitch should be consonant.
      for (int step = 1; step <= 2; ++step) {
        int resolution_pitch = static_cast<int>(held_pitch) - step;
        if (resolution_pitch < 0) continue;

        int res_ivl = std::abs(resolution_pitch -
                               static_cast<int>(other_note->pitch));
        // Resolution interval must be consonant (check as if on a weak beat
        // since resolutions typically land on the weak part of the beat).
        if (rules.isIntervalConsonant(res_ivl, false)) {
          result.pitch = held_pitch;
          result.penalty = 0.15f;
          result.accepted = true;
          return result;
        }
      }
    }
  }

  // Even without dissonance, do not use suspension strategy --
  // if the held pitch is consonant, "original" or "chord_tone" suffices.
  return result;
}

// ---------------------------------------------------------------------------
// Voice crossing check for octave shift
// ---------------------------------------------------------------------------

bool CollisionResolver::wouldCrossVoice(const CounterpointState& state,
                                        VoiceId voice_id, uint8_t pitch,
                                        Tick tick) const {
  const auto& voices = state.getActiveVoices();

  for (VoiceId other : voices) {
    if (other == voice_id) continue;

    const NoteEvent* other_note = state.getNoteAt(other, tick);
    if (!other_note) continue;

    // Determine voice ordering from registration order.
    // Lower voice_id = higher voice (soprano=0, alto=1, tenor=2, bass=3).
    if (voice_id < other) {
      // We are a higher voice; our pitch must not go below the other.
      if (pitch < other_note->pitch) return true;
    } else {
      // We are a lower voice; our pitch must not go above the other.
      if (pitch > other_note->pitch) return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Pedal resolver
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// 2-beat lookahead resolution
// ---------------------------------------------------------------------------

PlacementResult CollisionResolver::findSafePitchWithLookahead(
    const CounterpointState& state, const IRuleEvaluator& rules,
    VoiceId voice_id, uint8_t desired_pitch, Tick tick, Tick duration,
    uint8_t next_desired_pitch) const {
  // If no lookahead info, fall back to regular resolution.
  if (next_desired_pitch == 0) {
    return findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);
  }

  // Generate candidates: desired pitch +/- 0..max_search_range_.
  struct Candidate {
    uint8_t pitch;
    float current_penalty;
    float next_penalty;
    float total_score;
  };

  static constexpr size_t kMaxCandidates = 50;
  std::vector<Candidate> candidates;
  candidates.reserve(kMaxCandidates);

  for (int delta = 0; delta <= max_search_range_; ++delta) {
    for (int sign = (delta == 0 ? 1 : -1); sign <= 1; sign += 2) {
      int candidate_pitch = static_cast<int>(desired_pitch) + delta * sign;
      if (candidate_pitch < 0 || candidate_pitch > 127) continue;

      auto cand = static_cast<uint8_t>(candidate_pitch);
      if (!isSafeToPlace(state, rules, voice_id, cand, tick, duration)) {
        continue;
      }

      float current_penalty =
          static_cast<float>(delta) / 12.0f * 0.3f;

      // Evaluate next-beat approach quality.
      // Stepwise motion (1-2 semitones) is ideal; small leaps (3-4) are
      // acceptable; larger leaps receive increasing penalty.
      int interval_to_next = std::abs(static_cast<int>(cand) -
                                      static_cast<int>(next_desired_pitch));
      float next_penalty = 0.0f;
      if (interval_to_next <= 2) {
        next_penalty = 0.0f;  // Stepwise -- ideal approach.
      } else if (interval_to_next <= 4) {
        next_penalty = 0.1f;  // Small leap -- acceptable.
      } else {
        next_penalty = static_cast<float>(interval_to_next) / 12.0f * 0.5f;
      }

      float total = current_penalty + 0.5f * next_penalty;
      candidates.push_back({cand, current_penalty, next_penalty, total});

      if (candidates.size() >= kMaxCandidates) break;
    }
    if (candidates.size() >= kMaxCandidates) break;
  }

  if (candidates.empty()) {
    // No safe candidates found -- fall back to the regular cascade.
    return findSafePitch(state, rules, voice_id, desired_pitch, tick, duration);
  }

  // Select the candidate with the lowest total score.
  auto best = std::min_element(
      candidates.begin(), candidates.end(),
      [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.total_score < rhs.total_score;
      });

  PlacementResult result;
  result.pitch = best->pitch;
  result.penalty = best->current_penalty;
  result.strategy = "lookahead";
  result.accepted = true;
  return result;
}

void CollisionResolver::setMaxSearchRange(int semitones) {
  if (semitones > 0) {
    max_search_range_ = semitones;
  }
}

// ---------------------------------------------------------------------------
// Cadence-aware voice leading
// ---------------------------------------------------------------------------

void CollisionResolver::setCadenceTicks(const std::vector<Tick>& ticks) {
  cadence_ticks_ = ticks;
}

}  // namespace bach
