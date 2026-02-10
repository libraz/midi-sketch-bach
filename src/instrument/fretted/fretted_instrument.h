// Interface for fretted instrument models (guitar, lute).

#ifndef BACH_INSTRUMENT_FRETTED_FRETTED_INSTRUMENT_H
#define BACH_INSTRUMENT_FRETTED_FRETTED_INSTRUMENT_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Playability cost breakdown for a fretted instrument note.
struct PlayabilityCost {
  float total = 0.0f;           // Overall playability cost
  float stretch = 0.0f;         // Fretting hand stretch cost
  float position_shift = 0.0f;  // Cost of shifting hand position
  bool is_playable = true;      // Whether the note is physically possible
};

/// @brief Abstract interface for fretted instrument models.
///
/// Provides physical playability constraints for fretted string instruments
/// (guitar, lute, etc.). Evaluates whether pitches are reachable given
/// string tuning and fret count.
class IFrettedInstrument {
 public:
  virtual ~IFrettedInstrument() = default;

  /// @brief Get the lowest playable MIDI pitch.
  virtual uint8_t getLowestPitch() const = 0;

  /// @brief Get the highest playable MIDI pitch.
  virtual uint8_t getHighestPitch() const = 0;

  /// @brief Check if a pitch is playable on this instrument.
  /// @param pitch MIDI pitch number (0-127).
  /// @return True if the pitch can be produced on at least one string.
  virtual bool isPitchPlayable(uint8_t pitch) const = 0;

  /// @brief Calculate the ergonomic cost of playing a pitch.
  /// @param pitch MIDI pitch number.
  /// @return Playability cost breakdown.
  virtual PlayabilityCost calculateCost(uint8_t pitch) const = 0;

  /// @brief Get the number of strings on this instrument.
  virtual uint8_t getNumStrings() const = 0;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_FRETTED_FRETTED_INSTRUMENT_H
