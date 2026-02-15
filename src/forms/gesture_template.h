// Gesture template for Stylus Phantasticus toccata openings.

#ifndef BACH_FORMS_GESTURE_TEMPLATE_H
#define BACH_FORMS_GESTURE_TEMPLATE_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"

namespace bach {

/// @brief Harmonic function for gesture accumulation chords.
/// Derived pitch sets from key context; stable across modulations.
enum class HarmonicFunction : uint8_t {
  Tonic,
  Dominant,
  LeadingTone,
  Subdominant
};

/// @brief Result of gesture generation.
struct GestureResult {
  std::vector<NoteEvent> notes;
  std::vector<int> core_intervals;  ///< Leader descent intervals (ornaments excluded).
  uint16_t gesture_id = 0;
  Tick end_tick = 0;
};

/// @brief Tag notes with gesture metadata (gesture_id, gesture_role, source).
///
/// Voice 0 notes → Leader, voice 1 notes → OctaveEcho, voice 2+ notes → PedalHit.
/// All notes receive BachNoteSource::ToccataGesture.
///
/// @param notes Notes to tag (modified in-place).
/// @param gesture_id Unique gesture group identifier (must be > 0).
void tagGestureNotes(std::vector<NoteEvent>& notes, uint16_t gesture_id);

/// @brief Extract core intervals from Leader descent notes within a gesture.
///
/// Filters notes by gesture_id and GestureRole::Leader, sorted by onset.
/// Extracts only the descent portion: consecutive descending intervals
/// (ornament notes like mordents are excluded by filtering positive intervals).
///
/// @param notes All notes (may contain non-gesture notes).
/// @param gesture_id Gesture to extract from.
/// @return Directed intervals of the descent portion (negative values = descending).
std::vector<int> extractGestureCoreIntervals(
    const std::vector<NoteEvent>& notes, uint16_t gesture_id);

}  // namespace bach

#endif  // BACH_FORMS_GESTURE_TEMPLATE_H
