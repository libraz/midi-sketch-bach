// Motif transformations for fugue episode development.

#include "transform/motif_transform.h"

#include <algorithm>
#include <cstdint>

#include "core/pitch_utils.h"
#include "core/scale.h"

namespace bach {

namespace {



}  // namespace

std::vector<NoteEvent> invertMelody(const std::vector<NoteEvent>& notes, uint8_t pivot) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent inverted = note;
    int new_pitch = 2 * static_cast<int>(pivot) - static_cast<int>(note.pitch);
    inverted.pitch = clampPitch(new_pitch, 0, 127);
    result.push_back(inverted);
  }
  return result;
}

std::vector<NoteEvent> retrogradeMelody(const std::vector<NoteEvent>& notes, Tick start_tick) {
  if (notes.empty()) return {};

  // Collect the original offsets relative to the first note's start_tick and durations.
  // We reverse the note order but preserve the rhythmic structure.
  // Original: |--dur0--|gap01|--dur1--|gap12|--dur2--|
  // Retrograde: |--dur2--|gap12|--dur1--|gap01|--dur0--|
  const size_t count = notes.size();
  std::vector<NoteEvent> result(count);

  // Calculate original offsets from the first note
  Tick original_start = notes.front().start_tick;

  // Build a list of (offset_from_start, duration) for each note
  // Then reverse and reassign ticks
  struct NoteSlot {
    Tick offset;    // offset from original_start
    Tick duration;  // note duration
  };
  std::vector<NoteSlot> slots(count);
  for (size_t idx = 0; idx < count; ++idx) {
    slots[idx].offset = notes[idx].start_tick - original_start;
    slots[idx].duration = notes[idx].duration;
  }

  // Compute inter-note gaps: gap[i] = start[i+1] - (start[i] + duration[i])
  // Reversed: the gaps appear in reverse order between reversed notes.
  std::vector<Tick> gaps;
  gaps.reserve(count > 0 ? count - 1 : 0);
  for (size_t idx = 0; idx + 1 < count; ++idx) {
    Tick end_of_current = slots[idx].offset + slots[idx].duration;
    Tick start_of_next = slots[idx + 1].offset;
    // Guard against overlapping notes (gap could be negative in theory,
    // but Tick is unsigned, so treat overlap as zero gap).
    Tick gap = (start_of_next >= end_of_current) ? (start_of_next - end_of_current) : 0;
    gaps.push_back(gap);
  }

  // Build reversed sequence: reversed note order, reversed gap order
  Tick current_tick = start_tick;
  for (size_t idx = 0; idx < count; ++idx) {
    size_t src_idx = count - 1 - idx;  // Index into original notes (reversed)
    result[idx] = notes[src_idx];
    result[idx].start_tick = current_tick;
    current_tick += result[idx].duration;
    // Add the reversed gap (if not the last note)
    if (idx + 1 < count) {
      size_t gap_idx = count - 2 - idx;  // Reversed gap index
      current_tick += gaps[gap_idx];
    }
  }

  return result;
}

std::vector<NoteEvent> augmentMelody(const std::vector<NoteEvent>& notes, Tick start_tick,
                                     int factor) {
  if (notes.empty()) return {};
  if (factor <= 0) factor = 1;

  Tick original_start = notes.front().start_tick;
  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  for (const auto& note : notes) {
    NoteEvent augmented = note;
    // Scale the offset from the original start by the factor
    Tick offset = note.start_tick - original_start;
    augmented.start_tick = start_tick + offset * static_cast<Tick>(factor);
    augmented.duration = note.duration * static_cast<Tick>(factor);
    result.push_back(augmented);
  }

  return result;
}

std::vector<NoteEvent> diminishMelody(const std::vector<NoteEvent>& notes, Tick start_tick,
                                      int factor) {
  if (notes.empty()) return {};
  if (factor <= 0) factor = 1;

  Tick original_start = notes.front().start_tick;
  Tick ufactor = static_cast<Tick>(factor);
  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  for (const auto& note : notes) {
    NoteEvent diminished = note;
    Tick offset = note.start_tick - original_start;
    diminished.start_tick = start_tick + offset / ufactor;
    diminished.duration = std::max(note.duration / ufactor, static_cast<Tick>(1));
    result.push_back(diminished);
  }

  return result;
}

