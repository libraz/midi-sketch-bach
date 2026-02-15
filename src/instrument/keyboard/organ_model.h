// Organ instrument model -- pipe organ with manuals and pedal.

#ifndef BACH_INSTRUMENT_KEYBOARD_ORGAN_MODEL_H
#define BACH_INSTRUMENT_KEYBOARD_ORGAN_MODEL_H

#include <cstdint>

#include "core/pitch_utils.h"
#include "instrument/keyboard/piano_model.h"

namespace bach {

/// @brief Organ manual identifiers.
///
/// Maps directly to MIDI channel assignments:
///   Great (Ch 0), Swell (Ch 1), Positiv (Ch 2), Pedal (Ch 3).
enum class OrganManual : uint8_t {
  Great = 0,    // Manual I  - MIDI Channel 0
  Swell = 1,    // Manual II - MIDI Channel 1
  Positiv = 2,  // Manual III - MIDI Channel 2
  Pedal = 3     // Pedal keyboard - MIDI Channel 3
};

/// @brief Convert OrganManual enum to human-readable string.
/// @param manual The organ manual to convert.
/// @return Null-terminated string: "Great", "Swell", "Positiv", or "Pedal".
const char* organManualToString(OrganManual manual);

/// @brief Configuration for organ manual and pedal ranges.
///
/// Default values follow J.S. Bach-era organ conventions:
///   Manual I/II:  C2-C6 (MIDI 36-96)
///   Manual III:   C3-C6 (MIDI 48-96)
///   Pedal:        C1-D3 (MIDI 24-50)
struct OrganConfig {
  // Manual I (Great) range in MIDI note numbers
  uint8_t great_low = organ_range::kManual1Low;      // C2
  uint8_t great_high = organ_range::kManual1High;     // C6

  // Manual II (Swell) range
  uint8_t swell_low = organ_range::kManual2Low;       // C2
  uint8_t swell_high = organ_range::kManual2High;     // C6

  // Manual III (Positiv) range -- smaller than Great/Swell
  uint8_t positiv_low = organ_range::kManual3Low;     // C3
  uint8_t positiv_high = organ_range::kManual3High;   // C6

  // Pedal range
  uint8_t pedal_low = organ_range::kPedalLow;         // C1
  uint8_t pedal_high = organ_range::kPedalHigh;       // D3

  /// @brief Create a standard Bach-era organ configuration.
  /// @return Default OrganConfig with standard ranges.
  static OrganConfig standard() { return OrganConfig(); }
};

/// @brief Physical model of a pipe organ with multiple manuals and pedal.
///
/// Extends PianoModel with organ-specific behavior:
///   - No velocity sensitivity (pipe organs use stops, not touch dynamics)
///   - Fixed default velocity of 80
///   - Per-manual pitch range validation
///   - Soft penalty for pedal notes outside ideal range
///   - MIDI channel and GM program mapping per manual
///
/// The base PianoModel handles hand assignment and playability evaluation
/// using virtuoso-level span constraints (organists are assumed highly skilled).
class OrganModel : public PianoModel {
 public:
  /// @brief Construct an OrganModel with the given configuration.
  /// @param config Organ-specific range configuration.
  explicit OrganModel(const OrganConfig& config = OrganConfig::standard());

  /// @brief Pipe organs are not velocity sensitive.
  /// @return Always false.
  bool isVelocitySensitive() const { return false; }

  /// @brief Fixed velocity for all organ notes.
  /// @return Always 80.
  uint8_t defaultVelocity() const { return 80; }

  /// @brief Get the lowest MIDI pitch for a specific manual.
  /// @param manual The organ manual to query.
  /// @return Lowest playable MIDI pitch on that manual.
  uint8_t getManualLow(OrganManual manual) const;

  /// @brief Get the highest MIDI pitch for a specific manual.
  /// @param manual The organ manual to query.
  /// @return Highest playable MIDI pitch on that manual.
  uint8_t getManualHigh(OrganManual manual) const;

  /// @brief Check if a pitch is within the range of a specific manual.
  /// @param pitch MIDI pitch number (0-127).
  /// @param manual The organ manual to check against.
  /// @return True if the pitch falls within the manual's range.
  bool isInManualRange(uint8_t pitch, OrganManual manual) const;

  /// @brief Calculate soft penalty for pedal pitches outside ideal range.
  ///
  /// Returns 0.0 for pitches within the pedal ideal range (C1-D3, MIDI 24-50).
  /// For pitches outside, the penalty increases linearly at
  /// kPedalPenaltyPerSemitone (5.0) per semitone of distance.
  ///
  /// @param pitch MIDI pitch number.
  /// @return Penalty value (0.0 = no penalty, positive = outside range).
  float pedalPenalty(uint8_t pitch) const;

  /// @brief Get the MIDI channel assigned to a manual.
  /// @param manual The organ manual.
  /// @return MIDI channel number (0-3).
  static uint8_t channelForManual(OrganManual manual);

  /// @brief Get the General MIDI program number for a manual.
  ///
  /// Church Organ (19) for Great, Positiv, and Pedal.
  /// Reed Organ (20) for Swell.
  ///
  /// @param manual The organ manual.
  /// @return GM program number.
  static uint8_t programForManual(OrganManual manual);

  /// @brief Get the organ configuration.
  /// @return Const reference to the current OrganConfig.
  const OrganConfig& getOrganConfig() const { return config_; }

 private:
  OrganConfig config_;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_ORGAN_MODEL_H
