// Violin instrument model for solo string generation.

#ifndef BACH_INSTRUMENT_BOWED_VIOLIN_MODEL_H
#define BACH_INSTRUMENT_BOWED_VIOLIN_MODEL_H

#include <cstdint>
#include <vector>

#include "instrument/bowed/bowed_string_instrument.h"

namespace bach {

/// @brief Physical model of a solo violin.
///
/// Standard tuning: G3, D4, A4, E5 (MIDI 55, 62, 69, 76).
/// Range: G3 (55) to C7 (96).
///
/// Violin-specific behavior:
///   - Higher positions are more accessible than on cello (shorter string)
///   - Full bariolage support (especially on E and A strings)
///   - Double stops on adjacent strings only
///   - Position shifts are less costly than on cello
///   - Very high positions (above 7th) used in Bach Partitas
///
/// Used for BWV 1001-1006 violin sonatas/partitas and BWV 1004-5 Chaconne.
class ViolinModel : public IBowedStringInstrument {
 public:
  ViolinModel();

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

  /// @brief Check if a pitch is in the high position range.
  ///
  /// High positions (above ~5th position) require more precise intonation
  /// and are characteristic of virtuosic Bach violin writing.
  ///
  /// @param pitch MIDI pitch number.
  /// @return True if the most comfortable fingering uses a high position.
  bool isHighPosition(uint8_t pitch) const;

 private:
  static constexpr uint8_t kNumStrings = 4;
  static constexpr uint8_t kLowestPitch = 55;   // G3
  static constexpr uint8_t kHighestPitch = 96;  // C7

  /// Open string MIDI pitches (low to high): G3, D4, A4, E5.
  static constexpr uint8_t kOpenStrings[kNumStrings] = {55, 62, 69, 76};

  /// @brief Maximum semitones reachable above each open string.
  ///
  /// Violin strings are shorter than cello, allowing higher positions
  /// more comfortably. The E string can reach very high.
  static constexpr uint8_t kMaxSemitonesAboveOpen[kNumStrings] = {19, 20, 20, 20};

  /// @brief Pitch threshold for high positions on each string.
  ///
  /// Above these pitches, the left hand is in 5th position or higher,
  /// requiring more precise intonation.
  static constexpr uint8_t kHighPositionThreshold[kNumStrings] = {65, 72, 79, 86};

  /// @brief Cost multiplier for position shifts on violin.
  ///
  /// Lower than cello due to shorter string length.
  static constexpr float kShiftCostPerPosition = 0.1f;

  /// @brief Additional cost for high positions.
  static constexpr float kHighPositionCost = 0.15f;

  /// @brief Cost per string crossed with the bow.
  static constexpr float kStringCrossCostPerString = 0.08f;

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

#endif  // BACH_INSTRUMENT_BOWED_VIOLIN_MODEL_H
