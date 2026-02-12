// Implementation of shared baroque organ performance techniques.

#include "organ/organ_techniques.h"

#include <algorithm>

#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "harmony/chord_types.h"
#include "harmony/key.h"

namespace bach {

// ---------------------------------------------------------------------------
// Cadential pedal point
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateCadentialPedal(
    const KeySignature& key_sig, Tick start_tick, Tick end_tick,
    PedalPointType type, uint8_t voice_id) {
  std::vector<NoteEvent> notes;
  if (end_tick <= start_tick) return notes;

  // Determine pedal pitch: tonic or dominant in pedal range.
  int tonic_pc = static_cast<int>(key_sig.tonic);
  int target_pc = tonic_pc;
  if (type == PedalPointType::Dominant) {
    target_pc = static_cast<int>(getDominant(key_sig).tonic);
  }

  // Find the pitch in organ pedal range (C1-D3, MIDI 24-50), preferring
  // the instance closest to the center of the range (typical organ pedal
  // register is around C2-G2).
  int range_mid = (static_cast<int>(organ_range::kPedalLow) +
                   static_cast<int>(organ_range::kPedalHigh)) / 2;
  uint8_t pedal_pitch = 0;
  int best_dist = 128;
  for (int p = organ_range::kPedalLow; p <= organ_range::kPedalHigh; ++p) {
    if (p % 12 == target_pc) {
      int dist = std::abs(p - range_mid);
      if (dist < best_dist) {
        best_dist = dist;
        pedal_pitch = static_cast<uint8_t>(p);
      }
    }
  }
  if (pedal_pitch == 0) {
    pedal_pitch = static_cast<uint8_t>(organ_range::kPedalLow + target_pc);
  }

  // Generate sustained pedal note, re-articulating each bar for realism.
  Tick current = start_tick;
  while (current < end_tick) {
    Tick bar_end = ((current / kTicksPerBar) + 1) * kTicksPerBar;
    Tick note_end = std::min(bar_end, end_tick);
    Tick dur = note_end - current;
    if (dur == 0) break;

    NoteEvent note;
    note.start_tick = current;
    note.duration = dur;
    note.pitch = pedal_pitch;
    note.velocity = kOrganVelocity;
    note.voice = voice_id;
    note.source = BachNoteSource::PedalPoint;
    notes.push_back(note);

    current = note_end;
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Picardy third
// ---------------------------------------------------------------------------

void applyPicardyToFinalChord(std::vector<NoteEvent>& notes,
                              const KeySignature& key_sig,
                              Tick final_bar_tick) {
  if (!key_sig.is_minor) return;

  int tonic_pc = static_cast<int>(key_sig.tonic);
  // Minor third above tonic = tonic + 3 semitones.
  int minor_third_pc = (tonic_pc + 3) % 12;

  for (auto& note : notes) {
    if (note.start_tick >= final_bar_tick) {
      int pc = getPitchClass(note.pitch);
      if (pc == minor_third_pc) {
        // Raise minor third to major third.
        note.pitch = static_cast<uint8_t>(
            static_cast<int>(note.pitch) + 1);
        // Clamp to MIDI range.
        if (note.pitch > 127) note.pitch = 127;
      }
      // Also ensure no natural minor 7th clashes with Picardy.
      // (The raised 7th from harmonic minor is already correct.)
    }
  }
}

// ---------------------------------------------------------------------------
// Block chord
// ---------------------------------------------------------------------------

std::vector<NoteEvent> generateBlockChord(
    const KeySignature& key_sig, Tick tick, Tick duration,
    uint8_t num_voices,
    const std::vector<std::pair<uint8_t, uint8_t>>& voice_ranges) {
  std::vector<NoteEvent> notes;

  // Build tonic triad chord tones.
  int tonic_pc = static_cast<int>(key_sig.tonic);
  int third_pc = key_sig.is_minor ? (tonic_pc + 3) % 12 : (tonic_pc + 4) % 12;
  int fifth_pc = (tonic_pc + 7) % 12;

  for (uint8_t v = 0; v < num_voices && v < voice_ranges.size(); ++v) {
    uint8_t low = voice_ranges[v].first;
    uint8_t high = voice_ranges[v].second;

    // Choose the chord tone closest to the middle of the voice range.
    int mid = (static_cast<int>(low) + static_cast<int>(high)) / 2;
    int pcs[] = {tonic_pc, third_pc, fifth_pc};

    uint8_t best_pitch = static_cast<uint8_t>(mid);
    int best_dist = 128;
    for (int pc : pcs) {
      // Find the instance of this pitch class closest to mid.
      for (int p = low; p <= high; ++p) {
        if (p % 12 == pc) {
          int dist = std::abs(p - mid);
          if (dist < best_dist) {
            best_dist = dist;
            best_pitch = static_cast<uint8_t>(p);
          }
        }
      }
    }

    NoteEvent note;
    note.start_tick = tick;
    note.duration = duration;
    note.pitch = best_pitch;
    note.velocity = kOrganVelocity;
    note.voice = v;
    note.source = BachNoteSource::FreeCounterpoint;
    notes.push_back(note);
  }

  return notes;
}

// ---------------------------------------------------------------------------
// Registration presets
// ---------------------------------------------------------------------------

Registration OrganRegistrationPresets::piano() {
  return {GmProgram::kChurchOrgan, GmProgram::kReedOrgan,
          GmProgram::kChurchOrgan, GmProgram::kChurchOrgan, 60};
}

Registration OrganRegistrationPresets::mezzo() {
  return {GmProgram::kChurchOrgan, GmProgram::kReedOrgan,
          GmProgram::kChurchOrgan, GmProgram::kChurchOrgan, 75};
}

Registration OrganRegistrationPresets::forte() {
  return {GmProgram::kChurchOrgan, GmProgram::kChurchOrgan,
          GmProgram::kChurchOrgan, GmProgram::kChurchOrgan, 90};
}

Registration OrganRegistrationPresets::pleno() {
  return {GmProgram::kChurchOrgan, GmProgram::kChurchOrgan,
          GmProgram::kChurchOrgan, GmProgram::kChurchOrgan, 100};
}

Registration OrganRegistrationPresets::tutti() {
  return {GmProgram::kChurchOrgan, GmProgram::kChurchOrgan,
          GmProgram::kChurchOrgan, GmProgram::kChurchOrgan, 110};
}

// ---------------------------------------------------------------------------
// Registration plans
// ---------------------------------------------------------------------------

ExtendedRegistrationPlan createSimpleRegistrationPlan(
    Tick start_tick, Tick end_tick) {
  ExtendedRegistrationPlan plan;
  if (end_tick <= start_tick) return plan;

  Tick mid_tick = start_tick + (end_tick - start_tick) / 2;

  plan.addPoint(start_tick, OrganRegistrationPresets::mezzo(), "opening");
  plan.addPoint(mid_tick, OrganRegistrationPresets::forte(), "middle");
  plan.addPoint(end_tick - kTicksPerBar, OrganRegistrationPresets::pleno(),
                "closing");

  return plan;
}

ExtendedRegistrationPlan createVariationRegistrationPlan(
    int num_variations, Tick variation_duration) {
  ExtendedRegistrationPlan plan;
  if (num_variations <= 0) return plan;

  for (int i = 0; i < num_variations; ++i) {
    Tick tick = static_cast<Tick>(i) * variation_duration;
    float progress = static_cast<float>(i) / static_cast<float>(num_variations);

    Registration reg;
    if (progress < 0.25f) {
      reg = OrganRegistrationPresets::mezzo();
    } else if (progress < 0.50f) {
      reg = OrganRegistrationPresets::forte();
    } else if (progress < 0.75f) {
      reg = OrganRegistrationPresets::pleno();
    } else {
      reg = OrganRegistrationPresets::tutti();
    }

    plan.addPoint(tick, reg, "variation_" + std::to_string(i + 1));
  }

  return plan;
}

}  // namespace bach
