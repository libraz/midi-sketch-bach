// Shared utility for repairing parallel perfect consonances.
// Used by both postValidateNotes() and FugueGenerator to eliminate
// duplicate parallel repair logic (~300 lines each).

#ifndef BACH_COUNTERPOINT_PARALLEL_REPAIR_H
#define BACH_COUNTERPOINT_PARALLEL_REPAIR_H

#include <functional>
#include <utility>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// Parameters for parallel perfect consonance repair.
struct ParallelRepairParams {
  uint8_t num_voices = 0;
  ScaleType scale = ScaleType::Major;

  /// Key at a given tick (for diatonic validation of step shifts).
  std::function<Key(Tick)> key_at_tick;

  /// Voice range (low, high) for a given voice.
  std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range;

  /// Maximum repair iterations (default 3).
  int max_iterations = 3;
};

/// @brief Repair parallel perfect consonances in a note list.
///
/// Scans at beat grid positions and common onset positions (dual-scan).
/// For each detected parallel, tries step shifts (diatonic, ±1..±4) on
/// Flexible notes, or octave shifts (±12) on Structural notes. Immutable
/// notes are never modified.
///
/// @param notes Notes to repair (modified in-place).
/// @param params Repair parameters.
/// @return Number of notes fixed.
int repairParallelPerfect(std::vector<NoteEvent>& notes,
                          const ParallelRepairParams& params);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_PARALLEL_REPAIR_H
