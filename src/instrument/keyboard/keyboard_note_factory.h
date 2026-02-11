// Factory for creating keyboard-constrained NoteEvents.

#ifndef BACH_INSTRUMENT_KEYBOARD_KEYBOARD_NOTE_FACTORY_H
#define BACH_INSTRUMENT_KEYBOARD_KEYBOARD_NOTE_FACTORY_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

class IKeyboardInstrument;

/// @brief Factory that creates NoteEvents clamped to a keyboard instrument's range.
///
/// Phase 0 implementation: pitch clamping only. Future phases may add
/// hand-assignment metadata, velocity curves, and articulation adjustments.
class KeyboardNoteFactory {
 public:
  /// @brief Construct with a reference to a keyboard instrument model.
  /// @param instrument The keyboard instrument used for range constraints.
  explicit KeyboardNoteFactory(const IKeyboardInstrument& instrument);

  /// @brief Create a NoteEvent with pitch clamped to the keyboard range.
  /// @param pitch Desired MIDI pitch (will be clamped if out of range).
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @param velocity MIDI velocity (0-127).
  /// @return NoteEvent with pitch guaranteed to be within instrument range.
  NoteEvent createNote(uint8_t pitch, Tick start, Tick duration,
                       uint8_t velocity,
                       BachNoteSource source = BachNoteSource::Unknown) const;

 private:
  const IKeyboardInstrument& instrument_;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_KEYBOARD_KEYBOARD_NOTE_FACTORY_H
