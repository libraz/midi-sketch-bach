#ifndef BACH_COMPOSER_EXPRESSION_EVENTS_H
#define BACH_COMPOSER_EXPRESSION_EVENTS_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach::composer {

/**
 * @brief Build an arc-driven organ registration plan as a stream of CC events.
 *
 * Produces a small, seed-independent sequence of MIDI Control-Change events
 * that trace the macro-form energy arc (Establish -> Develop -> Climax ->
 * Resolve). Each registration point emits both CC#7 (Main Volume) and CC#11
 * (Expression) at the same value, mirroring the legacy organ RegistrationPlan
 * convention (a single velocity hint drove both controllers per channel).
 *
 * Design values are ported from the legacy 3-point organ registration plan
 * (src/organ/registration.cpp), which is forbidden to include here per the
 * composer isolation contract, so the shape is re-implemented:
 *   - Opening (Establish): 75  -- legacy exposition velocity_hint.
 *   - Develop step-up:     85  -- legacy episode/middle-entry mid-range.
 *   - Climax peak:         95  -- legacy stretto velocity_hint.
 *   - Resolve / coda settle: 88 -- a relaxation below the stretto peak. The
 *     legacy coda value was 100 (full-organ tutti), but a ritardando-led close
 *     eases the dynamic rather than driving to maximum, so the settle is a
 *     designed softening that keeps the curve "rise to climax, then settle".
 *
 * The returned events are channel-agnostic: each CcEvent carries only the
 * controller and value. The caller clones the plan onto every voice channel by
 * appending the events to each Track's cc_events (the channel is taken from the
 * Track at write time by MidiWriter).
 *
 * The number of registration points adapts to the piece:
 *   - cycle_count <= 1: 2 points (opening, settle).
 *   - cycle_count == 2: 3 points (opening, climax, settle).
 *   - cycle_count >= 3: 4 points (opening, develop, climax, settle).
 *
 * @param bars Total bar count of the piece (informational; tick math uses the
 *        ticks_per_bar and total_ticks arguments).
 * @param cycle_count Number of arc cycles in the piece (>= 1). Drives the
 *        number of registration points.
 * @param ticks_per_bar Ticks per bar for the piece's meter.
 * @param total_ticks Total length of the piece in ticks; registration points
 *        are placed within (0, total_ticks).
 * @return CC events (CC#7 + CC#11 pairs) in non-decreasing tick order. Empty if
 *         total_ticks is 0.
 * @note Pure function of its arguments: no RNG, identical output every call.
 */
std::vector<CcEvent> buildRegistrationPlan(std::uint16_t bars, std::size_t cycle_count,
                                           Tick ticks_per_bar, std::uint32_t total_ticks);

/**
 * @brief Build a phrase-level expression arch as a stream of CC#11 events.
 *
 * Overlays a per-phrase "breath" on the macro energy arc: each phrase opens at
 * the macro-arc value for its position and swells a small designed step above
 * it at mid-phrase, so the dynamic line rises and falls with the phrasing
 * instead of holding flat between the (at most four) registration points. The
 * macro curve interpolates the same design values as buildRegistrationPlan
 * (opening 75 -> develop 85 -> climax 95 at ~75% -> settle 88), with the same
 * cycle_count tiers, so the two streams agree wherever they coincide.
 *
 * Only CC#11 (Expression) is emitted: the phrase breath is a performance
 * inflection, not a registration change, so CC#7 (the registration level)
 * stays with buildRegistrationPlan. Callers layer this onto every voice track
 * for instruments that shape dynamics continuously (organ swell pedal, bowed
 * strings, piano); a harpsichord has no dynamic control, so callers skip it.
 *
 * @param cycle_count Number of arc cycles in the piece (>= 1). Selects the
 *        macro-curve tier exactly as in buildRegistrationPlan.
 * @param phrase_bars Phrase length in bars (the form's natural period). Values
 *        outside [2, 8] are clamped into that range.
 * @param ticks_per_bar Ticks per bar for the piece's meter.
 * @param total_ticks Total length of the piece in ticks.
 * @return CC#11 events in non-decreasing tick order: one phrase-start event at
 *         the macro value and one mid-phrase swell per phrase. Empty if
 *         total_ticks or ticks_per_bar is 0.
 * @note Pure function of its arguments: no RNG, identical output every call.
 */
std::vector<CcEvent> buildPhraseDynamics(std::size_t cycle_count, std::uint16_t phrase_bars,
                                         Tick ticks_per_bar, std::uint32_t total_ticks);

/**
 * @brief Build a final ritardando as a short stream of tempo events.
 *
 * Steps the tempo down across the final ~2 bars of the piece to shape a
 * poco-a-poco closing ritardando. The starting tempo event (at tick 0) is the
 * caller's responsibility; this function returns only the deceleration steps
 * that follow it. Design values (seed-independent), on the half-bar grid:
 *   - 94% of base BPM, entering the penultimate bar.
 *   - 90% of base BPM, at the penultimate bar's mid-point.
 *   - 85% of base BPM, entering the final bar.
 *   - 78% of base BPM, at the final bar's mid-point (the allargando floor).
 * For very short pieces (< 2 bars) a single 85% step is emitted near the end.
 *
 * @param bpm Base tempo in BPM (the tempo in force before the ritardando).
 * @param total_ticks Total length of the piece in ticks.
 * @param ticks_per_bar Ticks per bar for the piece's meter.
 * @return Tempo events with strictly decreasing bpm, in increasing tick order,
 *         all placed before total_ticks. Empty if total_ticks is 0.
 * @note Pure function of its arguments: no RNG, identical output every call.
 */
std::vector<TempoEvent> buildFinalRitardando(std::uint16_t bpm, Tick total_ticks,
                                             Tick ticks_per_bar);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_EXPRESSION_EVENTS_H
