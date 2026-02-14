// Implementation of fugue exposition: voice entry scheduling, note placement,
// and countersubject assignment.

#include "fugue/exposition.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/note_creator.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "fugue/fugue_config.h"
#include "fugue/voice_registers.h"
#include "harmony/chord_tone_utils.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

namespace {

/// @brief Get the pitch register for a voice based on Bach organ fugue practice.
///
/// Delegates to the shared getFugueVoiceRange() for consistent ranges.
///
/// @param voice_id Voice identifier (0 = soprano/first, increasing = lower).
/// @param num_voices Total number of voices in the fugue.
/// @return VoiceRegister with low and high pitch bounds.
VoiceRegister getVoiceRegister(VoiceId voice_id, uint8_t num_voices) {
  auto [lo, hi] = getFugueVoiceRange(voice_id, num_voices);
  return {lo, hi};
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

/// @brief 3-voice entry order templates.
static constexpr uint8_t kEntryOrder3_MiddleFirst[] = {1, 0, 2};  // alto->sop->bass
static constexpr uint8_t kEntryOrder3_TopFirst[] = {0, 1, 2};      // sop->alto->bass
static constexpr uint8_t kEntryOrder3_BottomFirst[] = {2, 1, 0};  // bass->alto->sop

/// @brief 4-voice entry order templates.
static constexpr uint8_t kEntryOrder4_ATSB[] = {1, 0, 2, 3};  // alto->sop->ten->bass
static constexpr uint8_t kEntryOrder4_TASB[] = {2, 1, 0, 3};  // ten->alto->sop->bass
static constexpr uint8_t kEntryOrder4_BTAS[] = {3, 2, 1, 0};  // bass->ten->alto->sop

/// @brief Select entry voice order based on SubjectCharacter and voice count.
///
/// Uses weighted random selection so that each character has a preferred entry
/// order but can occasionally produce alternatives for variety across seeds.
///
/// @param character The subject character.
/// @param num_voices Number of voices (2-5).
/// @param rng Mersenne Twister RNG for weighted selection.
/// @return Pointer to the voice order array, or nullptr for default sequential order.
const uint8_t* selectEntryOrder(SubjectCharacter character, uint8_t num_voices,
                                std::mt19937& rng) {  // NOLINT(runtime/references): mt19937 must be mutable
  if (num_voices <= 2) {
    return nullptr;  // Only one possible order for 2 voices.
  }
  if (num_voices == 3) {
    // Weighted random selection per character.
    static const uint8_t* k3VoiceOrders[] = {
      kEntryOrder3_TopFirst, kEntryOrder3_MiddleFirst, kEntryOrder3_BottomFirst
    };
    std::vector<float> weights;
    switch (character) {
      case SubjectCharacter::Severe:   weights = {0.50f, 0.30f, 0.20f}; break;
      case SubjectCharacter::Playful:  weights = {0.30f, 0.50f, 0.20f}; break;
      case SubjectCharacter::Noble:    weights = {0.40f, 0.25f, 0.35f}; break;
      case SubjectCharacter::Restless: weights = {0.25f, 0.40f, 0.35f}; break;
    }
    int idx = rng::selectWeighted(rng, std::vector<int>{0, 1, 2}, weights);
    return k3VoiceOrders[idx];
  }
  // 4-5 voices: weighted selection among templates (+ nullptr for default).
  static const uint8_t* k4VoiceOrders[] = {
    nullptr, kEntryOrder4_ATSB, kEntryOrder4_TASB, kEntryOrder4_BTAS
  };
  std::vector<float> weights;
  switch (character) {
    case SubjectCharacter::Severe:   weights = {0.50f, 0.20f, 0.10f, 0.20f}; break;
    case SubjectCharacter::Playful:  weights = {0.15f, 0.50f, 0.20f, 0.15f}; break;
    case SubjectCharacter::Noble:    weights = {0.20f, 0.15f, 0.15f, 0.50f}; break;
    case SubjectCharacter::Restless: weights = {0.15f, 0.20f, 0.50f, 0.15f}; break;
  }
  int idx = rng::selectWeighted(rng, std::vector<int>{0, 1, 2, 3}, weights);
  return k4VoiceOrders[idx];
}

/// @brief Place subject or answer notes for a voice entry, offset by entry tick.
///
/// Copies source notes into the target voice's note list, adjusting
/// start_tick by the entry offset, assigning the correct voice_id, and
/// shifting pitches by whole octaves to fit the target voice's register.
///
/// @param source_notes Notes to place (from subject or answer).
/// @param voice_id Target voice identifier.
/// @param entry_tick Tick offset for this entry.
/// @param voice_reg Voice register boundaries for pitch placement.
/// @param voice_notes Output map to append notes to.
void placeEntryNotes(const std::vector<NoteEvent>& source_notes,
                     VoiceId voice_id,
                     Tick entry_tick,
                     VoiceRegister voice_reg,
                     std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  if (source_notes.empty()) return;

  // Compute octave shift using fitToRegister for optimal register placement.
  int octave_shift = fitToRegister(source_notes,
                                    voice_reg.low, voice_reg.high);

  // Place all notes with octave shift.
  std::vector<NoteEvent> placed;
  placed.reserve(source_notes.size());
  for (const auto& src_note : source_notes) {
    NoteEvent note = src_note;
    note.start_tick = src_note.start_tick + entry_tick;
    note.voice = voice_id;
    int shifted = static_cast<int>(src_note.pitch) + octave_shift;
    if (shifted < static_cast<int>(voice_reg.low) || shifted > static_cast<int>(voice_reg.high)) {
      note.pitch = clampPitch(shifted, voice_reg.low, voice_reg.high);
    } else {
      note.pitch = static_cast<uint8_t>(shifted);
    }
    placed.push_back(note);
  }

  // Restore melodic contour: verify that interval directions match the source.
  // When clamping destroys an interval direction, adjust by small steps.
  for (size_t idx = 1; idx < placed.size(); ++idx) {
    int src_interval = static_cast<int>(source_notes[idx].pitch) -
                       static_cast<int>(source_notes[idx - 1].pitch);
    int placed_interval = static_cast<int>(placed[idx].pitch) -
                          static_cast<int>(placed[idx - 1].pitch);
    // Check direction mismatch: original goes up but placed goes down, or vice versa.
    if (src_interval > 0 && placed_interval <= 0) {
      // Should go up: try +1 semitone from previous note.
      int target = static_cast<int>(placed[idx - 1].pitch) + 1;
      if (target <= static_cast<int>(voice_reg.high)) {
        placed[idx].pitch = static_cast<uint8_t>(target);
      }
    } else if (src_interval < 0 && placed_interval >= 0) {
      // Should go down: try -1 semitone from previous note.
      int target = static_cast<int>(placed[idx - 1].pitch) - 1;
      if (target >= static_cast<int>(voice_reg.low)) {
        placed[idx].pitch = static_cast<uint8_t>(target);
      }
    }
  }

  for (auto& note : placed) {
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

  // Compute octave shift using fitToRegister for optimal register placement.
  int octave_shift = fitToRegister(cs_notes,
                                    voice_reg.low, voice_reg.high);

  for (const auto& cs_note : cs_notes) {
    NoteEvent note = cs_note;
    note.start_tick = cs_note.start_tick + start_tick;
    note.voice = voice_id;
    int shifted = static_cast<int>(cs_note.pitch) + octave_shift;
    if (shifted < static_cast<int>(voice_reg.low) || shifted > static_cast<int>(voice_reg.high)) {
      note.pitch = clampPitch(shifted, voice_reg.low, voice_reg.high);
    } else {
      note.pitch = static_cast<uint8_t>(shifted);
    }
    voice_notes[voice_id].push_back(note);
  }
}

/// @brief Snap CS strong-beat dissonances against concurrent entry notes.
///
/// Limited to +/-2 semitones to preserve melodic contour. Only processes
/// strong beats (beat 0 or 2 in 4/4). Finds the nearest consonant and
/// diatonic pitch within the snap range.
///
/// @param cs_notes CS notes to modify (voice_notes for the CS voice).
/// @param entry_notes Entry (answer) notes to check against.
/// @param key Key for diatonic constraint.
/// @param scale Scale type for diatonic constraint.
void snapCSStrongBeatsToEntry(std::vector<NoteEvent>& cs_notes,
                              const std::vector<NoteEvent>& entry_notes,
                              Key key, ScaleType scale) {
  for (auto& cs : cs_notes) {
    uint8_t beat = beatInBar(cs.start_tick);
    if (beat != 0 && beat != 2) continue;  // Strong beats only.
    for (const auto& entry : entry_notes) {
      if (entry.start_tick > cs.start_tick) break;
      if (entry.start_tick + entry.duration <= cs.start_tick) continue;
      int ivl = interval_util::compoundToSimple(
          absoluteInterval(cs.pitch, entry.pitch));
      if (interval_util::isConsonance(ivl)) break;  // Already consonant.
      // Search +/-2 semitones for the nearest consonant, diatonic pitch.
      uint8_t best = cs.pitch;
      int best_dist = 999;
      for (int delta = -2; delta <= 2; ++delta) {
        if (delta == 0) continue;
        int cand = static_cast<int>(cs.pitch) + delta;
        if (cand < 0 || cand > 127) continue;
        uint8_t cand_u8 = static_cast<uint8_t>(cand);
        if (!scale_util::isScaleTone(cand_u8, key, scale)) continue;
        int c_ivl = interval_util::compoundToSimple(
            absoluteInterval(cand_u8, entry.pitch));
        if (interval_util::isConsonance(c_ivl) &&
            std::abs(delta) < best_dist) {
          best_dist = std::abs(delta);
          best = cand_u8;
        }
      }
      if (best != cs.pitch) cs.pitch = best;
      break;
    }
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
                           bool is_minor,
                           VoiceRegister voice_reg,
                           std::mt19937& rng,  // NOLINT(runtime/references): mt19937 must be mutable
                           float energy,
                           std::map<VoiceId, std::vector<NoteEvent>>& voice_notes) {
  if (duration_ticks == 0) return;

  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int center_pitch = (static_cast<int>(voice_reg.low) +
                      static_cast<int>(voice_reg.high)) / 2;
  int current_deg = scale_util::pitchToAbsoluteDegree(
      clampPitch(center_pitch, 0, 127), key, scale);

  int direction = rng::rollProbability(rng, 0.5f) ? 1 : -1;
  Tick current_tick = start_tick;
  Tick remaining = duration_ticks;
  auto findOtherDuration = [&voice_notes, voice_id](Tick tick) -> Tick {
    for (const auto& [vid, notes] : voice_notes) {
      if (vid == voice_id || notes.empty()) continue;
      for (auto iter = notes.rbegin(); iter != notes.rend(); ++iter) {
        if (iter->start_tick <= tick && iter->start_tick + iter->duration > tick)
          return iter->duration;
      }
    }
    return 0;
  };

  while (remaining > 0) {
    Tick other_dur = findOtherDuration(current_tick);
    Tick raw_dur = FugueEnergyCurve::selectDuration(energy, current_tick, rng, other_dur);

    // Guard 1: Split duration at bar boundaries.
    // Non-suspension notes crossing bar lines feel unnatural in counterpoint.
    Tick ticks_to_bar = kTicksPerBar - (current_tick % kTicksPerBar);
    if (raw_dur > ticks_to_bar && ticks_to_bar >= kTicksPerBeat / 2) {
      raw_dur = ticks_to_bar;
    }

    // Guard 2: If remaining is less than minDuration, consume it in one note.
    Tick min_dur = FugueEnergyCurve::minDuration(energy);
    if (remaining < min_dur) {
      raw_dur = remaining;
    }

    Tick dur = std::min(remaining, raw_dur);
    // Floor: don't go below sixteenth note if we have room.
    if (dur < kTicksPerBeat / 4 && remaining >= kTicksPerBeat / 4) {
      dur = kTicksPerBeat / 4;
    }
    uint8_t pitch = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
    pitch = clampPitch(static_cast<int>(pitch), voice_reg.low, voice_reg.high);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = dur;
    note.pitch = pitch;
    note.velocity = 80;
    note.voice = voice_id;
    note.source = BachNoteSource::FreeCounterpoint;
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

  // Select entry order template based on character.
  const uint8_t* order_template = selectEntryOrder(config.character, num_voices, rng);

  // Build voice entry plan.
  for (uint8_t idx = 0; idx < num_voices; ++idx) {
    VoiceEntry entry;
    // Apply entry order template for voice_id assignment.
    if (order_template != nullptr && idx < 4) {
      entry.voice_id = order_template[idx];
    } else if (order_template != nullptr && idx == 4) {
      // 5th voice: append the remaining voice not in the 4-element template.
      entry.voice_id = 4;
    } else {
      entry.voice_id = idx;
    }
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
    VoiceRegister entry_reg = getVoiceRegister(entry.voice_id, num_voices);
    placeEntryNotes(source_notes, entry.voice_id, entry.entry_tick,
                    entry_reg, expo.voice_notes);

    // The voice that just finished its entry (idx - 1) plays the countersubject
    // against this new entry.
    if (idx > 0) {
      VoiceId prev_voice = expo.entries[idx - 1].voice_id;
      VoiceRegister prev_reg = getVoiceRegister(prev_voice, num_voices);

      // Adapt countersubject to answer key when accompanying an answer entry.
      std::vector<NoteEvent> cs_to_place = countersubject.notes;
      if (!entry.is_subject) {
        cs_to_place = adaptCSToKey(cs_to_place, answer.key,
                                   config.is_minor ? ScaleType::HarmonicMinor
                                                   : ScaleType::Major);
      }

      placeCountersubjectNotes(cs_to_place, prev_voice,
                               entry.entry_tick, prev_reg, expo.voice_notes);

      // Snap CS strong-beat dissonances against answer notes.
      // CS was generated against the subject, so it may clash with the
      // transposed answer. Only snap when accompanying an answer entry.
      if (!entry.is_subject) {
        ScaleType snap_scale = config.is_minor ? ScaleType::HarmonicMinor
                                               : ScaleType::Major;
        snapCSStrongBeatsToEntry(expo.voice_notes[prev_voice],
                                 expo.voice_notes[entry.voice_id],
                                 answer.key, snap_scale);
      }
    }

    // Voices that are two or more entries behind play free counterpoint.
    if (idx >= 2) {
      for (uint8_t earlier = 0; earlier < idx - 1; ++earlier) {
        VoiceId earlier_voice = expo.entries[earlier].voice_id;
        VoiceRegister earlier_reg = getVoiceRegister(earlier_voice, num_voices);
        placeFreeCounterpoint(earlier_voice, entry.entry_tick,
                              entry_interval, config.key, config.is_minor,
                              earlier_reg, rng, 0.5f, expo.voice_notes);
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

Exposition buildExposition(const Subject& subject,
                           const Answer& answer,
                           const Countersubject& countersubject,
                           const FugueConfig& config,
                           uint32_t seed,
                           CounterpointState& cp_state,
                           IRuleEvaluator& cp_rules,
                           CollisionResolver& cp_resolver,
                           const HarmonicTimeline& timeline) {
  // Build using the original logic.
  Exposition expo = buildExposition(subject, answer, countersubject, config, seed);

  // Post-validate free counterpoint notes through createBachNote.
  // Subject/answer/countersubject notes are kept as-is and registered in state.
  // Free counterpoint (walking bass filler) is validated through the cascade.
  for (auto& [voice_id, notes] : expo.voice_notes) {
    std::vector<NoteEvent> validated;
    validated.reserve(notes.size());

    // Determine which tick ranges contain entry material (subject/answer/CS).
    // Entry notes start at voice entry ticks and span entry_interval.
    // Free counterpoint starts at later entries' tick positions for earlier voices.
    // Since we can't easily distinguish by source field, we check if this voice_id
    // had its entry at an earlier tick -- notes overlapping a later entry tick
    // in a voice that already entered are free counterpoint candidates.
    //
    // Simple heuristic: the first (subject.length_ticks or answer.length_ticks)
    // worth of notes from each voice's entry tick are structural. Everything else
    // is free counterpoint.
    Tick voice_entry_tick = 0;
    for (const auto& entry : expo.entries) {
      if (entry.voice_id == voice_id) {
        voice_entry_tick = entry.entry_tick;
        break;
      }
    }
    Tick structural_end = voice_entry_tick + subject.length_ticks * 2;

    for (const auto& note : notes) {
      bool is_structural = (note.start_tick < structural_end);

      if (is_structural) {
        // Register structural notes in CP state, then check for voice crossing.
        // If a crossing is detected, try octave shifts to restore proper order.
        NoteEvent fixed_note = note;
        bool has_crossing = false;
        for (VoiceId other_v : cp_state.getActiveVoices()) {
          if (other_v == voice_id) continue;
          const NoteEvent* other_note = cp_state.getNoteAt(other_v, note.start_tick);
          if (!other_note) continue;
          bool crossed = (voice_id < other_v && note.pitch < other_note->pitch) ||
                         (voice_id > other_v && note.pitch > other_note->pitch);
          if (crossed) { has_crossing = true; break; }
        }
        if (has_crossing) {
          for (int shift : {12, -12, 24, -24}) {
            int candidate = static_cast<int>(note.pitch) + shift;
            if (candidate < 0 || candidate > 127) continue;
            bool resolved = true;
            for (VoiceId other_v : cp_state.getActiveVoices()) {
              if (other_v == voice_id) continue;
              const NoteEvent* other_note = cp_state.getNoteAt(other_v, note.start_tick);
              if (!other_note) continue;
              if ((voice_id < other_v &&
                   candidate < static_cast<int>(other_note->pitch)) ||
                  (voice_id > other_v &&
                   candidate > static_cast<int>(other_note->pitch))) {
                resolved = false;
                break;
              }
            }
            if (resolved) {
              fixed_note.pitch = static_cast<uint8_t>(candidate);
              break;
            }
          }
        }
        // Avoid unisons on strong beats (beat 1 or 3 in 4/4).
        bool is_strong_beat = (fixed_note.start_tick % (kTicksPerBeat * 2) == 0);
        if (is_strong_beat) {
          for (VoiceId other_v : cp_state.getActiveVoices()) {
            if (other_v == voice_id) continue;
            const NoteEvent* other_note = cp_state.getNoteAt(other_v, fixed_note.start_tick);
            if (other_note && other_note->pitch == fixed_note.pitch) {
              // Shift up by 2 semitones (diatonic step) to avoid unison.
              ScaleType sc = config.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
              int shifted = static_cast<int>(fixed_note.pitch) + 2;
              uint8_t snapped = scale_util::nearestScaleTone(
                  clampPitch(shifted, 0, 127), config.key, sc);
              if (snapped != other_note->pitch) {
                fixed_note.pitch = snapped;
              }
              break;
            }
          }
        }

        cp_state.addNote(voice_id, fixed_note);
        validated.push_back(fixed_note);
        continue;
      }

      // Free counterpoint: validate through createBachNote.
      const auto& harm_ev = timeline.getAt(note.start_tick);
      uint8_t desired_pitch = note.pitch;
      bool is_strong = (note.start_tick % kTicksPerBeat == 0);
      if (is_strong && !isChordTone(note.pitch, harm_ev)) {
        desired_pitch = nearestChordTone(note.pitch, harm_ev);
      }

      // For the first free counterpoint note per voice, ensure consonance
      // with all currently sounding voices.  This prevents the common case
      // where a voice re-entering after rest creates strong-beat dissonance.
      bool is_first_free_note = true;
      for (const auto& prev_note : validated) {
        if (prev_note.start_tick >= structural_end) {
          is_first_free_note = false;
          break;
        }
      }
      if (is_first_free_note && is_strong) {
        bool consonant_with_all = true;
        for (VoiceId other_vid : cp_state.getActiveVoices()) {
          if (other_vid == voice_id) continue;
          const NoteEvent* other_note = cp_state.getNoteAt(other_vid, note.start_tick);
          if (!other_note) continue;
          int diff = std::abs(static_cast<int>(desired_pitch) -
                              static_cast<int>(other_note->pitch));
          if (diff < 3) { consonant_with_all = false; break; }
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple)) {
            consonant_with_all = false;
            break;
          }
        }
        if (!consonant_with_all) {
          // Search for a consonant chord tone within the voice register.
          // Prefer imperfect consonances (m3, M3, m6, M6) over perfect ones.
          auto voice_reg = getVoiceRegister(voice_id, config.num_voices);
          int best_pitch = static_cast<int>(desired_pitch);
          int best_score = -1;
          for (int base = static_cast<int>(voice_reg.low);
               base <= static_cast<int>(voice_reg.high); ++base) {
            uint8_t chord_tone = nearestChordTone(static_cast<uint8_t>(base), harm_ev);
            if (chord_tone < voice_reg.low || chord_tone > voice_reg.high) continue;
            int cand = static_cast<int>(chord_tone);
            bool all_ok = true;
            int score = 0;
            for (VoiceId other_vid : cp_state.getActiveVoices()) {
              if (other_vid == voice_id) continue;
              const NoteEvent* other_note =
                  cp_state.getNoteAt(other_vid, note.start_tick);
              if (!other_note) continue;
              int cand_diff = std::abs(cand - static_cast<int>(other_note->pitch));
              if (cand_diff < 3) { all_ok = false; break; }
              int cand_simple = interval_util::compoundToSimple(cand_diff);
              if (!interval_util::isConsonance(cand_simple)) {
                all_ok = false;
                break;
              }
              // Imperfect consonance bonus (m3=3, M3=4, m6=8, M6=9).
              if (cand_simple == 3 || cand_simple == 4 ||
                  cand_simple == 8 || cand_simple == 9) {
                score += 2;
              } else {
                score += 1;
              }
            }
            if (!all_ok) continue;
            // Proximity to original pitch as tiebreaker.
            int dist = std::abs(cand - static_cast<int>(desired_pitch));
            int total = score * 100 - dist;
            if (total > best_score) {
              best_score = total;
              best_pitch = cand;
            }
          }
          if (best_score > 0) {
            desired_pitch = static_cast<uint8_t>(best_pitch);
          }
        }
      }

      BachNoteOptions opts;
      opts.voice = voice_id;
      opts.desired_pitch = desired_pitch;
      opts.tick = note.start_tick;
      opts.duration = note.duration;
      opts.velocity = note.velocity;
      opts.source = BachNoteSource::FreeCounterpoint;

      BachCreateNoteResult result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
      if (result.accepted) {
        validated.push_back(result.note);
      }
    }

    notes = std::move(validated);
  }

  return expo;
}

}  // namespace bach
