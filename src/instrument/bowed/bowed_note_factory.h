// Factory for creating bowed-instrument-constrained NoteEvents.

#ifndef BACH_INSTRUMENT_BOWED_BOWED_NOTE_FACTORY_H
#define BACH_INSTRUMENT_BOWED_BOWED_NOTE_FACTORY_H

#include <cstdint>

#include "core/basic_types.h"

namespace bach {

class IBowedStringInstrument;

/// @brief Options for creating a bowed-string NoteEvent.
///
/// Provides pitch, timing, and bowed-string-specific preferences
/// for note creation. The factory uses these to create NoteEvents
/// clamped to the instrument's playable range.
struct BowedNoteOptions {
  uint8_t pitch = 0;          // Desired MIDI pitch
  Tick tick = 0;               // Absolute tick position
  Tick duration = 0;           // Duration in ticks
  uint8_t velocity = 80;      // MIDI velocity (0-127)
  bool prefer_open_string = true;  // Prefer open string when pitch matches
  BachNoteSource source = BachNoteSource::Unknown;  // Provenance source
};

/// @brief Factory that creates NoteEvents constrained to a bowed instrument's range.
///
/// Clamps pitches to the instrument's playable range and applies
/// bowed-string-specific adjustments. When prefer_open_string is true
/// and the requested pitch matches an open string, the velocity may be
/// slightly boosted to reflect the natural resonance of open strings.
class BowedNoteFactory {
 public:
  /// @brief Construct with a reference to a bowed string instrument model.
  /// @param instrument The bowed instrument used for range constraints.
  explicit BowedNoteFactory(const IBowedStringInstrument& instrument);

  /// @brief Create a NoteEvent with pitch clamped to the instrument range.
  /// @param options Note creation options including pitch, timing, velocity.
  /// @return NoteEvent with pitch guaranteed to be within instrument range.
  NoteEvent createNote(const BowedNoteOptions& options) const;

  /// @brief Create a NoteEvent with explicit parameters (convenience overload).
  /// @param pitch Desired MIDI pitch (will be clamped if out of range).
  /// @param start Absolute tick position.
  /// @param duration Duration in ticks.
  /// @param velocity MIDI velocity (0-127).
  /// @param source Provenance source for the note.
  /// @return NoteEvent with pitch guaranteed to be within instrument range.
  NoteEvent createNote(uint8_t pitch, Tick start, Tick duration,
                       uint8_t velocity,
                       BachNoteSource source = BachNoteSource::Unknown) const;

 private:
  const IBowedStringInstrument& instrument_;
};

}  // namespace bach

#endif  // BACH_INSTRUMENT_BOWED_BOWED_NOTE_FACTORY_H
