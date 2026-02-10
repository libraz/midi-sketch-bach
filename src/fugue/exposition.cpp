// Implementation of fugue exposition: voice entry scheduling, note placement,
// and countersubject assignment.

#include "fugue/exposition.h"

#include <algorithm>
#include <random>

#include "core/note_source.h"
#include "core/rng_util.h"
#include "core/scale.h"

namespace bach {

namespace {

/// @brief Per-voice pitch register boundaries.
struct VoiceRegister {
  uint8_t low;
  uint8_t high;
};

/// @brief Get the pitch register for a voice based on Bach organ fugue practice.
///
/// Voices are assigned non-overlapping (or minimally overlapping) registers
/// to prevent voice crossings.
///
/// @param voice_id Voice identifier (0 = soprano/first, increasing = lower).
/// @param num_voices Total number of voices in the fugue.
/// @return VoiceRegister with low and high pitch bounds.
VoiceRegister getVoiceRegister(VoiceId voice_id, uint8_t num_voices) {
  if (num_voices == 2) {
    constexpr VoiceRegister kRanges2[] = {
        {55, 84},  // Voice 0 (upper): G3-C6
        {36, 67},  // Voice 1 (lower): C2-G4
    };
    return kRanges2[voice_id < 2 ? voice_id : 1];
  }
  if (num_voices == 3) {
    constexpr VoiceRegister kRanges3[] = {
        {60, 96},  // Voice 0 (soprano): C4-C6
        {55, 79},  // Voice 1 (alto): G3-G5
        {36, 60},  // Voice 2 (bass): C2-C4
    };
    return kRanges3[voice_id < 3 ? voice_id : 2];
  }
  // 4+ voices
  constexpr VoiceRegister kRanges4[] = {
      {60, 96},  // Voice 0 (soprano): C4-C6
      {55, 79},  // Voice 1 (alto): G3-G5
      {48, 72},  // Voice 2 (tenor): C3-C5
      {24, 50},  // Voice 3 (bass/pedal): C1-D3
  };
  return kRanges4[voice_id < 4 ? voice_id : 3];
}

/// @brief Adapt countersubject pitches from one key to another.
///
/// In Bach's practice, the countersubject is a fixed melodic identity
/// with minimal pitch-class substitution for the dominant key.
/// For C->G major: F(65,53,77...)→F#(66,54,78...) etc.
///
/// @param cs_notes Countersubject notes to adapt.
/// @param to_key Target key.
/// @param scale Scale type.
/// @return Adapted notes with non-scale pitches snapped to the target key.
std::vector<NoteEvent> adaptCSToKey(const std::vector<NoteEvent>& cs_notes,
                                     Key to_key, ScaleType scale) {
  auto adapted = cs_notes;
  for (auto& note : adapted) {
    if (!scale_util::isScaleTone(note.pitch, to_key, scale)) {
      note.pitch = scale_util::nearestScaleTone(note.pitch, to_key, scale);
    }
  }
  return adapted;
}

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
/// are offset to start at the new entry's tick position and shifted to fit
/// the voice's register.
///
/// @param cs_notes Countersubject notes to place.
/// @param voice_id Voice that plays the countersubject.
/// @param start_tick Tick when the countersubject begins.
/// @param voice_reg Voice register boundaries for pitch placement.
/// @param voice_notes Output map to append notes to.
void placeCountersubjectNotes(const std::vector<NoteEvent>& cs_notes,
                              VoiceId voice_id,
                              Tick start_tick,
                              VoiceRegister voice_reg,
                              std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  if (cs_notes.empty()) return;

  // Compute mean pitch of the countersubject.
  int cs_total = 0;
  for (const auto& n : cs_notes) {
    cs_total += static_cast<int>(n.pitch);
  }
  int cs_mean = cs_total / static_cast<int>(cs_notes.size());

  // Compute voice register center and octave shift needed.
  int voice_center = (static_cast<int>(voice_reg.low) +
                      static_cast<int>(voice_reg.high)) / 2;
  int octave_shift = ((voice_center - cs_mean + 6) / 12) * 12;

  for (const auto& cs_note : cs_notes) {
    NoteEvent note = cs_note;
    note.start_tick = cs_note.start_tick + start_tick;
    note.voice = voice_id;
    int shifted = static_cast<int>(cs_note.pitch) + octave_shift;
    note.pitch = clampPitch(shifted, voice_reg.low, voice_reg.high);
    voice_notes[voice_id].push_back(note);
  }
}

/// @brief Generate diatonic scalar passage work as free counterpoint filler.
///
/// When a voice is two or more entries behind the current entry, it plays
/// free counterpoint material. This generates stepwise diatonic motion
/// (quarter-note walking through scale degrees), characteristic of Bach's
/// free counterpoint in fugue expositions.
///
/// @param voice_id Voice to generate filler for.
/// @param start_tick When the filler begins.
/// @param duration_ticks How long the filler lasts.
/// @param key Musical key for pitch selection.
/// @param voice_reg Voice register boundaries.
/// @param rng Random number generator for variation.
/// @param voice_notes Output map to append notes to.
void placeFreeCounterpoint(VoiceId voice_id,
                           Tick start_tick,
                           Tick duration_ticks,
                           Key key,
                           VoiceRegister voice_reg,
                           std::mt19937& rng,  // NOLINT(runtime/references): mt19937 must be mutable
                           std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  if (duration_ticks == 0) return;

  ScaleType scale = ScaleType::Major;
  int center_pitch = (static_cast<int>(voice_reg.low) +
                      static_cast<int>(voice_reg.high)) / 2;
  int current_deg = scale_util::pitchToAbsoluteDegree(
      clampPitch(center_pitch, 0, 127), key, scale);

  int direction = rng::rollProbability(rng, 0.5f) ? 1 : -1;
  Tick current_tick = start_tick;
  Tick remaining = duration_ticks;
  constexpr Tick kStepDur = kTicksPerBeat;  // Quarter note steps.

  while (remaining > 0) {
    Tick dur = std::min(remaining, kStepDur);
    uint8_t pitch = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
    pitch = clampPitch(static_cast<int>(pitch), voice_reg.low, voice_reg.high);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = 80;
    note.voice = voice_id;
    voice_notes[voice_id].push_back(note);

    current_tick += dur;
    remaining -= dur;

    // Advance degree, reverse at range boundaries.
    current_deg += direction;
    uint8_t next_p = scale_util::absoluteDegreeToPitch(
        current_deg, key, scale);
    if (next_p > voice_reg.high || next_p < voice_reg.low) {
      direction = -direction;
      current_deg += direction * 2;
    }
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
      VoiceRegister prev_reg = getVoiceRegister(prev_voice, num_voices);

      // Adapt countersubject to answer key when accompanying an answer entry.
      std::vector<NoteEvent> cs_to_place = countersubject.notes;
      if (!entry.is_subject) {
        cs_to_place = adaptCSToKey(cs_to_place, answer.key, ScaleType::Major);
      }

      placeCountersubjectNotes(cs_to_place, prev_voice,
                               entry.entry_tick, prev_reg, expo.voice_notes);
    }

    // Voices that are two or more entries behind play free counterpoint.
    if (idx >= 2) {
      for (uint8_t earlier = 0; earlier < idx - 1; ++earlier) {
        VoiceId earlier_voice = expo.entries[earlier].voice_id;
        VoiceRegister earlier_reg = getVoiceRegister(earlier_voice, num_voices);
        placeFreeCounterpoint(earlier_voice, entry.entry_tick,
                              entry_interval, config.key, earlier_reg, rng,
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
