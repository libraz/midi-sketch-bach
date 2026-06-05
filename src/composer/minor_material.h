#ifndef BACH_COMPOSER_MINOR_MATERIAL_H
#define BACH_COMPOSER_MINOR_MATERIAL_H

#include <array>
#include <cstdint>

#include "composer/figuration.h"

namespace bach::composer::detail {

// First-class C-minor subject catalog: 5 slots x 16 quarter-note pitches,
// modeled on Bach minor-subject archetypes (BWV578 g-minor descent shape,
// BWV543 a-minor figuration, BWV565-style dramatic drop) transposed to C
// minor. Mirrors kSubjectPatterns (figuration.h) in shape and contract:
//   - register envelope ~70-84 (using Eb=75, Ab=80, Bb=70/82 as the minor
//     scale degrees) comparable to the major catalog,
//   - every melody ends with the mandatory leading-tone tail B natural -> C
//     (71, 72) so downstream cadence / leading-tone handling stays uniform
//     with the major catalog,
//   - stepwise-dominant, conjunct contours (the external scorer is dominated
//     by melodic-interval statistics, so wide leaps are minimized),
//   - NO augmented 2nd anywhere: the pitch-class pair {Ab=8, B=11} never
//     appears as an adjacent step (that interval of 3 semitones between pc 8
//     and pc 11 is the C-harmonic-minor aug-2nd to avoid). Ascending lines
//     reaching B come from G (pc 7) by leap, or from a non-Ab neighbour.
// Every adjacent interval lies in {+/-1..+/-5, +/-7, +/-8, +/-12} semitones.
// Verified note-by-note in tests/composer/minor_material_test.cpp.
inline constexpr std::array<std::array<std::uint8_t, 16>, 5> kSubjectsMinor = {{
    // 0: severe arch (BWV578-style). Mid-register arch up to Ab, conjunct
    //    descent, then a G->B cadential leap into the leading-tone tail.
    {72, 74, 75, 77, 79, 80, 79, 77, 75, 74, 72, 74, 75, 79, 71, 72},
    // 1: a-minor figuration (BWV543-style). Triad-outlined opening folded
    //    into a stepwise descent; the B is approached from G (leap of a 3rd),
    //    never from Ab.
    {72, 75, 79, 77, 75, 74, 75, 77, 79, 80, 79, 77, 75, 74, 71, 72},
    // 2: dramatic drop (BWV565-style). High Bb head, an octave-ish dramatic
    //    descent, recovery, and a scalar climb to the leading-tone tail.
    {82, 80, 79, 77, 75, 74, 72, 74, 75, 77, 79, 80, 77, 79, 71, 72},
    // 3: noble descent. A long, dignified conjunct descent from Ab through the
    //    natural-minor tetrachord, re-ascending only for the cadential close.
    {80, 79, 77, 75, 74, 72, 70, 72, 74, 75, 77, 79, 75, 79, 71, 72},
    // 4: restless upper-arch. Restless oscillating climb to Ab and back, the
    //    leading tone again reached from G so no Ab->B aug-2nd occurs.
    {75, 77, 79, 80, 79, 77, 75, 77, 79, 80, 79, 77, 75, 79, 71, 72},
}};

// 4 harmony patterns x 4 ChordSpec for C minor. V is MAJOR (harmonic-minor
// dominant). Roman numerals:
//   0 = i-iv-V-i        {0,m}{5,m}{7,M}{0,m}
//   1 = i-VI-iv-V       {0,m}{8,M}{5,m}{7,M}
//   2 = i-VII-VI-V      {0,m}{10,M}{8,M}{7,M}  (lament / descending bass)
//   3 = i-V-VI-i        {0,m}{7,M}{8,M}{0,m}   (deceptive then resolved)
inline constexpr std::array<std::array<ChordSpec, 4>, 4> kHarmonyPatternsMinor = {{
    {{{0, true}, {5, true}, {7, false}, {0, true}}},
    {{{0, true}, {8, false}, {5, true}, {7, false}}},
    {{{0, true}, {10, false}, {8, false}, {7, false}}},
    {{{0, true}, {7, false}, {8, false}, {0, true}}},
}};

// BWV582-style descending ground line for minor passacaglia / chaconne grounds.
// A C-minor descending tetrachord (C-Bb-Ab-G) expanded to 8 bass-register
// pitches: the lament tetrachord followed by a cadential return (F-Eb-D-C... ),
// here realized as C-Bb-Ab-G-F-Eb-D-C so the contour descends overall and the
// pitches stay in the bass register (36-48). All adjacent steps are <= a whole
// tone, so no aug-2nd appears (G=43 -> F=41, no Ab->B adjacency).
inline constexpr std::array<std::uint8_t, 8> kGroundMinorDescent = {48, 46, 44, 43, 41, 39, 38, 36};

/**
 * @brief Walk `steps` scale degrees upward from `midi` in a C-minor context.
 *
 * Harmonic-context-aware minor scale walker. When `harmonic_context` is false
 * the scale is C natural minor (degrees C D Eb F G Ab Bb), identical in spirit
 * to phase16ScaleUp (but a NEW function -- phase16ScaleUp stays byte-pinned).
 *
 * When `harmonic_context` is true (an ascending line over a V chord that wants
 * the leading tone B natural) the scale switches to the MELODIC minor ascending
 * upper tetrachord: degree 6 is raised to A natural (pc 9) and degree 7 to B
 * natural (pc 11). This is the deliberate substitution that AVOIDS the Ab->B
 * augmented 2nd: an ascending run through the 6-7 region produces
 * ...G(7) -> A natural(9) -> B natural(11) -> C(0) rather than
 * ...G(7) -> Ab(8) -> B natural(11) (the forbidden aug-2nd). The lower
 * tetrachord (C D Eb F) is unchanged. When `harmonic_context` is false the
 * descending/plain natural-minor degrees Ab(8) and Bb(10) are used.
 *
 * @param midi Starting MIDI pitch.
 * @param steps Number of scale degrees to ascend (>= 0).
 * @param harmonic_context When true, use the melodic-minor ascending upper
 *        tetrachord (A natural / B natural) to reach the leading tone without
 *        an augmented 2nd; when false, use plain C natural minor.
 * @return The MIDI pitch `steps` scale degrees above `midi`.
 */
int minorScaleUp(int midi, int steps, bool harmonic_context);

/**
 * @brief Deterministic Picardy-third decision for a final cadence.
 *
 * A Picardy third raises the final tonic chord's third to major. The choice is
 * seed-deterministic (even seed => Picardy) so a given seed always yields the
 * same final-cadence colour. Final-cadence material consumers call this to
 * decide whether to raise the closing third; it has no other side effects.
 *
 * @param seed The piece seed.
 * @return True when the final tonic chord should use a Picardy (major) third.
 */
bool usePicardy(std::uint32_t seed);

}  // namespace bach::composer::detail

#endif  // BACH_COMPOSER_MINOR_MATERIAL_H
