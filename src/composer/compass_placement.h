#ifndef BACH_COMPOSER_COMPASS_PLACEMENT_H
#define BACH_COMPOSER_COMPASS_PLACEMENT_H

#include <cstddef>
#include <functional>
#include <optional>

#include "composer/composer.h"
#include "composer/harmonic_plan.h"
#include "composer/material.h"
#include "core/basic_types.h"
#include "harmony/key.h"

namespace bach::composer {

// Output placement: the one site that holds a composed score inside its
// instrument's sounding compass.
//
// The composer writes in C and the MIDI writer transposes to the requested key
// plus a whole-octave displacement. This pass runs between the ornament pass
// and FinalScore validation, so it sees the transposition the writer will
// apply without applying it: notes stay in the composed frame, and only an
// octave is ever added to or removed from them.
//
// A displacement that fits the whole score moves nothing. Otherwise, for an
// instrument whose compass is a hard limit (pitchRangeFor), the bar holding a
// note outside the compass is moved an octave inward in that voice, together
// with the neighbouring voices it would otherwise cross and any voice doubling
// it, so the intervals inside the moved group and the register order against
// the rest of the texture are kept. Notes a declaration pins to an absolute
// pitch -- a sequence step, a suspension, a cadence formula -- are moved only
// when nothing else places the score. A moved note's authored pitch moves with
// it: the displacement is part of what the piece declares.

/// Result of placing a composed score in its output compass.
struct CompassPlacement {
  int octave_shift = 0;             ///< Whole-octave displacement the MIDI writer adds.
  std::size_t displaced_notes = 0;  ///< Notes moved by an octave in the composed frame.
  std::size_t crossing_notes = 0;   ///< Of those, notes moved without a crossing-free group.
  std::size_t pinned_notes = 0;     ///< Of those, notes a declaration pins to their pitch.
};

/// @brief Place a composed score inside the sounding compass of `instrument`.
///
/// Candidate placements are tried best first. One that moves no note is taken
/// as it stands; one that moves notes is kept only if `accept` takes the moved
/// score, so a placement never costs a piece its validity. The last candidate
/// is the nearest whole-octave displacement with nothing moved.
///
/// @param result Composed notes and their index-parallel provenance; moved in place.
/// @param material The material, read for the declarations it pins.
/// @param harmony The harmonic plan, read for its cadences.
/// @param key The output key the MIDI writer transposes to.
/// @param instrument The instrument whose compass applies.
/// @param ticks_per_bar Bar length; a bar of one voice is the smallest unit moved.
/// @param accept Judges a score with moved notes; called on the placed result.
/// @return The placement taken, or nullopt when no displacement keeps the score inside MIDI.
std::optional<CompassPlacement> placeInOutputCompass(
    ComposeResult* result, const Material& material, const HarmonicPlan& harmony, Key key,
    InstrumentType instrument, Tick ticks_per_bar,
    const std::function<bool(const ComposeResult&)>& accept);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_COMPASS_PLACEMENT_H
