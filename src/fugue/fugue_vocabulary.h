// Fugue-specific pattern vocabulary: episode sequences, stretto patterns,
// and subject-related figures. Built on core bach_vocabulary types.

#ifndef BACH_FUGUE_FUGUE_VOCABULARY_H
#define BACH_FUGUE_FUGUE_VOCABULARY_H

#include "core/bach_vocabulary.h"

namespace bach {

// ---------------------------------------------------------------------------
// Fugue bass/pedal melodic figures
// Extracted from organ_fugue pedal track (1477 ngrams, 8 works).
// Bass voice has distinctively larger leaps vs upper voices.
// ---------------------------------------------------------------------------

// Bass leap-step: step up, leap 3rd down, step up (pedal: 21‰, 4/8 works)
inline constexpr DegreeInterval kBassLeapStep_dg[] = {{1, 0}, {-2, 0}, {1, 0}};
inline constexpr MelodicFigure kBassLeapStep = {
    "bass_leap_step", IntervalMode::Degree, true,
    nullptr, kBassLeapStep_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:pedal:b35, BWV576:pedal:b65"};

// Bass octave zig-zag: leap 4th up, leap 5th down, leap 4th up
// (pedal: 12‰, 3/8 works — distinctive bass harmonic motion)
inline constexpr DegreeInterval kBassOctaveZig_dg[] = {{3, 0}, {-4, 0}, {3, 0}};
inline constexpr MelodicFigure kBassOctaveZig = {
    "bass_octave_zig", IntervalMode::Degree, true,
    nullptr, kBassOctaveZig_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV576:pedal:b65, BWV575:pedal:b7"};

// Bass descend + leap: leap 5th down, leap 4th up, step down
// (pedal: 18‰, 2/8 works — cadential bass)
inline constexpr DegreeInterval kBassHarmonicDrop_dg[] = {{-4, 0}, {2, 0}, {-2, 0}};
inline constexpr MelodicFigure kBassHarmonicDrop = {
    "bass_harmonic_drop", IntervalMode::Degree, true,
    nullptr, kBassHarmonicDrop_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV575:pedal:b60, BWV577:pedal:b32"};

// Bass inverted zigzag: leap 5th down, leap 4th up, leap 5th down
// (pedal: 8‰, 3/8 works — pedal harmonic alternation)
inline constexpr DegreeInterval kBassInvZig_dg[] = {{-4, 0}, {3, 0}, {-4, 0}};
inline constexpr MelodicFigure kBassInvZig = {
    "bass_inv_zig", IntervalMode::Degree, true,
    nullptr, kBassInvZig_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV576:pedal:b64, BWV574:pedal:b54"};

// Bass resolve: step down, leap 4th up, step down
// (pedal: 6‰, 3/8 works)
inline constexpr DegreeInterval kBassResolve_dg[] = {{-2, 0}, {3, 0}, {-1, 0}};
inline constexpr MelodicFigure kBassResolve = {
    "bass_resolve", IntervalMode::Degree, true,
    nullptr, kBassResolve_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:pedal:b13"};

inline constexpr const MelodicFigure* kFugueBassPatterns[] = {
    &kBassLeapStep, &kBassOctaveZig, &kBassHarmonicDrop,
    &kBassInvZig, &kBassResolve,
};
inline constexpr int kFugueBassPatternCount = 5;

// ---------------------------------------------------------------------------
// Fugue episode patterns (organ_fugue combined figures)
// Used as sequential cells in modulatory episodes.
// ---------------------------------------------------------------------------

// Episode sequence: leap 3rd up, leap 3rd down, leap 4th up
// (organ: 3.4‰, 2/8 works — modulatory sequence cell)
inline constexpr DegreeInterval kEpisodeSeq_dg[] = {{2, 0}, {-2, 0}, {3, 0}};
inline constexpr MelodicFigure kEpisodeSeq = {
    "episode_seq", IntervalMode::Degree, true,
    nullptr, kEpisodeSeq_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV576:upper:b41"};

// Broken 3rd oscillation: leap 3rd up, leap 3rd down, leap 3rd up
// (organ: 2.0‰, 6/8 works — idiomatic broken chord pattern)
inline constexpr DegreeInterval kBroken3rd_dg[] = {{2, 0}, {-2, 0}, {2, 0}};
inline constexpr MelodicFigure kBroken3rd = {
    "broken_3rd", IntervalMode::Degree, true,
    nullptr, kBroken3rd_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV574:upper:b87"};

inline constexpr const MelodicFigure* kFugueEpisodePatterns[] = {
    &kEpisodeSeq, &kBroken3rd,
};
inline constexpr int kFugueEpisodePatternCount = 2;

}  // namespace bach

#endif  // BACH_FUGUE_FUGUE_VOCABULARY_H
