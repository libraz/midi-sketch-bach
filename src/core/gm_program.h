// General MIDI program numbers used in Bach MIDI generation.

#ifndef BACH_CORE_GM_PROGRAM_H
#define BACH_CORE_GM_PROGRAM_H

#include <cstdint>

namespace bach {

/// General MIDI program numbers (0-indexed as per MIDI specification).
/// Only the programs actually used in this project are defined here.
namespace GmProgram {

constexpr uint8_t kPiano = 0;          // Acoustic Grand Piano
constexpr uint8_t kHarpsichord = 6;    // Harpsichord
constexpr uint8_t kChurchOrgan = 19;   // Church Organ
constexpr uint8_t kReedOrgan = 20;     // Reed Organ
constexpr uint8_t kNylonGuitar = 24;   // Nylon String Guitar
constexpr uint8_t kViolin = 40;        // Violin
constexpr uint8_t kCello = 42;         // Cello

}  // namespace GmProgram

}  // namespace bach

#endif  // BACH_CORE_GM_PROGRAM_H
