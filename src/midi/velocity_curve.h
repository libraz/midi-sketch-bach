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
/// Organ always uses fixed velocity (80) -- call this only for violin,
/// cello, guitar, harpsichord, and piano.
///
/// Beat emphasis:
///   - Beat 0 (downbeat): +10
///   - Beat 2 (secondary strong): +5
///   - Beats 1, 3 (weak): -5
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
/// @return Velocity value in [50, 110].
uint8_t computeVelocity(Tick tick, const std::vector<Tick>& cadence_ticks,
                         Tick phrase_start_tick = 0);

/// @brief Apply velocity curves to all notes in a track.
///
/// Only modifies notes for non-organ instruments. Organ notes are left at 80.
///
/// @param notes Notes to adjust (modified in place).
/// @param instrument The instrument type.
/// @param cadence_ticks Cadence positions for phrase shaping.
void applyVelocityCurve(std::vector<NoteEvent>& notes,
                        InstrumentType instrument,
                        const std::vector<Tick>& cadence_ticks);

}  // namespace bach

#endif  // BACH_MIDI_VELOCITY_CURVE_H
