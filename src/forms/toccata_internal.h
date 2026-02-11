// Shared utilities for toccata archetype implementations.
// Internal header -- not part of the public API.

#ifndef BACH_FORMS_TOCCATA_INTERNAL_H
#define BACH_FORMS_TOCCATA_INTERNAL_H

#include <algorithm>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_event.h"
#include "harmony/harmonic_timeline.h"
#include "harmony/key.h"
#include "organ/organ_techniques.h"
#include "organ/registration.h"

namespace bach {
namespace toccata_internal {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

using namespace duration;

constexpr uint8_t kGreatChannel = 0;
constexpr uint8_t kSwellChannel = 1;
constexpr uint8_t kPedalChannel = 3;

constexpr uint8_t kMinVoices = 2;
constexpr uint8_t kMaxVoices = 5;

// ---------------------------------------------------------------------------
// Track creation
// ---------------------------------------------------------------------------

/// @brief Create organ tracks for a toccata.
inline std::vector<Track> createToccataTracks(uint8_t num_voices) {
  std::vector<Track> tracks;
  tracks.reserve(num_voices);

  struct TrackSpec {
    uint8_t channel;
    uint8_t program;
    const char* name;
  };

  static constexpr TrackSpec kSpecs[] = {
      {kGreatChannel, GmProgram::kChurchOrgan, "Manual I (Great)"},
      {kSwellChannel, GmProgram::kReedOrgan, "Manual II (Swell)"},
      {kPedalChannel, GmProgram::kChurchOrgan, "Pedal"},
      {2, GmProgram::kChurchOrgan, "Manual III (Positiv)"},
      {4, GmProgram::kChurchOrgan, "Manual IV"},
  };

  for (uint8_t idx = 0; idx < num_voices && idx < 5; ++idx) {
    Track track;
    track.channel = kSpecs[idx].channel;
    track.program = kSpecs[idx].program;
    track.name = kSpecs[idx].name;
    tracks.push_back(track);
  }

  return tracks;
}

// ---------------------------------------------------------------------------
// Pitch range helpers
// ---------------------------------------------------------------------------

inline uint8_t getToccataLowPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1Low;
    case 1: return organ_range::kManual2Low;
    case 2: return organ_range::kPedalLow;
    case 3: return organ_range::kManual3Low;
    default: return organ_range::kManual1Low;
  }
}

inline uint8_t getToccataHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return organ_range::kManual1High;
    case 1: return organ_range::kManual2High;
    case 2: return organ_range::kPedalHigh;
    case 3: return organ_range::kManual3High;
    default: return organ_range::kManual1High;
  }
}

// ---------------------------------------------------------------------------
// NoteEvent creation helper
// ---------------------------------------------------------------------------

inline NoteEvent makeNote(Tick tick, Tick dur, uint8_t pitch, uint8_t voice,
                          BachNoteSource source = BachNoteSource::FreeCounterpoint) {
  NoteEvent n;
  n.start_tick = tick;
  n.duration = dur;
  n.pitch = pitch;
  n.velocity = kOrganVelocity;
  n.voice = voice;
  n.source = source;
  return n;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

inline uint8_t clampToccataVoiceCount(uint8_t num_voices) {
  if (num_voices < kMinVoices) return kMinVoices;
  if (num_voices > kMaxVoices) return kMaxVoices;
  return num_voices;
}

inline void sortToccataTrackNotes(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    std::sort(track.notes.begin(), track.notes.end(),
              [](const NoteEvent& lhs, const NoteEvent& rhs) {
                if (lhs.start_tick != rhs.start_tick) {
                  return lhs.start_tick < rhs.start_tick;
                }
                return lhs.pitch < rhs.pitch;
              });
  }
}

/// @brief Clean up within-voice overlaps: deduplicate same-tick notes and truncate.
inline void cleanupToccataOverlaps(std::vector<Track>& tracks) {
  for (auto& track : tracks) {
    auto& notes = track.notes;
    if (notes.size() < 2) continue;

    std::sort(notes.begin(), notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
                return a.duration > b.duration;
              });

    notes.erase(
        std::unique(notes.begin(), notes.end(),
                    [](const NoteEvent& a, const NoteEvent& b) {
                      return a.start_tick == b.start_tick;
                    }),
        notes.end());

    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].start_tick + notes[i].duration;
      if (end_tick > notes[i + 1].start_tick) {
        notes[i].duration = notes[i + 1].start_tick - notes[i].start_tick;
        if (notes[i].duration == 0) notes[i].duration = 1;
      }
    }
  }
}

/// @brief Assign notes to tracks by voice index.
inline void assignNotesToTracks(const std::vector<NoteEvent>& all_notes,
                                std::vector<Track>& tracks) {
  for (const auto& note : all_notes) {
    if (note.voice < tracks.size()) {
      tracks[note.voice].notes.push_back(note);
    }
  }
}

/// @brief Compute bar allocations from proportions.
/// Distributes total_bars according to proportions, rounding remainder to largest.
inline std::vector<Tick> allocateBars(int total_bars,
                                       const std::vector<float>& proportions) {
  std::vector<Tick> bars(proportions.size(), 0);
  Tick assigned = 0;
  size_t max_idx = 0;
  float max_prop = 0.0f;

  for (size_t i = 0; i < proportions.size(); ++i) {
    bars[i] = static_cast<Tick>(static_cast<float>(total_bars) * proportions[i]);
    if (bars[i] < 1) bars[i] = 1;
    assigned += bars[i];
    if (proportions[i] > max_prop) {
      max_prop = proportions[i];
      max_idx = i;
    }
  }

  // Distribute remainder to largest section.
  Tick remainder = static_cast<Tick>(total_bars) - assigned;
  bars[max_idx] += remainder;

  return bars;
}

/// @brief Build section boundaries from bar allocations.
inline std::vector<ToccataSectionBoundary> buildSectionBoundaries(
    const std::vector<Tick>& bar_counts,
    const std::vector<ToccataSectionId>& ids) {
  std::vector<ToccataSectionBoundary> sections;
  Tick tick = 0;
  for (size_t i = 0; i < bar_counts.size() && i < ids.size(); ++i) {
    Tick end = tick + bar_counts[i] * kTicksPerBar;
    sections.push_back({ids[i], tick, end});
    tick = end;
  }
  return sections;
}

/// @brief Populate legacy ToccataResult fields from sections.
inline void populateLegacyFields(ToccataResult& result) {
  if (result.sections.empty()) return;
  result.opening_start = result.sections[0].start;
  result.opening_end = result.sections[0].end;
  if (result.sections.size() >= 2) {
    result.recit_start = result.sections[1].start;
    result.recit_end = result.sections[1].end;
  }
  result.drive_start = result.sections.back().start;
  result.drive_end = result.sections.back().end;
}

}  // namespace toccata_internal
}  // namespace bach

#endif  // BACH_FORMS_TOCCATA_INTERNAL_H
