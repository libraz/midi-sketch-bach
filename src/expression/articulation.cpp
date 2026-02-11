// Implementation of articulation and phrasing rules.

#include "expression/articulation.h"

#include "harmony/harmonic_timeline.h"

namespace bach {

// ---------------------------------------------------------------------------
// Default articulation per voice role
// ---------------------------------------------------------------------------

ArticulationRule getDefaultArticulation(VoiceRole role) {
  ArticulationRule rule;
  switch (role) {
    case VoiceRole::Ground:
      rule.type = ArticulationType::Legato;
      rule.gate_ratio = 0.95f;
      rule.velocity_offset = 0;
      break;
    case VoiceRole::Assert:
      rule.type = ArticulationType::NonLegato;
      rule.gate_ratio = 0.85f;
      rule.velocity_offset = 0;
      break;
    case VoiceRole::Respond:
      rule.type = ArticulationType::NonLegato;
      rule.gate_ratio = 0.87f;
      rule.velocity_offset = 0;
      break;
    case VoiceRole::Propel:
      rule.type = ArticulationType::NonLegato;
      rule.gate_ratio = 0.85f;
      rule.velocity_offset = 0;
      break;
  }
  return rule;
}

// ---------------------------------------------------------------------------
// Cadence detection helpers
// ---------------------------------------------------------------------------

/// @brief Check whether a cadence occurs between two consecutive harmonic events.
///
/// Detects V->I (perfect cadence) by checking if the previous event's chord
/// degree is V and the current event's chord degree is I.
///
/// @param prev The preceding harmonic event.
/// @param curr The current harmonic event.
/// @return True if a perfect cadence (V->I) is detected.
static bool isCadenceTransition(const HarmonicEvent& prev, const HarmonicEvent& curr) {
  return prev.chord.degree == ChordDegree::V && curr.chord.degree == ChordDegree::I;
}

/// @brief Find tick positions where cadences or key changes occur.
///
/// Scans the timeline for V->I progressions and key changes, returning the
/// tick positions where phrase breathing should be applied.
///
/// @param timeline The harmonic timeline to scan.
/// @return Sorted vector of tick positions at cadence/key-change points.
static std::vector<Tick> findCadenceTicks(const HarmonicTimeline& timeline) {
  std::vector<Tick> cadence_ticks;
  const auto& events = timeline.events();

  for (size_t idx = 1; idx < events.size(); ++idx) {
    // Key change boundary.
    if (events[idx].key != events[idx - 1].key ||
        events[idx].is_minor != events[idx - 1].is_minor) {
      cadence_ticks.push_back(events[idx].tick);
      continue;
    }
    // V -> I perfect cadence.
    if (isCadenceTransition(events[idx - 1], events[idx])) {
      cadence_ticks.push_back(events[idx].tick);
    }
  }
  return cadence_ticks;
}

/// @brief Find the index of the note whose sounding region covers or immediately
///        precedes the given cadence tick.
///
/// Scans the note list to find the note that ends at or just before the cadence
/// tick, which is the note that should receive the breathing reduction.
///
/// @param notes The note events to search.
/// @param cadence_tick The tick of the cadence point.
/// @return Index of the preceding note, or -1 if none found.
static int findPrecedingNoteIndex(const std::vector<NoteEvent>& notes, Tick cadence_tick) {
  int best_idx = -1;
  Tick best_end = 0;

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    Tick note_end = notes[idx].start_tick + notes[idx].duration;
    // Note must start before the cadence tick.
    if (notes[idx].start_tick < cadence_tick) {
      // Prefer the note whose end is closest to the cadence tick.
      if (best_idx < 0 || note_end > best_end) {
        best_idx = static_cast<int>(idx);
        best_end = note_end;
      }
    }
  }
  return best_idx;
}

// ---------------------------------------------------------------------------
// Apply articulation
// ---------------------------------------------------------------------------

/// Minimum note duration after articulation (prevent zero-length notes).
static constexpr Tick kMinArticulatedDuration = 60;

/// Breathing reduction ratio at cadence points.
static constexpr float kBreathingReduction = 0.80f;

void applyArticulation(std::vector<NoteEvent>& notes, VoiceRole role,
                       const HarmonicTimeline* timeline, bool is_organ) {
  if (notes.empty()) {
    return;
  }

  ArticulationRule rule = getDefaultArticulation(role);

  // Step 1: Apply gate ratio to all note durations.
  // Organ pipes have no gate control — preserve metric durations exactly.
  if (!is_organ) {
    for (auto& note : notes) {
      Tick new_duration =
          static_cast<Tick>(static_cast<float>(note.duration) * rule.gate_ratio);
      if (new_duration < kMinArticulatedDuration) {
        new_duration = kMinArticulatedDuration;
      }
      note.duration = new_duration;
    }
  }

  // Step 2: Apply beat-position velocity accents for non-organ instruments.
  if (!is_organ) {
    for (auto& note : notes) {
      uint8_t beat = beatInBar(note.start_tick);
      int vel = static_cast<int>(note.velocity);
      if (beat == 0) {
        vel += 8;  // Downbeat accent.
      } else if (beat == 2) {
        vel += 4;  // Secondary stress.
      }
      // Clamp to valid MIDI range.
      if (vel > 127) vel = 127;
      if (vel < 1) vel = 1;
      note.velocity = static_cast<uint8_t>(vel);
    }
  }

  // Step 3: Phrase breathing at cadence points.
  // Organ sustains through cadences — no breathing reduction needed.
  if (!is_organ && timeline != nullptr) {
    std::vector<Tick> cadence_ticks = findCadenceTicks(*timeline);
    for (Tick cad_tick : cadence_ticks) {
      int preceding_idx = findPrecedingNoteIndex(notes, cad_tick);
      if (preceding_idx >= 0) {
        auto& note = notes[preceding_idx];
        Tick new_duration =
            static_cast<Tick>(static_cast<float>(note.duration) * kBreathingReduction);
        if (new_duration < kMinArticulatedDuration) {
          new_duration = kMinArticulatedDuration;
        }
        note.duration = new_duration;
      }
    }
  }
}

}  // namespace bach
