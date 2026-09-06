#ifndef BACH_COMPOSER_ARRIVAL_RELIEF_H
#define BACH_COMPOSER_ARRIVAL_RELIEF_H

#include <vector>

#include "composer/figuration.h"
#include "composer/material.h"
#include "composer/texture_helpers.h"
#include "core/basic_types.h"

/// @file
/// @brief Repair of the perfect intervals a form can only see once every one of
///        its voices has been placed.
///
/// A form whose lines are built in a fixed order cannot judge its own arrivals
/// while it writes them: the voice that will sound under a figuration is chosen
/// after it, and a line derived from an immutable skeleton or an immutable
/// ground was never free to answer for what it meets. So the fault is not
/// discovered by the builder that caused it -- it becomes visible only when the
/// texture is complete, which is a moment no single builder owns.
///
/// This is that moment, stated once. Every form that reaches it wants the same
/// judgement (how bad is this perfect approach) and the same remedy (displace
/// the one free tone around the arrival, without making either neighbouring
/// interval worse), and a form that answered the question for itself would be
/// free to rank the faults differently -- which is precisely how a tree ends up
/// trading the cardinal prohibition for a lesser blemish in one form and not in
/// another.

namespace bach::composer {

/// @brief Severity of the worst perfect-interval fault a (prev -> curr) motion
///        forms against any concurrently sounding line.
///
/// 0 clean, 1 battuta, 2 anti-parallel, 3 parallel or hidden -- the ranking
/// every displacement in this tree obeys.
int perfectFaultRank(int prev, int curr, const std::vector<ConcurrentMotion>& motions);

/// @brief A line as the relief pass wants it: every note of one voice in tick
///        order, by address, so a voice whose material is stored per block is
///        still one line.
std::vector<MaterialNote*> lineInTickOrder(const std::vector<std::vector<MaterialNote>*>& parts);

/// @brief Relieve one line's arrivals over the voices already placed.
///
/// Every beat is an arrival, not only the bar head. The voice under this one
/// moves within the bar as well as at its head -- the bass states the bar chord
/// and then walks through it -- so the two lines trace the same harmony and
/// reach a perfect interval together off the downbeat as readily as on it.
///
/// `arrival_grain` is how often the voice below actually moves. A bass that
/// walks in eighths reaches a perfect interval on its own off-beats too, and a
/// beat-grain reading cannot see it: the pair it forms there is never sampled.
/// Reading finer than the bass moves only costs time, so the grain is the
/// caller's to state rather than a constant here.
///
/// What moves is the onset immediately before the arrival, never the arrival
/// itself. At a bar head both ends are fixed: the bass band spans a single
/// octave so the root's register is determined, the cantus firmus lands on its
/// immutable skeleton tone, and the figuration's own head must be a chord tone.
/// Inside the bar the arrival is instead a running tone whose neighbours spell
/// the figure, and moving it there breaks the shape the block exists to state.
/// Either way the answer is on the way in. It is applied here rather than inside
/// the line's own loop because the lower voices are built last: this is the
/// earliest point at which the fault is visible at all.
///
/// The re-aim is non-regressive at both ends. The replacement must lower the
/// fault it forms arriving at the beat, and may not raise the one it forms
/// arriving at its own onset; it keeps the register order, and may be dissonant
/// only where the tone it displaces already was (the eighth-note fills are
/// passing tones and dissonant by design).
///
/// @param line The voice under repair, in tick order, rewritten in place.
/// @param registry The voices already placed, this one excluded.
/// @param voice The physical voice index of `line`.
/// @param num_voices Voice count of the texture.
/// @param bars Length of the piece in bars.
/// @param mode The active diatonic scale.
/// @param arrival_grain How often the voice below moves.
void relieveArrivals(const std::vector<MaterialNote*>& line, const ThemeToneRegistry& registry,
                     VoiceId voice, VoiceId num_voices, int bars, detail::Mode mode,
                     Tick arrival_grain = kTicksPerBeat);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_ARRIVAL_RELIEF_H
