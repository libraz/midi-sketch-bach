// Implementation of stretto generation for fugue climax.

#include "fugue/stretto.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>

#include "core/interval.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
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

constexpr uint8_t kOrganVelocity = 80;

/// @brief Snap a tick value to the nearest beat boundary.
/// @param tick Raw tick value.
/// @return Tick snapped to nearest multiple of kTicksPerBeat.
Tick snapToBeat(Tick tick) {
  if (tick == 0)
    return 0;
  Tick remainder = tick % kTicksPerBeat;
  if (remainder == 0)
    return tick;
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

std::vector<NoteEvent> createPreEntryLeadIn(const StrettoEntry& entry, Tick stretto_start, Key key,
                                            uint8_t num_voices) {
  std::vector<NoteEvent> lead_in;
  if (entry.notes.empty() || entry.entry_tick <= stretto_start) {
    return lead_in;
  }

  Tick lead_start = entry.entry_tick - stretto_start > kTicksPerBar
                        ? entry.entry_tick - kTicksPerBar
                        : stretto_start;
  if (lead_start >= entry.entry_tick) {
    return lead_in;
  }

  auto [lo, hi] = getFugueVoiceRange(entry.voice_id, num_voices);
  uint8_t target = clampPitch(entry.notes.front().pitch, lo, hi);
  int target_degree = scale_util::pitchToAbsoluteDegree(target, key, ScaleType::Major);

  static constexpr int kOffsets[8] = {-4, -3, -2, -1, -3, -2, -1, 0};
  size_t slot_idx = 0;
  for (Tick tick = lead_start; tick < entry.entry_tick; tick += duration::kEighthNote) {
    NoteEvent note;
    note.start_tick = tick;
    note.duration = std::min<Tick>(duration::kEighthNote, entry.entry_tick - tick);
    int offset = kOffsets[slot_idx % 8];
    uint8_t pitch =
        scale_util::absoluteDegreeToPitch(target_degree + offset, key, ScaleType::Major);
    note.pitch = clampPitch(pitch, lo, hi);
    note.velocity = kOrganVelocity;
    note.voice = entry.voice_id;
    note.source = BachNoteSource::EpisodeMaterial;
    lead_in.push_back(note);
    ++slot_idx;
  }
  return lead_in;
}

std::vector<NoteEvent> createPostEntryTail(const StrettoEntry& entry, Tick stretto_end, Key key,
                                           uint8_t num_voices) {
  std::vector<NoteEvent> tail;
  if (entry.notes.empty() || entry.entry_tick >= stretto_end) {
    return tail;
  }

  Tick entry_end = entry.entry_tick;
  uint8_t last_pitch = entry.notes.back().pitch;
  for (const auto& note : entry.notes) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end >= entry_end) {
      entry_end = note_end;
      last_pitch = note.pitch;
    }
  }
  if (entry_end >= stretto_end) {
    return tail;
  }

  Tick tail_end = std::min<Tick>(stretto_end, entry_end + kTicksPerBar);
  if (num_voices == 4 && (entry.voice_id == 0 || entry.voice_id == 2)) {
    tail_end = stretto_end;
  }
  auto [lo, hi] = getFugueVoiceRange(entry.voice_id, num_voices);
  uint8_t anchor = clampPitch(last_pitch, lo, hi);
  int anchor_degree = scale_util::pitchToAbsoluteDegree(anchor, key, ScaleType::Major);
  static constexpr int kOffsets[8] = {0, 1, 2, 1, 0, -1, -2, -1};

  size_t slot_idx = 0;
  for (Tick tick = entry_end; tick < tail_end; tick += duration::kEighthNote) {
    NoteEvent note;
    note.start_tick = tick;
    note.duration = std::min<Tick>(duration::kEighthNote, tail_end - tick);
    int offset = kOffsets[slot_idx % 8];
    if (tick + duration::kEighthNote >= tail_end) {
      offset = 0;
    }
    uint8_t pitch =
        scale_util::absoluteDegreeToPitch(anchor_degree + offset, key, ScaleType::Major);
    note.pitch = clampPitch(pitch, lo, hi);
    note.velocity = kOrganVelocity;
    note.voice = entry.voice_id;
    note.source = BachNoteSource::EpisodeMaterial;
    tail.push_back(note);
    ++slot_idx;
  }
  return tail;
}

