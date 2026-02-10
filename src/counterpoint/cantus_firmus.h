// Cantus firmus: Fux-style fixed melody for species counterpoint exercises.

#ifndef BACH_COUNTERPOINT_CANTUS_FIRMUS_H
#define BACH_COUNTERPOINT_CANTUS_FIRMUS_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Cantus firmus: a simple, stepwise melody of whole notes used as
/// the fixed voice in species counterpoint.
///
/// Properties (Fux-style):
///   - All whole notes (duration = kTicksPerBar)
///   - Starts and ends on the tonic
///   - Mostly stepwise motion
///   - Single climax point (highest note appears exactly once)
///   - Range within an octave (12 semitones)
///   - 4-8 bars
struct CantusFirmus {
  std::vector<NoteEvent> notes;
  Key key = Key::C;

  /// @brief Get the number of notes in the cantus firmus.
  /// @return Note count.
  size_t noteCount() const;

  /// @brief Get the lowest MIDI pitch in the cantus firmus.
  /// @return Lowest pitch, or 127 if empty.
  uint8_t lowestPitch() const;

  /// @brief Get the highest MIDI pitch in the cantus firmus.
  /// @return Highest pitch, or 0 if empty.
  uint8_t highestPitch() const;
};

/// @brief Generates Fux-style cantus firmus melodies.
class CantusFirmusGenerator {
 public:
  /// @brief Generate a cantus firmus in the given key.
  /// @param key Musical key.
  /// @param length_bars Number of bars (clamped to 4-8).
  /// @param seed Random seed for deterministic generation.
  /// @return Generated CantusFirmus.
  CantusFirmus generate(Key key, uint8_t length_bars, uint32_t seed) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_CANTUS_FIRMUS_H
