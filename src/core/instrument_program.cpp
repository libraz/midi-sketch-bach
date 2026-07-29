// Implementation of instrument-to-GM-program mapping and track helpers.

#include "core/instrument_program.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/gm_program.h"
#include "core/pitch_utils.h"

namespace bach {

std::uint8_t gmProgramFor(InstrumentType instrument) {
  switch (instrument) {
    case InstrumentType::Organ:
      return GmProgram::kChurchOrgan;  // 19
    case InstrumentType::Harpsichord:
      return GmProgram::kHarpsichord;  // 6
    case InstrumentType::Piano:
      return GmProgram::kPiano;  // 0 (Acoustic Grand Piano)
    case InstrumentType::Violin:
      return GmProgram::kViolin;  // 40
    case InstrumentType::Cello:
      return GmProgram::kCello;  // 42
    case InstrumentType::Guitar:
      return GmProgram::kNylonGuitar;  // 24
  }
  return GmProgram::kChurchOrgan;  // Default: Organ, matches legacy fallback.
}

InstrumentPitchRange pitchRangeFor(InstrumentType instrument) {
  switch (instrument) {
    case InstrumentType::Organ:
      return {organ_range::kPedalLow, organ_range::kManual1High};
    case InstrumentType::Harpsichord:
      return {36, 96};
    case InstrumentType::Piano:
      return {21, 108};
    case InstrumentType::Violin:
      // Product forms can carry a scored bass on the same GM program as a
      // violin upper voice (for example the three-voice Chaconne).  Keep the
      // complete polyphonic product in one safe output compass rather than
      // clipping that bass note by note to the solo G3 floor.
      return {36, string_range::kViolinHigh};
    case InstrumentType::Cello:
      // Likewise, the public Cello Prelude may use a broad multi-octave
      // arpeggio. This is an output compass for the complete product, not a
      // claim that every line is a literal unaccompanied solo part.
      return {organ_range::kPedalLow, organ_range::kManual1High};
    case InstrumentType::Guitar:
      return {string_range::kGuitarLow, string_range::kGuitarHigh};
  }
  return {0, 127};
}

std::optional<int> selectOutputOctaveShift(const std::vector<NoteEvent>& notes, Key key,
                                           InstrumentType instrument) {
  if (notes.empty()) {
    return 0;
  }
  const InstrumentPitchRange range = pitchRangeFor(instrument);
  const int key_shift = keyTranspositionSemitones(key);
  int low = 127;
  int high = 0;
  for (const NoteEvent& note : notes) {
    const int transposed = static_cast<int>(note.pitch) + key_shift;
    low = std::min(low, transposed);
    high = std::max(high, transposed);
  }

  std::optional<int> best_shift;
  // MIDI itself spans fewer than eleven octaves, so this covers every
  // feasible displacement while retaining a fixed, deterministic bound.
  for (int octaves = -10; octaves <= 10; ++octaves) {
    const int shift = octaves * interval::kOctave;
    if (low + shift < static_cast<int>(range.low) || high + shift > static_cast<int>(range.high)) {
      continue;
    }
    if (!best_shift || std::abs(shift) < std::abs(*best_shift) ||
        (std::abs(shift) == std::abs(*best_shift) && shift < *best_shift)) {
      best_shift = shift;
    }
  }
  return best_shift;
}

void applyInstrument(std::vector<Track>& tracks, InstrumentType instrument) {
  const std::uint8_t program = gmProgramFor(instrument);
  for (std::size_t idx = 0; idx < tracks.size(); ++idx) {
    tracks[idx].program = program;
    tracks[idx].instrument_name = instrumentTypeToString(instrument);
    if (tracks[idx].name.empty()) {
      tracks[idx].name = "Voice " + std::to_string(idx);
    }
  }
}

}  // namespace bach