void demoteExposedDissonantCores(std::vector<StrettoEntry>& entries) {
  std::vector<NoteEvent*> notes;
  for (auto& entry : entries) {
    for (auto& note : entry.notes) {
      notes.push_back(&note);
    }
  }

  for (auto* note : notes) {
    if (note->source != BachNoteSource::SubjectCore) {
      continue;
    }
    bool dissonant = false;
    Tick note_end = note->start_tick + note->duration;
    for (const auto* other : notes) {
      if (other == note || other->voice == note->voice)
        continue;
      bool protected_other = other->source == BachNoteSource::FugueSubject ||
                             other->source == BachNoteSource::SubjectCore;
      for (Tick tick = note->start_tick; tick < note_end; tick += duration::kSixteenthNote) {
        if (other->start_tick > tick || other->start_tick + other->duration <= tick) {
          continue;
        }
        int interval = absoluteInterval(note->pitch, other->pitch) % 12;
        bool strong_exposed = (tick % kTicksPerBeat) == 0 && !interval_util::isConsonance(interval);
        bool hard_protected_clash =
            protected_other &&
            (interval == 1 || interval == 2 || interval == 6 || interval == 10 || interval == 11);
        if (strong_exposed || hard_protected_clash) {
          dissonant = true;
          break;
        }
      }
      if (dissonant)
        break;
    }
    if (dissonant) {
      note->source = BachNoteSource::EpisodeMaterial;
    }
  }
}

}  // namespace

std::vector<NoteEvent> Stretto::allNotes() const {
  std::vector<NoteEvent> all;
  for (const auto& entry : entries) {
    all.insert(all.end(), entry.notes.begin(), entry.notes.end());
  }
  std::sort(all.begin(), all.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    if (lhs.start_tick != rhs.start_tick)
      return lhs.start_tick < rhs.start_tick;
    return lhs.voice < rhs.voice;
  });
  return all;
}

