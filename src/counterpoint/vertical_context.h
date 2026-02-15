// Vertical context for generation-time vertical safety checks.
// Delegates to checkVerticalConsonance() (Single Source of Truth).

#ifndef BACH_COUNTERPOINT_VERTICAL_CONTEXT_H
#define BACH_COUNTERPOINT_VERTICAL_CONTEXT_H

#include <cstdint>
#include <functional>
#include <vector>

#include "core/basic_types.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// Predicate to allow specific weak-beat dissonances (e.g., NHT exceptions).
/// @param tick Current tick.
/// @param voice Target voice index.
/// @param candidate_pitch Candidate MIDI pitch.
/// @param other_pitch Other sounding MIDI pitch.
/// @param simple_interval Simple interval [0-11] between candidate and other.
/// @param melodic_prev_pitch Previous pitch in same voice (0 = unknown).
/// @return true to allow this dissonance.
using WeakBeatDissonancePredicate =
    std::function<bool(Tick, uint8_t, uint8_t, uint8_t, int, uint8_t)>;

/// Generation-time vertical reference for candidate pitch evaluation.
///
/// Provides a unified interface for checking vertical safety during voice
/// generation. Internally delegates to checkVerticalConsonance() for strong
/// beats and adds m2/M7/TT rejection on weak beats.
///
/// Usage: construct once per generation pass with placed_notes growing as
/// voices are generated. Each new voice queries isSafe()/score() against
/// already-placed notes.
struct VerticalContext {
  const std::vector<NoteEvent>* placed_notes = nullptr;
  const HarmonicTimeline* timeline = nullptr;
  uint8_t num_voices = 0;

  /// Optional weak-beat NHT exception predicate.
  /// nullptr = no exceptions (strict m2/M7/TT rejection on weak beats).
  /// Set this to enable future passing/neighbor/suspension exemptions.
  WeakBeatDissonancePredicate weak_beat_allow = nullptr;

  /// Check if a candidate pitch is vertically safe at the given tick/voice.
  ///
  /// Strong beats: delegates to checkVerticalConsonance().
  /// Weak beats: rejects m2(1), TT(6), M7(11) unless weak_beat_allow permits.
  /// @return true if the candidate pitch is safe.
  bool isSafe(Tick tick, uint8_t voice, uint8_t pitch) const;

  /// Score a candidate pitch for vertical quality (for candidate ranking).
  ///
  /// Returns 0.0 if isSafe() is false.
  /// Returns graduated values [0.0, 1.0] based on consonance quality:
  ///   1.0 = perfect consonance (P1, P5, P8)
  ///   0.8 = imperfect consonance (m3, M3, m6, M6)
  ///   0.5 = P4 between upper voices
  ///   0.3 = weak-beat non-harsh dissonance (m2/M7/TT excluded)
  float score(Tick tick, uint8_t voice, uint8_t pitch) const;

  /// Find the previous pitch in the given voice from placed_notes.
  /// @return 0 if no previous pitch found.
  uint8_t findPrevPitch(uint8_t voice, Tick before_tick) const;
};

}  // namespace bach

#endif  // BACH_COUNTERPOINT_VERTICAL_CONTEXT_H
