// Interface for keyboard instrument models.

#ifndef BACH_INSTRUMENT_KEYBOARD_KEYBOARD_INSTRUMENT_H
#define BACH_INSTRUMENT_KEYBOARD_KEYBOARD_INSTRUMENT_H

#include <cstdint>
#include <vector>

#include "instrument/keyboard/keyboard_types.h"

namespace bach {

/// @brief Abstract interface for keyboard instrument models.
///
/// Provides physical playability evaluation for keyboard instruments
/// (organ, harpsichord, piano). Evaluates whether a voicing (set of
/// simultaneous pitches) is playable by two hands, and calculates
/// ergonomic transition costs between successive voicings.
class IKeyboardInstrument {
 public:
  virtual ~IKeyboardInstrument() = default;

  /// @brief Get the lowest playable MIDI pitch on this keyboard.
  virtual uint8_t getLowestPitch() const = 0;

  /// @brief Get the highest playable MIDI pitch on this keyboard.
  virtual uint8_t getHighestPitch() const = 0;

  /// @brief Check if a single pitch is within the instrument's range.
  /// @param pitch MIDI pitch number (0-127).
  /// @return True if the pitch is playable.
  virtual bool isPitchInRange(uint8_t pitch) const = 0;

  /// @brief Assign pitches to left and right hands.
  /// @param pitches Sorted (ascending) vector of simultaneous pitches.
  /// @return Hand assignment with validity flag.
  virtual VoicingHandAssignment assignHands(
      const std::vector<uint8_t>& pitches) const = 0;

  /// @brief Check if all pitches can be played by a single hand.
  /// @param pitches Sorted vector of pitches.
  /// @param hand Which hand to check.
  /// @return True if playable by the specified hand alone.
  virtual bool isPlayableByOneHand(const std::vector<uint8_t>& pitches,
                                   Hand hand) const = 0;

  /// @brief Check if a full voicing (both hands) is playable.
  /// @param pitches Sorted vector of all simultaneous pitches.
  /// @return True if some valid hand assignment exists.
  virtual bool isVoicingPlayable(const std::vector<uint8_t>& pitches) const = 0;

  /// @brief Calculate transition cost between two successive voicings.
  /// @param from_state Current keyboard state (hand positions).
  /// @param to_pitches Next set of pitches to play.
  /// @return Playability cost with component breakdown.
  virtual KeyboardPlayabilityCost calculateTransitionCost(
      const KeyboardState& from_state,
      const std::vector<uint8_t>& to_pitches) const = 0;

  /// @brief Suggest a playable voicing close to the desired pitches.
  /// @param desired_pitches The ideal set of pitches.
  /// @return A modified set of pitches that is playable, or empty if impossible.
  virtual std::vector<uint8_t> suggestPlayableVoicing(
      const std::vector<uint8_t>& desired_pitches) const = 0;

  /// @brief Get the physical span constraints for this instrument's player.
  virtual const KeyboardSpanConstraints& getSpanConstraints() const = 0;

  /// @brief Get the hand physics parameters for this instrument's player.
  virtual const KeyboardHandPhysics& getHandPhysics() const = 0;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_KEYBOARD_INSTRUMENT_H
