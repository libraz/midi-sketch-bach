// Instrument-to-GM-program mapping and track application helpers.

#ifndef BACH_CORE_INSTRUMENT_PROGRAM_H
#define BACH_CORE_INSTRUMENT_PROGRAM_H

#include <cstdint>
#include <optional>
#include <vector>

#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach {

/// @brief Map an instrument type to its General MIDI program number.
///
/// Returns the legacy-consistent GM program used across the existing organ and
/// solo-string output paths, so MIDI output matches prior releases:
/// Organ=Church Organ (19), Harpsichord (6), Piano=Acoustic Grand (0),
/// Violin (40), Cello (42), Guitar=Nylon String Guitar (24).
///
/// @param instrument The instrument type.
/// @return GM program number (0-indexed per the MIDI specification).
std::uint8_t gmProgramFor(InstrumentType instrument);

/// Inclusive sounding-MIDI compass used by the output adapter for one instrument.
struct InstrumentPitchRange {
  std::uint8_t low = 0;
  std::uint8_t high = 127;
  /// True when the instrument has no key outside the range, so no note may
  /// sound there; false when the range is only the preferred placement.
  bool hard_limit = false;
};

/// @brief Return the playable sounding range used for MIDI export.
InstrumentPitchRange pitchRangeFor(InstrumentType instrument);

/// Composed-frame pitches that sound inside an instrument's compass.
struct ComposedCompassWindow {
  int low = 0;
  int high = 127;
};

/// @brief The instrument compass read back through a key offset into the
///        composed frame, at the lowest whole octave that still sounds `anchor`.
///
/// A builder whose lowest tone is fixed (an immutable ground) bounds the
/// register above it with `high`: that octave leaves the most room above the
/// anchor, and the output placement finds the same octave for the whole score.
///
/// @param instrument The instrument whose compass applies.
/// @param key_offset Semitones the output adds for the tonic
///        (keyTranspositionSemitones).
/// @param anchor The lowest composed pitch the placement must hold.
/// @return The composed window [low, high]; `anchor` lies within its lowest octave.
ComposedCompassWindow composedCompassWindow(InstrumentType instrument, int key_offset, int anchor);

/// @brief Select a single octave displacement for a complete C-major score.
///
/// The result is added after key transposition. It is either zero or a
/// whole-octave multiple; individual notes are never clamped.
///
/// The compass is a preference, not a precondition. Only whole octaves are
/// available to compensate for key transposition, so a score whose ambitus
/// comes within a few semitones of the compass fits in some keys and not in
/// others; refusing those keys would withhold the piece entirely rather than
/// place it a little outside the ideal range. When no displacement fits, the
/// one that leaves the fewest semitones outside the compass is returned. For an
/// instrument whose range is a hard limit, the composer's output placement
/// (composer/compass_placement.h) moves what this displacement leaves outside.
///
/// `nullopt` means no displacement keeps the score inside MIDI's own 0-127
/// range, which requires an ambitus wider than MIDI itself.
std::optional<int> selectOutputOctaveShift(const std::vector<NoteEvent>& notes, Key key,
                                           InstrumentType instrument);

/// @brief Apply an instrument to a set of tracks.
///
/// Sets each track's GM program to gmProgramFor(instrument). For any track whose
/// name is empty, assigns a default "Voice N" name where N is the track index,
/// preserving any name already set by the generation path.
///
/// @param tracks Tracks to update in place.
/// @param instrument The instrument to apply.
void applyInstrument(std::vector<Track>& tracks, InstrumentType instrument);

}  // namespace bach

#endif  // BACH_CORE_INSTRUMENT_PROGRAM_H
