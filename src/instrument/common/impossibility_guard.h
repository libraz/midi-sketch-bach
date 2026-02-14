// Physical impossibility guard for instrument-aware note filtering.

#ifndef BACH_INSTRUMENT_COMMON_IMPOSSIBILITY_GUARD_H
#define BACH_INSTRUMENT_COMMON_IMPOSSIBILITY_GUARD_H

#include <cstdint>
#include <functional>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

namespace bach {

/// Physical impossibility type (fact only, no repair policy).
enum class Violation : uint8_t {
  None,
  PitchOutOfRange,           // Pitch outside instrument range.
  SimultaneousExceedsLimit,  // Bowed: 3+ sustained, Guitar: 2+ sustained.
  ImpossibleDoubleStop,      // Bowed: non-adjacent string double stop.
};

/// Repair action taken (policy, separate from violation).
enum class RepairAction : uint8_t {
  NoAction,
  OctaveShift,       // +/-12 with melodic contour preservation.
  ClampToRange,      // Last resort range clamping.
  TinyOffset,        // Bowed: 1-3 tick micro-offset for simultaneous notes.
  DropByPriority,    // Drop Flexible voice first.
  SuggestVoicing,    // Keyboard: suggestPlayableVoicing().
};

/// Set of notes sounding at a given tick (sustain-aware).
struct SoundingGroup {
  Tick tick;
  std::vector<NoteEvent*> notes;  // All notes sounding at this tick.
};

/// Instrument-specific impossibility detection and repair (type-erased).
struct ImpossibilityGuard {
  /// Single-pitch range check.
  std::function<bool(uint8_t pitch)> isPitchPlayable;

  /// Range repair: ProtectionLevel-aware staged repair.
  /// Immutable -> NoAction (return original pitch, log warning).
  /// Structural -> octave shift only (melodic contour condition).
  /// Flexible -> octave shift -> clamp.
  /// prev_pitch: previous note for contour check (0 = unknown).
  std::function<uint8_t(uint8_t pitch, ProtectionLevel level,
                         uint8_t prev_pitch)> fixPitchRange;

  /// Simultaneous sounding violation detection.
  std::function<Violation(const SoundingGroup& group)> checkSounding;

  /// Simultaneous sounding repair (role-aware, ProtectionLevel ordering).
  std::function<void(SoundingGroup& group)> repairSounding;
};

/// Create an ImpossibilityGuard for the given instrument type.
ImpossibilityGuard createGuard(InstrumentType instrument);

/// Enforce physical impossibility constraints on all tracks.
/// Returns the number of notes modified.
uint32_t enforceImpossibilityGuard(std::vector<Track>& tracks,
                                    const ImpossibilityGuard& guard);

}  // namespace bach

#endif  // BACH_INSTRUMENT_COMMON_IMPOSSIBILITY_GUARD_H
