// Implementation of instrument-to-GM-program mapping and track helpers.

#include "core/instrument_program.h"

#include <string>

#include "core/gm_program.h"

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

void applyInstrument(std::vector<Track>& tracks, InstrumentType instrument) {
  const std::uint8_t program = gmProgramFor(instrument);
  for (std::size_t idx = 0; idx < tracks.size(); ++idx) {
    tracks[idx].program = program;
    if (tracks[idx].name.empty()) {
      tracks[idx].name = "Voice " + std::to_string(idx);
    }
  }
}

}  // namespace bach
