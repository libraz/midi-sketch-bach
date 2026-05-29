#ifndef BACH_COMPOSER_HARMONIC_PLAN_H
#define BACH_COMPOSER_HARMONIC_PLAN_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach::composer {

// Chord quality. Minimal set for Phase 2-3. Inversions and 7th chords
// land in Phase 4+ when the diatonic vocabulary widens.
enum class ChordQuality : std::uint8_t {
  Major = 0,
  Minor = 1,
  Diminished = 2,
};

// Chord with a start tick. The chord runs until the next ChordEvent or
// the piece end. Root is expressed as a pitch class (0=C, 1=C#, ..., 11=B).
struct ChordEvent {
  Tick start_tick = 0;
  std::uint8_t root_pc = 0;
  ChordQuality quality = ChordQuality::Major;
};

// Harmonic plan: piece-wide chord progression and tonic anchor.
//
// HarmonicPlan is produced before CandidateSearch and is immutable for the
// rest of the pipeline. Cadence locations and modulation joins both surface
// as ChordEvent boundaries.
struct HarmonicPlan {
  // Tonic pitch class of the piece. The composer engine runs C-internal
  // per CLAUDE.md "C major internally"; transposition (if any) happens at
  // MIDI output. tonic_pc therefore is usually 0 in Phase 2-3.
  std::uint8_t tonic_pc = 0;
  bool is_minor = false;
  std::vector<ChordEvent> chords;
};

}  // namespace bach::composer

#endif  // BACH_COMPOSER_HARMONIC_PLAN_H
