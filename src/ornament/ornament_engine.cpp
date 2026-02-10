// Ornament engine implementation: post-processing ornament application.

#include "ornament/ornament_engine.h"

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

}  // namespace bach
