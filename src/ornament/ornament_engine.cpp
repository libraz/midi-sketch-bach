// Ornament engine implementation: post-processing ornament application.

#include "ornament/ornament_engine.h"

#include <algorithm>

#include "analysis/counterpoint_analyzer.h"
#include "harmony/chord_types.h"
#include "harmony/harmonic_timeline.h"
#include "ornament/appoggiatura.h"
#include "ornament/compound_ornament.h"
#include "ornament/mordent.h"
#include "ornament/nachschlag.h"
#include "ornament/pralltriller.h"
#include "ornament/trill.h"
#include "ornament/turn.h"
#include "ornament/vorschlag.h"

namespace bach {
namespace {

/// Simple deterministic LCG RNG for ornament decisions.
/// Uses the classic linear congruential generator parameters.
/// No external dependencies (future WASM support).
struct DeterministicRng {
  uint32_t state;

  explicit DeterministicRng(uint32_t seed) : state(seed) {}

  /// @brief Advance the RNG and return a float in [0.0, 1.0).
  float next() {
    state = state * 1103515245 + 12345;
    return static_cast<float>(state >> 16) / 65536.0f;
  }
};

/// @brief Get the effective ornament density for a given voice role.
/// @param base_density The base density from config.
/// @param role The voice role.
/// @return Adjusted density value.
float effectiveDensity(float base_density, VoiceRole role) {
  switch (role) {
    case VoiceRole::Assert:
      return base_density * 0.5f;  // Minimal ornamentation for subject
    case VoiceRole::Respond:
      return base_density;         // Normal density for answer
    case VoiceRole::Propel:
      return base_density;         // Full density for free counterpoint
    case VoiceRole::Ground:
      return 0.0f;                 // No ornaments on pedal/bass
  }
  return base_density;
}

/// @brief Calculate upper neighbor pitch (whole step up).
/// @param pitch Original MIDI pitch.
/// @return Upper neighbor pitch, clamped to MIDI range.
uint8_t upperNeighborPitch(uint8_t pitch) {
  return (pitch <= 125) ? static_cast<uint8_t>(pitch + 2) : pitch;
}

/// @brief Calculate lower neighbor pitch (whole step down).
/// @param pitch Original MIDI pitch.
/// @return Lower neighbor pitch, clamped to MIDI range.
uint8_t lowerNeighborPitch(uint8_t pitch) {
  return (pitch >= 2) ? static_cast<uint8_t>(pitch - 2) : pitch;
}

/// @brief Apply a single ornament to a note based on the selected type.
/// @param note The note to ornament.
/// @param type The ornament type to apply.
/// @return Vector of sub-notes replacing the original.
std::vector<NoteEvent> applyOrnamentToNote(const NoteEvent& note, OrnamentType type) {
  const uint8_t upper = upperNeighborPitch(note.pitch);
  const uint8_t lower = lowerNeighborPitch(note.pitch);

  switch (type) {
    case OrnamentType::Trill:
      return generateTrill(note, upper);
    case OrnamentType::Mordent:
      return generateMordent(note, lower);
    case OrnamentType::Turn:
      return generateTurn(note, upper, lower);
    case OrnamentType::Appoggiatura:
      return generateAppoggiatura(note, upper);
    case OrnamentType::Pralltriller:
      return generatePralltriller(note, upper);
    case OrnamentType::Vorschlag:
      return generateVorschlag(note, upper);
    case OrnamentType::Nachschlag:
      return generateNachschlag(note, lower);
    case OrnamentType::CompoundTrillNachschlag:
      return generateCompoundOrnament(note, CompoundOrnamentType::TrillWithNachschlag,
                                      upper, lower);
    case OrnamentType::CompoundTurnTrill:
      return generateCompoundOrnament(note, CompoundOrnamentType::TurnThenTrill, upper, lower);
    default:
      // Unsupported ornament types return the note unchanged.
      return {note};
  }
}

/// @brief Check if any ornament type is enabled in the config.
/// @param config The ornament configuration.
/// @return true if at least one ornament type is enabled.
bool anyOrnamentEnabled(const OrnamentConfig& config) {
  return config.enable_trill || config.enable_mordent || config.enable_turn ||
         config.enable_appoggiatura || config.enable_pralltriller ||
         config.enable_vorschlag || config.enable_nachschlag || config.enable_compound;
}

}  // namespace

bool isEligibleForOrnament(const NoteEvent& note, VoiceRole role) {
  // Ground voice never receives ornaments.
  if (role == VoiceRole::Ground) {
    return false;
  }

  // Note must be at least an eighth note duration.
  const Tick min_duration = kTicksPerBeat / 2;  // 240 ticks
  return note.duration >= min_duration;
}

OrnamentType selectOrnamentType(const NoteEvent& note, const OrnamentConfig& config) {
  const uint8_t beat = beatInBar(note.start_tick);
  const bool strong_beat = (beat == 0 || beat == 2);

  if (strong_beat) {
    // Strong beats prefer trills and appoggiaturas (on-beat emphasis).
    if (config.enable_trill) return OrnamentType::Trill;
    if (config.enable_appoggiatura) return OrnamentType::Appoggiatura;
    if (config.enable_vorschlag) return OrnamentType::Vorschlag;
    if (config.enable_mordent) return OrnamentType::Mordent;
    if (config.enable_turn) return OrnamentType::Turn;
    if (config.enable_pralltriller) return OrnamentType::Pralltriller;
  } else {
    // Weak beats prefer mordents and pralltriller (lighter ornaments).
    if (config.enable_mordent) return OrnamentType::Mordent;
    if (config.enable_pralltriller) return OrnamentType::Pralltriller;
    if (config.enable_nachschlag) return OrnamentType::Nachschlag;
    if (config.enable_trill) return OrnamentType::Trill;
    if (config.enable_turn) return OrnamentType::Turn;
    if (config.enable_appoggiatura) return OrnamentType::Appoggiatura;
    if (config.enable_vorschlag) return OrnamentType::Vorschlag;
  }

  // Fallback: trill (always available as last resort).
  return OrnamentType::Trill;
}

OrnamentType selectOrnamentType(const NoteEvent& note, const OrnamentConfig& config,
                                const HarmonicTimeline& timeline, Tick tick) {
  const auto& event = timeline.getAt(tick);
  const bool chord_tone = isChordTone(note.pitch, event);

  // Compound ornaments for long notes (>= 1 beat).
  if (note.duration >= kTicksPerBeat && config.enable_compound) {
    const uint8_t beat = beatInBar(note.start_tick);
    const bool strong_beat = (beat == 0 || beat == 2);

    if (chord_tone && strong_beat) {
      return OrnamentType::CompoundTrillNachschlag;
    }
    if (!chord_tone && !strong_beat) {
      return OrnamentType::CompoundTurnTrill;
    }
  }

  // Chord tones prefer sustained ornaments (trill).
  if (chord_tone) {
    if (config.enable_trill) return OrnamentType::Trill;
  } else {
    // Non-chord tones prefer approach ornaments (vorschlag).
    if (config.enable_vorschlag) return OrnamentType::Vorschlag;
  }

  // Fall back to metric-position-based selection.
  return selectOrnamentType(note, config);
}

std::vector<NoteEvent> applyOrnaments(const std::vector<NoteEvent>& notes,
                                      const OrnamentContext& context) {
  // Ground voice: return all notes unchanged.
  if (context.role == VoiceRole::Ground) {
    return notes;
  }

  const float density = effectiveDensity(context.config.ornament_density, context.role);

  // Zero density: no ornaments to apply.
  if (density <= 0.0f) {
    return notes;
  }

  // Check if any ornament type is enabled.
  if (!anyOrnamentEnabled(context.config)) {
    return notes;
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size() * 2);  // Ornaments expand note count.

  // Seed the RNG deterministically: base seed XOR'd with a mixing constant.
  DeterministicRng rng(context.seed ^ 0x4261636Bu);  // "Bach" in ASCII

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];

