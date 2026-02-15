// Physical impossibility guard implementation.

#include "instrument/common/impossibility_guard.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#include "core/pitch_utils.h"
#include "instrument/bowed/cello_model.h"
#include "instrument/bowed/violin_model.h"
#include "instrument/fretted/guitar_model.h"
#include "instrument/keyboard/harpsichord_model.h"
#include "instrument/keyboard/organ_model.h"
#include "instrument/keyboard/piano_model.h"

namespace bach {

namespace {

/// Tactus resolution for beat-head detection (quarter note).
constexpr Tick kTactus = kTicksPerBeat;

/// Maximum tiny-offset delta in ticks for bowed simultaneous resolution.
constexpr Tick kTinyOffsetMax = 3;

/// Check if pitch is a bass-line source that should be preserved.
bool isBassoSource(BachNoteSource source) {
  return source == BachNoteSource::GroundBass ||
         source == BachNoteSource::GoldbergBass ||
         source == BachNoteSource::PedalPoint;
}

/// Check melodic contour preservation for octave shift.
/// Returns true if the shift is acceptable (interval doesn't increase
/// and contour direction is maintained).
bool isOctaveShiftAcceptable(uint8_t original, uint8_t shifted,
                              uint8_t prev_pitch) {
  if (prev_pitch == 0) return true;  // No previous note, accept any shift.
  int orig_dist = std::abs(static_cast<int>(original) -
                           static_cast<int>(prev_pitch));
  int shift_dist = std::abs(static_cast<int>(shifted) -
                            static_cast<int>(prev_pitch));
  if (shift_dist > orig_dist) return false;  // Interval increased.
  // Check contour direction.
  int orig_dir = static_cast<int>(original) - static_cast<int>(prev_pitch);
  int shift_dir = static_cast<int>(shifted) - static_cast<int>(prev_pitch);
  if (orig_dir > 0 && shift_dir < 0) return false;  // Direction reversed.
  if (orig_dir < 0 && shift_dir > 0) return false;
  return true;
}

/// Build fixPitchRange for a given range [low, high].
std::function<uint8_t(uint8_t, ProtectionLevel, uint8_t)>
makeFixPitchRange(uint8_t range_low, uint8_t range_high) {
  return [range_low, range_high](uint8_t pitch, ProtectionLevel level,
                                  uint8_t prev_pitch) -> uint8_t {
    // Already in range?
    if (pitch >= range_low && pitch <= range_high) return pitch;

    // Immutable: never touch. Log warning.
    if (level == ProtectionLevel::Immutable) {
      std::fprintf(stderr,
          "[ImpossibilityGuard] WARNING: Immutable note pitch=%u "
          "out of range [%u,%u] -- not modified\n",
          pitch, range_low, range_high);
      return pitch;
    }

    // Try octave up.
    int up = static_cast<int>(pitch) + 12;
    if (up >= range_low && up <= range_high && up <= 127) {
      if (isOctaveShiftAcceptable(pitch, static_cast<uint8_t>(up),
                                   prev_pitch)) {
        return static_cast<uint8_t>(up);
      }
    }

    // Try octave down.
    int down = static_cast<int>(pitch) - 12;
    if (down >= range_low && down <= range_high && down >= 0) {
      if (isOctaveShiftAcceptable(pitch, static_cast<uint8_t>(down),
                                   prev_pitch)) {
        return static_cast<uint8_t>(down);
      }
    }

    // Structural / SemiImmutable: retry octave shift ignoring contour
    // (out-of-range is worse than a direction reversal).
    if (level == ProtectionLevel::Structural ||
        level == ProtectionLevel::SemiImmutable) {
      if (down >= range_low && down <= range_high && down >= 0) {
        return static_cast<uint8_t>(down);
      }
      if (up >= range_low && up <= range_high && up <= 127) {
        return static_cast<uint8_t>(up);
      }
      return pitch;  // Neither direction fits; leave as-is.
    }

    // Flexible: clamp as last resort.
    return clampPitch(static_cast<int>(pitch), range_low, range_high);
  };
}

/// Create guard for bowed instrument (Violin/Cello).
template <typename ModelT>
ImpossibilityGuard createBowedGuard() {
  auto model = std::make_shared<ModelT>();
  ImpossibilityGuard guard;

  uint8_t low = model->getLowestPitch();
  uint8_t high = model->getHighestPitch();

  guard.isPitchPlayable = [model](uint8_t pitch) {
    return model->isPitchPlayable(pitch);
  };

  guard.fixPitchRange = makeFixPitchRange(low, high);

  guard.checkSounding = [model](const SoundingGroup& group) -> Violation {
    if (group.notes.size() < 2) return Violation::None;
    if (group.notes.size() >= 3) return Violation::SimultaneousExceedsLimit;
    // Exactly 2 notes: check double-stop feasibility.
    if (!model->isDoubleStopFeasible(group.notes[0]->pitch,
                                      group.notes[1]->pitch)) {
      return Violation::ImpossibleDoubleStop;
    }
    return Violation::None;
  };

  guard.repairSounding = [](SoundingGroup& group) {
    if (group.notes.size() < 2) return;

    // Sort notes by ProtectionLevel (Immutable first, Flexible last).
    std::sort(group.notes.begin(), group.notes.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                return static_cast<int>(getProtectionLevel(a->source)) <
                       static_cast<int>(getProtectionLevel(b->source));
              });

    if (group.notes.size() >= 3) {
      // SimultaneousExceedsLimit: apply tiny offset to Flexible notes.
      Tick offset_delta = 1;
      for (size_t i = group.notes.size(); i-- > 0;) {
        auto* note = group.notes[i];
        auto level = getProtectionLevel(note->source);
        if (level == ProtectionLevel::Immutable ||
            level == ProtectionLevel::SemiImmutable) continue;
        // Structural on beat-head: skip offset.
        if (level == ProtectionLevel::Structural &&
            note->start_tick % kTactus == 0) {
          continue;
        }
        // Apply tiny offset.
        note->start_tick += offset_delta;
        if (note->duration > offset_delta) {
          note->duration -= offset_delta;
        }
        offset_delta = std::min(offset_delta + 1, kTinyOffsetMax);
      }
      return;
    }

    // ImpossibleDoubleStop (2 notes): role-aware resolution.
    auto* note_a = group.notes[0];  // Higher protection.
    auto* note_b = group.notes[1];  // Lower protection.
    auto level_a = getProtectionLevel(note_a->source);
    auto level_b = getProtectionLevel(note_b->source);

    // If both Immutable: NoAction (generation bug).
    if (level_a == ProtectionLevel::Immutable &&
        level_b == ProtectionLevel::Immutable) {
      std::fprintf(stderr,
          "[ImpossibilityGuard] WARNING: Two Immutable notes in "
          "impossible double stop at tick=%u -- not modified\n",
          static_cast<unsigned>(group.tick));
      return;
    }

    // If Flexible exists, drop it.
    if (level_b == ProtectionLevel::Flexible) {
      note_b->pitch = 0;
      note_b->duration = 0;
      return;
    }

    // Both Structural or mixed: preserve basso, shift the other.
    NoteEvent* to_shift = note_b;
    if (isBassoSource(note_b->source) && !isBassoSource(note_a->source)) {
      to_shift = note_a;
    }

    // Try octave shift.
    int up = static_cast<int>(to_shift->pitch) + 12;
    if (up <= 127) {
      to_shift->pitch = static_cast<uint8_t>(up);
      return;
    }
    int down = static_cast<int>(to_shift->pitch) - 12;
    if (down >= 0) {
      to_shift->pitch = static_cast<uint8_t>(down);
      return;
    }
  };

