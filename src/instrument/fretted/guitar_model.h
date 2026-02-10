// Classical guitar instrument model.

#ifndef BACH_INSTRUMENT_FRETTED_GUITAR_MODEL_H
#define BACH_INSTRUMENT_FRETTED_GUITAR_MODEL_H

#include <cstdint>

#include "instrument/fretted/fretted_instrument.h"

namespace bach {

/// @brief Physical model of a classical (nylon-string) guitar.
///
/// Standard tuning: E2, A2, D3, G3, B3, E4.
/// Range: E2 (40) to B5 (83) with 19 frets.
/// Used for BWV 1007 cello suite transcriptions and similar.
class GuitarModel : public IFrettedInstrument {
 public:
  GuitarModel();

  uint8_t getLowestPitch() const override { return kLowestPitch; }
  uint8_t getHighestPitch() const override { return kHighestPitch; }
  bool isPitchPlayable(uint8_t pitch) const override;
  PlayabilityCost calculateCost(uint8_t pitch) const override;
  uint8_t getNumStrings() const override { return kNumStrings; }

 private:
  static constexpr uint8_t kNumStrings = 6;
  static constexpr uint8_t kLowestPitch = 40;   // E2
  static constexpr uint8_t kHighestPitch = 83;  // B5
  static constexpr uint8_t kMaxFret = 19;

  /// Open string MIDI pitches in standard tuning (low to high).
  static constexpr uint8_t kOpenStrings[kNumStrings] = {40, 45, 50, 55, 59, 64};

  /// @brief Find the best (lowest fret) string for a given pitch.
  /// @param pitch MIDI pitch number.
  /// @param out_string_idx Output: index of the best string (0-5).
  /// @param out_fret Output: fret number on that string.
  /// @return True if the pitch is reachable on at least one string.
  bool findBestString(uint8_t pitch, uint8_t& out_string_idx,
                      uint8_t& out_fret) const;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_FRETTED_GUITAR_MODEL_H
