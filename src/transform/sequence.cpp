// Sequential transposition (Sequence/Zeugma) for fugue episodes.

#include "transform/sequence.h"

#include <algorithm>
#include <cstdint>

#include "transform/motif_transform.h"

namespace bach {

Tick motifDuration(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0;

  Tick min_start = notes.front().start_tick;
  Tick max_end = 0;

  for (const auto& note : notes) {
    if (note.start_tick < min_start) {
      min_start = note.start_tick;
    }
    Tick note_end = note.start_tick + note.duration;
    if (note_end > max_end) {
      max_end = note_end;
    }
  }

  return max_end - min_start;
}

std::vector<NoteEvent> generateSequence(const std::vector<NoteEvent>& motif, int repetitions,
                                        int interval_step, Tick start_tick) {
  if (motif.empty() || repetitions <= 0) return {};

  Tick dur = motifDuration(motif);
  Tick motif_start = motif.front().start_tick;
  // Find the actual minimum start in case notes are not sorted
  for (const auto& note : motif) {
    if (note.start_tick < motif_start) {
      motif_start = note.start_tick;
    }
  }

  std::vector<NoteEvent> result;
  result.reserve(motif.size() * static_cast<size_t>(repetitions));

  for (int rep = 1; rep <= repetitions; ++rep) {
    // Transpose the motif by (interval_step * rep) semitones
    std::vector<NoteEvent> transposed = transposeMelody(motif, interval_step * rep);

    // Offset tick positions: place this repetition after previous ones
    Tick tick_offset = start_tick + dur * static_cast<Tick>(rep - 1) - motif_start;
    for (auto& note : transposed) {
      note.start_tick += tick_offset;
    }

    result.insert(result.end(), transposed.begin(), transposed.end());
  }

  return result;
}

std::vector<NoteEvent> generateDiatonicSequence(const std::vector<NoteEvent>& motif,
                                                int repetitions, int degree_step,
                                                Tick start_tick, Key key, ScaleType scale) {
  if (motif.empty() || repetitions <= 0) return {};

  Tick dur = motifDuration(motif);
  Tick motif_start = motif.front().start_tick;
  for (const auto& note : motif) {
    if (note.start_tick < motif_start) {
      motif_start = note.start_tick;
    }
  }

  std::vector<NoteEvent> result;
  result.reserve(motif.size() * static_cast<size_t>(repetitions));

  for (int rep = 1; rep <= repetitions; ++rep) {
    std::vector<NoteEvent> transposed =
        transposeMelodyDiatonic(motif, degree_step * rep, key, scale);

    Tick tick_offset = start_tick + dur * static_cast<Tick>(rep - 1) - motif_start;
    for (auto& note : transposed) {
      note.start_tick += tick_offset;
    }

    result.insert(result.end(), transposed.begin(), transposed.end());
  }

  return result;
}

}  // namespace bach
