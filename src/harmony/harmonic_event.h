// Harmonic event -- a single chord with timing and context.

#ifndef BACH_HARMONY_HARMONIC_EVENT_H
#define BACH_HARMONY_HARMONIC_EVENT_H

#include <cstdint>

#include "core/basic_types.h"
#include "harmony/chord_types.h"

namespace bach {

/// @brief A single harmonic event in a timeline.
///
/// Represents a chord active over a time span [tick, end_tick).
/// Carries key context, chord identity, bass pitch, and metric weight.
struct HarmonicEvent {
  Tick tick = 0;          // Start tick (inclusive)
  Tick end_tick = 0;      // End tick (exclusive)
  Key key = Key::C;       // Current key context
  bool is_minor = false;  // Major or minor mode
  Chord chord;            // Current chord
  uint8_t bass_pitch = 0; // Bass note MIDI pitch
  float weight = 1.0f;    // Harmonic weight (1.0 = strong beat, 0.5 = weak)
  float rhythm_factor = 1.0f;  // Harmonic rhythm multiplier (1.0 = normal, <1.0 = faster, >1.0 = slower)
  bool is_immutable = false;  // If true, cannot be altered by generation
  /// Reserved for future modulation support.
  Key modulation_target = Key::C;  // Target key for modulation (same as key if none)
  /// Reserved for future modulation support.
  bool has_modulation = false;     // Whether this event represents a modulation point
};

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_EVENT_H
