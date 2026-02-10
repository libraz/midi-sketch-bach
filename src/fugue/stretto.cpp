// Implementation of stretto generation for fugue climax.

#include "fugue/stretto.h"

#include <algorithm>
#include <random>

#include "core/rng_util.h"
#include "transform/motif_transform.h"

namespace bach {

std::vector<NoteEvent> Stretto::allNotes() const {
  std::vector<NoteEvent> all;
  for (const auto& entry : entries) {
    all.insert(all.end(), entry.notes.begin(), entry.notes.end());
  }
  std::sort(all.begin(), all.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick) return lhs.start_tick < rhs.start_tick;
    return lhs.voice < rhs.voice;
  });
  return all;
}

Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick,
                        uint8_t num_voices, uint32_t seed) {
  Stretto stretto;
  stretto.start_tick = start_tick;
  stretto.key = home_key;

  // Clamp voice count to valid range.
  if (num_voices < 2) num_voices = 2;
  if (num_voices > 5) num_voices = 5;

  // Handle empty subject gracefully.
  if (subject.notes.empty() || subject.length_ticks == 0) {
    stretto.end_tick = start_tick;
    return stretto;
  }

  // Design value: entry interval is subject_length / num_voices,
  // but at least 1 bar. Snapped to beat boundary.
  Tick entry_interval = subject.length_ticks / num_voices;
  if (entry_interval < kTicksPerBar) {
    entry_interval = kTicksPerBar;
  }
  // Snap to beat boundary.
  entry_interval = (entry_interval / kTicksPerBeat) * kTicksPerBeat;
  if (entry_interval == 0) {
    entry_interval = kTicksPerBar;
  }

  // Calculate transposition from subject key to home key.
  int semitones = static_cast<int>(home_key) - static_cast<int>(subject.key);
  auto transposed = transposeMelody(subject.notes, semitones);

  // Normalize transposed notes to start at tick 0.
  Tick original_start = transposed[0].start_tick;
  for (auto& note : transposed) {
    note.start_tick -= original_start;
  }

  // Prepare inverted version for odd-indexed entries.
  uint8_t pivot = transposed[0].pitch;
  auto inverted = invertMelody(transposed, pivot);

  // RNG reserved for future use (e.g. slight rhythmic variation).
  std::mt19937 rng_engine(seed);
  (void)rng_engine;  // NOLINT(readability-unused-variable): reserved for future stretto variants

  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    StrettoEntry entry;
    entry.voice_id = idx;
    entry.entry_tick = start_tick + static_cast<Tick>(idx) * entry_interval;

    // Alternate between original and inverted for contrapuntal variety.
    const auto& source_notes = (idx % 2 == 0) ? transposed : inverted;

    entry.notes.reserve(source_notes.size());
    for (const auto& note : source_notes) {
      NoteEvent placed = note;
      placed.start_tick = note.start_tick + entry.entry_tick;
      placed.voice = entry.voice_id;
      entry.notes.push_back(placed);
    }

    stretto.entries.push_back(std::move(entry));
  }

  // End tick: last entry start + subject length.
  stretto.end_tick =
      start_tick + static_cast<Tick>(num_voices - 1) * entry_interval + subject.length_ticks;

  return stretto;
}

}  // namespace bach
