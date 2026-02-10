// Implementation of fugue exposition: voice entry scheduling, note placement,
// and countersubject assignment.

#include "fugue/exposition.h"

#include <algorithm>
#include <random>

#include "core/note_source.h"
#include "core/rng_util.h"

namespace bach {

namespace {

/// @brief Assign a VoiceRole based on entry index within the exposition.
///
/// Role assignment is fixed and immutable:
///   Index 0 -> Assert  (presents first subject)
///   Index 1 -> Respond (presents first answer)
///   Index 2 -> Propel  (counterpoint drive)
///   Index 3+ -> Ground (bass foundation)
///
/// @param entry_index 0-based index of the voice entry.
/// @return The VoiceRole for this entry position.
VoiceRole assignVoiceRole(uint8_t entry_index) {
  switch (entry_index) {
    case 0:  return VoiceRole::Assert;
    case 1:  return VoiceRole::Respond;
    case 2:  return VoiceRole::Propel;
    default: return VoiceRole::Ground;
  }
}

/// @brief Determine whether an entry at this index presents subject or answer.
///
/// Entries alternate: even indices (0, 2, 4) present the subject,
/// odd indices (1, 3) present the answer. This follows standard fugal
/// practice where subject (tonic) and answer (dominant) alternate.
///
/// @param entry_index 0-based entry index.
/// @return true if this entry presents the subject, false for answer.
bool isSubjectEntry(uint8_t entry_index) {
  return (entry_index % 2) == 0;
}

/// @brief Place subject or answer notes for a voice entry, offset by entry tick.
///
/// Copies source notes into the target voice's note list, adjusting
/// start_tick by the entry offset and assigning the correct voice_id.
///
/// @param source_notes Notes to place (from subject or answer).
/// @param voice_id Target voice identifier.
/// @param entry_tick Tick offset for this entry.
/// @param voice_notes Output map to append notes to.
void placeEntryNotes(const std::vector<NoteEvent>& source_notes,
                     VoiceId voice_id,
                     Tick entry_tick,
                     std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  for (const auto& src_note : source_notes) {
    NoteEvent note = src_note;
    note.start_tick = src_note.start_tick + entry_tick;
    note.voice = voice_id;
    voice_notes[voice_id].push_back(note);
  }
}

/// @brief Place countersubject notes for a voice that has finished its entry.
///
/// When a new voice enters with subject/answer, the voice that most recently
/// completed its own entry plays the countersubject. The countersubject notes
/// are offset to start at the new entry's tick position.
///
/// @param cs_notes Countersubject notes to place.
/// @param voice_id Voice that plays the countersubject.
/// @param start_tick Tick when the countersubject begins.
/// @param voice_notes Output map to append notes to.
void placeCountersubjectNotes(const std::vector<NoteEvent>& cs_notes,
                              VoiceId voice_id,
                              Tick start_tick,
                              std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  for (const auto& cs_note : cs_notes) {
    NoteEvent note = cs_note;
    note.start_tick = cs_note.start_tick + start_tick;
    note.voice = voice_id;
    voice_notes[voice_id].push_back(note);
  }
}

/// @brief Generate simple free counterpoint filler for earlier voices.
///
/// When a voice is two or more entries behind the current entry, it plays
/// free counterpoint material. Currently this generates sustained tonic
/// notes as a placeholder. Full free counterpoint generation will be
/// implemented with the counterpoint engine integration.
///
/// @param voice_id Voice to generate filler for.
/// @param start_tick When the filler begins.
/// @param duration_ticks How long the filler lasts.
/// @param key Musical key for pitch selection.
/// @param rng Random number generator for variation.
/// @param voice_notes Output map to append notes to.
void placeFreeCounterpoint(VoiceId voice_id,
                           Tick start_tick,
                           Tick duration_ticks,
                           Key key,
                           std::mt19937& rng,  // NOLINT(runtime/references): mt19937 must be mutable
                           std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  if (duration_ticks == 0) return;

  // Generate sustained notes on scale degrees as placeholder free counterpoint.
  // Use whole-note durations (one per bar) at tonic pitch.
  int tonic_pitch = 60 + static_cast<int>(key);  // C4 transposed by key offset.

  // Vary the pitch slightly based on voice and RNG for voice independence.
  int pitch_offset = rng::rollRange(rng, -3, 3);
  int pitch = tonic_pitch + pitch_offset;

  // Clamp to valid range.
  if (pitch < 36) pitch = 36;
  if (pitch > 96) pitch = 96;

  Tick current_tick = start_tick;
  Tick remaining = duration_ticks;

  while (remaining > 0) {
    Tick note_dur = std::min(remaining, kTicksPerBar);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = note_dur;
    note.pitch = static_cast<uint8_t>(pitch);
    note.velocity = 80;
    note.voice = voice_id;
    voice_notes[voice_id].push_back(note);

    current_tick += note_dur;
    remaining -= note_dur;

    // Slight pitch variation for next note.
    pitch_offset = rng::rollRange(rng, -2, 2);
    pitch = tonic_pitch + pitch_offset;
    if (pitch < 36) pitch = 36;
    if (pitch > 96) pitch = 96;
  }
}

/// @brief Clamp the number of voices to the valid range [2, 5].
/// @param num_voices Raw voice count from configuration.
/// @return Clamped voice count.
uint8_t clampVoiceCount(uint8_t num_voices) {
  if (num_voices < 2) return 2;
  if (num_voices > 5) return 5;
  return num_voices;
}

}  // namespace

// ---------------------------------------------------------------------------
// Exposition::allNotes
// ---------------------------------------------------------------------------

std::vector<NoteEvent> Exposition::allNotes() const {
  std::vector<NoteEvent> all_notes;

  // Pre-calculate total size for efficiency.
  size_t total_count = 0;
  for (const auto& [vid, notes] : voice_notes) {
    total_count += notes.size();
  }
  all_notes.reserve(total_count);

  // Collect all notes from every voice.
  for (const auto& [vid, notes] : voice_notes) {
    all_notes.insert(all_notes.end(), notes.begin(), notes.end());
  }

  // Sort by start_tick, then by voice_id for deterministic ordering.
  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
              if (lhs.start_tick != rhs.start_tick) {
                return lhs.start_tick < rhs.start_tick;
              }
              return lhs.voice < rhs.voice;
            });

  return all_notes;
}

