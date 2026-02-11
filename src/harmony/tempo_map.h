// Tempo map generators for per-form MIDI tempo events.
//
// Each form has a dedicated function that produces structurally meaningful
// tempo changes at section boundaries. Tempo percentages are fixed design
// values (Principle 4: Trust Design Values), not generated or searched.

#ifndef BACH_HARMONY_TEMPO_MAP_H
#define BACH_HARMONY_TEMPO_MAP_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "fugue/fugue_structure.h"

namespace bach {

struct ToccataSectionBoundary;

/// @brief Apply a percentage change to a base BPM, clamped to [40, 200].
/// @param base_bpm Base tempo in beats per minute.
/// @param percent_change Percentage adjustment (e.g. +3.0 for 3% faster, -10.0 for 10% slower).
/// @return Adjusted BPM clamped to valid range.
uint16_t adjustBpm(uint16_t base_bpm, float percent_change);

/// @brief Generate a tempo map for a fugue based on its formal structure.
///
/// Tempo design values (Baroque practice, subtle changes):
///   - Exposition:        0% (steady declaration)
///   - Episode:          +3% (lighter texture)
///   - MiddleEntry:       0% (return to subject weight)
///   - Stretto:          +4% (tension building)
///   - Coda:            -10% (final ritardando)
///
/// A dominant pedal zone (last 2 bars before Stretto) gets -5%.
///
/// @param structure Formal fugue structure with section boundaries.
/// @param base_bpm Base tempo in beats per minute.
/// @return Sorted vector of TempoEvent for the fugue.
std::vector<TempoEvent> generateFugueTempoMap(const FugueStructure& structure,
                                               uint16_t base_bpm);

/// @brief Generate a tempo map for a toccata free section (BWV 565 style).
///
/// Tempo design values (freer Baroque style, dramatic flexibility):
///   - Opening gesture:  +8% (virtuosic energy)
///   - Pre-recitative:  -15% (dramatic pause)
///   - Recitative base: -10% (speech-like)
///   - Fermata spots:   -25% (very slow moments, 2 points within recitative)
///   - Drive to cadence:+12% (building acceleration)
///   - Transition:       -8% (brief ritardando at end)
///
/// @param opening_start Start tick of opening gesture.
/// @param opening_end End tick of opening gesture.
/// @param recit_start Start tick of recitative section.
/// @param recit_end End tick of recitative section.
/// @param drive_start Start tick of drive-to-cadence section.
/// @param drive_end End tick of drive-to-cadence section.
/// @param base_bpm Base tempo in beats per minute.
/// @return Sorted vector of TempoEvent for the toccata.
std::vector<TempoEvent> generateToccataTempoMap(Tick opening_start, Tick opening_end,
                                                 Tick recit_start, Tick recit_end,
                                                 Tick drive_start, Tick drive_end,
                                                 uint16_t base_bpm);

/// @brief Generate an archetype-aware tempo map for a toccata.
///
/// Dispatches to archetype-specific tempo curves:
///   - Dramaticus: delegates to the 6-arg overload above.
///   - Perpetuus:  minimal variation (metronome-like persistence).
///   - Concertato: large inter-movement contrasts.
///   - Sectionalis: moderate changes at section boundaries.
///
/// @param archetype Toccata structural archetype.
/// @param sections Section boundary list (from ToccataResult).
/// @param base_bpm Base tempo in beats per minute.
/// @return Sorted vector of TempoEvent for the toccata.
std::vector<TempoEvent> generateToccataTempoMap(ToccataArchetype archetype,
                                                 const std::vector<ToccataSectionBoundary>& sections,
                                                 uint16_t base_bpm);

/// @brief Generate a tempo map for a fantasia free section (BWV 537/542 style).
///
/// Tempo design values (contemplative, gentle breathing):
///   - Base:            -5% (contemplative)
///   - Phrase boundary:  additional -5% at each phrase boundary
///   - Final 2 bars:    additional -8% (final broadening)
///
/// @param total_duration Total duration in ticks.
/// @param section_bars Number of bars in the fantasia section.
/// @param base_bpm Base tempo in beats per minute.
/// @return Sorted vector of TempoEvent for the fantasia.
std::vector<TempoEvent> generateFantasiaTempoMap(Tick total_duration, int section_bars,
                                                  uint16_t base_bpm);

/// @brief Generate a tempo map for a passacaglia (BWV 582 style).
///
/// Tempo design values (steady ground bass, minimal variation):
///   - All variations:   0% (steady)
///   - Final variation:  -3% (slight broadening)
///
/// @param num_variations Number of ground bass variations.
/// @param ground_bass_bars Bars per ground bass cycle.
/// @param base_bpm Base tempo in beats per minute.
/// @return Sorted vector of TempoEvent for the passacaglia.
std::vector<TempoEvent> generatePassacagliaTempoMap(int num_variations,
                                                     int ground_bass_bars,
                                                     uint16_t base_bpm);

// ---------------------------------------------------------------------------
// Cadence ritardando (Principle 4: fixed design values)
// ---------------------------------------------------------------------------

/// @brief Tempo factor 2 beats before cadence (-5%).
constexpr float kRitardandoFactor2Beats = 0.95f;

/// @brief Tempo factor 1 beat before cadence (-8%).
constexpr float kRitardandoFactor1Beat = 0.92f;

/// @brief Tempo factor on the cadence beat (-12%).
constexpr float kRitardandoFactorCadence = 0.88f;

/// @brief Generate ritardando tempo events before cadence points.
///
/// For each cadence tick, inserts a 3-stage deceleration:
///   - 2 beats before: BPM * 0.95 (-5%)
///   - 1 beat before:  BPM * 0.92 (-8%)
///   - On cadence:     BPM * 0.88 (-12%)
///   - 1 beat after:   BPM restored (a tempo)
///
/// Cadences where cadence_tick < 2 * kTicksPerBeat are skipped (too early).
///
/// @param base_bpm Base tempo in BPM.
/// @param cadence_ticks Vector of cadence tick positions.
/// @return Vector of TempoEvent pairs (deceleration + a tempo restoration), sorted by tick.
std::vector<TempoEvent> generateCadenceRitardando(uint16_t base_bpm,
                                                    const std::vector<Tick>& cadence_ticks);

}  // namespace bach

#endif  // BACH_HARMONY_TEMPO_MAP_H
