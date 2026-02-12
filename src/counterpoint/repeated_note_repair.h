// Shared utility for repairing consecutive repeated pitches.
// Extracts the inline repair logic from FugueGenerator into a reusable
// function callable from passacaglia, prelude, and fugue generators.

#ifndef BACH_COUNTERPOINT_REPEATED_NOTE_REPAIR_H
#define BACH_COUNTERPOINT_REPEATED_NOTE_REPAIR_H

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

namespace bach {

/// Parameters for repeated-note repair.
struct RepeatedNoteRepairParams {
  uint8_t num_voices = 0;

  /// Notes beyond this count in a same-pitch run are repaired.
  int max_consecutive = 3;

  /// Maximum tick gap between consecutive notes to consider them part of the
  /// same repeated run. Notes separated by more than this are treated as
  /// independent runs.
  Tick run_gap_threshold = 2 * kTicksPerBar;

  /// Key at a given tick (required). Returns the Key enum for diatonic
  /// validation of replacement pitches.
  std::function<Key(Tick)> key_at_tick;

  /// Scale type at a given tick (required). Used together with key_at_tick
  /// for scale-tone membership checks.
  std::function<ScaleType(Tick)> scale_at_tick;

  /// Optional: voice pitch range (low, high). Return (0, 127) if unused.
  /// Candidates outside this range are rejected.
  std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range;

  /// Optional: vertical safety check. Returns true if the candidate pitch
  /// is acceptable at the given tick in the given voice. Returns false if
  /// the candidate would create parallel 5ths/octaves or other problematic
  /// intervals with other voices. nullptr / empty = skip check.
  std::function<bool(Tick tick, uint8_t voice, uint8_t pitch)> vertical_safe;
};

/// @brief Repair runs of consecutive repeated pitches in each voice.
///
/// For each voice, detects runs of notes with the same pitch within the
/// gap threshold. Notes beyond max_consecutive are replaced with nearby
/// scale tones, alternating direction relative to the approach motion.
///
/// Protected sources are never modified:
/// - Structural sources (subject, answer, countersubject, pedal, false entry,
///   coda, sequence) via isStructuralSource()
/// - Ground bass (BachNoteSource::GroundBass)
///
/// @param notes All notes across all voices (modified in place).
/// @param params Configuration for repair behavior.
/// @return Number of notes actually modified.
int repairRepeatedNotes(std::vector<NoteEvent>& notes,
                        const RepeatedNoteRepairParams& params);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_REPEATED_NOTE_REPAIR_H