    if (!isEligibleForOrnament(note, context.role)) {
      result.push_back(note);
      continue;
    }

    // Mix note index into RNG for per-note determinism.
    DeterministicRng note_rng(context.seed ^ static_cast<uint32_t>(idx) * 2654435761u);
    const float roll = note_rng.next();

    if (roll < density) {
      // Select ornament type: use harmonic context if available.
      OrnamentType type;
      if (context.timeline != nullptr) {
        type = selectOrnamentType(note, context.config, *context.timeline, note.start_tick);
      } else {
        type = selectOrnamentType(note, context.config);
      }
      auto ornamented = applyOrnamentToNote(note, type);
      for (auto& sub_note : ornamented) {
        result.push_back(sub_note);
      }
    } else {
      // Pass through unchanged.
      result.push_back(note);
    }
  }

  return result;
}

std::vector<NoteEvent> applyOrnaments(const std::vector<NoteEvent>& notes,
                                      const OrnamentContext& context,
                                      const std::vector<std::vector<NoteEvent>>& all_voice_notes) {
  // Apply ornaments using the base overload.
  auto result = applyOrnaments(notes, context);

  // If no multi-voice context provided, skip verification (backward compatible).
  if (all_voice_notes.empty()) {
    return result;
  }

  // Determine number of voices from all_voice_notes.
  auto num_voices = static_cast<uint8_t>(all_voice_notes.size());
  if (num_voices < 2) {
    return result;  // No cross-voice checking possible with fewer than 2 voices.
  }

  // Build all_voices with the ornamented voice substituted in for counterpoint checking.
  // Keep original notes for reversion on violation.
  verifyOrnamentCounterpoint(result, notes, all_voice_notes, num_voices);

  return result;
}