  return guard;
}

/// Create guard for keyboard instrument (Organ/Harpsichord/Piano).
template <typename ModelT>
ImpossibilityGuard createKeyboardGuard() {
  auto model = std::make_shared<ModelT>();
  ImpossibilityGuard guard;

  uint8_t low = model->getLowestPitch();
  uint8_t high = model->getHighestPitch();

  guard.isPitchPlayable = [model](uint8_t pitch) {
    return model->isPitchInRange(pitch);
  };

  guard.fixPitchRange = makeFixPitchRange(low, high);

  guard.checkSounding = [model](const SoundingGroup& group) -> Violation {
    if (group.notes.size() < 2) return Violation::None;
    // Collect pitches.
    std::vector<uint8_t> pitches;
    pitches.reserve(group.notes.size());
    for (const auto* note : group.notes) {
      pitches.push_back(note->pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    if (!model->isVoicingPlayable(pitches)) {
      return Violation::SimultaneousExceedsLimit;
    }
    return Violation::None;
  };

  guard.repairSounding = [model](SoundingGroup& group) {
    if (group.notes.size() < 2) return;

    // Collect pitches and try suggestPlayableVoicing.
    std::vector<uint8_t> pitches;
    pitches.reserve(group.notes.size());
    for (const auto* note : group.notes) {
      pitches.push_back(note->pitch);
    }
    std::sort(pitches.begin(), pitches.end());

    auto suggested = model->suggestPlayableVoicing(pitches);
    if (suggested.empty() || suggested.size() != pitches.size()) {
      // suggestPlayableVoicing failed: drop lowest-priority notes.
      std::sort(group.notes.begin(), group.notes.end(),
                [](const NoteEvent* a, const NoteEvent* b) {
                  return static_cast<int>(getProtectionLevel(a->source)) <
                         static_cast<int>(getProtectionLevel(b->source));
                });
      // Drop from the back (Flexible first).
      while (group.notes.size() > 1) {
        auto* note = group.notes.back();
        if (getProtectionLevel(note->source) == ProtectionLevel::Flexible) {
          note->pitch = 0;
          note->duration = 0;
          group.notes.pop_back();
        } else {
          break;
        }
      }
      return;
    }

    // Map suggested pitches back to notes.
    // Sort notes by pitch ascending to match suggested (also sorted).
    std::sort(group.notes.begin(), group.notes.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                return a->pitch < b->pitch;
              });
    for (size_t i = 0; i < group.notes.size() && i < suggested.size(); ++i) {
      auto level = getProtectionLevel(group.notes[i]->source);
      if (level == ProtectionLevel::Immutable ||
          level == ProtectionLevel::SemiImmutable) continue;
      group.notes[i]->pitch = suggested[i];
    }
  };

  return guard;
}

/// Create guard for Organ (keyboard, but all sounding checks pass).
ImpossibilityGuard createOrganGuard() {
  OrganModel model;
  uint8_t low = model.getLowestPitch();
  uint8_t high = model.getHighestPitch();

  ImpossibilityGuard guard;
  guard.isPitchPlayable = [low, high](uint8_t pitch) {
    return pitch >= low && pitch <= high;
  };
  guard.fixPitchRange = makeFixPitchRange(low, high);
  guard.checkSounding = [](const SoundingGroup&) -> Violation {
    return Violation::None;  // Organ: always valid.
  };
  guard.repairSounding = [](SoundingGroup&) {};  // No-op.
  return guard;
}

/// Create guard for Guitar.
ImpossibilityGuard createGuitarGuard() {
  auto model = std::make_shared<GuitarModel>();
  uint8_t low = model->getLowestPitch();
  uint8_t high = model->getHighestPitch();

  ImpossibilityGuard guard;
  guard.isPitchPlayable = [model](uint8_t pitch) {
    return model->isPitchPlayable(pitch);
  };
  guard.fixPitchRange = makeFixPitchRange(low, high);

  guard.checkSounding = [](const SoundingGroup& group) -> Violation {
    if (group.notes.size() >= 2) return Violation::SimultaneousExceedsLimit;
    return Violation::None;
  };

  guard.repairSounding = [](SoundingGroup& group) {
    if (group.notes.size() < 2) return;
    // Sort by protection level and drop Flexible.
    std::sort(group.notes.begin(), group.notes.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                return static_cast<int>(getProtectionLevel(a->source)) <
                       static_cast<int>(getProtectionLevel(b->source));
              });
    while (group.notes.size() > 1) {
      auto* note = group.notes.back();
      if (getProtectionLevel(note->source) == ProtectionLevel::Flexible) {
        note->pitch = 0;
        note->duration = 0;
        group.notes.pop_back();
      } else {
        break;
      }
    }
  };

  return guard;
}

}  // namespace

