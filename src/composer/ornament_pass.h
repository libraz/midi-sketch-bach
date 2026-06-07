#ifndef BACH_COMPOSER_ORNAMENT_PASS_H
#define BACH_COMPOSER_ORNAMENT_PASS_H

#include <cstdint>
#include <vector>

#include "composer/composer.h"
#include "composer/figuration.h"
#include "core/basic_types.h"

namespace bach::composer {

// Deterministic ornament post-pass for the composer subsystem.
//
// OPT-IN ONLY. This pass is NOT part of Composer::run and is NOT wired into
// any builder. The closure harness pins byte-identical Composer::run output,
// so ornamentation must be a separate step applied by callers AFTER run().
// Calling Composer::run alone reproduces the pinned outputs exactly.
//
// The pass is a pure function of its inputs (ComposeResult + OrnamentParams):
// every placement choice is derived from (seed, bar, voice) hashes, never from
// an RNG, so applyOrnamentPass(result, params) is reproducible.
//
// Musical contract (port of the legacy ornament rules, composer-adapted):
//   * Candidate notes: duration >= a quarter note, not in `exempt_voices`,
//     not the lowest-sounding voice at their onset (the bass stays clean;
//     a single-voice piece has no bass, so the guard is multi-voice only),
//     not the voice's final attack (the resolution tone always sounds plain),
//     and the pitch must have a diatonic upper neighbour in `mode`.
//   * Inside the cadence window (the last two bars) only the TOP sounding
//     line is ornamented: simultaneous trills in two voices clash at the
//     alternation level.
//   * A trill starts on the upper auxiliary (Baroque standard; at a cadence
//     the upper tone is the 4-3 suspension over the dominant), alternates
//     upper/main, and ends in a lower-neighbour Nachschlag pair before the
//     final main tone.
//   * Long trills (>= half note) open with a held upper-neighbour
//     appoggiatura (appuy, max(span/4, eighth) capped at a half note) before
//     the alternation; long CADENCE trills deterministically alternate
//     between that appuy opening and the von-unten doppelt-cadence opening
//     (a lower -> main two-note prefix), keyed by the placement hash.
//   * Trill pacing follows tempo: 32nd-note alternation at bpm <= 100,
//     16th-note alternation above. Long trills (longer than a quarter) always
//     pace at sixteenths -- a stately alternation, not a buzz.
//   * Beyond the trill the pass carries the Explication vocabulary --
//     mordent (main-lower-main), appoggiatura (the upper neighbour leans for
//     half the note's value, two thirds when dotted, then resolves), turn
//     (upper-main-lower in 32nds, then the held main tone), and slide (two
//     rising 32nds from the diatonic third below). Which figure decorates
//     which site class is the character's design table:
//       Severe   : boundary -> appoggiatura; interior -> nothing.
//       Noble    : boundary -> appoggiatura on a matched approach (falling-
//                  third gap / stepwise descent), turn otherwise; interior ->
//                  turn on half-or-longer tones.
//       Restless : boundary -> short trill (long tones) / mordent; interior
//                  -> downbeat mordent.
//       Playful  : boundary -> slide under a rising-leap approach / turn
//                  (long tones) / mordent (quarters); interior -> inner trill
//                  on long tones, slide under leap arrivals, downbeat quarter
//                  mordent.
//     Approach-matched figures carry their own placement-hash quantile gate;
//     design-value figures run through the generic deterministic gate.
//     Placement priority on a contested note: cadential trill > appoggiatura
//     > turn > slide > mordent.
//   * The ornament subdivides ONLY the original note's [start, start+dur)
//     span: total piece duration never changes, and no ornament tone leaves
//     the original note's diatonic neighbourhood or crosses above the
//     next-higher voice's concurrent pitch.
//
// Density (number of ornaments placed) is driven by the character's
// ornament_density (CharacterProfile) scaled by instrument:
//   density 0 -> the mandatory cadence trill plus one boundary figure per
//                8 bars (appoggiatura; mordent for Restless).
//   density 1+ -> + the character's boundary and interior vocabulary above.
// Instrument scaling: Harpsichord +1 (cap 2); Violin/Cello/Guitar -1 (min 0);
// Organ/Piano unchanged.
struct OrnamentParams {
  SubjectCharacter character = SubjectCharacter::Severe;  // density via CharacterProfile.
  InstrumentType instrument = InstrumentType::Organ;      // density scaling.
  detail::Mode mode = detail::Mode::Major;                // scale for neighbour tones.
  std::uint32_t seed = 0;                                 // deterministic placement.
  Tick ticks_per_bar = kTicksPerBar;                      // meter (3/4 forms pass 1440).
  std::uint16_t bpm = 72;                                 // trill pacing (32nds <= 100 < 16ths).
  std::vector<VoiceId> exempt_voices;                     // ground / CF carriers: never ornamented.
};

/**
 * @brief Resolve the effective ornament-density tier for a request.
 *
 * Combines the character's base ornament_density (0..2) with the instrument
 * scaling (Harpsichord +1, plucked/bowed strings -1, others 0) and clamps the
 * result to [0, 2]. Exposed so callers and tests can reason about the density
 * matrix without re-deriving it.
 *
 * @param character Subject character (selects the base density).
 * @param instrument Instrument (selects the density delta).
 * @return Effective density tier in [0, 2].
 */
std::uint8_t effectiveOrnamentDensity(SubjectCharacter character, InstrumentType instrument);

/**
 * @brief Apply the deterministic ornament pass to a composed result in place.
 *
 * Decorates eligible notes of `result` with trills / mordents per the musical
 * contract documented on OrnamentParams. The pass keeps `result.notes`,
 * `result.provenance`, and `result.tracks` mutually consistent: every replaced
 * candidate expands into its ornament sub-notes in all three views, provenance
 * stays index-parallel with notes, and ornament sub-notes carry
 * NoteSource::Ornament. `result.validation` is left untouched (re-run the
 * Validator separately to confirm the ornamented line still passes).
 *
 * Idempotent guards: notes already carrying NoteSource::Ornament are never
 * re-ornamented, so applying the pass twice is a no-op on the second call.
 *
 * @param result The composed result to ornament in place.
 * @param params Ornament configuration (character, instrument, mode, seed,
 *               meter, exempt voices).
 */
void applyOrnamentPass(ComposeResult& result, const OrnamentParams& params);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_ORNAMENT_PASS_H
