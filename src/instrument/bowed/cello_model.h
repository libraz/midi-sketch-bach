// Cello instrument model for solo string generation.

#ifndef BACH_INSTRUMENT_BOWED_CELLO_MODEL_H
#define BACH_INSTRUMENT_BOWED_CELLO_MODEL_H

#include <cstdint>
#include <vector>

#include "instrument/bowed/bowed_string_instrument.h"

namespace bach {

/// @brief Physical model of a solo cello.
///
/// Standard tuning: C2, G2, D3, A3 (MIDI 36, 43, 50, 57).
/// Range: C2 (36) to A5 (81).
///
/// Cello-specific behavior:
///   - Thumb position used above A3 (MIDI 57) on the A string
///   - 4 strings, all supporting bariolage
///   - Double stops on adjacent strings only
///   - Position shifts are more costly than on violin due to longer string length
///
/// Used for BWV 1007-1012 cello suite generation.
class CelloModel : public IBowedStringInstrument {
 public:
  CelloModel();

  // Range and tuning
  uint8_t getStringCount() const override { return kNumStrings; }
  const std::vector<uint8_t>& getTuning() const override { return tuning_; }
  uint8_t getLowestPitch() const override { return kLowestPitch; }
  uint8_t getHighestPitch() const override { return kHighestPitch; }
  bool isPitchPlayable(uint8_t pitch) const override;

  // String and fingering queries
  bool isOpenString(uint8_t pitch) const override;
  std::vector<FingerPosition> getPositionsForPitch(uint8_t pitch) const override;

  // Double stops and chords
  bool isDoubleStopFeasible(uint8_t pitch_a, uint8_t pitch_b) const override;
  float doubleStopCost(uint8_t pitch_a, uint8_t pitch_b) const override;
  bool requiresArpeggiation(const std::vector<uint8_t>& pitches) const override;

  // Bow and string crossing
  float stringCrossingCost(uint8_t from_string, uint8_t to_string) const override;
  bool supportsBariolage() const override { return true; }

  // Playability cost
  BowedPlayabilityCost calculateCost(uint8_t pitch) const override;
  BowedPlayabilityCost calculateTransitionCost(
      uint8_t from_pitch, uint8_t to_pitch,
      const BowedPerformerState& state) const override;
  void updateState(BowedPerformerState& state, uint8_t pitch) const override;
  BowedPerformerState createInitialState() const override;

  /// @brief Check if a pitch requires thumb position technique.
  ///
  /// Thumb position is used on the cello for pitches above the
  /// comfortable fingered range of each string, typically above A3 (57)
  /// on the highest string or above the equivalent on lower strings.
  ///
  /// @param pitch MIDI pitch number.
  /// @return True if the most comfortable fingering uses thumb position.
  bool requiresThumbPosition(uint8_t pitch) const;

 private:
  static constexpr uint8_t kNumStrings = 4;
  static constexpr uint8_t kLowestPitch = 36;   // C2
  static constexpr uint8_t kHighestPitch = 81;  // A5

  /// Open string MIDI pitches (low to high): C2, G2, D3, A3.
  static constexpr uint8_t kOpenStrings[kNumStrings] = {36, 43, 50, 57};

  /// @brief Maximum semitones reachable above each open string.
  ///
  /// Higher strings can reach further due to shorter vibrating length
  /// and use of thumb position on upper strings.
  static constexpr uint8_t kMaxSemitonesAboveOpen[kNumStrings] = {18, 19, 20, 24};

  /// @brief Pitch threshold for thumb position on each string.
  ///
  /// Above these pitches, the left hand must shift to thumb position,
  /// which changes the fingering technique and adds cost.
  static constexpr uint8_t kThumbPositionThreshold[kNumStrings] = {48, 55, 62, 69};

  /// @brief Cost multiplier for position shifts on cello.
  ///
  /// Cello has longer string length than violin, making shifts more costly.
  static constexpr float kShiftCostPerPosition = 0.15f;

  /// @brief Additional cost for entering thumb position.
  static constexpr float kThumbPositionCost = 0.3f;

  /// @brief Cost per string crossed with the bow.
  static constexpr float kStringCrossCostPerString = 0.1f;

  /// Stored tuning vector (returned by reference from getTuning()).
  std::vector<uint8_t> tuning_;

  /// @brief Find the best string for a given pitch.
  /// @param pitch MIDI pitch number.
  /// @param out_string_idx Output: index of the best string (0-3).
  /// @param out_semitones Output: semitones above the open string.
  /// @return True if the pitch is reachable on at least one string.
  bool findBestString(uint8_t pitch, uint8_t& out_string_idx,
                      uint8_t& out_semitones) const;

  /// @brief Find the string index for a given pitch (lowest position preferred).
  /// @param pitch MIDI pitch number.
  /// @return String index (0-3) or kNumStrings if unplayable.
  uint8_t findStringForPitch(uint8_t pitch) const;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_BOWED_CELLO_MODEL_H
