// Common types for the physical performer abstraction layer.

#ifndef BACH_INSTRUMENT_COMMON_PERFORMER_TYPES_H
#define BACH_INSTRUMENT_COMMON_PERFORMER_TYPES_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

/// @brief Classification of physical instrument performers.
///
/// Each performer type has fundamentally different physical constraints
/// that affect playability and cost calculations.
enum class PerformerType : uint8_t {
  Keyboard,  // Organ, harpsichord, piano
  Bowed,     // Violin, cello
  Fretted    // Guitar
};

/// @brief Mutable state of a physical performer during generation.
///
/// Subclasses may add instrument-specific state (e.g. hand positions for
/// keyboard, bow direction for strings). The base state tracks timing
/// and basic ergonomic fatigue.
struct PerformerState {
  Tick current_tick = 0;
  float fatigue = 0.0f;
  uint8_t last_pitch = 0;

  virtual ~PerformerState() = default;

  /// @brief Reset to initial (rested) state.
  virtual void reset() {
    current_tick = 0;
    fatigue = 0.0f;
    last_pitch = 0;
  }
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_COMMON_PERFORMER_TYPES_H
