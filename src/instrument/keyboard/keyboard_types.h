// Types for keyboard instrument models (organ, harpsichord, piano).

#ifndef BACH_INSTRUMENT_KEYBOARD_KEYBOARD_TYPES_H
#define BACH_INSTRUMENT_KEYBOARD_KEYBOARD_TYPES_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Which hand plays a note or passage.
enum class Hand : uint8_t {
  Left,
  Right
};

/// @brief Keyboard technique classification for cost estimation.
enum class KeyboardTechnique : uint8_t {
  Normal,       // Standard fingering
  Stretch,      // Finger stretch required
  CrossOver,    // Thumb-under or finger-over
  Jump,         // Large positional jump
  Repeated      // Same note repeated quickly
};

/// @brief Pedal (sustain/sostenuto) state for piano.
enum class PedalState : uint8_t {
  Off,
  SustainOn,
  SostenutoOn
};

/// @brief Current position of a key (for modeling legato / staccato).
enum class KeyPosition : uint8_t {
  Released,
  Depressed,
  Sustained  // Key released but sustain pedal held
};

/// @brief Physical span constraints for a keyboard player's hand.
///
/// Use the static factory methods for predefined skill levels.
struct KeyboardSpanConstraints {
  uint8_t normal_span = 9;    // Comfortable reach in semitones (e.g. C4-A4)
  uint8_t max_span = 12;      // Maximum stretch in semitones (e.g. octave)
  uint8_t max_notes = 5;      // Maximum simultaneous notes per hand

  /// @brief Beginner: small hands, limited stretch.
  static KeyboardSpanConstraints beginner() { return {7, 9, 4}; }

  /// @brief Intermediate: average adult hands.
  static KeyboardSpanConstraints intermediate() { return {9, 11, 5}; }

  /// @brief Advanced: trained pianist, comfortable with large spans.
  static KeyboardSpanConstraints advanced() { return {10, 12, 5}; }

  /// @brief Virtuoso: exceptional hand span and independence.
  static KeyboardSpanConstraints virtuoso() { return {12, 14, 5}; }
};

/// @brief Physics parameters for hand movement cost calculation.
///
/// Use the static factory methods for predefined skill levels.
struct KeyboardHandPhysics {
  float jump_cost_per_semitone = 0.05f;    // Cost per semitone of hand jump
  float stretch_cost_per_semitone = 0.1f;  // Extra cost per semitone beyond normal span
  float cross_over_cost = 0.3f;            // Cost for thumb-under / finger-over
  float repeated_note_cost = 0.15f;        // Cost for repeated note at fast tempo
  float fatigue_recovery_rate = 0.01f;     // Fatigue reduction per beat of rest

  /// @brief Beginner: high movement costs.
  static KeyboardHandPhysics beginner() { return {0.08f, 0.15f, 0.5f, 0.25f, 0.005f}; }

  /// @brief Intermediate: moderate costs.
  static KeyboardHandPhysics intermediate() { return {0.05f, 0.1f, 0.3f, 0.15f, 0.01f}; }

  /// @brief Advanced: low costs from training.
  static KeyboardHandPhysics advanced() { return {0.03f, 0.07f, 0.2f, 0.1f, 0.015f}; }

  /// @brief Virtuoso: minimal physical limitations.
  static KeyboardHandPhysics virtuoso() { return {0.02f, 0.04f, 0.1f, 0.05f, 0.02f}; }
};

/// @brief State of a single hand during keyboard performance.
struct HandState {
  uint8_t center_pitch = 60;  // Current hand center position (MIDI pitch)
  uint8_t num_held = 0;       // Number of keys currently held by this hand
  float fatigue = 0.0f;       // Accumulated fatigue (0.0 = rested)
};

/// @brief Full keyboard performer state (both hands + pedal).
struct KeyboardState {
  HandState left;
  HandState right;
  PedalState pedal = PedalState::Off;
  Tick current_tick = 0;

  /// @brief Reset to default resting position.
  void reset() {
    left = {48, 0, 0.0f};   // Left hand around C3
    right = {72, 0, 0.0f};  // Right hand around C5
    pedal = PedalState::Off;
    current_tick = 0;
  }
};

/// @brief Assignment of notes in a voicing to left/right hands.
struct VoicingHandAssignment {
  std::vector<uint8_t> left_pitches;   // Pitches assigned to left hand
  std::vector<uint8_t> right_pitches;  // Pitches assigned to right hand
  bool is_valid = false;               // Whether the assignment is playable
};

/// @brief Detailed playability cost for a keyboard passage.
struct KeyboardPlayabilityCost {
  float total = 0.0f;            // Overall cost (sum of components)
  float left_hand_cost = 0.0f;   // Cost for left hand part
  float right_hand_cost = 0.0f;  // Cost for right hand part
  float transition_cost = 0.0f;  // Cost for transitioning from previous state
  bool is_playable = true;       // Whether the passage is physically possible
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_KEYBOARD_TYPES_H
