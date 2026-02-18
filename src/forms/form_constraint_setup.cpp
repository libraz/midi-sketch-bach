// Phase 6: Shared constraint-driven generation helpers for all Organ forms.

#include "forms/form_constraint_setup.h"

#include <algorithm>

#include "core/markov_tables.h"
#include "core/note_source.h"

namespace bach {

ConstraintState setupFormConstraintState(
    uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    Tick total_duration,
    FuguePhase phase,
    float energy,
    const std::vector<Tick>& cadence_ticks) {
  ConstraintState cs;

  // Layer 2: InvariantSet -- use voice 0's range as the default.
  // Callers should override per-voice if needed.
  if (voice_range && num_voices > 0) {
    auto [lo, hi] = voice_range(0);
    cs.invariants.voice_range_lo = lo;
    cs.invariants.voice_range_hi = hi;
  }
  cs.invariants.max_active_voices = num_voices;
  cs.invariants.hard_repeat_limit = 3;

  // Layer 3: Gravity -- wire Markov models from Bach organ corpus.
  cs.gravity.melodic_model = &kFugueUpperMarkov;
  cs.gravity.vertical_table = &kFugueVerticalTable;
  cs.gravity.phase = phase;
  cs.gravity.energy = energy;

  // Configuration.
  cs.total_duration = total_duration;
  cs.cadence_ticks = cadence_ticks;

  return cs;
}

void finalizeFormNotes(std::vector<NoteEvent>& notes,
                       uint8_t /*num_voices*/) {
  if (notes.size() < 2) return;

  // Sort by voice, then tick, then duration descending (longer note wins).
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.voice != rhs.voice) return lhs.voice < rhs.voice;
              if (lhs.start_tick != rhs.start_tick)
                return lhs.start_tick < rhs.start_tick;
              return lhs.duration > rhs.duration;
            });

  // Remove same-tick duplicates within each voice.
  notes.erase(
      std::unique(notes.begin(), notes.end(),
                  [](const NoteEvent& lhs, const NoteEvent& rhs) {
                    return lhs.voice == rhs.voice &&
                           lhs.start_tick == rhs.start_tick;
                  }),
      notes.end());

  // Truncate overlapping notes within each voice.
  // Drop notes truncated below kSixteenthNote (120 ticks).
  for (size_t idx = 0; idx + 1 < notes.size(); ++idx) {
    if (notes[idx].voice != notes[idx + 1].voice) continue;
    Tick end_tick = notes[idx].start_tick + notes[idx].duration;
    if (end_tick > notes[idx + 1].start_tick) {
      Tick trimmed = notes[idx + 1].start_tick - notes[idx].start_tick;
      notes[idx].duration = (trimmed >= 120) ? trimmed : 0;  // Mark for removal.
    }
  }
  // Remove notes with sub-sixteenth duration (from truncation or ornaments).
  notes.erase(
      std::remove_if(notes.begin(), notes.end(),
                     [](const NoteEvent& n) { return n.duration < 120; }),
      notes.end());
}

void finalizeFormNotes(
    std::vector<NoteEvent>& notes, uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    int max_consecutive) {
  // Base finalize: overlap dedup.
  finalizeFormNotes(notes, num_voices);

  if (!voice_range) return;

  // Voice range clamping (skip protected notes).
  for (auto& note : notes) {
    auto pl = getProtectionLevel(note.source);
    if (pl == ProtectionLevel::Immutable) continue;
    auto [lo, hi] = voice_range(note.voice);
    if (note.pitch < lo) note.pitch = lo;
    if (note.pitch > hi) note.pitch = hi;
  }

  // Break runs of consecutive same-pitch notes within each voice.
  // Notes are already sorted by voice/tick from the base finalize.
  // Skip protected notes (subject, pedal, ground bass, etc.).
  if (max_consecutive < 1) return;
  int run = 1;
  for (size_t i = 1; i < notes.size(); ++i) {
    if (notes[i].voice == notes[i - 1].voice &&
        notes[i].pitch == notes[i - 1].pitch) {
      ++run;
      if (run > max_consecutive &&
          getProtectionLevel(notes[i].source) == ProtectionLevel::Flexible) {
        auto [lo, hi] = voice_range(notes[i].voice);
        // Alternate ±2 semitones, clamped to range.
        int delta = (run % 2 == 1) ? 2 : -2;
        int cand = static_cast<int>(notes[i].pitch) + delta;
        if (cand < lo || cand > hi) cand = static_cast<int>(notes[i].pitch) - delta;
        if (cand >= lo && cand <= hi) {
          notes[i].pitch = static_cast<uint8_t>(cand);
        }
      }
    } else {
      run = 1;
    }
  }
}

}  // namespace bach
