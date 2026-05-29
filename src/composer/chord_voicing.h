#ifndef BACH_COMPOSER_CHORD_VOICING_H
#define BACH_COMPOSER_CHORD_VOICING_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "composer/harmonic_plan.h"

namespace bach::composer {

// Composer-local chord-tone arithmetic. Mirrors the contract previously
// owned by `harmony::ChordVoicer` (see backup/reuse_contract.md) but
// stays inside the composer namespace so the engine has no link-time
// dependency on the legacy harmony tree.
//
// P7 needs three primitives that Phase 2-6 didn't surface:
//   * chord tone pitch classes for both triads and 7th chords,
//   * the bass pitch class implied by a ChordEvent's inversion,
//   * the diatonic leading-tone pitch class for a given key.
//
// Doubling, spacing, and voicing tests share these helpers; the
// Validator already imports them via translation-unit-local copies.

// Returns the chord tones (root, third, fifth, optional seventh) as
// pitch classes in [0, 11]. `out_count` is set to 3 for triads and 4
// for 7th-quality chords. Augmented sixth chords are not handled here
// (P8 territory).
std::array<std::uint8_t, 4> chordPitchClasses(const ChordEvent& chord, std::size_t* out_count);

// True if `chord.quality` carries a seventh chord factor.
bool hasSeventh(ChordQuality quality);

// Seventh-degree offset in semitones from the chord root. Returns 0
// when the quality has no seventh — callers should guard via
// hasSeventh().
std::uint8_t seventhOffset(ChordQuality quality);

// Pitch class that the bass would carry given the chord's inversion.
//   Root      → root_pc
//   First     → 3rd above root
//   Second    → 5th above root
//   Third     → 7th above root (only meaningful for 7th chords)
std::uint8_t bassPitchClassFor(const ChordEvent& chord);

// Leading tone of the diatonic mode rooted at `tonic_pc`. Always the
// major-7th degree (tonic + 11) regardless of mode — minor-mode
// pieces raise the seventh at the dominant and v7 chord, matching
// Bach practice.
std::uint8_t leadingTonePitchClass(std::uint8_t tonic_pc);

// True if pc is the leading tone for the key rooted at tonic_pc.
bool isLeadingTonePc(std::uint8_t pc, std::uint8_t tonic_pc);

// Classify a 6/4 chord (Second-inversion) into the three Bach idioms:
//
//   Cadential   — the 6/4 sits on a strong beat and the next chord is
//                 the dominant (V) of the same key (e.g. I 6/4 → V).
//                 Bass is held; upper voices resolve 6→5 and 4→3.
//   Passing     — the bass moves by step into the 6/4 and continues
//                 by step out of it. Classic figured-bass passing 6/4
//                 (e.g. I → V6/4 → I6).
//   Neighboring — the bass stays the same across the 6/4 (pedal
//                 effect); upper voices neighbor in and back out.
//
// `prev` and `next` may be nullptr at the edges of the plan; the
// classifier degrades gracefully (defaults to Cadential when it
// cannot decide, matching the most common Bach use).
SixFourType classifySixFour(const ChordEvent* prev, const ChordEvent& six_four,
                            const ChordEvent* next);

}  // namespace bach::composer

#endif  // BACH_COMPOSER_CHORD_VOICING_H
