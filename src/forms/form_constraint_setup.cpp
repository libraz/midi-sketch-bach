// Phase 6: Shared constraint-driven generation helpers for all Organ forms.

#include "forms/form_constraint_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/markov_tables.h"
#include "core/note_source.h"
#include "core/scale.h"
#include "forms/form_utils.h"

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
      notes[idx].duration =
          (trimmed >= duration::kSixteenthNote) ? trimmed : 0;  // Mark for removal.
    }
  }
  // Remove notes with sub-sixteenth duration (from truncation or ornaments).
  notes.erase(
      std::remove_if(notes.begin(), notes.end(),
                     [](const NoteEvent& n) {
                       return n.duration < duration::kSixteenthNote;
                     }),
      notes.end());
}

/// @brief Compute the diatonic step size above a pitch (distance to next scale tone).
/// @param pitch MIDI pitch.
/// @param key Musical key.
/// @param scale Scale type.
/// @return Semitone distance to the next higher scale tone (1 or 2, typically).
static int diatonicStepAbove(uint8_t pitch, Key key, ScaleType scale) {
  for (int offset = 1; offset <= 3; ++offset) {
    int cand = static_cast<int>(pitch) + offset;
    if (cand > 127) break;
    if (scale_util::isScaleTone(static_cast<uint8_t>(cand), key, scale)) {
      return offset;
    }
  }
  return 2;  // Fallback: whole step.
}

/// @brief Compute the diatonic step size below a pitch (distance to next lower scale tone).
/// @param pitch MIDI pitch.
/// @param key Musical key.
/// @param scale Scale type.
/// @return Semitone distance to the next lower scale tone (positive value).
static int diatonicStepBelow(uint8_t pitch, Key key, ScaleType scale) {
  for (int offset = 1; offset <= 3; ++offset) {
    int cand = static_cast<int>(pitch) - offset;
    if (cand < 0) break;
    if (scale_util::isScaleTone(static_cast<uint8_t>(cand), key, scale)) {
      return offset;
    }
  }
  return 2;  // Fallback: whole step.
}

/// @brief Apply diatonic decoration to break a same-pitch run.
///
/// Decoration patterns by run position (0-indexed from first repeated note past
/// max_consecutive):
///   - run=3 (pos 0): upper neighbor (+1 diatonic step)
///   - run=4 (pos 1): lower neighbor (-1 diatonic step)
///   - run>=5 (pos 2+): graduated return with max 3 modified notes per group,
///     alternating (+2_diatonic, +1_diatonic, 0) / (-2_diatonic, -1_diatonic, 0).
///
/// All shifts are snapped to nearestScaleTone and clamped to voice range.
///
/// @param base_pitch The repeated pitch to decorate around.
/// @param run Current run length (must be > max_consecutive).
/// @param key Musical key.
/// @param scale Scale type.
/// @param lo Voice range low bound.
/// @param hi Voice range high bound.
/// @return Decorated pitch, or base_pitch if no valid decoration found.
static uint8_t decorateRepeat(uint8_t base_pitch, int run, Key key,
                              ScaleType scale, uint8_t lo, uint8_t hi) {
  int pos = run - 3;  // 0-indexed position past max_consecutive (assuming default 2).
  int pitch_int = static_cast<int>(base_pitch);
  int cand = pitch_int;

  if (pos == 0) {
    // Upper neighbor: +1 diatonic step.
    cand = pitch_int + diatonicStepAbove(base_pitch, key, scale);
  } else if (pos == 1) {
    // Lower neighbor: -1 diatonic step.
    cand = pitch_int - diatonicStepBelow(base_pitch, key, scale);
  } else {
    // Graduated return: groups of 3 notes with diminishing displacement.
    // Group alternates direction: even groups go up, odd groups go down.
    int group = (pos - 2) / 3;
    int offset_in_group = (pos - 2) % 3;
    bool go_up = (group % 2 == 0);

    // Displacement decreases: 2, 1, 0 diatonic steps.
    int disp = 2 - offset_in_group;
    if (disp <= 0) {
      return base_pitch;  // Third note in group returns to original.
    }

    if (go_up) {
      // Accumulate diatonic steps upward.
      int total = 0;
      for (int step = 0; step < disp; ++step) {
        uint8_t cur = (pitch_int + total > 127)
                          ? 127
                          : static_cast<uint8_t>(pitch_int + total);
        total += diatonicStepAbove(cur, key, scale);
      }
      cand = pitch_int + total;
    } else {
      // Accumulate diatonic steps downward.
      int total = 0;
      for (int step = 0; step < disp; ++step) {
        uint8_t cur = (pitch_int - total < 0)
                          ? 0
                          : static_cast<uint8_t>(pitch_int - total);
        total += diatonicStepBelow(cur, key, scale);
      }
      cand = pitch_int - total;
    }
  }

  // Clamp to voice range.
  if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi)) {
    // Try opposite direction.
    if (cand > static_cast<int>(hi)) {
      cand = pitch_int - diatonicStepBelow(base_pitch, key, scale);
    } else {
      cand = pitch_int + diatonicStepAbove(base_pitch, key, scale);
    }
  }
  if (cand < static_cast<int>(lo) || cand > static_cast<int>(hi)) {
    return base_pitch;  // Cannot decorate within range.
  }

  // Snap to nearest scale tone (should already be on scale, but safety net).
  return scale_util::nearestScaleTone(static_cast<uint8_t>(cand), key, scale);
}

