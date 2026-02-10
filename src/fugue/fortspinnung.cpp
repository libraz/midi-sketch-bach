// Fortspinnung ("spinning forth") episode generation using motif pool fragments.

#include "fugue/fortspinnung.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/rng_util.h"
#include "fugue/motif_pool.h"
#include "transform/motif_transform.h"
#include "transform/sequence.h"

namespace bach {

namespace {

/// @brief Fixed design values for fragment selection probability per character.
///
/// Each character has a probability distribution over motif pool ranks.
/// These are design values (Principle 4: Trust Design Values), not searched.
/// Index 0 = rank 0 (subject_head), index 1 = rank 1 (characteristic), etc.
struct FragmentSelectionWeights {
  float rank_weights[4] = {0.4f, 0.3f, 0.2f, 0.1f};
};

/// @brief Get character-specific fragment selection weights.
/// @param character Subject character.
/// @return Fixed design weights for fragment selection.
FragmentSelectionWeights weightsForCharacter(SubjectCharacter character) {
  FragmentSelectionWeights weights;
  switch (character) {
    case SubjectCharacter::Severe:
      // Prefer subject_head (rank 0) for thematic unity.
      weights.rank_weights[0] = 0.6f;
      weights.rank_weights[1] = 0.2f;
      weights.rank_weights[2] = 0.1f;
      weights.rank_weights[3] = 0.1f;
      break;
    case SubjectCharacter::Playful:
      // Alternate between fragments, more variety.
      weights.rank_weights[0] = 0.2f;
      weights.rank_weights[1] = 0.3f;
      weights.rank_weights[2] = 0.3f;
      weights.rank_weights[3] = 0.2f;
      break;
    case SubjectCharacter::Noble:
      // Prefer head and characteristic for stately presence.
      weights.rank_weights[0] = 0.4f;
      weights.rank_weights[1] = 0.4f;
      weights.rank_weights[2] = 0.1f;
      weights.rank_weights[3] = 0.1f;
      break;
    case SubjectCharacter::Restless:
      // Frequent fragmentation, favor lower-ranked fragments.
      weights.rank_weights[0] = 0.2f;
      weights.rank_weights[1] = 0.2f;
      weights.rank_weights[2] = 0.3f;
      weights.rank_weights[3] = 0.3f;
      break;
  }
  return weights;
}

/// @brief Select a fragment rank using weighted random selection.
/// @param rng RNG instance.
/// @param weights Fragment selection weights.
/// @param pool_size Number of available motifs in the pool.
/// @return Selected rank index, clamped to [0, pool_size - 1].
size_t selectFragmentRank(std::mt19937& rng, const FragmentSelectionWeights& weights,
                          size_t pool_size) {
  size_t max_rank = std::min(static_cast<size_t>(4), pool_size);
  if (max_rank == 0) return 0;

  // Normalize weights to the available ranks.
  float total = 0.0f;
  for (size_t idx = 0; idx < max_rank; ++idx) {
    total += weights.rank_weights[idx];
  }

  float roll = rng::rollFloat(rng, 0.0f, total);
  float cumulative = 0.0f;
  for (size_t idx = 0; idx < max_rank; ++idx) {
    cumulative += weights.rank_weights[idx];
    if (roll <= cumulative) {
      return idx;
    }
  }
  return max_rank - 1;
}

/// @brief Get the sequential transposition step in semitones for a character.
///
/// Determines how far each sequential repetition is transposed.
/// Design values: Severe = -2 (descending step), Playful = -1 or -3,
/// Noble = -2 (moderate), Restless = -1 (tight).
///
/// @param character Subject character.
/// @param rng RNG for characters with variable step.
/// @return Semitone transposition per sequence step (negative = descending).
int sequenceStepForCharacter(SubjectCharacter character, std::mt19937& rng) {
  switch (character) {
    case SubjectCharacter::Severe:
      return -2;
    case SubjectCharacter::Playful:
      return rng::rollRange(rng, -3, -1);
    case SubjectCharacter::Noble:
      return -2;
    case SubjectCharacter::Restless:
      return -1;
    default:
      return -2;
  }
}

/// @brief Adjust a fragment's pitch to close a large gap with the previous fragment.
///
/// If the gap between the last note of the previous fragment and the first note
/// of this fragment exceeds 4 semitones, transpose this fragment to bring the
/// gap within range. This ensures smooth voice-leading connections.
///
/// @param fragment Fragment to adjust (modified in place).
/// @param prev_last_pitch Last pitch of the preceding fragment.
void closeGapIfNeeded(std::vector<NoteEvent>& fragment, uint8_t prev_last_pitch) {
  if (fragment.empty()) return;

  int gap = static_cast<int>(fragment[0].pitch) - static_cast<int>(prev_last_pitch);
  if (std::abs(gap) <= 4) return;

  // Transpose to bring within 4 semitones. Target: prev_last_pitch + direction * 2.
  int direction = (gap > 0) ? 1 : -1;
  int target_first = static_cast<int>(prev_last_pitch) + direction * 2;
  int transpose_amount = target_first - static_cast<int>(fragment[0].pitch);

  fragment = transposeMelody(fragment, transpose_amount);
}

/// @brief Place a fragment at a given tick offset, assigning voice ID.
/// @param fragment Source fragment notes (tick-normalized to 0).
/// @param offset_tick Tick position for the first note.
/// @param voice Voice ID to assign.
/// @return Placed notes with adjusted start ticks and voice.
std::vector<NoteEvent> placeFragment(const std::vector<NoteEvent>& fragment,
                                     Tick offset_tick, uint8_t voice) {
  std::vector<NoteEvent> placed;
  placed.reserve(fragment.size());
  for (const auto& note : fragment) {
    NoteEvent evt = note;
    evt.start_tick = note.start_tick + offset_tick;
    evt.voice = voice;
    evt.source = BachNoteSource::EpisodeMaterial;
    placed.push_back(evt);
  }
  return placed;
}

}  // namespace

std::vector<NoteEvent> generateFortspinnung(const MotifPool& pool,
                                            Tick start_tick,
                                            Tick duration_ticks,
                                            uint8_t num_voices,
                                            uint32_t seed,
                                            SubjectCharacter character) {
  std::vector<NoteEvent> result;

  if (pool.empty() || duration_ticks == 0) {
    return result;
  }

  std::mt19937 rng(seed);
  FragmentSelectionWeights weights = weightsForCharacter(character);
  int seq_step = sequenceStepForCharacter(character, rng);

  // --- Voice 0: Primary Fortspinnung line ---
  // Select fragments sequentially and connect them via transposition.
  Tick current_tick = 0;
  std::vector<NoteEvent> prev_fragment;

  while (current_tick < duration_ticks) {
    // Select a fragment from the pool by weighted rank.
    size_t rank = selectFragmentRank(rng, weights, pool.size());
    const PooledMotif* motif = pool.getByRank(rank);
    if (motif == nullptr || motif->notes.empty()) break;

    std::vector<NoteEvent> fragment = motif->notes;

    // Apply character-specific transformation.
    switch (character) {
      case SubjectCharacter::Restless:
        // Diminution for urgency.
        fragment = diminishMelody(fragment, 0);
        break;
      case SubjectCharacter::Noble:
        // Augmentation for stateliness.
        fragment = augmentMelody(fragment, 0);
        break;
      case SubjectCharacter::Playful:
        // Alternate between normal and diminished.
        if (current_tick > 0 && rng::rollProbability(rng, 0.5f)) {
          fragment = diminishMelody(fragment, 0);
        }
        break;
      case SubjectCharacter::Severe:
      default:
        // Keep original rhythm for strictness.
        break;
    }

    // Apply sequential transposition based on how many fragments placed so far.
    if (current_tick > 0) {
      int repetition_count = static_cast<int>(current_tick / std::max(motifDuration(fragment),
                                                                       static_cast<Tick>(1)));
      int transpose = seq_step * std::min(repetition_count, 4);
      fragment = transposeMelody(fragment, transpose);
    }

    // Close pitch gap with previous fragment for smooth voice-leading.
    if (!prev_fragment.empty()) {
      closeGapIfNeeded(fragment, prev_fragment.back().pitch);
    }

    // Check if fragment fits within remaining duration.
    Tick frag_dur = motifDuration(fragment);
    if (frag_dur == 0) frag_dur = kTicksPerBeat;

    if (current_tick + frag_dur > duration_ticks) {
      // Truncate: only place notes that fit.
      for (const auto& note : fragment) {
        if (note.start_tick + current_tick >= duration_ticks) break;
        NoteEvent evt = note;
        evt.start_tick = note.start_tick + start_tick + current_tick;
        evt.voice = 0;
        evt.source = BachNoteSource::EpisodeMaterial;
        // Clamp duration to not exceed episode boundary.
        Tick remaining = duration_ticks - (current_tick + note.start_tick);
        if (evt.duration > remaining) evt.duration = remaining;
        result.push_back(evt);
      }
      break;
    }

    // Place the full fragment.
    auto placed = placeFragment(fragment, start_tick + current_tick, 0);
    result.insert(result.end(), placed.begin(), placed.end());

    prev_fragment = fragment;
    current_tick += frag_dur;
  }

  // --- Voice 1: Inverted imitation (if 2+ voices) ---
  if (num_voices >= 2 && !result.empty()) {
    // Collect voice 0 notes.
    std::vector<NoteEvent> voice0_notes;
    for (const auto& note : result) {
      if (note.voice == 0) {
        voice0_notes.push_back(note);
      }
    }

    if (!voice0_notes.empty()) {
      // Invert voice 0 material around the first note's pitch.
      uint8_t pivot = voice0_notes[0].pitch;
      auto inverted = invertMelody(voice0_notes, pivot);

      // Offset by half the first fragment's duration for imitation effect.
      Tick first_frag_dur = kTicksPerBeat;
      if (pool.best() != nullptr && !pool.best()->notes.empty()) {
        first_frag_dur = motifDuration(pool.best()->notes);
        if (first_frag_dur == 0) first_frag_dur = kTicksPerBeat;
      }
      Tick imitation_offset = std::max(first_frag_dur / 2, static_cast<Tick>(kTicksPerBeat));

      for (auto& note : inverted) {
        note.start_tick += imitation_offset;
        note.voice = 1;
        note.source = BachNoteSource::EpisodeMaterial;
        // Only include notes that fit within the episode duration.
        if (note.start_tick < start_tick + duration_ticks) {
          // Clamp duration.
          Tick end_boundary = start_tick + duration_ticks;
          if (note.start_tick + note.duration > end_boundary) {
            note.duration = end_boundary - note.start_tick;
          }
          result.push_back(note);
        }
      }
    }
  }

  return result;
}

}  // namespace bach
