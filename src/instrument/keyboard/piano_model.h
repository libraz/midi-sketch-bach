// Piano instrument model.

#ifndef BACH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H
#define BACH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H

#include "instrument/keyboard/keyboard_instrument.h"

namespace bach {

/// @brief Physical model of a standard 88-key piano.
///
/// Implements IKeyboardInstrument with range A0 (21) to C8 (108).
/// Used for playability evaluation and hand-assignment of keyboard parts.
/// Constructor takes explicit constraint/physics parameters so callers
/// can select skill levels (beginner through virtuoso).
class PianoModel : public IKeyboardInstrument {
 public:
  /// @brief Construct a PianoModel with explicit physical parameters.
  /// @param span_constraints Hand span constraints (use factory methods for presets).
  /// @param hand_physics Hand movement cost parameters.
  PianoModel(KeyboardSpanConstraints span_constraints, KeyboardHandPhysics hand_physics);

  /// @brief Construct with intermediate (default) skill level.
  PianoModel();

  uint8_t getLowestPitch() const override;
  uint8_t getHighestPitch() const override;
  bool isPitchInRange(uint8_t pitch) const override;

  VoicingHandAssignment assignHands(
      const std::vector<uint8_t>& pitches) const override;

  bool isPlayableByOneHand(const std::vector<uint8_t>& pitches,
                           Hand hand) const override;

  bool isVoicingPlayable(const std::vector<uint8_t>& pitches) const override;

  KeyboardPlayabilityCost calculateTransitionCost(
      const KeyboardState& from_state,
      const std::vector<uint8_t>& to_pitches) const override;

  std::vector<uint8_t> suggestPlayableVoicing(
      const std::vector<uint8_t>& desired_pitches) const override;

  const KeyboardSpanConstraints& getSpanConstraints() const override;
  const KeyboardHandPhysics& getHandPhysics() const override;

 private:
  static constexpr uint8_t kMinPitch = 21;   // A0
  static constexpr uint8_t kMaxPitch = 108;  // C8

  KeyboardSpanConstraints span_constraints_;
  KeyboardHandPhysics hand_physics_;

  /// @brief Compute cost for a set of pitches played by a single hand.
  /// @param pitches Sorted pitches assigned to one hand.
  /// @param hand_state Current state of the hand.
  /// @return Cost value (0.0 = trivial).
  float computeHandCost(const std::vector<uint8_t>& pitches,
                        const HandState& hand_state) const;

  /// @brief Clamp a pitch to the piano range.
  uint8_t clampPitch(uint8_t pitch) const;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H
