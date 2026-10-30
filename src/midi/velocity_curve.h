// Velocity curve for non-organ instruments: phrase-aware dynamic shaping.

#ifndef BACH_MIDI_VELOCITY_CURVE_H
#define BACH_MIDI_VELOCITY_CURVE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Compute a phrase-aware velocity for a note based on metric position.
///
/// Applies beat emphasis and phrase shaping for non-organ instruments.
/// Organ and harpsichord keep their input velocity -- call this only for violin,
/// cello, guitar, and piano.
///
/// Beat emphasis:
///   - 4/4: downbeat +10, beat 3 +5, other beats -5
///   - Sarabande 3/4: downbeat +10, beat 2 +5, beat 3 -5
///   - Standard 3/4: downbeat +10, other beats -5
///
/// Phrase shaping:
///   - First bar of phrase: +8
///   - 2 beats before cadence: -3 (diminuendo)
///
/// Base velocity: 70. Range clamped to [50, 110].
///
/// @param tick Note start tick.
/// @param cadence_ticks Sorted cadence tick positions (may be empty).
/// @param phrase_start_tick Start tick of the current phrase.
/// @param time_signature The notated time signature used for beat and bar positions.
/// @param meter_profile Beat hierarchy used for triple meter.
/// @return Velocity value in [50, 110].
uint8_t computeVelocity(Tick tick, const std::vector<Tick>& cadence_ticks,
                        Tick phrase_start_tick = 0, TimeSignature time_signature = {4, 4},
                        MeterProfile meter_profile = MeterProfile::StandardTriple);

/// @brief Apply velocity curves to all notes in a track.
///
/// Only modifies notes for instruments other than organ and harpsichord; those keep their
/// input velocity.
/// A phrase starts at tick 0 and at each cadence tick; only the first bar of a phrase
/// receives the phrase-start accent.
///
/// @param notes Notes to adjust (modified in place).
/// @param instrument The instrument type.
/// @param cadence_ticks Cadence positions for phrase shaping.
/// @param time_signature The notated time signature used for beat and bar positions.
/// @param meter_profile Beat hierarchy used for triple meter.
void applyVelocityCurve(std::vector<NoteEvent>& notes, InstrumentType instrument,
                        const std::vector<Tick>& cadence_ticks,
                        TimeSignature time_signature = {4, 4},
                        MeterProfile meter_profile = MeterProfile::StandardTriple);

}  // namespace bach

#endif  // BACH_MIDI_VELOCITY_CURVE_H
