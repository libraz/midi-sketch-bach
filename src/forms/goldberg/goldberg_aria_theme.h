// AriaTheme: generative Sarabande melody for Goldberg Variations.
// Two-layer generation: Kern (beat 1 chord tones) + Surface (beat 2/3 functions).

#ifndef BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_THEME_H
#define BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_THEME_H

#include <array>
#include <cstdint>

#include "forms/goldberg/goldberg_structural_grid.h"
#include "harmony/key.h"

namespace bach {

/// Beat-level stylistic function classification.
enum class BeatFunction : uint8_t {
  Stable,        ///< Chord tone / harmonic anchor.
  Suspension43,  ///< 4-3 suspension (preparation -> dissonance -> resolution).
  Appoggiatura,  ///< Appoggiatura (non-chord tone resolving stepwise down).
  Passing,       ///< Passing tone (stepwise motion between structural pitches).
  Hold           ///< Tied from previous beat (no new onset).
};

/// One beat of the Aria skeletal melody.
struct AriaThemeBeat {
  uint8_t pitch;      ///< MIDI pitch. 0 = hold (tie from previous beat).
  BeatFunction func;  ///< Stylistic function of this beat.
};

/// The 32-bar Aria theme skeleton (96 beats in 3/4 time).
///
/// Generated per seed from the structural grid's harmonic constraints.
/// Kern layer (Beat 1): chord tones selected by scoring.
/// Surface layer (Beat 2/3): BeatFunction-driven non-chord tones.
/// pitch=0 with func=Hold means the previous pitch is sustained.
struct AriaTheme {
  static constexpr int kBars = 32;
  static constexpr int kBeatsPerBar = 3;
  static constexpr int kTotalBeats = kBars * kBeatsPerBar;  // 96

  std::array<AriaThemeBeat, 96> beats;

  /// Get the structural pitch for a given bar and beat.
  /// If pitch==0 (Hold), returns the most recent non-zero pitch.
  /// @param bar 0-indexed bar (0-31).
  /// @param beat 0-indexed beat within bar (0-2).
  /// @return MIDI pitch (never 0 for valid input).
  uint8_t getPitch(int bar, int beat) const;

  /// Get the BeatFunction for a given bar and beat.
  /// @param bar 0-indexed bar (0-31).
  /// @param beat 0-indexed beat within bar (0-2).
  BeatFunction getFunction(int bar, int beat) const;

  /// Extract a downbeat fragment (beat 1 pitches) for a range of bars.
  /// @param start_bar 0-indexed start bar.
  /// @param length_bars Number of bars to extract.
  /// @return Array of up to 4 downbeat pitches (0-filled if out of range).
  std::array<uint8_t, 4> getDownbeatFragment(int start_bar, int length_bars) const;
};

/// Generate an Aria melody from the structural grid's harmonic constraints.
/// Kern: Beat 1 pitches are chord tones selected by scoring.
/// Surface: Beat 2/3 are BeatFunction-driven (suspension, appoggiatura, passing).
/// Same seed + same grid = same melody. Different seed = different melody.
/// @param grid The 32-bar structural grid providing harmonic foundation.
/// @param key Key signature for scale-aware generation.
/// @param seed Random seed for deterministic generation.
/// @return AriaTheme with 96 beats of generated melody.
AriaTheme generateAriaMelody(
    const GoldbergStructuralGrid& grid,
    const KeySignature& key,
    uint32_t seed);

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_GOLDBERG_ARIA_THEME_H
