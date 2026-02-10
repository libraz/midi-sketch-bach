// Harmonic rhythm dynamic control -- phase-based rhythm factor computation.

#ifndef BACH_HARMONY_HARMONIC_RHYTHM_H
#define BACH_HARMONY_HARMONIC_RHYTHM_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

/// Design value tables for harmonic rhythm factor by fugue phase.
/// These are FIXED values (Principle 4: Trust Design Values).

/// Harmonic rhythm factor for Establish phase: steady, normal pace.
constexpr float kHarmonicRhythmEstablish = 1.0f;

/// Harmonic rhythm factor for Develop phase: slightly faster harmonic changes.
constexpr float kHarmonicRhythmDevelop = 0.85f;

/// Harmonic rhythm factor for Resolve phase: broadening, slowing harmonic rhythm.
constexpr float kHarmonicRhythmResolve = 1.2f;

/// Pre-cadence acceleration factor.
/// Harmonic rhythm speeds up within 2 beats before a cadence point.
constexpr float kPreCadenceAcceleration = 0.7f;

/// Number of ticks before a cadence where acceleration begins (2 beats).
constexpr Tick kPreCadenceWindow = kTicksPerBeat * 2;

/// @brief Compute the harmonic rhythm factor for a given tick position.
///
/// Uses FuguePhase-based design value table with pre-cadence acceleration.
/// The rhythm_factor modulates harmonic event duration at query time
/// without rebuilding the timeline (lazy evaluation).
///
/// Phase boundaries (design values, not configurable):
///   - First third of total duration: Establish (1.0)
///   - Middle third: Develop (0.85)
///   - Last third: Resolve (1.2)
///
/// If the tick falls within kPreCadenceWindow ticks before any cadence point,
/// the pre-cadence acceleration factor (0.7) takes priority.
///
/// @param tick Current tick position.
/// @param total_duration Total piece duration in ticks.
/// @param cadence_ticks Planned cadence positions (sorted).
/// @return Rhythm factor: 1.0 = normal, <1.0 = faster harmonic rhythm, >1.0 = slower.
float computeRhythmFactor(Tick tick, Tick total_duration,
                          const std::vector<Tick>& cadence_ticks);

/// @brief Apply rhythm factors to a harmonic timeline's events.
///
/// Iterates through the timeline events and sets each event's rhythm_factor
/// based on its tick position. This is called once after timeline construction
/// and does NOT rebuild the timeline structure.
///
/// @param events Mutable reference to timeline events.
/// @param total_duration Total piece duration.
/// @param cadence_ticks Planned cadence positions.
void applyRhythmFactors(std::vector<HarmonicEvent>& events,
                        Tick total_duration,
                        const std::vector<Tick>& cadence_ticks);

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_RHYTHM_H
