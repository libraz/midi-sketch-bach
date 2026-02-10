// Factory for creating fretted-instrument-constrained NoteEvents.

#ifndef BACH_INSTRUMENT_FRETTED_FRETTED_NOTE_FACTORY_H
#define BACH_INSTRUMENT_FRETTED_FRETTED_NOTE_FACTORY_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

class IFrettedInstrument;

/// @brief Factory that creates NoteEvents clamped to a fretted instrument's range.
///
/// Phase 0 implementation: pitch clamping only. Future phases may add
/// string assignment, position metadata, and articulation adjustments.
class FrettedNoteFactory {
 public:
  /// @brief Construct with a reference to a fretted instrument model.
  /// @param instrument The fretted instrument used for range constraints.
  explicit FrettedNoteFactory(const IFrettedInstrument& instrument);

  /// @brief Create a NoteEvent with pitch clamped to the instrument range.
  /// @param pitch Desired MIDI pitch (will be clamped if out of range).
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @param velocity MIDI velocity (0-127).
  /// @return NoteEvent with pitch guaranteed to be within instrument range.
  NoteEvent createNote(uint8_t pitch, Tick start, Tick duration,
                       uint8_t velocity) const;

 private:
  const IFrettedInstrument& instrument_;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_FRETTED_FRETTED_NOTE_FACTORY_H
