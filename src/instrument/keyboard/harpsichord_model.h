// Harpsichord instrument model.

#ifndef BACH_INSTRUMENT_KEYBOARD_HARPSICHORD_MODEL_H
#define BACH_INSTRUMENT_KEYBOARD_HARPSICHORD_MODEL_H

#include <cstdint>

#include "instrument/keyboard/piano_model.h"

namespace bach {

/// @brief Configuration for harpsichord keyboard ranges.
///
/// Default values follow typical 18th-century harpsichord conventions:
///   Lower manual: F1-F6 (MIDI 29-89)
///   Upper manual: F1-F6 (MIDI 29-89)
struct HarpsichordConfig {
  // Lower manual range in MIDI note numbers
  uint8_t lower_low = 29;   // F1
  uint8_t lower_high = 89;  // F6

  // Upper manual range
  uint8_t upper_low = 29;   // F1
  uint8_t upper_high = 89;  // F6

  /// @brief Create a standard 18th-century harpsichord configuration.
  /// @return Default HarpsichordConfig with standard ranges.
  static HarpsichordConfig standard() { return HarpsichordConfig(); }
};

/// @brief Physical model of a two-manual harpsichord.
///
/// Extends PianoModel with harpsichord-specific behavior:
///   - No velocity sensitivity (plucked strings have fixed dynamics)
///   - Fixed default velocity of 80
///   - Range F1-F6 (MIDI 29-89), narrower than a modern piano
///
/// The base PianoModel handles hand assignment and playability evaluation
/// using advanced-level span constraints.
class HarpsichordModel : public PianoModel {
 public:
  /// @brief Construct a HarpsichordModel with the given configuration.
  /// @param config Harpsichord-specific range configuration.
  explicit HarpsichordModel(
      const HarpsichordConfig& config = HarpsichordConfig::standard());

  /// @brief Harpsichords are not velocity sensitive.
  /// @return Always false.
  bool isVelocitySensitive() const { return false; }

  /// @brief Fixed velocity for all harpsichord notes.
  /// @return Always 80.
  uint8_t defaultVelocity() const { return 80; }

  /// @brief Get the harpsichord configuration.
  /// @return Const reference to the current HarpsichordConfig.
  const HarpsichordConfig& getHarpsichordConfig() const { return config_; }

 private:
  HarpsichordConfig config_;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_HARPSICHORD_MODEL_H
