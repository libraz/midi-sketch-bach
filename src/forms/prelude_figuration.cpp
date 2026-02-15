// Figuration pattern implementation for harmony-first prelude generation.

#include "forms/prelude_figuration.h"

#include <cassert>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
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

}  // namespace bach
