// Shared utility for resolving unresolved melodic leaps (contrary step rule).
// After a large interval leap, the next note should resolve by step in the
// opposite direction. This utility scans all voices and fixes unresolved leaps.

#ifndef BACH_COUNTERPOINT_LEAP_RESOLUTION_H
#define BACH_COUNTERPOINT_LEAP_RESOLUTION_H

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"

namespace bach {

/// Parameters for leap resolution.
struct LeapResolutionParams {
  uint8_t num_voices = 0;

  /// Minimum semitone interval to consider a "leap" (default 5 = P4+,
  /// matching the Python validator threshold).
  int leap_threshold = 5;

  /// Key at a given tick (required). Returns the Key enum for scale-tone
  /// validation of candidate resolution pitches.
  std::function<Key(Tick)> key_at_tick;

  /// Scale type at a given tick (required). Used together with key_at_tick
  /// for scale-tone membership checks.
  std::function<ScaleType(Tick)> scale_at_tick;

  /// Static voice pitch range (low, high). Used when voice_range is not set.
  /// Existing callers set this field only (gradual migration).
  std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range_static;

  /// Tick-aware voice pitch range (low, high). When set, takes priority over
  /// voice_range_static. Enables phase-ceiling enforcement in repair.
  std::function<std::pair<uint8_t, uint8_t>(uint8_t, Tick)> voice_range;

  /// Optional: vertical safety check. Returns true if the candidate pitch
  /// is acceptable at the given tick in the given voice (no tritone,
  /// no hidden octave). nullptr / empty = skip check.
  std::function<bool(Tick tick, uint8_t voice, uint8_t pitch)> vertical_safe;

  /// Optional: chord tone membership at a given tick. nullptr = skip
  /// harmonic awareness (falls back to contrary-step-only resolution).
  std::function<bool(Tick tick, uint8_t pitch)> is_chord_tone;
};

namespace leap_detail {

/// @brief Check if a pitch is a leading tone in the given key/scale context.
/// Leading tone = pitch_class == (tonic + 11) % 12.
/// NaturalMinor returns false (subtonic, not leading tone).
bool isLeadingTone(uint8_t pitch, Key key, ScaleType scale);

/// @brief Check if the motion from prev_pitch to curr_pitch is a tendency
/// resolution: leading tone -> tonic (ascending semitone), fa -> mi
/// (4th degree descending to 3rd), or 7th -> 6th (prepared seventh
/// resolving down).
bool isTendencyResolution(uint8_t prev_pitch, uint8_t curr_pitch,
                          Key key, ScaleType scale);

/// @brief Detect sequential interval patterns (for episode protection).
/// Returns true if pitches[0..count-1] exhibit an interval pattern where
/// interval(0,1)==interval(2,3) AND interval(1,2)==interval(3,4).
/// Requires count >= 5.
bool isSequencePattern(const uint8_t* pitches, int count);

}  // namespace leap_detail

/// @brief Resolve unresolved melodic leaps across all voices.
///
/// For each voice, scans triplets of consecutive notes (k, k+1, k+2).
/// If the interval from k to k+1 exceeds the threshold, checks whether
/// k+2 resolves by contrary step. If not, attempts to replace k+2's pitch
/// with a nearby scale tone in the opposite direction.
///
/// Multiple protection conditions prevent modification:
/// - Immutable/Structural source protection
/// - Strong-beat protection (non-leading-tone)
/// - Already-resolved notes (contrary step + chord tone / tendency)
/// - Scalar run protection (consecutive same-direction steps)
/// - Chord-tone landing protection
/// - Sequence pattern protection (episode motifs)
/// - Bar-line crossing protection (tonic cadence)
/// - Previously modified notes (vibration prevention)
///
/// @param notes All notes across all voices (modified in place).
/// @param params Configuration for resolution behavior.
/// @return Number of notes actually modified.
int resolveLeaps(std::vector<NoteEvent>& notes,
                 const LeapResolutionParams& params);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_LEAP_RESOLUTION_H
