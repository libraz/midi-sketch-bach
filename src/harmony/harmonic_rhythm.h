// Harmonic rhythm dynamic control -- phase-based rhythm factor computation.

#ifndef BACH_HARMONY_HARMONIC_RHYTHM_H
#define BACH_HARMONY_HARMONIC_RHYTHM_H

#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_event.h"

namespace bach {

/// Design value tables for harmonic rhythm factor by fugue phase.
/// These are FIXED values (Principle 4: Trust Design Values).

/// Harmonic rhythm factor for Establish phase: bar-level, leisurely pace.
constexpr float kHarmonicRhythmEstablish = 2.0f;

/// Harmonic rhythm factor for Develop phase: beat-level, standard pace.
constexpr float kHarmonicRhythmDevelop = 1.0f;

/// Harmonic rhythm factor for Resolve phase: slightly faster, building tension.
constexpr float kHarmonicRhythmResolve = 0.75f;

/// Pre-cadence acceleration factor.
/// Harmonic rhythm speeds up to half-beat level within 2 beats before a cadence point.
constexpr float kPreCadenceAcceleration = 0.5f;

/// Number of ticks before a cadence where acceleration begins (2 beats).
constexpr Tick kPreCadenceWindow = kTicksPerBeat * 2;

/// @brief Compute the harmonic rhythm factor for a given tick position.
///
/// Uses FuguePhase-based design value table with pre-cadence acceleration.
/// The rhythm_factor modulates harmonic event duration at query time
/// without rebuilding the timeline (lazy evaluation).
///
/// Phase boundaries (design values, not configurable):
///   - First third of total duration: Establish (2.0)
///   - Middle third: Develop (1.0)
///   - Last third: Resolve (0.75)
///
/// If the tick falls within kPreCadenceWindow ticks before any cadence point,
/// the pre-cadence acceleration factor (0.5) takes priority.
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

/// @brief Compute rhythm factor directly from a FuguePhase value.
///
/// Maps FuguePhase to a fixed rhythm factor (design values, Principle 4).
/// Pre-cadence acceleration takes priority when near_cadence is true.
///
/// @param phase The current fugue phase.
/// @param near_cadence True if within pre-cadence window.
/// @return Rhythm factor: >1.0 = slower, <1.0 = faster, 1.0 = normal.
float phaseToRhythmFactor(FuguePhase phase, bool near_cadence);

}  // namespace bach

#endif  // BACH_HARMONY_HARMONIC_RHYTHM_H