void verifyOrnamentCounterpoint(std::vector<NoteEvent>& notes,
                                const std::vector<NoteEvent>& original_notes,
                                const std::vector<std::vector<NoteEvent>>& all_voices,
                                uint8_t num_voices) {
  if (num_voices < 2 || original_notes.empty() || notes.empty()) {
    return;
  }

  // Determine which voice is being ornamented.
  VoiceId ornamented_voice = notes[0].voice;

  // Find ornament regions: ranges of ticks where notes differ from original_notes.
  // An ornament region is the tick range of an original note that was expanded.
  struct OrnamentRegion {
    Tick start_tick = 0;
    Tick end_tick = 0;
    size_t orig_idx = 0;      // Index into original_notes.
    size_t result_begin = 0;  // Index range in the result notes vector.
    size_t result_end = 0;
  };

  std::vector<OrnamentRegion> regions;

  // Walk through original notes and match against result notes.
  // Original notes are 1-to-1 or 1-to-many in the result.
  size_t result_pos = 0;
  for (size_t orig_idx = 0; orig_idx < original_notes.size(); ++orig_idx) {
    const auto& orig = original_notes[orig_idx];
    Tick orig_end = orig.start_tick + orig.duration;

    // Find all result notes that fall within this original note's tick range.
    size_t region_begin = result_pos;
    while (result_pos < notes.size() && notes[result_pos].start_tick < orig_end) {
      ++result_pos;
    }
    size_t region_end = result_pos;

    // Check if this region differs from the original (ornament was applied).
    bool is_ornamented = false;
    if (region_end - region_begin != 1) {
      is_ornamented = true;  // Expanded to multiple notes.
    } else if (region_begin < notes.size() && notes[region_begin].pitch != orig.pitch) {
      is_ornamented = true;  // Pitch changed.
    }

    if (is_ornamented) {
      OrnamentRegion region;
      region.start_tick = orig.start_tick;
      region.end_tick = orig_end;
      region.orig_idx = orig_idx;
      region.result_begin = region_begin;
      region.result_end = region_end;
      regions.push_back(region);
    }
  }

  if (regions.empty()) {
    return;  // No ornaments applied, nothing to verify.
  }

  // For each ornament region, build a local snapshot and check counterpoint.
  // Context window: ornament tick range +/- 2 beats for surrounding context.
  constexpr Tick kContextWindow = kTicksPerBeat * 2;

  // Process regions in reverse so index adjustments don't invalidate later regions.
  for (auto region_iter = regions.rbegin(); region_iter != regions.rend(); ++region_iter) {
    const auto& region = *region_iter;

    Tick window_start = (region.start_tick > kContextWindow)
                            ? region.start_tick - kContextWindow
                            : 0;
    Tick window_end = region.end_tick + kContextWindow;

    // Build a flat note list for the local window across all voices.
    std::vector<NoteEvent> local_notes;

    for (uint8_t vid = 0; vid < num_voices; ++vid) {
      if (vid == ornamented_voice) {
        // Use the ornamented notes for this voice.
        for (const auto& note : notes) {
          if (note.start_tick + note.duration > window_start &&
              note.start_tick < window_end) {
            local_notes.push_back(note);
          }
        }
      } else {
        // Use the original voice notes.
        for (const auto& note : all_voices[vid]) {
          if (note.start_tick + note.duration > window_start &&
              note.start_tick < window_end) {
            local_notes.push_back(note);
          }
        }
      }
    }

    // Also build a reference local snapshot using the original (unornamented) notes.
    std::vector<NoteEvent> reference_notes;
    for (uint8_t vid = 0; vid < num_voices; ++vid) {
      if (vid == ornamented_voice) {
        for (const auto& note : original_notes) {
          if (note.start_tick + note.duration > window_start &&
              note.start_tick < window_end) {
            reference_notes.push_back(note);
          }
        }
      } else {
        for (const auto& note : all_voices[vid]) {
          if (note.start_tick + note.duration > window_start &&
              note.start_tick < window_end) {
            reference_notes.push_back(note);
          }
        }
      }
    }

    // Count violations with ornament vs without ornament.
    uint32_t ornamented_parallels = countParallelPerfect(local_notes, num_voices);
    uint32_t ornamented_crossings = countVoiceCrossings(local_notes, num_voices);
    uint32_t reference_parallels = countParallelPerfect(reference_notes, num_voices);
    uint32_t reference_crossings = countVoiceCrossings(reference_notes, num_voices);

    // If the ornament introduced NEW violations (more than the original had), revert.
    bool has_new_violations = (ornamented_parallels > reference_parallels) ||
                              (ornamented_crossings > reference_crossings);

    if (has_new_violations) {
      // Revert: replace the ornament region with the original note.
      const auto& orig = original_notes[region.orig_idx];
      auto erase_begin = notes.begin() +
                         static_cast<std::ptrdiff_t>(region.result_begin);
      auto erase_end = notes.begin() +
                       static_cast<std::ptrdiff_t>(region.result_end);
      notes.erase(erase_begin, erase_end);
      notes.insert(notes.begin() +
                       static_cast<std::ptrdiff_t>(region.result_begin),
                   orig);
    }
  }
}

}  // namespace bach