std::vector<NoteEvent> transposeMelody(const std::vector<NoteEvent>& notes, int semitones) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent transposed = note;
    int new_pitch = static_cast<int>(note.pitch) + semitones;
    transposed.pitch = clampPitch(new_pitch, 0, 127);
    result.push_back(transposed);
  }
  return result;
}

std::vector<NoteEvent> invertMelodyDiatonic(const std::vector<NoteEvent>& notes, uint8_t pivot,
                                            Key key, ScaleType scale) {
  if (notes.empty()) return {};

  int pivot_degree = scale_util::pitchToAbsoluteDegree(pivot, key, scale);

  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent inverted = note;
    int note_degree = scale_util::pitchToAbsoluteDegree(note.pitch, key, scale);
    int inverted_degree = 2 * pivot_degree - note_degree;
    inverted.pitch = scale_util::absoluteDegreeToPitch(inverted_degree, key, scale);
    result.push_back(inverted);
  }
  return result;
}

std::vector<NoteEvent> transposeMelodyDiatonic(const std::vector<NoteEvent>& notes,
                                               int degree_steps, Key key, ScaleType scale) {
  if (notes.empty()) return {};

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  uint8_t prev_pitch = 0;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    NoteEvent transposed = notes[idx];
    int note_degree = scale_util::pitchToAbsoluteDegree(notes[idx].pitch, key, scale);
    int new_degree = note_degree + degree_steps;
    uint8_t raw_pitch = scale_util::absoluteDegreeToPitch(new_degree, key, scale);

    // Snap off-scale pitches respecting melodic direction.
    if (!scale_util::isScaleTone(raw_pitch, key, scale)) {
      int int_pitch = static_cast<int>(raw_pitch);
      uint8_t lower = 0;
      uint8_t upper = 0;
      bool found_lower = false;
      bool found_upper = false;
      for (int off = 1; off <= 6; ++off) {
        if (!found_lower && int_pitch - off >= 0) {
          auto cand = static_cast<uint8_t>(int_pitch - off);
          if (scale_util::isScaleTone(cand, key, scale)) {
            lower = cand;
            found_lower = true;
          }
        }
        if (!found_upper && int_pitch + off <= 127) {
          auto cand = static_cast<uint8_t>(int_pitch + off);
          if (scale_util::isScaleTone(cand, key, scale)) {
            upper = cand;
            found_upper = true;
          }
        }
        if (found_lower && found_upper) break;
      }

      // Choose candidate that preserves melodic direction.
      if (found_lower && found_upper) {
        if (prev_pitch > 0) {
          int dir = (degree_steps > 0) ? 1 : (degree_steps < 0) ? -1 : 0;
          if (idx > 0) {
            // Use transposed pitches for direction (not original pitches).
            int local_dir = static_cast<int>(raw_pitch) -
                            static_cast<int>(prev_pitch);
            if (local_dir != 0) dir = (local_dir > 0) ? 1 : -1;
          }
          if (dir > 0) {
            raw_pitch = upper;
          } else if (dir < 0) {
            raw_pitch = lower;
          } else {
            // No direction: pick closer, prefer lower on tie.
            int d_lo = int_pitch - static_cast<int>(lower);
            int d_hi = static_cast<int>(upper) - int_pitch;
            raw_pitch = (d_lo <= d_hi) ? lower : upper;
          }
        } else {
          // First note: prefer closer, lower on tie.
          int d_lo = int_pitch - static_cast<int>(lower);
          int d_hi = static_cast<int>(upper) - int_pitch;
          raw_pitch = (d_lo <= d_hi) ? lower : upper;
        }
      } else if (found_lower) {
        raw_pitch = lower;
      } else if (found_upper) {
        raw_pitch = upper;
      }
    }

    transposed.pitch = raw_pitch;
    prev_pitch = raw_pitch;
    result.push_back(transposed);
  }
  return result;
}

}  // namespace bach
