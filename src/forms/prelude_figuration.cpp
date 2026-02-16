// Figuration pattern implementation for harmony-first prelude generation.

#include "forms/prelude_figuration.h"

#include <cassert>
#include <cmath>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"

namespace bach {
namespace {

constexpr Tick kBeat = kTicksPerBeat;          // 480
constexpr Tick kEighth = kTicksPerBeat / 2;    // 240
constexpr Tick kSixteenth = kTicksPerBeat / 4; // 120

// Clamp voice index to available voices.
uint8_t clampVoice(uint8_t idx, uint8_t num_voices) {
  return (idx < num_voices) ? idx : static_cast<uint8_t>(num_voices - 1);
}

// Resolve scale offset: given a chord tone pitch, find the adjacent scale tone
// at the given offset direction (±1).
uint8_t resolveScaleOffset(uint8_t chord_pitch, int8_t offset,
                           const HarmonicEvent& event,
                           uint8_t low, uint8_t high) {
  if (offset == 0) return chord_pitch;

  ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;
  int target = static_cast<int>(chord_pitch) + offset;

  // Find nearest scale tone in the offset direction.
  for (int step = 1; step <= 3; ++step) {
    int candidate = static_cast<int>(chord_pitch) + offset * step;
    if (candidate < 0 || candidate > 127) break;
    auto cu8 = static_cast<uint8_t>(candidate);
    if (scale_util::isScaleTone(cu8, event.key, scale)) {
      return clampPitch(candidate, low, high);
    }
  }

  // Fallback: return chord tone.
  return chord_pitch;
}

}  // namespace

FigurationTemplate createFigurationTemplate(FigurationType type,
                                            uint8_t num_voices) {
  FigurationTemplate tmpl;
  tmpl.type = type;
  assert(num_voices >= 2);

  // Voice indices: 0=soprano(top), num_voices-1=bass(bottom).
  uint8_t sop = 0;
  uint8_t bass = static_cast<uint8_t>(num_voices - 1);
  uint8_t mid = clampVoice(1, num_voices);

  switch (type) {
    case FigurationType::BrokenChord: {
      // BWV 846 pattern: bass sustained full beat, mid onset with bass,
      // soprano enters on second eighth.
      tmpl.steps.push_back({bass, 0, 0, kBeat});      // bass: full beat
      tmpl.steps.push_back({mid, 0, 0, kBeat});       // mid: sustain full beat
      tmpl.steps.push_back({sop, 0, kEighth, kEighth}); // soprano: second eighth
      break;
    }

    case FigurationType::Alberti: {
      // Alberti bass: low-high-mid-high in sixteenth notes.
      tmpl.steps.push_back({bass, 0, 0, kSixteenth});
      tmpl.steps.push_back({sop, 0, kSixteenth, kSixteenth});
      tmpl.steps.push_back({mid, 0, kSixteenth * 2, kSixteenth});
      tmpl.steps.push_back({sop, 0, kSixteenth * 3, kSixteenth});
      break;
    }

    case FigurationType::ScaleConnect: {
      // BWV 543 type perpetual motion: 4 sixteenth notes per beat using all voices.
      // sop → mid → sop(passing) → bass: descending sweep each beat.
      tmpl.steps.push_back({sop, 0, 0, kSixteenth});             // strong: soprano chord tone
      tmpl.steps.push_back({mid, 0, kSixteenth, kSixteenth});    // mid chord tone
      tmpl.steps.push_back({sop, -1, kSixteenth * 2, kSixteenth}); // passing: scale step down from soprano
      tmpl.steps.push_back({bass, 0, kSixteenth * 3, kSixteenth}); // bass chord tone
      break;
    }

    case FigurationType::SlotPattern: {
      // Default: use 3-voice rising as fallback.
      return createFigurationTemplateFromSlot(kFig3vRising, num_voices);
    }

    default:
      // New figuration types (Falling3v, Arch3v, Mixed3v, Falling4v) require
      // the rng overload. Fallback to BrokenChord for the no-rng overload.
      return createFigurationTemplate(FigurationType::BrokenChord, num_voices);
  }

  return tmpl;
}

FigurationTemplate createFigurationTemplateFromSlot(
    const FigurationSlotPattern& pattern, uint8_t num_voices) {
  FigurationTemplate tmpl;
  tmpl.type = FigurationType::SlotPattern;

  auto clamp_voice = [](uint8_t idx, uint8_t nv) -> uint8_t {
    return (idx < nv) ? idx : static_cast<uint8_t>(nv - 1);
  };

  constexpr Tick kStepDur = kTicksPerBeat / 4;  // 120 ticks = sixteenth note
  for (uint8_t idx = 0; idx < pattern.slot_count; ++idx) {
    FigurationStep step;
    // Map slot index to voice index, clamped to available voices.
    step.voice_index = clamp_voice(pattern.slots[idx], num_voices);
    step.scale_offset = 0;  // Always chord tones.
    step.relative_tick = static_cast<Tick>(idx) * kStepDur;
    step.duration = kStepDur;
    tmpl.steps.push_back(step);
  }

  return tmpl;
}

FigurationTemplate createFigurationTemplate(FigurationType type,
                                            uint8_t num_voices,
                                            std::mt19937& rng,
                                            float rhythm_variation_prob) {
  // For existing types, delegate to original overload.
  if (type == FigurationType::BrokenChord ||
      type == FigurationType::Alberti ||
      type == FigurationType::ScaleConnect ||
      type == FigurationType::SlotPattern) {
    return createFigurationTemplate(type, num_voices);
  }

  FigurationTemplate tmpl;
  tmpl.type = type;
  assert(num_voices >= 2);

  uint8_t sop = 0;
  uint8_t bass = static_cast<uint8_t>(num_voices - 1);
  uint8_t mid = clampVoice(1, num_voices);

  // Check if rhythm variation applies to this beat.
  bool vary = false;
  if (rhythm_variation_prob > 0.0f) {
    vary = rng::rollProbability(rng, rhythm_variation_prob);
  }

  switch (type) {
    case FigurationType::Falling3v: {
      if (vary) {
        // 8th+16th+16th: extended first note for gestural opening.
        tmpl.steps.push_back({sop, 0, 0, kEighth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, -1, kEighth, kSixteenth, NCTFunction::Passing});
        tmpl.steps.push_back({bass, 0, kEighth + kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
      } else {
        // 4x16th standard: sop->mid->passing->bass descending.
        tmpl.steps.push_back({sop, 0, 0, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 0, kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, -1, kSixteenth * 2, kSixteenth,
                              NCTFunction::Passing});
        tmpl.steps.push_back({bass, 0, kSixteenth * 3, kSixteenth,
                              NCTFunction::ChordTone});
      }
      break;
    }

    case FigurationType::Arch3v: {
      if (vary) {
        // Dotted eighth + sixteenth + sixteenth: broad arch.
        Tick dotted = kEighth + kSixteenth;  // 360
        tmpl.steps.push_back({bass, 0, 0, dotted, NCTFunction::ChordTone});
        tmpl.steps.push_back({sop, 0, dotted, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 0, dotted + kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
      } else {
        // 4x16th: bass->mid->sop->mid arch.
        tmpl.steps.push_back({bass, 0, 0, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 0, kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({sop, 0, kSixteenth * 2, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 1, kSixteenth * 3, kSixteenth,
                              NCTFunction::Neighbor});
      }
      break;
    }

    case FigurationType::Mixed3v: {
      if (vary) {
        // 16th+8th+16th: syncopated inner voice.
        tmpl.steps.push_back({sop, 0, 0, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({bass, 0, kSixteenth, kEighth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, -1, kSixteenth * 3, kSixteenth,
                              NCTFunction::Neighbor});
      } else {
        // 4x16th: sop->bass->mid->sop(neighbor) alternating.
        tmpl.steps.push_back({sop, 0, 0, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({bass, 0, kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 0, kSixteenth * 2, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({sop, 1, kSixteenth * 3, kSixteenth,
                              NCTFunction::Neighbor});
      }
      break;
    }

    case FigurationType::Falling4v: {
      // Only meaningful with 4+ voices; fall back to Falling3v otherwise.
      if (num_voices < 4) {
        tmpl.type = FigurationType::Falling3v;
        return createFigurationTemplate(FigurationType::Falling3v, num_voices,
                                        rng, rhythm_variation_prob);
      }
      uint8_t mid2 = clampVoice(2, num_voices);
      if (vary) {
        // 8th+3x16th grouped descent.
        tmpl.steps.push_back({sop, 0, 0, kEighth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, -1, kEighth, kSixteenth, NCTFunction::Passing});
        tmpl.steps.push_back({mid2, 0, kEighth + kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
      } else {
        // 4x16th: sop->mid->mid2(passing)->bass.
        tmpl.steps.push_back({sop, 0, 0, kSixteenth, NCTFunction::ChordTone});
        tmpl.steps.push_back({mid, 0, kSixteenth, kSixteenth,
                              NCTFunction::ChordTone});
        tmpl.steps.push_back({mid2, -1, kSixteenth * 2, kSixteenth,
                              NCTFunction::Passing});
        tmpl.steps.push_back({bass, 0, kSixteenth * 3, kSixteenth,
                              NCTFunction::ChordTone});
      }
      break;
    }

    default:
      // Fallback for original types (should not reach here).
      return createFigurationTemplate(type, num_voices);
  }

  return tmpl;
}

std::vector<NoteEvent> applyFiguration(const ChordVoicing& voicing,
                                       const FigurationTemplate& tmpl,
                                       Tick beat_start_tick,
                                       const HarmonicEvent& event,
                                       VoiceRangeFn voice_range) {
  std::vector<NoteEvent> notes;
  notes.reserve(tmpl.steps.size());

  for (const auto& step : tmpl.steps) {
    uint8_t vid = step.voice_index;
    if (vid >= voicing.num_voices) vid = voicing.num_voices - 1;

    uint8_t base_pitch = voicing.pitches[vid];
    auto [v_low, v_high] = voice_range(vid);

    uint8_t pitch;
    if (step.scale_offset == 0) {
      pitch = base_pitch;
    } else {
      pitch = resolveScaleOffset(base_pitch, step.scale_offset, event, v_low, v_high);

      // NCT validation: ensure enclosure conditions are met.
      if (step.nct_function == NCTFunction::Passing && !notes.empty()) {
        // Passing tone must have stepwise approach (interval <= major 3rd).
        int prev_pitch = static_cast<int>(notes.back().pitch);
        int approach = static_cast<int>(pitch) - prev_pitch;
        if (std::abs(approach) > 4) {
          pitch = base_pitch;  // Fallback to chord tone.
        }
      }
      // Neighbor tones: no additional runtime validation needed beyond
      // scale_offset resolution, since the template guarantees the return
      // step. The resolveScaleOffset already finds the nearest scale tone.
    }

    NoteEvent note;
    note.start_tick = beat_start_tick + step.relative_tick;
    note.duration = step.duration;
    note.pitch = pitch;
    note.velocity = 80;  // Organ fixed velocity.
    note.voice = vid;
    note.source = BachNoteSource::PreludeFiguration;
    notes.push_back(note);
  }

  return notes;
}

std::vector<NoteEvent> applyFiguration(const ChordVoicing& voicing,
                                       const FigurationTemplate& tmpl,
                                       Tick beat_start_tick,
                                       const HarmonicEvent& event,
                                       VoiceRangeFn voice_range,
                                       float section_progress) {
  std::vector<NoteEvent> notes;
  notes.reserve(tmpl.steps.size());

  for (const auto& step : tmpl.steps) {
    uint8_t vid = step.voice_index;
    if (vid >= voicing.num_voices) vid = voicing.num_voices - 1;

    uint8_t base_pitch = voicing.pitches[vid];
    auto [v_low, v_high] = voice_range(vid);

    uint8_t pitch;
    if (step.scale_offset == 0) {
      pitch = base_pitch;
    } else {
      // Section-aware NCT direction bias.
      int8_t effective_offset = step.scale_offset;
      if (step.nct_function == NCTFunction::Neighbor) {
        // Opening: prefer lower neighbor (stability).
        // Closing: prefer lower neighbor (resolution direction).
        if (section_progress < 0.15f || section_progress > 0.85f) {
          effective_offset = static_cast<int8_t>(-std::abs(step.scale_offset));
        }
        // Middle: keep original direction.
      } else if (step.nct_function == NCTFunction::Passing) {
        // Closing: bias toward downward resolution.
        if (section_progress > 0.85f) {
          effective_offset = static_cast<int8_t>(-std::abs(step.scale_offset));
        }
      }
      pitch = resolveScaleOffset(base_pitch, effective_offset, event, v_low, v_high);

      // NCT validation: ensure enclosure conditions are met.
      if (step.nct_function == NCTFunction::Passing && !notes.empty()) {
        int prev_pitch = static_cast<int>(notes.back().pitch);
        int approach = static_cast<int>(pitch) - prev_pitch;
        if (std::abs(approach) > 4) {
          pitch = base_pitch;  // Fallback to chord tone.
        }
      }
    }

    NoteEvent note;
    note.start_tick = beat_start_tick + step.relative_tick;
    note.duration = step.duration;
    note.pitch = pitch;
    note.velocity = 80;  // Organ fixed velocity.
    note.voice = vid;
    note.source = BachNoteSource::PreludeFiguration;
    notes.push_back(note);
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Non-chord-tone injection (post-processing pass)
// ---------------------------------------------------------------------------

namespace {

/// @brief Compute a diatonic passing tone between two pitches.
///
/// Uses absolute scale degrees to find the midpoint step. If the interval is
/// a 3rd (2 diatonic steps), returns the single scale tone between them. For
/// larger intervals, returns a scale tone one step from the source pitch in
/// the direction of the target.
///
/// @param src_pitch Source pitch (preceding note).
/// @param dst_pitch Destination pitch (following note).
/// @param key Musical key context.
/// @param scale Scale type.
/// @param low Voice range low bound.
/// @param high Voice range high bound.
/// @return Passing tone pitch, or src_pitch if no valid passing tone exists.
uint8_t computePassingTone(uint8_t src_pitch, uint8_t dst_pitch,
                           Key key, ScaleType scale,
                           uint8_t low, uint8_t high) {
  int src_deg = scale_util::pitchToAbsoluteDegree(src_pitch, key, scale);
  int dst_deg = scale_util::pitchToAbsoluteDegree(dst_pitch, key, scale);
  int deg_diff = dst_deg - src_deg;

  if (std::abs(deg_diff) < 2) {
    // Already stepwise or unison -- no room for a passing tone.
    return src_pitch;
  }

  // Step one diatonic degree from source toward destination.
  int passing_deg = src_deg + (deg_diff > 0 ? 1 : -1);
  uint8_t passing_pitch = scale_util::absoluteDegreeToPitch(passing_deg, key, scale);

  return clampPitch(static_cast<int>(passing_pitch), low, high);
}

/// @brief Compute a diatonic neighbor tone (one scale step above or below).
///
/// @param chord_pitch The chord tone to ornament.
/// @param direction +1 for upper neighbor, -1 for lower neighbor.
/// @param key Musical key context.
/// @param scale Scale type.
/// @param low Voice range low bound.
/// @param high Voice range high bound.
/// @return Neighbor tone pitch, or chord_pitch if out of range.
uint8_t computeNeighborTone(uint8_t chord_pitch, int direction,
                            Key key, ScaleType scale,
                            uint8_t low, uint8_t high) {
  int base_deg = scale_util::pitchToAbsoluteDegree(chord_pitch, key, scale);
  int neighbor_deg = base_deg + direction;
  uint8_t neighbor_pitch = scale_util::absoluteDegreeToPitch(neighbor_deg, key, scale);

  // Verify the neighbor is actually one step away (sanity check).
  int semitone_dist = std::abs(static_cast<int>(neighbor_pitch) -
                               static_cast<int>(chord_pitch));
  if (semitone_dist > 3) {
    // More than a minor 3rd -- something went wrong, fall back.
    return chord_pitch;
  }

  return clampPitch(static_cast<int>(neighbor_pitch), low, high);
}

/// @brief Check whether a note sits on the beat start (strong position).
/// @param note_tick Absolute tick of the note.
/// @param beat_start Absolute tick of the beat start.
/// @return True if the note is on beat 1 of this beat group.
bool isStrongBeatPosition(Tick note_tick, Tick beat_start) {
  return note_tick == beat_start;
}

/// @brief Check whether a note is on a weak sub-beat (2nd or 4th sixteenth).
///
/// Within a beat of 4 sixteenths (positions 0, 1, 2, 3), positions 1 and 3
/// are weak sub-beats, ideal for NCT placement.
///
/// @param note_tick Absolute tick of the note.
/// @param beat_start Absolute tick of the beat start.
/// @return True if the note falls on a weak sixteenth sub-beat.
bool isWeakSubBeat(Tick note_tick, Tick beat_start) {
  Tick offset = note_tick - beat_start;
  Tick sub_beat = offset / kSixteenth;
  // Sub-beats 1 and 3 are weak (0-indexed within the beat).
  return (sub_beat == 1 || sub_beat == 3);
}

}  // namespace

void injectNonChordTones(std::vector<NoteEvent>& notes,
                         const FigurationTemplate& tmpl,
                         Tick beat_start_tick,
                         const HarmonicEvent& event,
                         VoiceRangeFn voice_range,
                         std::mt19937& rng,
                         float nct_probability,
                         float section_progress) {
  if (notes.size() < 2) return;

  ScaleType scale = event.is_minor ? ScaleType::HarmonicMinor : ScaleType::Major;

  // Build a mapping from note index to template step for NCT function check.
  // Only modify notes whose template step was originally ChordTone.
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    auto& note = notes[idx];

    // Never modify beat-1 notes (strong anchor).
    if (isStrongBeatPosition(note.start_tick, beat_start_tick)) continue;

    // Only modify notes on weak sub-beats.
    if (!isWeakSubBeat(note.start_tick, beat_start_tick)) continue;

    // Only modify notes that were originally chord tones from the template.
    if (idx < tmpl.steps.size() &&
        tmpl.steps[idx].nct_function != NCTFunction::ChordTone) {
      continue;  // Already a template-level NCT, leave it alone.
    }

    // Probabilistic gate.
    if (!rng::rollProbability(rng, nct_probability)) continue;

    auto [v_low, v_high] = voice_range(note.voice);

    // Determine NCT type based on melodic context: look at previous and next
    // notes in the same voice to decide between passing and neighbor.
    //
    // Strategy:
    //   - If we can find both a preceding and following note (same voice or
    //     adjacent in the sequence), and their interval is a 3rd or larger,
    //     use a passing tone.
    //   - Otherwise, use a neighbor tone.

    // Find previous pitch (in sequence, any voice -- for melodic continuity).
    uint8_t prev_pitch = 0;
    bool has_prev = false;
    if (idx > 0) {
      prev_pitch = notes[idx - 1].pitch;
      has_prev = true;
    }

    // Find next pitch.
    uint8_t next_pitch = 0;
    bool has_next = false;
    if (idx + 1 < notes.size()) {
      next_pitch = notes[idx + 1].pitch;
      has_next = true;
    }

    // Maximum allowed displacement from the original pitch (in semitones).
    // A perfect 4th keeps the NCT within the same register as the chord tone.
    constexpr int kMaxNCTDisplacement = 5;

    if (has_prev && has_next) {
      int interval_semitones = std::abs(static_cast<int>(prev_pitch) -
                                        static_cast<int>(next_pitch));

      // Only attempt passing tones when surrounding notes are close enough
      // that a stepwise fill makes musical sense (within an octave).
      if (interval_semitones >= 3 && interval_semitones <= 12) {
        // 3rd to octave interval between surrounding notes: use passing tone.
        uint8_t passing = computePassingTone(prev_pitch, next_pitch,
                                             event.key, scale, v_low, v_high);
        // Validate: passing tone must be between prev and next.
        int pass_int = static_cast<int>(passing);
        int prev_int = static_cast<int>(prev_pitch);
        int next_int = static_cast<int>(next_pitch);
        bool is_between = (prev_int <= pass_int && pass_int <= next_int) ||
                          (next_int <= pass_int && pass_int <= prev_int);

        // Also verify the passing tone is not too far from the original pitch.
        int displacement = std::abs(pass_int - static_cast<int>(note.pitch));

        if (is_between && passing != prev_pitch && passing != next_pitch &&
            displacement <= kMaxNCTDisplacement) {
          note.pitch = passing;
          continue;
        }
      }
    }

    // Fallback: neighbor tone.
    if (has_prev || has_next) {
      // Direction bias based on section progress.
      int direction;
      if (section_progress < 0.15f || section_progress > 0.85f) {
        // Opening/closing: prefer lower neighbor for stability/resolution.
        direction = -1;
      } else {
        // Middle: alternate upper/lower based on note index for variety.
        direction = (idx % 2 == 0) ? 1 : -1;
      }

      uint8_t neighbor = computeNeighborTone(note.pitch, direction,
                                             event.key, scale, v_low, v_high);

      // Only apply if the neighbor is actually different from the chord tone.
      if (neighbor != note.pitch) {
        note.pitch = neighbor;
      }
    }
  }
}

}  // namespace bach
