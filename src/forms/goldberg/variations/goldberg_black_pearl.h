// BlackPearl (Var 25) generator: G minor Adagio with suspension chains.

#ifndef BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_BLACK_PEARL_H
#define BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_BLACK_PEARL_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "forms/goldberg/goldberg_structural_grid.h"
#include "forms/goldberg/goldberg_types.h"
#include "harmony/key.h"

namespace bach {

/// Suspension phase tracking for the Prepare-Suspend-Resolve cycle.
enum class SuspensionPhase : uint8_t {
  Prepared,   ///< Consonant preparation (anticipation on weak beat).
  Suspended,  ///< Held over into dissonance (strong beat).
  Resolved    ///< Stepwise descent to resolution.
};

/// A single suspension event describing one full Prepare-Suspend-Resolve cycle.
struct SuspensionEvent {
  SuspensionPhase phase;
  uint8_t suspended_pitch;    ///< Pitch held through suspension.
  uint8_t resolution_pitch;   ///< Descending 2nd resolution target.
  Tick duration;              ///< Duration of this phase in ticks.
};

/// Dissonance control profile for Var 25 generation.
struct DissonanceProfile {
  int max_chain_length = 4;                 ///< Maximum suspension chain length.
  float chain_probability = 0.4f;           ///< Probability of extending a chain.
  bool allow_chromatic_neighbor = true;     ///< Allow chromatic neighbor tones.
  bool allow_appoggiatura_on_strong = true; ///< Allow appoggiaturas on strong beats.
  float max_dissonance_duration = 2.0f;     ///< Maximum dissonance in beats.
  bool enable_chromatic_tetrachord = true;  ///< Enable lamento bass chromatic descent.
};

/// @brief Result of BlackPearl variation generation.
struct BlackPearlResult {
  std::vector<NoteEvent> notes;  ///< All notes (melody + bass + suspensions).
  int suspension_count = 0;      ///< Diagnostic: total suspension events generated.
  bool success = false;
};

/// @brief Generates the BlackPearl variation (Var 25).
///
/// G minor Adagio with suspension chains as the primary expressive device.
/// Uses Suspirans (sigh motif) figures for melody, chromatic descending
/// tetrachord (lamento bass) for bass, and inserts suspension chains at
/// phrase boundaries for expressive dissonance.
class BlackPearlGenerator {
 public:
  /// @brief Generate the complete BlackPearl variation.
  /// @param grid The 32-bar structural grid providing harmonic foundation.
  /// @param key Key signature (expected G minor).
  /// @param time_sig Time signature for tick calculation.
  /// @param seed Random seed for deterministic generation.
  /// @return BlackPearlResult with generated notes, suspension count, and success status.
  BlackPearlResult generate(const GoldbergStructuralGrid& grid,
                            const KeySignature& key,
                            const TimeSignature& time_sig,
                            uint32_t seed) const;

 private:
  /// @brief Generate a suspension chain (Prepared -> Suspended -> Resolved).
  ///
  /// Each suspension in the chain follows the pattern:
  /// 1. Consonant preparation on weak beat
  /// 2. Hold over into dissonance on the next strong beat
  /// 3. Resolve by descending 2nd (scale step down)
  /// For chains, the resolution becomes the preparation for the next suspension.
  ///
  /// @param start_pitch Starting consonant pitch for preparation.
  /// @param chain_length Number of suspensions in the chain.
  /// @param start_tick Tick position to begin the chain.
  /// @param beat_duration Duration of one beat in ticks.
  /// @param key Key signature for scale-step resolution.
  /// @param rng Random number generator for chain type selection.
  /// @return Vector of NoteEvents forming the suspension chain.
  std::vector<NoteEvent> generateSuspensionChain(uint8_t start_pitch,
                                                  int chain_length,
                                                  Tick start_tick,
                                                  Tick beat_duration,
                                                  const KeySignature& key,
                                                  std::mt19937& rng) const;

  /// @brief Generate chromatic descending tetrachord (lamento bass).
  ///
  /// The lamento bass is a Baroque expressive device: a chromatic descent
  /// spanning a perfect fourth from the tonic. For G minor: G-F#-F-E-Eb-D.
  /// This is a design value (constexpr pitch array).
  ///
  /// @param start_tick Tick position to begin the bass line.
  /// @param span_bars Number of bars to spread the tetrachord across.
  /// @param bar_duration Duration of one bar in ticks.
  /// @param key Key signature for tonic determination.
  /// @return Vector of NoteEvents forming the descending tetrachord bass.
  std::vector<NoteEvent> generateLamentoBass(Tick start_tick,
                                              int span_bars,
                                              Tick bar_duration,
                                              const KeySignature& key) const;

  /// @brief Generate structural bass line from the grid for non-lamento bars.
  /// @param grid The 32-bar structural grid.
  /// @param time_sig Time signature for bar duration.
  /// @param lamento_bars Set of bar indices covered by lamento bass.
  /// @return Vector of bass NoteEvents for non-lamento bars.
  std::vector<NoteEvent> generateStructuralBass(
      const GoldbergStructuralGrid& grid,
      const TimeSignature& time_sig,
      const std::vector<bool>& lamento_bars) const;

  /// Dissonance control profile (design value).
  DissonanceProfile profile_;
};

}  // namespace bach

#endif  // BACH_FORMS_GOLDBERG_VARIATIONS_GOLDBERG_BLACK_PEARL_H
