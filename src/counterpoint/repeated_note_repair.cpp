// Shared repeated-note repair implementation.
// Consolidates the inline repair block from fugue_generator.cpp into a
// reusable utility for fugue, passacaglia, and prelude generators.

#include "counterpoint/repeated_note_repair.h"

#include <algorithm>
#include <vector>

#include "core/scale.h"

namespace bach {

namespace {

/// @brief Check whether a note source is protected from pitch modification.
/// Protected sources include structural notes (subject, answer, countersubject,
/// pedal, false entry, coda, sequence) and ground bass.
bool isProtectedSource(BachNoteSource source) {
  return isStructuralSource(source) || source == BachNoteSource::GroundBass;
}

/// @brief Attempt to place a replacement pitch at a given semitone offset.
/// @param base_pitch The repeated pitch being replaced.
/// @param delta Signed semitone offset from base_pitch.
/// @param key Current key for scale-tone validation.
/// @param scale Current scale type for scale-tone validation.
/// @param voice Voice index of the note being repaired.
/// @param tick Tick position of the note being repaired.
/// @param params Repair parameters (for voice_range and vertical_safe).
/// @param out_pitch Output: the validated candidate pitch.
/// @return true if the candidate is valid and placed, false otherwise.
bool tryCandidate(uint8_t base_pitch, int delta, Key key, ScaleType scale,
                  uint8_t voice, Tick tick,
                  const RepeatedNoteRepairParams& params,
                  uint8_t& out_pitch) {
  int cand = static_cast<int>(base_pitch) + delta;
  if (cand < 0 || cand > 127) return false;

  uint8_t ucand = static_cast<uint8_t>(cand);

  if (!scale_util::isScaleTone(ucand, key, scale)) return false;

  // Voice range check.
  if (params.voice_range) {
    auto [low, high] = params.voice_range(voice);
    if (ucand < low || ucand > high) return false;
  }

  // Vertical safety check (parallel 5ths/octaves, etc.).
  if (params.vertical_safe) {
    if (!params.vertical_safe(tick, voice, ucand)) return false;
  }

  out_pitch = ucand;
  return true;
}

}  // namespace

int repairRepeatedNotes(std::vector<NoteEvent>& notes,
                        const RepeatedNoteRepairParams& params) {
  if (notes.empty() || params.num_voices == 0) return 0;

  int total_modified = 0;

  // Build per-voice index lists sorted by start_tick.
  std::vector<std::vector<size_t>> voice_indices(params.num_voices);
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    if (notes[idx].voice < params.num_voices) {
      voice_indices[notes[idx].voice].push_back(idx);
    }
  }
  for (auto& indices : voice_indices) {
    std::sort(indices.begin(), indices.end(), [&](size_t lhs, size_t rhs) {
      return notes[lhs].start_tick < notes[rhs].start_tick;
    });
  }

  for (uint8_t voice = 0; voice < params.num_voices; ++voice) {
    const auto& idxs = voice_indices[voice];
    if (idxs.size() < 2) continue;

    int run_len = 1;
    size_t run_start = 0;

    for (size_t pos = 1; pos <= idxs.size(); ++pos) {
      bool same_run = false;
      if (pos < idxs.size()) {
        Tick gap = notes[idxs[pos]].start_tick - notes[idxs[pos - 1]].start_tick;
        same_run = (notes[idxs[pos]].pitch == notes[idxs[run_start]].pitch) &&
                   (gap <= params.run_gap_threshold);
      }

      if (same_run) {
        ++run_len;
        continue;
      }

      // End of run -- repair if it exceeds the maximum allowed length.
      if (run_len > params.max_consecutive) {
        uint8_t base_pitch = notes[idxs[run_start]].pitch;

        // Determine approach direction from the note before the run.
        int prev_dir = 0;
        if (run_start > 0) {
          int diff = static_cast<int>(base_pitch) -
                     static_cast<int>(notes[idxs[run_start - 1]].pitch);
          prev_dir = (diff > 0) ? 1 : (diff < 0) ? -1 : 0;
        }

        int alt = 0;
        for (size_t jdx = run_start + static_cast<size_t>(params.max_consecutive);
             jdx < run_start + static_cast<size_t>(run_len); ++jdx) {
          size_t note_idx = idxs[jdx];

          // Never modify protected sources.
          if (isProtectedSource(notes[note_idx].source)) continue;

          Key cur_key = params.key_at_tick(notes[note_idx].start_tick);
          ScaleType cur_scale = params.scale_at_tick(notes[note_idx].start_tick);

          // Alternate step direction: counter to approach direction.
          // Even alt index: preferred direction; odd: opposite.
          int step_dir = ((alt % 2 == 0) ? -1 : 1);
          if (prev_dir != 0) {
            step_dir = ((alt % 2 == 0) ? -prev_dir : prev_dir);
          }
          ++alt;

          // Try preferred direction: +1, +2, +3 semitones along step_dir.
          bool placed = false;
          uint8_t new_pitch = 0;
          for (int delta = 1; delta <= 3 && !placed; ++delta) {
            placed = tryCandidate(base_pitch, step_dir * delta, cur_key,
                                  cur_scale, voice, notes[note_idx].start_tick,
                                  params, new_pitch);
          }

          // Fallback: try opposite direction if preferred failed.
          if (!placed) {
            for (int delta = 1; delta <= 3 && !placed; ++delta) {
              placed = tryCandidate(base_pitch, -step_dir * delta, cur_key,
                                    cur_scale, voice, notes[note_idx].start_tick,
                                    params, new_pitch);
            }
          }

          if (placed) {
            notes[note_idx].pitch = new_pitch;
            notes[note_idx].modified_by |=
                static_cast<uint8_t>(NoteModifiedBy::RepeatedNoteRep);
            ++total_modified;
          }
        }
      }

      // Reset run for the next group.
      run_start = pos;
      run_len = 1;
    }
  }

  return total_modified;
}

}  // namespace bach
