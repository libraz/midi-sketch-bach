// Implementation of fugue exposition: voice entry scheduling, note placement,
// and countersubject assignment.

#include "fugue/exposition.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/figure_injector.h"
#include "core/markov_tables.h"
#include "core/note_creator.h"
#include "core/interval.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "counterpoint/i_rule_evaluator.h"
#include "fugue/countersubject.h"
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
// adaptCSToKey is now public in countersubject.h/.cpp (Phase C1).

/// @brief Info about an adjacent voice for register fitting.
struct AdjacentInfo {
  uint8_t sounding_pitch = 0;  ///< Pitch sounding at entry_tick (or most recent).
  uint8_t recent_lo = 0;       ///< Low bound of recent pitch band.
  uint8_t recent_hi = 0;       ///< High bound of recent pitch band.
};

/// @brief Extract adjacent voice info: sounding pitch and recent range.
///
/// Uses rbegin traversal (newest first) for efficient sounding note lookup.
/// recent_lo/hi are computed from up to 4 most recent notes at or before entry_tick.
/// Follows the 3-stage protocol:
///   1. Sounding note priority (active at entry_tick)
///   2. Fallback: most recent ended note
///   3. Band from up to K=4 recent notes; if <2 notes, use voice range
AdjacentInfo extractAdjacentInfo(
    const std::map<VoiceId, std::vector<NoteEvent>>& voice_notes,
    VoiceId adj_voice, Tick entry_tick, uint8_t num_voices) {
  AdjacentInfo info;
  auto map_it = voice_notes.find(adj_voice);
  if (map_it == voice_notes.end() || map_it->second.empty()) return info;

  const auto& notes = map_it->second;
  constexpr int kRecentK = 4;

  // Stage 1+2: rbegin scan for sounding pitch, fallback to most recent.
  for (auto rit = notes.rbegin(); rit != notes.rend(); ++rit) {
    if (rit->start_tick > entry_tick) continue;
    if (entry_tick < rit->start_tick + rit->duration) {
      info.sounding_pitch = rit->pitch;  // Stage 1: sounding.
      break;
    }
    // Stage 2: most recent ended note (first match after skipping future).
    info.sounding_pitch = rit->pitch;
    break;
  }

  // Stage 3: recent_lo/hi from up to K notes at or before entry_tick.
  uint8_t lo = 127, hi = 0;
  int count = 0;
  for (auto rit = notes.rbegin(); rit != notes.rend() && count < kRecentK; ++rit) {
    if (rit->start_tick > entry_tick) continue;
    lo = std::min(lo, rit->pitch);
    hi = std::max(hi, rit->pitch);
    ++count;
  }
  if (count < 2) {
    auto [plo, phi] = getFugueVoiceRange(adj_voice, num_voices);
    info.recent_lo = plo;
    info.recent_hi = phi;
  } else {
    info.recent_lo = lo;
    info.recent_hi = hi;
  }

  return info;
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
                     std::map<VoiceId, std::vector<NoteEvent>>& voice_notes,
                     uint8_t num_voices = 0,
                     float phase_pos = 0.0f,
                     uint8_t adjacent_last_pitch = 0,
                     uint8_t adjacent_lo = 0,
                     uint8_t adjacent_hi = 0,
                     uint8_t adjacent2_last_pitch = 0,
                     uint8_t adjacent2_lo = 0,
                     uint8_t adjacent2_hi = 0) {
  (void)adjacent2_hi;  // Used implicitly via adj2_lo direction inference.
  if (source_notes.empty()) return;

  // Compute octave shift using envelope-aware register fitting when possible.
  RegisterEnvelope envelope = getRegisterEnvelope(FormType::Fugue);
  int octave_shift;
  if (num_voices > 0 && adjacent2_last_pitch > 0) {
    // Dual-adjacent scoring for middle voice (V1).
    // Evaluate all shift candidates with combined penalties from both adjacents.
    static constexpr int kShifts[] = {-48, -36, -24, -12, 0, 12, 24, 36, 48};

    // Find min/max/first pitch in source.
    uint8_t min_p = source_notes[0].pitch, max_p = source_notes[0].pitch;
    for (const auto& n : source_notes) {
      min_p = std::min(min_p, n.pitch);
      max_p = std::max(max_p, n.pitch);
    }
    uint8_t first_pitch = source_notes[0].pitch;

    // Characteristic range approximation for clarity penalty.
    auto charLo = [](uint8_t lo) -> int {
      if (lo >= 60) return 60;
      if (lo >= 55) return 55;
      if (lo >= 48) return 48;
      return 36;
    };
    auto charHi = [](uint8_t lo, uint8_t hi) -> int {
      if (lo >= 60) return 84;
      if (lo >= 55) return 74;
      if (lo >= 48) return 67;
      if (hi <= 50) return 50;
      return 60;
    };
    int char_lo = charLo(voice_reg.low);
    int char_hi = charHi(voice_reg.low, voice_reg.high);
    int center = (static_cast<int>(voice_reg.low) + static_cast<int>(voice_reg.high)) / 2;

    int best_shift = 0;
    int best_score = INT32_MAX;

    for (int shift : kShifts) {
      int sf = static_cast<int>(first_pitch) + shift;
      int s_min = static_cast<int>(min_p) + shift;
      int s_max = static_cast<int>(max_p) + shift;

      // overflow: out-of-range penalty.
      int overflow = 0;
      if (s_min < static_cast<int>(voice_reg.low))
        overflow += static_cast<int>(voice_reg.low) - s_min;
      if (s_max > static_cast<int>(voice_reg.high))
        overflow += s_max - static_cast<int>(voice_reg.high);

      // instant_cross with first adjacent (upper voice).
      // adj_lo > voice_reg.low → adj is upper voice → sf should be below adj.
      int upper_cross = 0;
      if (adjacent_last_pitch > 0) {
        if (adjacent_lo > voice_reg.low) {
          if (sf >= static_cast<int>(adjacent_last_pitch))
            upper_cross = sf - static_cast<int>(adjacent_last_pitch) + 1;
        } else {
          if (sf <= static_cast<int>(adjacent_last_pitch))
            upper_cross = static_cast<int>(adjacent_last_pitch) - sf + 1;
        }
      }

      // instant_cross with second adjacent (lower voice).
      int lower_cross = 0;
      if (adjacent2_last_pitch > 0) {
        if (adjacent2_lo > voice_reg.low) {
          if (sf >= static_cast<int>(adjacent2_last_pitch))
            lower_cross = sf - static_cast<int>(adjacent2_last_pitch) + 1;
        } else {
          if (sf <= static_cast<int>(adjacent2_last_pitch))
            lower_cross = static_cast<int>(adjacent2_last_pitch) - sf + 1;
        }
      }

      // close_spacing with each adjacent (≤3st → penalty).
      int upper_spacing = 0;
      if (adjacent_last_pitch > 0) {
        int sp = std::abs(sf - static_cast<int>(adjacent_last_pitch));
        if (sp > 0 && sp <= 3) upper_spacing = 4 - sp;
      }
      int lower_spacing = 0;
      if (adjacent2_last_pitch > 0) {
        int sp = std::abs(sf - static_cast<int>(adjacent2_last_pitch));
        if (sp > 0 && sp <= 3) lower_spacing = 4 - sp;
      }

      // clarity: distance from characteristic range.
      int clarity = 0;
      if (sf < char_lo) clarity = char_lo - sf;
      if (sf > char_hi) clarity = sf - char_hi;

      // center_distance.
      int center_dist = std::abs(sf - center);

      int score = overflow * 100
                + (upper_cross + lower_cross) * 50
                + (upper_spacing + lower_spacing) * 10
                + clarity * 2 + center_dist * 1;

      if (score < best_score ||
          (score == best_score && std::abs(shift) < std::abs(best_shift))) {
        best_score = score;
        best_shift = shift;
      }
    }
    octave_shift = best_shift;
  } else if (num_voices > 0) {
    octave_shift = fitToRegisterWithEnvelope(source_notes, voice_id, num_voices,
                                              phase_pos, envelope,
                                              /*reference_pitch=*/0,
                                              adjacent_last_pitch,
                                              /*envelope_overflow_count=*/nullptr,
                                              adjacent_lo, adjacent_hi);
  } else {
    octave_shift = fitToRegister(source_notes, voice_reg.low, voice_reg.high);
  }

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
                              std::map<VoiceId, std::vector<NoteEvent>>& voice_notes,
                              uint8_t num_voices = 0,
                              float phase_pos = 0.0f,
                              uint8_t adjacent_last_pitch = 0,
                              uint8_t adjacent_lo = 0,
                              uint8_t adjacent_hi = 0) {
  if (cs_notes.empty()) return;

  // Compute octave shift using envelope-aware register fitting when possible.
  RegisterEnvelope envelope = getRegisterEnvelope(FormType::Fugue);
  int octave_shift = (num_voices > 0)
      ? fitToRegisterWithEnvelope(cs_notes, voice_id, num_voices,
                                   phase_pos, envelope,
                                   /*reference_pitch=*/0,
                                   adjacent_last_pitch,
                                   /*envelope_overflow_count=*/nullptr,
                                   adjacent_lo, adjacent_hi)
      : fitToRegister(cs_notes, voice_reg.low, voice_reg.high);

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
                           std::map<VoiceId, std::vector<NoteEvent>>& voice_notes,
                           uint8_t num_voices = 0) {
  if (duration_ticks == 0) return;

  ScaleType scale = is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int center_pitch = (static_cast<int>(voice_reg.low) +
                      static_cast<int>(voice_reg.high)) / 2;
  int current_deg = scale_util::pitchToAbsoluteDegree(
      clampPitch(center_pitch, 0, 127), key, scale);

  int direction = rng::rollProbability(rng, 0.5f) ? 1 : -1;
  Tick current_tick = start_tick;
  Tick remaining = duration_ticks;
  auto findShortestOtherDuration = [&voice_notes, voice_id](Tick tick) -> Tick {
    Tick shortest = 0;
    for (const auto& [vid, notes] : voice_notes) {
      if (vid == voice_id || notes.empty()) continue;
      for (auto iter = notes.rbegin(); iter != notes.rend(); ++iter) {
        if (iter->start_tick <= tick && iter->start_tick + iter->duration > tick) {
          if (shortest == 0 || iter->duration < shortest)
            shortest = iter->duration;
          break;
        }
      }
    }
    return shortest;
  };

  VoiceProfile voice_profile = getVoiceProfile(voice_id, num_voices);
  const MarkovModel& markov_ref = isPedalVoice(voice_id, num_voices)
                                      ? kFuguePedalMarkov
                                      : kFugueUpperMarkov;

  while (remaining > 0) {
    Tick other_dur = findShortestOtherDuration(current_tick);

    // Markov duration context from previous note in this voice.
    const MarkovModel* markov = nullptr;
    DurCategory prev_dur_cat = DurCategory::Qtr;
    DirIntervalClass dir_ivl = DirIntervalClass::StepUp;
    if (!voice_notes[voice_id].empty()) {
      const auto& prev_note = voice_notes[voice_id].back();
      prev_dur_cat = ticksToDurCategory(prev_note.duration);
      uint8_t current_pitch_est = scale_util::absoluteDegreeToPitch(
          current_deg, key, scale);
      DegreeStep step = computeDegreeStep(prev_note.pitch, current_pitch_est,
                                          key, scale);
      dir_ivl = toDirIvlClass(step);
      markov = &markov_ref;
    }

    Tick raw_dur = FugueEnergyCurve::selectDuration(energy, current_tick, rng,
                                                     other_dur, voice_profile,
                                                     false, 1.0f, false,
                                                     markov, prev_dur_cat, dir_ivl);

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
    // Wave 2a: Evaluate degree candidates ±2 with crossing/spacing scoring.
    int best_deg = current_deg;
    {
      float best_cand_score = -1e9f;
      for (int d_off = -2; d_off <= 2; ++d_off) {
        int cand_deg = current_deg + d_off;
        uint8_t cand_pitch = scale_util::absoluteDegreeToPitch(cand_deg, key, scale);
        cand_pitch = clampPitch(static_cast<int>(cand_pitch),
                                voice_reg.low, voice_reg.high);

        float cand_score = 0.0f;
        bool has_crossing = false;
        for (const auto& [vid, vnotes] : voice_notes) {
          if (vid == voice_id || vnotes.empty()) continue;
          uint8_t other_pitch = 0;
          for (auto rit = vnotes.rbegin(); rit != vnotes.rend(); ++rit) {
            if (rit->start_tick <= current_tick &&
                current_tick < rit->start_tick + rit->duration) {
              other_pitch = rit->pitch;
              break;
            }
          }
          if (other_pitch == 0) continue;

          // Voice order: lower voice_id = higher pitch.
          bool crossed = (voice_id < vid && cand_pitch < other_pitch) ||
                         (voice_id > vid && cand_pitch > other_pitch);
          if (crossed) has_crossing = true;

          int spacing = std::abs(static_cast<int>(cand_pitch) -
                                 static_cast<int>(other_pitch));
          if (spacing <= 3) cand_score -= 0.3f;
          else if (spacing <= 6) cand_score -= 0.1f;
        }

        if (has_crossing) cand_score -= 1000.0f;

        // Melodic step penalty: 5th+ leap from original degree.
        int mel_interval = std::abs(d_off);
        if (mel_interval >= 3) {
          cand_score -= static_cast<float>(mel_interval - 2) * 0.15f;
        }

        if (cand_score > best_cand_score) {
          best_cand_score = cand_score;
          best_deg = cand_deg;
        }
      }

      // Wave 2b: All candidates cross → try octave shift (middle/lower voices only).
      if (best_cand_score <= -999.0f && voice_id > 0) {
        uint8_t orig_pitch = scale_util::absoluteDegreeToPitch(
            current_deg, key, scale);
        for (int oct_shift : {-12, 12}) {
          int shifted = static_cast<int>(orig_pitch) + oct_shift;
          if (shifted < static_cast<int>(voice_reg.low) ||
              shifted > static_cast<int>(voice_reg.high)) continue;

          bool still_crossed = false;
          for (const auto& [vid, vnotes] : voice_notes) {
            if (vid == voice_id || vnotes.empty()) continue;
            uint8_t other_pitch = 0;
            for (auto rit = vnotes.rbegin(); rit != vnotes.rend(); ++rit) {
              if (rit->start_tick <= current_tick &&
                  current_tick < rit->start_tick + rit->duration) {
                other_pitch = rit->pitch;
                break;
              }
            }
            if (other_pitch == 0) continue;
            if ((voice_id < vid && shifted < static_cast<int>(other_pitch)) ||
                (voice_id > vid && shifted > static_cast<int>(other_pitch))) {
              still_crossed = true;
              break;
            }
          }
          if (!still_crossed) {
            best_deg = scale_util::pitchToAbsoluteDegree(
                static_cast<uint8_t>(shifted), key, scale);
            break;
          }
        }
      }
    }
    current_deg = best_deg;
    uint8_t pitch = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
    pitch = clampPitch(static_cast<int>(pitch), voice_reg.low, voice_reg.high);

    // Attempt vocabulary figure injection (25% probability).
    bool figure_placed = false;
    do {
      MelodicState mel_state;
      mel_state.phrase_progress = 1.0f - static_cast<float>(remaining) /
                                         static_cast<float>(duration_ticks);
      auto fig = tryInjectFigure(mel_state, pitch, key, scale, current_tick,
                                 voice_reg.low, voice_reg.high, rng, 0.25f);
      if (!fig.has_value()) break;

      Tick fig_dur = std::min(remaining / static_cast<Tick>(fig->pitches.size()),
                              kTicksPerBeat / 2);
      if (fig_dur < kTicksPerBeat / 4) break;

      // Lightweight vertical safety check: reject if first figure pitch
      // creates harsh dissonance (m2, M2, tritone, M7) with other voices.
      bool vertically_safe = true;
      for (const auto& [vid, vnotes] : voice_notes) {
        if (vid == voice_id || vnotes.empty()) continue;
        for (auto it = vnotes.rbegin(); it != vnotes.rend(); ++it) {
          if (it->start_tick + it->duration > current_tick) {
            int ivl = std::abs(static_cast<int>(fig->pitches[0]) -
                               static_cast<int>(it->pitch)) % 12;
            if (ivl == 1 || ivl == 2 || ivl == 6 || ivl == 11) {
              vertically_safe = false;
            }
            break;
          }
        }
      }
      if (!vertically_safe) break;

      // Wave 2c: Voice order filter — reject figure if first pitch violates
      // voice order with any sounding voice.
      bool order_safe = true;
      for (const auto& [vid, vnotes] : voice_notes) {
        if (vid == voice_id || vnotes.empty()) continue;
        for (auto rit = vnotes.rbegin(); rit != vnotes.rend(); ++rit) {
          if (rit->start_tick <= current_tick &&
              current_tick < rit->start_tick + rit->duration) {
            bool crossed = (voice_id < vid &&
                            fig->pitches[0] < rit->pitch) ||
                           (voice_id > vid &&
                            fig->pitches[0] > rit->pitch);
            if (crossed) order_safe = false;
            break;
          }
        }
        if (!order_safe) break;
      }
      if (!order_safe) break;

      for (uint8_t fp : fig->pitches) {
        if (remaining < fig_dur) break;
        NoteEvent fn;
        fn.start_tick = current_tick;
        fn.duration = fig_dur;
        fn.pitch = fp;
        fn.velocity = 80;
        fn.voice = voice_id;
        fn.source = BachNoteSource::FreeCounterpoint;
        voice_notes[voice_id].push_back(fn);
        current_tick += fig_dur;
        remaining -= fig_dur;
      }
      current_deg = scale_util::pitchToAbsoluteDegree(
          fig->pitches.back(), key, scale);
      figure_placed = true;
    } while (false);

    if (figure_placed) {
      // Advance direction state after figure.
      uint8_t next_p = scale_util::absoluteDegreeToPitch(
          current_deg, key, scale);
      if (next_p > voice_reg.high || next_p < voice_reg.low) {
        direction = -direction;
        current_deg += direction * 2;
      }
      continue;
    }

    // Attempt rhythm cell injection (20% probability) before single note.
    auto rhythm = tryInjectRhythmCell(energy, remaining, current_tick, rng, 0.20f);
    if (rhythm.has_value()) {
      bool rhythm_placed = false;
      for (Tick rd : rhythm->durations) {
        if (remaining < rd) break;
        uint8_t rp = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
        rp = clampPitch(static_cast<int>(rp), voice_reg.low, voice_reg.high);
        NoteEvent rn;
        rn.start_tick = current_tick;
        rn.duration = rd;
        rn.pitch = rp;
        rn.velocity = 80;
        rn.voice = voice_id;
        rn.source = BachNoteSource::FreeCounterpoint;
        voice_notes[voice_id].push_back(rn);
        current_tick += rd;
        remaining -= rd;
        rhythm_placed = true;
        current_deg += direction;
        uint8_t np = scale_util::absoluteDegreeToPitch(current_deg, key, scale);
        if (np > voice_reg.high || np < voice_reg.low) {
          direction = -direction;
          current_deg += direction * 2;
        }
      }
      if (rhythm_placed) continue;
    }

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
                           uint32_t seed,
                           Tick estimated_duration) {
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
    float phase_pos = estimated_duration > 0
        ? static_cast<float>(entry.entry_tick) / static_cast<float>(estimated_duration)
        : 0.0f;
    // Extract adjacent voice info for register fitting (Wave 1).
    uint8_t ent_adj_pitch = 0, ent_adj_lo = 0, ent_adj_hi = 0;
    uint8_t ent_adj2_pitch = 0, ent_adj2_lo = 0, ent_adj2_hi = 0;
    VoiceId vid = entry.voice_id;

    // Upper adjacent: voice vid-1 (higher voice = lower voice_id).
    if (vid > 0) {
      AdjacentInfo upper = extractAdjacentInfo(
          expo.voice_notes, vid - 1, entry.entry_tick, num_voices);
      if (upper.sounding_pitch > 0 || upper.recent_lo < 127) {
        ent_adj_pitch = upper.sounding_pitch;
        ent_adj_lo = upper.recent_lo;
        ent_adj_hi = upper.recent_hi;
      }
    }

    // Lower adjacent: voice vid+1 (lower voice = higher voice_id).
    if (vid + 1 < num_voices) {
      AdjacentInfo lower = extractAdjacentInfo(
          expo.voice_notes, vid + 1, entry.entry_tick, num_voices);
      if (lower.sounding_pitch > 0 || lower.recent_lo < 127) {
        if (ent_adj_pitch > 0) {
          // Both adjacents available: dual case (middle voice V1).
          ent_adj2_pitch = lower.sounding_pitch;
          ent_adj2_lo = lower.recent_lo;
          ent_adj2_hi = lower.recent_hi;
        } else {
          ent_adj_pitch = lower.sounding_pitch;
          ent_adj_lo = lower.recent_lo;
          ent_adj_hi = lower.recent_hi;
        }
      }
    }

    placeEntryNotes(source_notes, entry.voice_id, entry.entry_tick,
                    entry_reg, expo.voice_notes, num_voices, phase_pos,
                    ent_adj_pitch, ent_adj_lo, ent_adj_hi,
                    ent_adj2_pitch, ent_adj2_lo, ent_adj2_hi);

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

      float cs_phase_pos = estimated_duration > 0
          ? static_cast<float>(entry.entry_tick) / static_cast<float>(estimated_duration)
          : 0.0f;
      // Extract concurrent entry voice's sounding pitch at CS start_tick.
      uint8_t adj_pitch = 0;
      const auto& entry_notes = expo.voice_notes[entry.voice_id];
      for (const auto& n : entry_notes) {
        if (n.start_tick <= entry.entry_tick &&
            n.start_tick + n.duration > entry.entry_tick) {
          adj_pitch = n.pitch;
          break;
        }
      }
      auto [adj_lo, adj_hi] = getFugueVoiceRange(entry.voice_id, num_voices);
      placeCountersubjectNotes(cs_to_place, prev_voice,
                               entry.entry_tick, prev_reg, expo.voice_notes,
                               num_voices, cs_phase_pos,
                               adj_pitch,
                               static_cast<uint8_t>(adj_lo),
                               static_cast<uint8_t>(adj_hi));

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
                              earlier_reg, rng, 0.5f, expo.voice_notes,
                              num_voices);
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
                           const HarmonicTimeline& timeline,
                           Tick estimated_duration) {
  // Build using the original logic.
  Exposition expo = buildExposition(subject, answer, countersubject, config, seed,
                                    estimated_duration);

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

    // Entry consonance correction: ensure the first note of each voice
    // entry is consonant with concurrent voices.  Bach always starts
    // entries on consonant intervals.  When fitToRegister chose a
    // dissonant octave, shift the entire entry by ±12 to fix it.
    int entry_octave_correction = 0;
    if (voice_entry_tick > 0) {
      for (const auto& n : notes) {
        if (n.start_tick < voice_entry_tick) continue;
        if (n.start_tick >= structural_end) break;
        // Check consonance of first structural note against sounding voices.
        bool consonant = true;
        for (VoiceId ov : cp_state.getActiveVoices()) {
          if (ov == voice_id) continue;
          const NoteEvent* on = cp_state.getNoteAt(ov, n.start_tick);
          if (!on) continue;
          int diff = std::abs(static_cast<int>(n.pitch) -
                              static_cast<int>(on->pitch));
          if (diff == 0) continue;
          int simple = interval_util::compoundToSimple(diff);
          if (!interval_util::isConsonance(simple)) {
            consonant = false;
            break;
          }
        }
        if (!consonant) {
          auto entry_reg = getVoiceRegister(voice_id, config.num_voices);
          for (int shift : {-12, 12, -24, 24}) {
            int cand = static_cast<int>(n.pitch) + shift;
            if (cand < entry_reg.low || cand > entry_reg.high) continue;
            bool ok = true;
            for (VoiceId ov : cp_state.getActiveVoices()) {
              if (ov == voice_id) continue;
              const NoteEvent* on = cp_state.getNoteAt(ov, n.start_tick);
              if (!on) continue;
              int d = std::abs(cand - static_cast<int>(on->pitch));
              if (d == 0) continue;
              int s = interval_util::compoundToSimple(d);
              if (!interval_util::isConsonance(s)) { ok = false; break; }
              // Preserve voice order (higher voice_id = lower pitch).
              if ((voice_id < ov &&
                   cand > static_cast<int>(on->pitch)) ||
                  (voice_id > ov &&
                   cand < static_cast<int>(on->pitch))) {
                ok = false;
                break;
              }
            }
            if (ok) {
              entry_octave_correction = shift;
              break;
            }
          }
        }
        break;
      }
    }

    for (const auto& note : notes) {
      bool is_structural = (note.start_tick < structural_end);

      if (is_structural) {
        // Register structural notes in CP state, then check for voice crossing.
        // If a crossing is detected, try octave shifts to restore proper order.
        NoteEvent fixed_note = note;
        // Apply entry octave correction for consonance.
        if (entry_octave_correction != 0) {
          int p = static_cast<int>(fixed_note.pitch) + entry_octave_correction;
          auto entry_reg = getVoiceRegister(voice_id, config.num_voices);
          fixed_note.pitch = clampPitch(p, entry_reg.low, entry_reg.high);
        }
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
