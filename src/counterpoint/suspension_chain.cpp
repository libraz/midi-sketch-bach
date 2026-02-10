/// @file
/// @brief Implementation of suspension chain generator -- creates sequences of
/// connected preparation-dissonance-resolution patterns for Bach counterpoint.

#include "counterpoint/suspension_chain.h"

#include <algorithm>

namespace bach {

namespace {

/// Minimum allowed MIDI pitch for suspension chains.
constexpr uint8_t kMinPitch = 24;

/// Maximum allowed MIDI pitch for suspension chains.
constexpr uint8_t kMaxPitch = 96;

/// Number of beats per suspension event (preparation + dissonance + resolution).
constexpr uint8_t kBeatsPerSuspension = 3;

/// Ticks per suspension event.
constexpr Tick kTicksPerSuspension = kBeatsPerSuspension * kTicksPerBeat;

/// @brief Clamp a pitch value to the valid MIDI range [kMinPitch, kMaxPitch].
uint8_t clampPitch(int pitch) {
  return static_cast<uint8_t>(std::clamp(pitch, static_cast<int>(kMinPitch),
                                         static_cast<int>(kMaxPitch)));
}

}  // namespace

// ---------------------------------------------------------------------------
// Resolution intervals (fixed design values -- Principle 4)
// ---------------------------------------------------------------------------

int resolutionInterval(SuspensionType type) {
  switch (type) {
    case SuspensionType::Sus4_3:
      return -1;  // Down by 1 semitone
    case SuspensionType::Sus7_6:
      return -1;  // Down by 1 semitone
    case SuspensionType::Sus9_8:
      return -2;  // Down by 2 semitones
    case SuspensionType::Sus2_3:
      return 1;   // Up by 1 semitone (ascending bass resolution)
    default:
      return -1;
  }
}

// ---------------------------------------------------------------------------
// SuspensionType to string
// ---------------------------------------------------------------------------

const char* suspensionTypeToString(SuspensionType type) {
  switch (type) {
    case SuspensionType::Sus4_3: return "4-3";
    case SuspensionType::Sus7_6: return "7-6";
    case SuspensionType::Sus9_8: return "9-8";
    case SuspensionType::Sus2_3: return "2-3";
    default: return "unknown";
  }
}

// ---------------------------------------------------------------------------
// Chain generation
// ---------------------------------------------------------------------------

SuspensionChain generateSuspensionChain(Tick start_tick, uint8_t num_suspensions,
                                        uint8_t base_pitch, VoiceId voice,
                                        SuspensionType type) {
  SuspensionChain chain;
  chain.start_tick = start_tick;

  if (num_suspensions == 0) {
    chain.end_tick = start_tick;
    return chain;
  }

  // Clamp the starting pitch to valid range.
  uint8_t current_pitch = clampPitch(static_cast<int>(base_pitch));
  int interval = resolutionInterval(type);

  chain.events.reserve(num_suspensions);

  for (uint8_t idx = 0; idx < num_suspensions; ++idx) {
    SuspensionEvent event;
    event.type = type;
    event.voice = voice;

    // Each suspension occupies 3 consecutive beats:
    //   Beat 0: preparation (consonant)
    //   Beat 1: dissonance (strong beat, held note)
    //   Beat 2: resolution (stepwise)
    Tick offset = static_cast<Tick>(idx) * kTicksPerSuspension;
    event.preparation_tick = start_tick + offset;
    event.dissonance_tick = start_tick + offset + kTicksPerBeat;
    event.resolution_tick = start_tick + offset + 2 * kTicksPerBeat;

    event.suspended_pitch = current_pitch;
    event.resolution_pitch = clampPitch(static_cast<int>(current_pitch) + interval);

    chain.events.push_back(event);

    // Chain connection: the resolution pitch becomes the preparation
    // consonance for the next suspension in the chain.
    current_pitch = event.resolution_pitch;
  }

  // End tick is the resolution tick of the last event plus one beat duration.
  chain.end_tick = chain.events.back().resolution_tick + kTicksPerBeat;

  return chain;
}

}  // namespace bach
