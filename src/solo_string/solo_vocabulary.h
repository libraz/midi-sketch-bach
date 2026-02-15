// Solo string pattern vocabulary: figuration slot patterns, arpeggio idioms,
// and texture patterns for the Solo String generation system.

#ifndef BACH_SOLO_STRING_SOLO_VOCABULARY_H
#define BACH_SOLO_STRING_SOLO_VOCABULARY_H

#include "core/bach_vocabulary.h"

namespace bach {

// ---------------------------------------------------------------------------
// Figuration slot patterns
// ---------------------------------------------------------------------------

/// @brief A named figuration pattern for arpeggio and broken chord voicings.
///
/// Encodes the order of chord-tone slots (0=bass, N-1=soprano) within a
/// single beat or figure unit. Used by the Solo String Flow system to
/// generate idiomatic broken-chord patterns (e.g., Alberti bass, rolled
/// chord, bariolage).
///
/// @note bass_degree enables inversion detection: a pattern with bass_degree=0
///       is root position, bass_degree=2 is first inversion (3rd in bass), etc.
struct FigurationSlotPattern {
  const char* name;          ///< Human-readable identifier (e.g., "alberti_4").
  const uint8_t* slots;      ///< Chord tone indices. Length = slot_count.
  uint8_t slot_count;        ///< Number of slots in the pattern.
  uint8_t voice_count;       ///< Number of distinct chord tones available.
  uint8_t bass_degree;       ///< Scale degree of bass (0=tonic). For inversion detection.
  const char* provenance;    ///< Source references.
};

// ---------------------------------------------------------------------------
// 3-voice figuration slot patterns (BWV 1007-1012 cello suites)
// Total: 3222 patterns extracted from 36 works.
// ---------------------------------------------------------------------------

// 3v falling: soprano → alto → bass (291 occ, 34/36 works)
inline constexpr uint8_t kFig3vFalling_s[] = {2, 1, 0};
inline constexpr FigurationSlotPattern kFig3vFalling = {
    "3v_falling", kFig3vFalling_s, 3, 3, 4, "BWV1007_1:b8"};

// 3v rising: bass → alto → soprano (288 occ, 34/36 works)
inline constexpr uint8_t kFig3vRising_s[] = {0, 1, 2};
inline constexpr FigurationSlotPattern kFig3vRising = {
    "3v_rising", kFig3vRising_s, 3, 3, 4, "BWV1007_1:b9"};

// 3v arch: alto → soprano → bass (263 occ, 34/36 works)
inline constexpr uint8_t kFig3vArch_s[] = {1, 2, 0};
inline constexpr FigurationSlotPattern kFig3vArch = {
    "3v_arch", kFig3vArch_s, 3, 3, 0, "BWV1007_1:b33"};

// 3v dip: alto → bass → soprano (215 occ, 34/36 works)
inline constexpr uint8_t kFig3vDip_s[] = {1, 0, 2};
inline constexpr FigurationSlotPattern kFig3vDip = {
    "3v_dip", kFig3vDip_s, 3, 3, 0, "BWV1007_1:b24"};

// 3v mixed: soprano → bass → alto (154 occ, 32/36 works)
inline constexpr uint8_t kFig3vMixed_s[] = {2, 0, 1};
inline constexpr FigurationSlotPattern kFig3vMixed = {
    "3v_mixed", kFig3vMixed_s, 3, 3, 0, "BWV1007_1:b17"};

// ---------------------------------------------------------------------------
// 4-voice figuration slot patterns
// ---------------------------------------------------------------------------

// 4v rising: bass → tenor → alto → soprano (46 occ, 17/36 works)
inline constexpr uint8_t kFig4vRising_s[] = {0, 1, 2, 3};
inline constexpr FigurationSlotPattern kFig4vRising = {
    "4v_rising", kFig4vRising_s, 4, 4, 0, "BWV1007_1:b23"};

// 4v falling: soprano → alto → tenor → bass (36 occ, 16/36 works)
inline constexpr uint8_t kFig4vFalling_s[] = {3, 2, 1, 0};
inline constexpr FigurationSlotPattern kFig4vFalling = {
    "4v_falling", kFig4vFalling_s, 4, 4, 0, "BWV1007_1:b10"};

// 4v mixed: alto → bass → tenor → soprano (31 occ, 12/36 works)
inline constexpr uint8_t kFig4vMixed_s[] = {2, 0, 1, 3};
inline constexpr FigurationSlotPattern kFig4vMixed = {
    "4v_mixed", kFig4vMixed_s, 4, 4, 0, "BWV1007_1:b6"};

// ---------------------------------------------------------------------------
// 2-voice figuration slot patterns
// ---------------------------------------------------------------------------

// 2v oscillation up: soprano → bass → soprano (182 occ, 29/36 works)
inline constexpr uint8_t kFig2vOscUp_s[] = {1, 0, 1};
inline constexpr FigurationSlotPattern kFig2vOscUp = {
    "2v_osc_up", kFig2vOscUp_s, 3, 2, 0, "BWV1007_1:b4"};

// 2v oscillation down: bass → soprano → bass (148 occ, 25/36 works)
inline constexpr uint8_t kFig2vOscDown_s[] = {0, 1, 0};
inline constexpr FigurationSlotPattern kFig2vOscDown = {
    "2v_osc_down", kFig2vOscDown_s, 3, 2, 1, "BWV1007_1:b17"};

inline constexpr const FigurationSlotPattern* kSoloFigurations[] = {
    &kFig3vFalling, &kFig3vRising, &kFig3vArch, &kFig3vDip, &kFig3vMixed,
    &kFig4vRising, &kFig4vFalling, &kFig4vMixed,
    &kFig2vOscUp, &kFig2vOscDown,
};
inline constexpr int kSoloFigurationCount = 10;

// ---------------------------------------------------------------------------
// Solo string melodic figures (distinctive to cello suite)
// ---------------------------------------------------------------------------

// Bariolage: alternating chromatic neighbor (184 occ, 19/36 works)
inline constexpr DegreeInterval kBariolage_dg[] = {{1, -1}, {-1, 1}, {1, -1}};
inline constexpr MelodicFigure kBariolage = {
    "bariolage", IntervalMode::Degree, true,
    nullptr, kBariolage_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV1007_1:solo:b23"};

// Descending arpeggio step: 3rd down, step up, 3rd down (79 occ, 13/36 works)
inline constexpr DegreeInterval kArpDesc_dg[] = {{-2, 0}, {1, 0}, {-2, 0}};
inline constexpr MelodicFigure kArpDesc = {
    "arp_desc", IntervalMode::Degree, true,
    nullptr, kArpDesc_dg, detail::kEqR4, detail::kEqO4,
    4, "BWV1007_1:solo:b21"};

inline constexpr const MelodicFigure* kSoloMelodicPatterns[] = {
    &kBariolage, &kArpDesc,
};
inline constexpr int kSoloMelodicPatternCount = 2;

}  // namespace bach

#endif  // BACH_SOLO_STRING_SOLO_VOCABULARY_H
