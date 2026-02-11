// Implementation of stretto generation for fugue climax.

#include "fugue/stretto.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

#include "core/note_creator.h"
#include "core/interval.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_timeline.h"
#include "transform/motif_transform.h"

namespace bach {

namespace {

/// @brief Snap a tick value to the nearest beat boundary.
/// @param tick Raw tick value.
/// @return Tick snapped to nearest multiple of kTicksPerBeat.
Tick snapToBeat(Tick tick) {
  if (tick == 0) return 0;
  Tick remainder = tick % kTicksPerBeat;
  if (remainder == 0) return tick;
  // Round to nearest beat boundary.
  if (remainder >= kTicksPerBeat / 2) {
    return tick + (kTicksPerBeat - remainder);
  }
  return tick - remainder;
}

/// @brief Enforce minimum interval of 1 bar and snap to beat boundary.
/// @param interval Raw interval value.
/// @return Clamped and snapped interval (at least kTicksPerBar).
Tick clampAndSnapInterval(Tick interval) {
  if (interval < kTicksPerBar) {
    interval = kTicksPerBar;
  }
  interval = snapToBeat(interval);
  if (interval == 0) {
    interval = kTicksPerBar;
  }
  return interval;
}



}  // namespace

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

std::vector<Tick> findValidStrettoIntervals(const std::vector<NoteEvent>& subject_notes,
                                            Tick max_offset) {
  std::vector<Tick> valid;
  if (subject_notes.empty()) return valid;

  // Calculate subject duration.
  Tick subject_dur = 0;
  for (const auto& note : subject_notes) {
    Tick end = note.start_tick + note.duration;
    if (end > subject_dur) subject_dur = end;
  }

  // Test each beat offset from 1 beat to max_offset.
  for (Tick offset = kTicksPerBeat; offset < max_offset && offset < subject_dur;
       offset += kTicksPerBeat) {
    bool all_consonant = true;
    // For each beat where both entries sound.
    for (Tick beat = offset; beat < subject_dur; beat += kTicksPerBeat) {
      // Find pitch at 'beat' in original and at 'beat - offset' in delayed entry.
      int orig_pitch = -1;
      int delayed_pitch = -1;
      Tick delayed_beat = beat - offset;

      for (const auto& note : subject_notes) {
        if (note.start_tick <= beat && beat < note.start_tick + note.duration) {
          orig_pitch = note.pitch;
        }
        if (note.start_tick <= delayed_beat && delayed_beat < note.start_tick + note.duration) {
          delayed_pitch = note.pitch;
        }
      }

      if (orig_pitch < 0 || delayed_pitch < 0) continue;

      int interval = std::abs(orig_pitch - delayed_pitch) % 12;
      if (!interval_util::isConsonance(interval)) {
        all_consonant = false;
        break;
      }
    }
    if (all_consonant) valid.push_back(offset);
  }
  return valid;
}

Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick,
                        uint8_t num_voices, uint32_t seed,
                        SubjectCharacter character) {
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

  // --- D1: Progressive entry interval shortening ---
  // First entry interval: subject_length / num_voices (minimum 1 bar, beat-snapped).
  Tick base_interval = subject.length_ticks / num_voices;
  base_interval = clampAndSnapInterval(base_interval);

  // --- D3: Use valid stretto intervals when available ---
  // Try to find consonant entry intervals; prefer the one closest to base_interval.
  auto valid_intervals = findValidStrettoIntervals(subject.notes, subject.length_ticks);
  if (!valid_intervals.empty()) {
    // Pick the valid interval closest to the calculated base_interval.
    Tick best_valid = valid_intervals[0];
    Tick best_diff = (base_interval >= best_valid) ? (base_interval - best_valid)
                                                   : (best_valid - base_interval);
    for (size_t idx = 1; idx < valid_intervals.size(); ++idx) {
      Tick diff = (base_interval >= valid_intervals[idx])
                      ? (base_interval - valid_intervals[idx])
                      : (valid_intervals[idx] - base_interval);
      if (diff < best_diff) {
        best_diff = diff;
        best_valid = valid_intervals[idx];
      }
    }
    base_interval = clampAndSnapInterval(best_valid);
  }

  // Build per-entry intervals with progressive shortening.
  // Entry 0 starts at start_tick. Entry i starts after summing intervals 0..i-1.
  // Each interval is 75% of the previous, minimum 1 bar, snapped to beat.
  std::vector<Tick> entry_intervals;
  entry_intervals.reserve(num_voices);
  Tick current_interval = base_interval;
  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    entry_intervals.push_back(current_interval);
    // Compute next interval: 75% of current, clamped and snapped.
    Tick next = (current_interval * 3) / 4;
    current_interval = clampAndSnapInterval(next);
  }

  // Calculate transposition from subject key to home key.
  int semitones = static_cast<int>(home_key) - static_cast<int>(subject.key);
  auto transposed = transposeMelody(subject.notes, semitones);

  // Normalize transposed notes to start at tick 0.
  Tick original_start = transposed[0].start_tick;
  for (auto& note : transposed) {
    note.start_tick -= original_start;
  }

  // --- D2: Prepare character-based transforms for odd-indexed entries ---
  uint8_t pivot = transposed[0].pitch;
  auto inverted = invertMelody(transposed, pivot);
  auto retrograded = retrogradeMelody(transposed, 0);
  auto augmented = augmentMelody(transposed, 0, 2);

  // RNG for potential future stretto variation.
  std::mt19937 rng_engine(seed);
  (void)rng_engine;  // NOLINT(readability-unused-variable): reserved for future stretto variants

  Tick cumulative_tick = start_tick;
  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    StrettoEntry entry;
    entry.voice_id = idx;
    entry.entry_tick = cumulative_tick;

    // Select source notes: even entries use original, odd entries use
    // character-specific transform.
    const std::vector<NoteEvent>* source_ptr = &transposed;
    if (idx % 2 != 0) {
      switch (character) {
        case SubjectCharacter::Playful:
          source_ptr = &retrograded;
          break;
        case SubjectCharacter::Noble:
          source_ptr = &augmented;
          break;
        case SubjectCharacter::Severe:
        case SubjectCharacter::Restless:
        default:
          source_ptr = &inverted;
          break;
      }
    }
    const auto& source_notes = *source_ptr;

    // Compute octave shift to fit the voice's register.
    auto [lo, hi] = getFugueVoiceRange(idx, num_voices);
    int src_total = 0;
    for (const auto& n : source_notes) {
      src_total += static_cast<int>(n.pitch);
    }
    int src_mean = source_notes.empty() ? 60
        : src_total / static_cast<int>(source_notes.size());
    int vc = (static_cast<int>(lo) + static_cast<int>(hi)) / 2;
    int vdiff = vc - src_mean;
    int oct_shift = nearestOctaveShift(vdiff);

    entry.notes.reserve(source_notes.size());
    for (const auto& note : source_notes) {
      NoteEvent placed = note;
      placed.start_tick = note.start_tick + entry.entry_tick;
      placed.voice = entry.voice_id;
      int shifted_p = static_cast<int>(note.pitch) + oct_shift;
      placed.pitch = clampPitch(shifted_p, lo, hi);
      entry.notes.push_back(placed);
    }

    stretto.entries.push_back(std::move(entry));

    // Advance cumulative tick by this entry's interval (for next entry).
    if (idx + 1 < num_voices) {
      cumulative_tick += entry_intervals[idx];
    }
  }

  // End tick: last entry start + subject length (or augmented length for Noble odd entries).
  Tick last_entry_tick = stretto.entries.back().entry_tick;
  // For Noble character, the last entry may use augmented notes (doubled duration).
  Tick last_entry_duration = subject.length_ticks;
  if ((num_voices - 1) % 2 != 0 && character == SubjectCharacter::Noble) {
    last_entry_duration = subject.length_ticks * 2;
  }
  stretto.end_tick = last_entry_tick + last_entry_duration;

  return stretto;
}

