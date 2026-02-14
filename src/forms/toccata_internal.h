// Shared utilities for toccata archetype implementations.
// Internal header -- not part of the public API.

#ifndef BACH_FORMS_TOCCATA_INTERNAL_H
#define BACH_FORMS_TOCCATA_INTERNAL_H

#include <algorithm>
#include <map>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/gm_program.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/scale.h"
#include "counterpoint/bach_rule_evaluator.h"
#include "counterpoint/collision_resolver.h"
#include "counterpoint/counterpoint_state.h"
#include "forms/form_utils.h"
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
    case 0: return 60;                          // C4 (Great)
    case 1: return 52;                          // E3 (Swell)
    case 2: return organ_range::kPedalLow;      // 24 (Pedal unchanged)
    case 3: return 43;                          // G2 (Positiv)
    default: return 52;
  }
}

inline uint8_t getToccataHighPitch(uint8_t voice_idx) {
  switch (voice_idx) {
    case 0: return 88;                          // E6 (Great)
    case 1: return 76;                          // E5 (Swell)
    case 2: return organ_range::kPedalHigh;     // 50 (Pedal unchanged)
    case 3: return 67;                          // G4 (Positiv)
    default: return 76;
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
  form_utils::sortTrackNotes(tracks);
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

/// @brief Discrete bars allocation with preset tables and weighted minimum guarantee.
/// @param total_bars Total number of bars to allocate.
/// @param discrete_tables Map of total_bars to preset bar allocations.
/// @param fallback_proportions Proportions for bars not in discrete_tables.
/// @param min_bars_per_section Minimum bars per section (weighted guarantee).
/// @return Bar allocations per section.
inline std::vector<Tick> allocateBarsDiscrete(
    int total_bars,
    const std::map<int, std::vector<Tick>>& discrete_tables,
    const std::vector<float>& fallback_proportions,
    const std::vector<Tick>& min_bars_per_section) {
  // 1. Check if total_bars matches a discrete table entry.
  auto table_iter = discrete_tables.find(total_bars);
  if (table_iter != discrete_tables.end()) {
    return table_iter->second;
  }

  // 2. Fallback: use proportions with minimum guarantee.
  size_t num = fallback_proportions.size();
  std::vector<Tick> bars(num, 0);

  // Check if minimum guarantee exceeds total.
  Tick min_total = 0;
  for (size_t idx = 0; idx < num && idx < min_bars_per_section.size(); ++idx) {
    min_total += min_bars_per_section[idx];
  }

  if (total_bars <= 0 || min_total >= static_cast<Tick>(total_bars)) {
    // Scale down minimums proportionally.
    float scale = static_cast<float>(total_bars) / static_cast<float>(min_total);
    Tick assigned = 0;
    for (size_t idx = 0; idx < num; ++idx) {
      Tick min_b = (idx < min_bars_per_section.size()) ? min_bars_per_section[idx] : 1;
      bars[idx] = std::max(static_cast<Tick>(1),
                           static_cast<Tick>(static_cast<float>(min_b) * scale));
      assigned += bars[idx];
    }
    // Adjust to match total_bars exactly.
    if (assigned <= static_cast<Tick>(total_bars)) {
      bars.back() += static_cast<Tick>(total_bars) - assigned;
    } else {
      // Over-allocated (each section clamped to 1 exceeds total): trim from end.
      Tick excess = assigned - static_cast<Tick>(total_bars);
      for (size_t idx = num; idx > 0 && excess > 0; --idx) {
        Tick reduce = std::min(bars[idx - 1], excess);
        bars[idx - 1] -= reduce;
        excess -= reduce;
      }
    }
    return bars;
  }

  // 3. Allocate by proportions, then enforce minimums.
  for (size_t idx = 0; idx < num; ++idx) {
    bars[idx] = static_cast<Tick>(static_cast<float>(total_bars) * fallback_proportions[idx]);
    Tick min_b = (idx < min_bars_per_section.size()) ? min_bars_per_section[idx] : 1;
    if (bars[idx] < min_b) bars[idx] = min_b;
  }

  // Adjust to match total_bars exactly.
  Tick assigned = 0;
  for (auto bar : bars) assigned += bar;
  Tick diff = static_cast<Tick>(total_bars) - assigned;

  if (diff > 0) {
    // Distribute excess to largest-proportion section.
    size_t max_idx = 0;
    float max_prop = 0.0f;
    for (size_t idx = 0; idx < num; ++idx) {
      if (fallback_proportions[idx] > max_prop) {
        max_prop = fallback_proportions[idx];
        max_idx = idx;
      }
    }
    bars[max_idx] += diff;
  } else if (diff < 0) {
    // Reduce from largest sections (but not below minimum).
    for (Tick cur = 0; cur < static_cast<Tick>(-diff);) {
      size_t max_idx = 0;
      Tick max_val = 0;
      for (size_t idx = 0; idx < num; ++idx) {
        Tick min_b = (idx < min_bars_per_section.size()) ? min_bars_per_section[idx] : 1;
        if (bars[idx] > min_b && bars[idx] > max_val) {
          max_val = bars[idx];
          max_idx = idx;
        }
      }
      if (max_val == 0) break;  // Can't reduce further.
      bars[max_idx]--;
      ++cur;
    }
  }

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

/// @brief Coordinate all voices through createBachNote for vertical dissonance control.
///
/// Sorts notes by tick, groups simultaneous notes, processes pedal first (immutable),
/// then upper voices through createBachNote with free counterpoint rules.
/// @param all_notes All generated notes (will be consumed).
/// @param num_voices Number of active voices.
/// @param tonic Key tonic for counterpoint state.
/// @return Coordinated notes with dissonances resolved.
inline std::vector<NoteEvent> coordinateVoices(
    std::vector<NoteEvent> all_notes, uint8_t num_voices, Key tonic,
    const HarmonicTimeline* timeline = nullptr) {
  BachRuleEvaluator cp_rules(num_voices);
  cp_rules.setFreeCounterpoint(true);
  CollisionResolver cp_resolver;
  cp_resolver.setHarmonicTimeline(timeline);
  CounterpointState cp_state;
  cp_state.setKey(tonic);
  for (uint8_t v = 0; v < num_voices; ++v) {
    cp_state.registerVoice(v, getToccataLowPitch(v), getToccataHighPitch(v));
  }

  std::sort(all_notes.begin(), all_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  std::vector<NoteEvent> coordinated;
  coordinated.reserve(all_notes.size());
  int accepted_count = 0;
  int total_count = 0;

  size_t i = 0;
  while (i < all_notes.size()) {
    Tick current_tick = all_notes[i].start_tick;
    size_t group_end = i;
    while (group_end < all_notes.size() &&
           all_notes[group_end].start_tick == current_tick) {
      ++group_end;
    }

    // Sort group: pedal first (voice 2), then lower voices first.
    std::sort(all_notes.begin() + static_cast<ptrdiff_t>(i),
              all_notes.begin() + static_cast<ptrdiff_t>(group_end),
              [](const NoteEvent& a, const NoteEvent& b) {
                bool a_pedal = (a.source == BachNoteSource::PedalPoint);
                bool b_pedal = (b.source == BachNoteSource::PedalPoint);
                if (a_pedal != b_pedal) return a_pedal;
                return a.voice > b.voice;
              });

    for (size_t j = i; j < group_end; ++j) {
      const auto& note = all_notes[j];
      ++total_count;

      if (note.source == BachNoteSource::PedalPoint) {
        cp_state.addNote(note.voice, note);
        coordinated.push_back(note);
        ++accepted_count;
        continue;
      }

      BachNoteOptions opts;
      opts.voice = note.voice;
      opts.desired_pitch = note.pitch;
      opts.tick = note.start_tick;
      opts.duration = note.duration;
      opts.velocity = note.velocity;
      opts.source = note.source;

      auto result = createBachNote(&cp_state, &cp_rules, &cp_resolver, opts);
      if (result.accepted) {
        coordinated.push_back(result.note);
        ++accepted_count;
      }
    }
    i = group_end;
  }

  fprintf(stderr, "[Toccata] createBachNote: accepted %d/%d (%.0f%%)\n",
          accepted_count, total_count,
          total_count > 0 ? 100.0 * accepted_count / total_count : 0.0);
  return coordinated;
}

}  // namespace toccata_internal
}  // namespace bach

#endif  // BACH_FORMS_TOCCATA_INTERNAL_H