/// Create guard for Harpsichord with correct range from HarpsichordConfig.
ImpossibilityGuard createHarpsichordGuard() {
  auto model = std::make_shared<HarpsichordModel>();
  ImpossibilityGuard guard;

  // Use HarpsichordConfig range, not inherited PianoModel range.
  const auto& config = model->getHarpsichordConfig();
  uint8_t low = std::min(config.lower_low, config.upper_low);
  uint8_t high = std::max(config.lower_high, config.upper_high);

  guard.isPitchPlayable = [low, high](uint8_t pitch) {
    return pitch >= low && pitch <= high;
  };

  guard.fixPitchRange = makeFixPitchRange(low, high);

  guard.checkSounding = [model](const SoundingGroup& group) -> Violation {
    if (group.notes.size() < 2) return Violation::None;
    std::vector<uint8_t> pitches;
    pitches.reserve(group.notes.size());
    for (const auto* note : group.notes) {
      pitches.push_back(note->pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    if (!model->isVoicingPlayable(pitches)) {
      return Violation::SimultaneousExceedsLimit;
    }
    return Violation::None;
  };

  guard.repairSounding = [model](SoundingGroup& group) {
    if (group.notes.size() < 2) return;
    std::vector<uint8_t> pitches;
    pitches.reserve(group.notes.size());
    for (const auto* note : group.notes) {
      pitches.push_back(note->pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    auto suggested = model->suggestPlayableVoicing(pitches);
    if (suggested.empty() || suggested.size() != pitches.size()) {
      // Drop Flexible notes.
      std::sort(group.notes.begin(), group.notes.end(),
                [](const NoteEvent* a, const NoteEvent* b) {
                  return static_cast<int>(getProtectionLevel(a->source)) <
                         static_cast<int>(getProtectionLevel(b->source));
                });
      while (group.notes.size() > 1) {
        auto* note = group.notes.back();
        if (getProtectionLevel(note->source) == ProtectionLevel::Flexible) {
          note->pitch = 0;
          note->duration = 0;
          group.notes.pop_back();
        } else {
          break;
        }
      }
      return;
    }
    std::sort(group.notes.begin(), group.notes.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                return a->pitch < b->pitch;
              });
    for (size_t i = 0; i < group.notes.size() && i < suggested.size(); ++i) {
      auto level = getProtectionLevel(group.notes[i]->source);
      if (level == ProtectionLevel::Immutable ||
          level == ProtectionLevel::SemiImmutable) continue;
      group.notes[i]->pitch = suggested[i];
    }
  };

  return guard;
}

ImpossibilityGuard createGuard(InstrumentType instrument) {
  switch (instrument) {
    case InstrumentType::Organ:
      return createOrganGuard();
    case InstrumentType::Harpsichord:
      return createHarpsichordGuard();
    case InstrumentType::Piano:
      return createKeyboardGuard<PianoModel>();
    case InstrumentType::Violin:
      return createBowedGuard<ViolinModel>();
    case InstrumentType::Cello:
      return createBowedGuard<CelloModel>();
    case InstrumentType::Guitar:
      return createGuitarGuard();
  }
  // Unreachable for well-formed InstrumentType.
  return createOrganGuard();
}

uint32_t enforceImpossibilityGuard(std::vector<Track>& tracks,
                                    const ImpossibilityGuard& guard) {
  uint32_t changes = 0;

  // --- Pass 1: Fix pitch range for each note. ---
  // Track per-voice previous pitch for melodic contour check.
  std::map<VoiceId, uint8_t> prev_pitch_map;

  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      if (note.duration == 0) continue;  // Skip already-dropped notes.
      if (guard.isPitchPlayable(note.pitch)) {
        prev_pitch_map[note.voice] = note.pitch;
        continue;
      }

      auto level = getProtectionLevel(note.source);
      uint8_t prev = prev_pitch_map.count(note.voice)
                         ? prev_pitch_map[note.voice]
                         : 0;
      uint8_t fixed = guard.fixPitchRange(note.pitch, level, prev);
      if (fixed != note.pitch) {
        note.pitch = fixed;
        ++changes;
      }
      prev_pitch_map[note.voice] = note.pitch;
    }
  }

  // --- Pass 2: Event-driven simultaneous sounding check. ---
  // Collect all note pointers.
  std::vector<NoteEvent*> all_notes;
  for (auto& track : tracks) {
    for (auto& note : track.notes) {
      if (note.duration == 0) continue;
      all_notes.push_back(&note);
    }
  }

  if (all_notes.empty()) return changes;

  // Build event list: (tick, +1=start/-1=end, note_ptr).
  struct SoundEvent {
    Tick tick;
    int8_t type;  // +1 = note-on, -1 = note-off.
    NoteEvent* note;
  };

  std::vector<SoundEvent> events;
  events.reserve(all_notes.size() * 2);
  for (auto* note : all_notes) {
    events.push_back({note->start_tick, +1, note});
    events.push_back({note->start_tick + note->duration, -1, note});
  }

  // Sort: by tick, then note-off before note-on at same tick.
  std::sort(events.begin(), events.end(),
            [](const SoundEvent& a, const SoundEvent& b) {
              if (a.tick != b.tick) return a.tick < b.tick;
              return a.type < b.type;  // -1 (off) before +1 (on).
            });

  // Sweep through events, building sounding groups at boundaries.
  std::set<NoteEvent*> active;
  size_t idx = 0;

  while (idx < events.size()) {
    Tick current_tick = events[idx].tick;

    // Process all events at this tick.
    while (idx < events.size() && events[idx].tick == current_tick) {
      if (events[idx].type == -1) {
        active.erase(events[idx].note);
      } else {
        active.insert(events[idx].note);
      }
      ++idx;
    }

    if (active.size() < 2) continue;

    // Build SoundingGroup from active set.
    SoundingGroup group;
    group.tick = current_tick;
    group.notes.assign(active.begin(), active.end());

    // Check and repair with iteration limit.
    for (int iteration = 0; iteration < 2; ++iteration) {
      Violation v = guard.checkSounding(group);
      if (v == Violation::None) break;
      uint32_t pre_count = changes;
      guard.repairSounding(group);

      // Count modifications.
      for (auto* note : group.notes) {
        if (note->duration == 0) {
          // Note was dropped.
          active.erase(note);
        }
      }
      // Remove dropped notes from group.
      group.notes.erase(
          std::remove_if(group.notes.begin(), group.notes.end(),
                          [](const NoteEvent* n) { return n->duration == 0; }),
          group.notes.end());
      ++changes;
      (void)pre_count;
    }
  }

  // Clean up dropped notes (duration == 0) from tracks.
  for (auto& track : tracks) {
    track.notes.erase(
        std::remove_if(track.notes.begin(), track.notes.end(),
                        [](const NoteEvent& n) { return n.duration == 0; }),
        track.notes.end());
  }

  return changes;
}

}  // namespace bach