// ---------------------------------------------------------------------------
// buildExposition
// ---------------------------------------------------------------------------

Exposition buildExposition(const Subject& subject,
                           const Answer& answer,
                           const Countersubject& countersubject,
                           const FugueConfig& config,
                           uint32_t seed) {
  Exposition expo;
  std::mt19937 rng(seed);

  uint8_t num_voices = clampVoiceCount(config.num_voices);

  // Entry interval: each voice enters after the previous voice's
  // subject/answer completes. Use subject length as the standard interval.
  Tick entry_interval = subject.length_ticks;
  if (entry_interval == 0) {
    // Safety: avoid zero-length entries.
    entry_interval = kTicksPerBar * 2;
  }

  // Build voice entry plan.
  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    VoiceEntry entry;
    entry.voice_id = idx;
    entry.role = assignVoiceRole(idx);
    entry.entry_tick = static_cast<Tick>(idx) * entry_interval;
    entry.is_subject = isSubjectEntry(idx);
    entry.entry_number = idx + 1;

    expo.entries.push_back(entry);
  }

  // Place notes for each voice entry.
  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    const VoiceEntry& entry = expo.entries[idx];

    // Select source material: subject for even entries, answer for odd.
    const std::vector<NoteEvent>& source_notes =
        entry.is_subject ? subject.notes : answer.notes;

    // Place the main entry (subject or answer) for this voice.
    placeEntryNotes(source_notes, entry.voice_id, entry.entry_tick,
                    expo.voice_notes);

    // The voice that just finished its entry (idx - 1) plays the countersubject
    // against this new entry.
    if (idx > 0) {
      VoiceId prev_voice = expo.entries[idx - 1].voice_id;
      placeCountersubjectNotes(countersubject.notes, prev_voice,
                               entry.entry_tick, expo.voice_notes);
    }

    // Voices that are two or more entries behind play free counterpoint.
    if (idx >= 2) {
      for (uint8_t earlier = 0; earlier < idx - 1; ++earlier) {
        VoiceId earlier_voice = expo.entries[earlier].voice_id;
        placeFreeCounterpoint(earlier_voice, entry.entry_tick,
                              entry_interval, config.key, rng,
                              expo.voice_notes);
      }
    }
  }

  // Calculate total exposition duration: last entry tick + entry material length.
  if (!expo.entries.empty()) {
    const VoiceEntry& last_entry = expo.entries.back();
    const std::vector<NoteEvent>& last_source =
        last_entry.is_subject ? subject.notes : answer.notes;

    // Duration of last entry's material. Use subject length as fallback.
    Tick last_material_length = subject.length_ticks;
    if (!last_source.empty()) {
      const NoteEvent& final_note = last_source.back();
      Tick material_end = final_note.start_tick + final_note.duration;
      if (material_end > last_material_length) {
        last_material_length = material_end;
      }
    }

    expo.total_ticks = last_entry.entry_tick + last_material_length;
  }

  return expo;
}

}  // namespace bach
