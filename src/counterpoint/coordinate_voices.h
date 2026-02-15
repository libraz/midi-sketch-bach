// Unified coordination pass for all organ forms.
// Replaces per-form coordination loops with a single configurable pass.

#ifndef BACH_COUNTERPOINT_COORDINATE_VOICES_H
#define BACH_COUNTERPOINT_COORDINATE_VOICES_H

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "counterpoint/vertical_context.h"
#include "harmony/harmonic_timeline.h"

namespace bach {

/// Configuration for the unified coordination pass.
struct CoordinationConfig {
  uint8_t num_voices = 4;
  Key tonic = Key::C;
  const HarmonicTimeline* timeline = nullptr;

  /// Per-voice range function: returns (low, high) for a given voice.
  std::function<std::pair<uint8_t, uint8_t>(uint8_t)> voice_range = nullptr;

  /// Optional custom priority function for group sorting.
  /// Lower return value = higher priority (processed first).
  /// nullptr = default: immutable(0) -> lower-voices(voice descending) -> upper.
  std::function<int(const NoteEvent&)> priority = nullptr;

  /// Sources treated as immutable (accepted without modification).
  std::vector<BachNoteSource> immutable_sources = {BachNoteSource::PedalPoint};

  /// Sources treated as lightweight (range + strong-beat consonance check only).
  /// No full createBachNote pipeline.
  std::vector<BachNoteSource> lightweight_sources;

  /// Build per-voice next-pitch map for NHT lookahead in createBachNote.
  bool use_next_pitch_map = false;

  /// Check for cross-relations and attempt alternatives.
  bool check_cross_relations = false;

  /// Optional weak-beat NHT exception predicate.
  WeakBeatDissonancePredicate weak_beat_allow = nullptr;

  /// Form name for diagnostic logging.
  const char* form_name = "Unknown";
};

/// Run the unified coordination pass on all_notes.
///
/// Groups notes by tick, sorts each group by priority (immutable first,
/// then lower voices), and processes each note through one of three tiers:
///   1. Immutable: accept directly (PedalPoint, CantusFixed, etc.)
///   2. Lightweight: range + strong-beat chord-tone + vertical consonance
///   3. Full: createBachNote() pipeline with optional NHT lookahead
///
/// Also applies weak-beat m2/M7/TT rejection via checkVerticalConsonance.
///
/// @param all_notes Input notes (consumed by move).
/// @param config Coordination configuration.
/// @return Coordinated notes with dissonances resolved.
std::vector<NoteEvent> coordinateVoices(std::vector<NoteEvent> all_notes,
                                        const CoordinationConfig& config);

}  // namespace bach

#endif  // BACH_COUNTERPOINT_COORDINATE_VOICES_H
