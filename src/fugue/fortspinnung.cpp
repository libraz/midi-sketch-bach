// Fortspinnung ("spinning forth") episode generation using motif pool fragments.

#include "fugue/fortspinnung.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "fugue/motif_pool.h"
#include "transform/motif_transform.h"
#include "fugue/episode.h"
#include "fugue/voice_registers.h"
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

/// @brief Get character-specific fragment selection weights with per-seed variation.
///
/// Applies small RNG-driven variation (+-0.05) to each weight, then re-normalizes
/// so weights sum to 1.0. This provides subtle variety across seeds.
///
/// @param character Subject character.
/// @param rng Mersenne Twister for per-seed variation.
/// @return Design weights with small per-seed perturbation (normalized).
FragmentSelectionWeights weightsForCharacter(SubjectCharacter character,
                                             std::mt19937& rng) {
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

  // Apply per-seed variation and re-normalize to sum to 1.0.
  float total = 0.0f;
  for (int idx = 0; idx < 4; ++idx) {
    weights.rank_weights[idx] = std::max(
        0.01f, weights.rank_weights[idx] + rng::rollFloat(rng, -0.05f, 0.05f));
    total += weights.rank_weights[idx];
  }
  for (int idx = 0; idx < 4; ++idx) {
    weights.rank_weights[idx] /= total;
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

/// @brief Map pitch to target register with octave folding.
/// @param pitch Input pitch (may be outside range).
/// @param lo Lower bound of target register.
/// @param hi Upper bound of target register.
/// @return Clamped pitch within [lo, hi].
uint8_t mapToRegister(int pitch, int lo, int hi) {
  if (pitch < lo) pitch += 12;
  if (pitch > hi) pitch -= 12;
  return clampPitch(pitch, static_cast<uint8_t>(lo), static_cast<uint8_t>(hi));
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
                                            SubjectCharacter character,
                                            Key key) {
  std::vector<NoteEvent> result;

  if (pool.empty() || duration_ticks == 0) {
    return result;
  }

  std::mt19937 rng(seed);
  FragmentSelectionWeights weights = weightsForCharacter(character, rng);
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
        // Alternate between normal and diminished. Per-seed diminution
        // probability varies in [0.40, 0.60] for subtle rhythmic variety.
        if (current_tick > 0 &&
            rng::rollProbability(rng, rng::rollFloat(rng, 0.40f, 0.60f))) {
          fragment = diminishMelody(fragment, 0);
        }
        break;
      case SubjectCharacter::Severe:
      default:
        // Keep original rhythm for strictness.
        break;
    }

    // Apply dotted rhythm variation (30% probability, not for Severe).
    // Alternates long-short pairs within the fragment for rhythmic interest.
    if (character != SubjectCharacter::Severe &&
        rng::rollProbability(rng, 0.30f) && fragment.size() >= 2) {
      for (size_t fi = 0; fi + 1 < fragment.size(); fi += 2) {
        Tick combined = fragment[fi].duration + fragment[fi + 1].duration;
        // Dotted: first note gets 75%, second gets 25%.
        fragment[fi].duration = (combined * 3) / 4;
        fragment[fi + 1].duration = combined / 4;
        // Adjust start tick of the short note.
        fragment[fi + 1].start_tick =
            fragment[fi].start_tick + fragment[fi].duration;
      }
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

    // --- Tritone repair: eliminate augmented 4th (6 semitones) melodic leaps ---
    {
      bool has_tritone = false;
      for (size_t idx = 1; idx < fragment.size(); ++idx) {
        int mel = std::abs(static_cast<int>(fragment[idx].pitch) -
                           static_cast<int>(fragment[idx - 1].pitch));
        if (mel == 6) {
          has_tritone = true;
          break;
        }
      }

      // Stage 1: Re-select from pool (3 attempts).
      if (has_tritone) {
        for (int attempt = 0; attempt < 3 && has_tritone; ++attempt) {
          size_t alt_rank = selectFragmentRank(rng, weights, pool.size());
          const PooledMotif* alt_motif = pool.getByRank(alt_rank);
          if (alt_motif == nullptr || alt_motif->notes.empty()) continue;
          std::vector<NoteEvent> alt_frag = alt_motif->notes;
          if (!prev_fragment.empty()) {
            closeGapIfNeeded(alt_frag, prev_fragment.back().pitch);
          }
          bool alt_tritone = false;
          for (size_t idx = 1; idx < alt_frag.size(); ++idx) {
            int mel = std::abs(static_cast<int>(alt_frag[idx].pitch) -
                               static_cast<int>(alt_frag[idx - 1].pitch));
            if (mel == 6) {
              alt_tritone = true;
              break;
            }
          }
          if (!alt_tritone) {
            fragment = std::move(alt_frag);
            has_tritone = false;
          }
        }
      }

      // Stages 2-4: Fix individual tritone intervals.
      if (has_tritone) {
        for (size_t idx = 1; idx < fragment.size(); ++idx) {
          int mel = static_cast<int>(fragment[idx].pitch) -
                    static_cast<int>(fragment[idx - 1].pitch);
          if (std::abs(mel) != 6) continue;

          // Stage 2: Octave transposition to eliminate tritone.
          uint8_t oct_up =
              (fragment[idx].pitch <= 115) ? fragment[idx].pitch + 12 : fragment[idx].pitch;
          uint8_t oct_dn =
              (fragment[idx].pitch >= 12) ? fragment[idx].pitch - 12 : fragment[idx].pitch;
          int mel_up =
              std::abs(static_cast<int>(oct_up) - static_cast<int>(fragment[idx - 1].pitch));
          int mel_dn =
              std::abs(static_cast<int>(oct_dn) - static_cast<int>(fragment[idx - 1].pitch));
          if (mel_up != 6 && oct_up <= 127) {
            fragment[idx].pitch = oct_up;
            continue;
          }
          if (mel_dn != 6 && oct_dn > 0) {
            fragment[idx].pitch = oct_dn;
            continue;
          }

          // Stage 3: Neighbor substitution (+-1 semitone).
          int shift_dir = (mel > 0) ? -1 : 1;
          uint8_t shifted = static_cast<uint8_t>(
              std::clamp(static_cast<int>(fragment[idx].pitch) + shift_dir, 0, 127));
          if (std::abs(static_cast<int>(shifted) -
                       static_cast<int>(fragment[idx - 1].pitch)) != 6) {
            fragment[idx].pitch = shifted;
            continue;
          }

          // Stage 4: Duration extension + deletion (protect strong beats).
          bool is_strong =
              (fragment[idx].start_tick % kTicksPerBar) % (2 * kTicksPerBeat) == 0;
          if (!is_strong) {
            fragment[idx - 1].duration += fragment[idx].duration;
            fragment.erase(fragment.begin() + static_cast<ptrdiff_t>(idx));
            --idx;  // Re-check new pair.
          }
          break;
        }
      }
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

  // --- Voice 2: Bass fragments + anchor notes (if 3+ voices) ---
  // Strategy: Extract tail motif head (2-3 notes) and augment for bass fragments.
  // Fill gaps between fragments with chord root / 5th anchor notes.
  // Probabilistic emission (70-85%) prevents over-dense bass that causes parallels.
  if (num_voices >= 3 && !result.empty()) {
    std::vector<NoteEvent> voice0_notes;
    for (const auto& note : result) {
      if (note.voice == 0) {
        voice0_notes.push_back(note);
      }
    }

    if (!voice0_notes.empty()) {
      std::mt19937 bass_rng(seed ^ 0xBA550002u);
      // Emission probability: 70-85% per-seed variation.
      float emit_prob = rng::rollFloat(bass_rng, 0.70f, 0.85f);

      // Extract tail motif (last 3 notes of voice 0) and augment (2x duration).
      auto tail = extractTailMotif(voice0_notes, 3);
      // Only use 2-3 notes from the tail for the bass fragment.
      if (tail.size() > 3) {
        tail.resize(3);
      }
      auto bass_fragment = augmentMelody(tail, 0, 2);

      // Transpose fragment to appropriate register from getFugueVoiceRange.
      auto [v2_lo_u, v2_hi_u] = getFugueVoiceRange(2, num_voices);
      int v2_lo = static_cast<int>(v2_lo_u);
      int v2_hi = static_cast<int>(v2_hi_u);
      int v2_offset = 0;
      for (auto& note : bass_fragment) {
        note.pitch = mapToRegister(static_cast<int>(note.pitch) + v2_offset, v2_lo, v2_hi);
      }

      Tick bass_tick = start_tick;
      Tick frag_dur = motifDuration(bass_fragment);
      if (frag_dur == 0) frag_dur = kTicksPerBeat * 2;

      // Alternate between bass fragment and anchor notes across the episode.
      bool use_fragment = true;
      while (bass_tick < start_tick + duration_ticks) {
        if (!rng::rollProbability(bass_rng, emit_prob)) {
          // Skip this segment (rest).
          bass_tick += use_fragment ? frag_dur : kTicksPerBar;
          use_fragment = !use_fragment;
          continue;
        }

        if (use_fragment && !bass_fragment.empty()) {
          // Place augmented tail fragment.
          for (const auto& frag_note : bass_fragment) {
            NoteEvent evt = frag_note;
            evt.start_tick = frag_note.start_tick + bass_tick;
            evt.voice = 2;
            evt.source = BachNoteSource::EpisodeMaterial;
            if (evt.start_tick >= start_tick + duration_ticks) break;
            Tick remaining = start_tick + duration_ticks - evt.start_tick;
            if (evt.duration > remaining) evt.duration = remaining;
            result.push_back(evt);
          }
          bass_tick += frag_dur;
        } else {
          // Anchor note: tonic held for one bar, derived from key.
          int v2_anchor_pitch = 48 + static_cast<int>(key);
          int bass_anchor = mapToRegister(v2_anchor_pitch, v2_lo, v2_hi);

          Tick anchor_dur = std::min(kTicksPerBar,
                                     start_tick + duration_ticks - bass_tick);
          if (anchor_dur > 0) {
            NoteEvent anchor;
            anchor.start_tick = bass_tick;
            anchor.duration = anchor_dur;
            anchor.pitch = static_cast<uint8_t>(bass_anchor);
            anchor.velocity = 80;
            anchor.voice = 2;
            anchor.source = BachNoteSource::EpisodeMaterial;
            result.push_back(anchor);
          }
          bass_tick += kTicksPerBar;
        }
        use_fragment = !use_fragment;
      }
    }
  }

  // --- Voice 3: Pedal foundation (if 4+ voices) ---
  // Alternates between augmented tail fragments and tonic/dominant anchor notes.
  // Hard constraint: no more than 4 consecutive bars of silence.
  if (num_voices >= 4 && !result.empty()) {
    std::vector<NoteEvent> voice0_notes;
    for (const auto& note : result) {
      if (note.voice == 0) voice0_notes.push_back(note);
    }
    if (!voice0_notes.empty()) {
      std::mt19937 pedal_rng(seed ^ 0xBA550003u);
      float emit_prob = rng::rollFloat(pedal_rng, 0.50f, 0.65f);

      // Pedal anchor pitches from key (tonicBassPitch pattern).
      constexpr int kPedalLo = 24;          // C1
      constexpr int kPedalHiNormal = 45;    // A2 (normal upper limit)
      constexpr int kPedalHiAnchor = 50;    // D3 (anchor exception)
      int tonic_bass = 36 + static_cast<int>(key);  // C2 octave
      tonic_bass = clampPitch(tonic_bass, static_cast<uint8_t>(kPedalLo),
                              static_cast<uint8_t>(kPedalHiAnchor));
      int dominant_bass = tonic_bass + 7;   // Perfect 5th
      if (dominant_bass > kPedalHiAnchor) dominant_bass -= 12;
      dominant_bass = clampPitch(dominant_bass, static_cast<uint8_t>(kPedalLo),
                                 static_cast<uint8_t>(kPedalHiAnchor));

      // Augmented tail fragment from voice 0.
      auto tail = extractTailMotif(voice0_notes, 3);
      if (tail.size() > 3) tail.resize(3);
      auto pedal_fragment = augmentMelody(tail, 0, 2);
      for (auto& note : pedal_fragment) {
        note.pitch = mapToRegister(static_cast<int>(note.pitch) - 24,
                                   kPedalLo, kPedalHiNormal);
      }
      Tick frag_dur = motifDuration(pedal_fragment);
      if (frag_dur == 0) frag_dur = kTicksPerBeat * 2;

      // Generate with max-silence hard constraint.
      constexpr int kMaxSilentBars = 4;
      Tick pedal_tick = start_tick;
      int consecutive_silent_bars = 0;
      bool use_fragment = true;

      while (pedal_tick < start_tick + duration_ticks) {
        Tick segment_dur = use_fragment ? frag_dur : kTicksPerBar;
        bool force_emit = (consecutive_silent_bars >= kMaxSilentBars);
        bool emit = force_emit || rng::rollProbability(pedal_rng, emit_prob);

        if (!emit) {
          int bars_skipped = std::max(1, static_cast<int>(segment_dur / kTicksPerBar));
          consecutive_silent_bars += bars_skipped;
          pedal_tick += segment_dur;
          use_fragment = !use_fragment;
          continue;
        }

        consecutive_silent_bars = 0;

        if (use_fragment && !pedal_fragment.empty()) {
          // Augmented tail fragment (0-based start_ticks + pedal_tick).
          for (const auto& frag_note : pedal_fragment) {
            NoteEvent evt = frag_note;
            evt.start_tick = frag_note.start_tick + pedal_tick;
            evt.voice = 3;
            evt.source = BachNoteSource::EpisodeMaterial;
            if (evt.start_tick >= start_tick + duration_ticks) break;
            Tick remaining = start_tick + duration_ticks - evt.start_tick;
            if (evt.duration > remaining) evt.duration = remaining;
            result.push_back(evt);
          }
          pedal_tick += frag_dur;
        } else {
          // Anchor note: tonic or dominant from key (alternating).
          int anchor_pitch = use_fragment ? tonic_bass : dominant_bass;
          Tick anchor_dur = std::min(kTicksPerBar,
                                     start_tick + duration_ticks - pedal_tick);
          if (anchor_dur > 0) {
            NoteEvent anchor;
            anchor.start_tick = pedal_tick;
            anchor.duration = anchor_dur;
            anchor.pitch = static_cast<uint8_t>(anchor_pitch);
            anchor.velocity = 80;
            anchor.voice = 3;
            anchor.source = BachNoteSource::EpisodeMaterial;
            result.push_back(anchor);
          }
          pedal_tick += kTicksPerBar;
        }
        use_fragment = !use_fragment;
      }
    }
  }

  return result;
}

}  // namespace bach