std::vector<NoteEvent> createStrettoFragment(const Subject& subject,
                                              float fragment_ratio) {
  if (subject.notes.empty()) return {};

  // Clamp ratio to [0.1, 1.0].
  if (fragment_ratio < 0.1f) fragment_ratio = 0.1f;
  if (fragment_ratio > 1.0f) fragment_ratio = 1.0f;

  size_t fragment_count = static_cast<size_t>(
      static_cast<float>(subject.notes.size()) * fragment_ratio);
  if (fragment_count < 1) fragment_count = 1;
  if (fragment_count > subject.notes.size()) fragment_count = subject.notes.size();

  return std::vector<NoteEvent>(subject.notes.begin(),
                                 subject.notes.begin() + fragment_count);
}

Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick,
                        uint8_t num_voices, uint32_t seed,
                        SubjectCharacter character,
                        CounterpointState& cp_state, IRuleEvaluator& cp_rules,
                        CollisionResolver& cp_resolver,
                        const HarmonicTimeline& timeline) {
  // Generate unvalidated stretto.
  Stretto stretto = generateStretto(subject, home_key, start_tick,
                                    num_voices, seed, character);

  // Post-validate each entry's notes.
  for (size_t entry_idx = 0; entry_idx < stretto.entries.size(); ++entry_idx) {
    auto& entry = stretto.entries[entry_idx];
    std::vector<NoteEvent> validated;
    validated.reserve(entry.notes.size());

    // Even entries (original subject) = Immutable (register but don't alter).
    // Odd entries (transformed) = Flexible (full cascade).
    bool is_immutable = (entry_idx % 2 == 0);

    // Sort notes by tick for chronological processing.
    std::sort(entry.notes.begin(), entry.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    for (const auto& note : entry.notes) {
      if (is_immutable) {
        // Immutable: try original pitch, accept or reject (rest).
        BachNoteOptions opts;
        opts.voice = note.voice;
        opts.desired_pitch = note.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = BachNoteSource::FugueSubject;

        BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
        if (result.accepted) {
          validated.push_back(result.note);
        }
      } else {
        // Flexible: chord-tone snap + full cascade.
        const auto& harm_ev = timeline.getAt(note.start_tick);
        uint8_t desired_pitch = note.pitch;
        bool is_strong = (note.start_tick % kTicksPerBeat == 0);
        if (is_strong && !isChordTone(note.pitch, harm_ev)) {
          desired_pitch = nearestChordTone(note.pitch, harm_ev);
        }

        BachNoteOptions opts;
        opts.voice = note.voice;
        opts.desired_pitch = desired_pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source = BachNoteSource::EpisodeMaterial;

        BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
        if (result.accepted) {
          validated.push_back(result.note);
        }
      }
    }

    entry.notes = std::move(validated);
  }

  return stretto;
}

}  // namespace bach