void finalizeFormNotes(
    std::vector<NoteEvent>& notes, uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    Key key, ScaleType scale,
    int max_consecutive) {
  // Base finalize: overlap dedup.
  finalizeFormNotes(notes, num_voices);

  if (!voice_range) return;

  // Voice range clamping (skip protected notes).
  for (auto& note : notes) {
    auto prot = getProtectionLevel(note.source);
    if (prot == ProtectionLevel::Immutable) continue;
    auto [lo, hi] = voice_range(note.voice);
    if (note.pitch < lo) note.pitch = lo;
    if (note.pitch > hi) note.pitch = hi;
  }

  // Break runs of consecutive same-pitch notes within each voice.
  // Notes are already sorted by voice/tick from the base finalize.
  // Skip protected notes (subject, pedal, ground bass, etc.).
  if (max_consecutive < 1) return;
  int run = 1;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].voice == notes[idx - 1].voice &&
        notes[idx].pitch == notes[idx - 1].pitch) {
      ++run;
      if (run > max_consecutive &&
          getProtectionLevel(notes[idx].source) == ProtectionLevel::Flexible) {
        auto [lo, hi] = voice_range(notes[idx].voice);
        notes[idx].pitch = decorateRepeat(notes[idx].pitch, run, key, scale, lo, hi);
      }
    } else {
      run = 1;
    }
  }

  // Lightweight leap resolution: leaps > 5 semitones should be followed by
  // stepwise contrary motion (standard Bach counterpoint practice).
  // Only modifies Flexible notes; skips if it would flatten the contour.
  for (size_t idx = 1; idx + 1 < notes.size(); ++idx) {
    // Only process consecutive notes within the same voice.
    if (notes[idx - 1].voice != notes[idx].voice) continue;
    if (notes[idx].voice != notes[idx + 1].voice) continue;

    int leap = static_cast<int>(notes[idx].pitch) -
               static_cast<int>(notes[idx - 1].pitch);
    int abs_leap = std::abs(leap);
    if (abs_leap <= 5) continue;  // Not a leap worth resolving.

    // The note after the leap should move stepwise in the opposite direction.
    if (getProtectionLevel(notes[idx + 1].source) != ProtectionLevel::Flexible) continue;

    // Compute desired resolution pitch: one diatonic step opposite to leap.
    int resolved;
    if (leap > 0) {
      // Leapt up -> resolve down by one diatonic step.
      resolved = static_cast<int>(notes[idx].pitch) -
                 diatonicStepBelow(notes[idx].pitch, key, scale);
    } else {
      // Leapt down -> resolve up by one diatonic step.
      resolved = static_cast<int>(notes[idx].pitch) +
                 diatonicStepAbove(notes[idx].pitch, key, scale);
    }

    // Check voice range.
    auto [lo, hi] = voice_range(notes[idx + 1].voice);
    if (resolved < static_cast<int>(lo) || resolved > static_cast<int>(hi)) continue;

    // Contour flattening guard: if fixing would make all 3 contour signs
    // identical, skip it.  sign(a-b) for the 3 pairs: (i-1->i), (i->i+1_new),
    // and check the note after i+1 if available.
    int sign_before = (leap > 0) ? 1 : -1;
    int sign_after_new = (resolved > static_cast<int>(notes[idx].pitch)) ? 1
                         : (resolved < static_cast<int>(notes[idx].pitch)) ? -1
                                                                           : 0;

    // Check with the note after idx+1 (if same voice and exists).
    if (idx + 2 < notes.size() && notes[idx + 1].voice == notes[idx + 2].voice) {
      int sign_next = static_cast<int>(notes[idx + 2].pitch) - resolved;
      int sign_next_dir = (sign_next > 0) ? 1 : (sign_next < 0) ? -1 : 0;
      // If all three contour directions are the same, skip (would flatten).
      if (sign_before == sign_after_new && sign_after_new == sign_next_dir &&
          sign_before != 0) {
        continue;
      }
    }

    // Apply the resolution.
    notes[idx + 1].pitch =
        scale_util::nearestScaleTone(static_cast<uint8_t>(resolved), key, scale);
  }
}

namespace form_utils {

void normalizeAndRedistribute(
    std::vector<Track>& tracks, uint8_t num_voices,
    std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range,
    Key key, ScaleType scale,
    int max_consecutive) {
  // Flatten all tracks into a single note vector.
  std::vector<NoteEvent> all_notes;
  for (const auto& track : tracks) {
    all_notes.insert(all_notes.end(), track.notes.begin(), track.notes.end());
  }

  // Run the appropriate finalize pipeline.
  if (voice_range) {
    finalizeFormNotes(all_notes, num_voices, voice_range, key, scale, max_consecutive);
  } else {
    finalizeFormNotes(all_notes, num_voices);
  }

  // Clear and redistribute notes back to tracks by voice index.
  for (auto& track : tracks) {
    track.notes.clear();
  }
  for (auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(std::move(note));
    }
  }

  sortTrackNotes(tracks);
}

}  // namespace form_utils

}  // namespace bach