std::vector<Tick> findValidStrettoIntervals(const std::vector<NoteEvent>& subject_notes,
                                            Tick max_offset) {
  std::vector<Tick> valid;
  if (subject_notes.empty())
    return valid;

  // Calculate subject duration.
  Tick subject_dur = 0;
  for (const auto& note : subject_notes) {
    Tick end = note.start_tick + note.duration;
    if (end > subject_dur)
      subject_dur = end;
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

      if (orig_pitch < 0 || delayed_pitch < 0)
        continue;

      int interval = interval_util::compoundToSimple(orig_pitch - delayed_pitch);
      if (!interval_util::isConsonance(interval)) {
        all_consonant = false;
        break;
      }
    }
    if (all_consonant)
      valid.push_back(offset);
  }
  return valid;
}

Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick, uint8_t num_voices,
                        uint32_t seed, SubjectCharacter character,
                        const uint8_t* voice_last_pitches, Tick estimated_duration) {
  Stretto stretto;
  stretto.start_tick = start_tick;
  stretto.key = home_key;

  // Clamp voice count to valid range.
  if (num_voices < 2)
    num_voices = 2;
  if (num_voices > 5)
    num_voices = 5;

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
    Tick best_diff =
        (base_interval >= best_valid) ? (base_interval - best_valid) : (best_valid - base_interval);
    for (size_t idx = 1; idx < valid_intervals.size(); ++idx) {
      Tick diff = (base_interval >= valid_intervals[idx]) ? (base_interval - valid_intervals[idx])
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

    // Compute octave shift using envelope-aware register fitting.
    auto [lo, hi] = getFugueVoiceRange(idx, num_voices);
    uint8_t voice_last = voice_last_pitches ? voice_last_pitches[idx] : 0;
    float phase_pos = estimated_duration > 0 ? static_cast<float>(cumulative_tick) /
                                                   static_cast<float>(estimated_duration)
                                             : 0.0f;
    RegisterEnvelope envelope = getRegisterEnvelope(FormType::Fugue);
    int oct_shift =
        fitToRegisterWithEnvelope(source_notes, idx, num_voices, phase_pos, envelope, voice_last);

    entry.notes.reserve(source_notes.size());
    for (const auto& note : source_notes) {
      NoteEvent placed = note;
      placed.start_tick = note.start_tick + entry.entry_tick;
      placed.voice = entry.voice_id;
      int shifted_p = static_cast<int>(note.pitch) + oct_shift;
      if (shifted_p < static_cast<int>(lo) || shifted_p > static_cast<int>(hi)) {
        placed.pitch = clampPitch(shifted_p, lo, hi);
      } else {
        placed.pitch = static_cast<uint8_t>(shifted_p);
      }
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

std::vector<NoteEvent> createStrettoFragment(const Subject& subject, float fragment_ratio) {
  if (subject.notes.empty())
    return {};

  // Clamp ratio to [0.1, 1.0].
  if (fragment_ratio < 0.1f)
    fragment_ratio = 0.1f;
  if (fragment_ratio > 1.0f)
    fragment_ratio = 1.0f;

  size_t fragment_count =
      static_cast<size_t>(static_cast<float>(subject.notes.size()) * fragment_ratio);
  if (fragment_count < 1)
    fragment_count = 1;
  if (fragment_count > subject.notes.size())
    fragment_count = subject.notes.size();

  return std::vector<NoteEvent>(subject.notes.begin(), subject.notes.begin() + fragment_count);
}

Stretto generateStretto(const Subject& subject, Key home_key, Tick start_tick, uint8_t num_voices,
                        uint32_t seed, SubjectCharacter character, CounterpointState& cp_state,
                        IRuleEvaluator& cp_rules, CollisionResolver& cp_resolver,
                        const HarmonicTimeline& timeline, const uint8_t* voice_last_pitches,
                        Tick estimated_duration) {
  // Generate unvalidated stretto (with leap guard).
  Stretto stretto = generateStretto(subject, home_key, start_tick, num_voices, seed, character,
                                    voice_last_pitches, estimated_duration);

  // Post-validate each entry's notes.
  for (size_t entry_idx = 0; entry_idx < stretto.entries.size(); ++entry_idx) {
    auto& entry = stretto.entries[entry_idx];
    std::vector<NoteEvent> validated;
    auto lead_in = createPreEntryLeadIn(entry, stretto.start_tick, home_key, num_voices);
    auto tail = createPostEntryTail(entry, stretto.end_tick, home_key, num_voices);
    validated.reserve(entry.notes.size() + lead_in.size() + tail.size());

    // Even entries (original subject) = Immutable (register but don't alter).
    // Odd entries (transformed) = Flexible (full cascade).
    bool is_immutable = (entry_idx % 2 == 0);
    entry.notes.insert(entry.notes.end(), lead_in.begin(), lead_in.end());
    entry.notes.insert(entry.notes.end(), tail.begin(), tail.end());

    // Sort notes by tick for chronological processing.
    std::sort(entry.notes.begin(), entry.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

    size_t subject_note_idx = 0;
    for (const auto& note : entry.notes) {
      bool is_inserted_episode = note.source == BachNoteSource::EpisodeMaterial;
      bool is_late_subject_core = !is_inserted_episode && entry_idx > 0 && subject_note_idx < 10;
      if (!is_inserted_episode) {
        ++subject_note_idx;
      }
      if ((is_immutable || is_late_subject_core) && !is_inserted_episode) {
        // Immutable: try original pitch, accept or reject (rest).
        BachNoteOptions opts;
        opts.voice = note.voice;
        opts.desired_pitch = note.pitch;
        opts.tick = note.start_tick;
        opts.duration = note.duration;
        opts.velocity = note.velocity;
        opts.source =
            is_late_subject_core ? BachNoteSource::SubjectCore : BachNoteSource::FugueSubject;

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

  demoteExposedDissonantCores(stretto.entries);

  return stretto;
}

}  // namespace bach
