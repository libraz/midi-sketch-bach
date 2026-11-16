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
      return {organ_range::kPedalLow, organ_range::kManualHigh};
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
      return {24, 96};
    case InstrumentType::Guitar:
      return {string_range::kGuitarLow, string_range::kGuitarHigh};
  }
  return {0, 127};
}

ComposedCompassWindow composedCompassWindow(InstrumentType instrument, int key_offset, int anchor) {
  const InstrumentPitchRange range = pitchRangeFor(instrument);
  const int sounding = anchor + key_offset;
  int octave = 0;
  while (sounding + octave - interval::kOctave >= static_cast<int>(range.low))
    octave -= interval::kOctave;
  while (sounding + octave < static_cast<int>(range.low))
    octave += interval::kOctave;
  return {static_cast<int>(range.low) - key_offset - octave,
          static_cast<int>(range.high) - key_offset - octave};
}

namespace {

/// The smallest whole-octave displacement that fits the compass, and the one
/// leaving the fewest semitones outside it.
struct OutputOctaveShifts {
  std::optional<int> fitting;
  std::optional<int> nearest;
};

OutputOctaveShifts outputOctaveShifts(const std::vector<NoteEvent>& notes, Key key,
                                      InstrumentType instrument) {
  if (notes.empty()) {
    return {0, 0};
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

  const auto closer = [](int candidate, int incumbent) {
    return std::abs(candidate) < std::abs(incumbent) ||
           (std::abs(candidate) == std::abs(incumbent) && candidate < incumbent);
  };

  OutputOctaveShifts shifts;
  int nearest_excess = 0;
  // MIDI itself spans fewer than eleven octaves, so this covers every
  // feasible displacement while retaining a fixed, deterministic bound.
  for (int octaves = -10; octaves <= 10; ++octaves) {
    const int shift = octaves * interval::kOctave;
    if (low + shift < 0 || high + shift > 127) {
      continue;
    }
    const int below = std::max(0, static_cast<int>(range.low) - (low + shift));
    const int above = std::max(0, (high + shift) - static_cast<int>(range.high));
    const int excess = below + above;
    if (excess == 0) {
      if (!shifts.fitting || closer(shift, *shifts.fitting)) {
        shifts.fitting = shift;
      }
      continue;
    }
    if (!shifts.nearest || excess < nearest_excess ||
        (excess == nearest_excess && closer(shift, *shifts.nearest))) {
      shifts.nearest = shift;
      nearest_excess = excess;
    }
  }
  return shifts;
}

}  // namespace

std::optional<int> selectOutputOctaveShift(const std::vector<NoteEvent>& notes, Key key,
                                           InstrumentType instrument) {
  // Prefer the smallest displacement that fits the compass exactly; the
  // nearest-fitting displacement is only consulted when nothing fits, so a
  // score that already had a placement keeps the one it had.
  const OutputOctaveShifts shifts = outputOctaveShifts(notes, key, instrument);
  return shifts.fitting ? shifts.fitting : shifts.nearest;
}

std::optional<int> fittingOutputOctaveShift(const std::vector<NoteEvent>& notes, Key key,
                                            InstrumentType instrument) {
  return outputOctaveShifts(notes, key, instrument).fitting;
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
